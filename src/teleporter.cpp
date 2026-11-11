// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "nvse/PluginAPI.h"
#include "config.h"
#include "net/session.h"
#include "coop/activator_uses.h"
#include "coop/clock.h"
#include "coop/coop.h"
#include "coop/vats_feel.h"
#include "coop/game_mode.h"
#include "coop/quests.h"
#include "coop/frame_parts.h"
#include "coop/threat.h"
#include "coop/reinforcements.h"
#include "coop/balance.h"
#include "coop/steer.h"
#include "speech.h"
#include "display.h"
#include "voice_out.h"
#include "game/steam.h"
#include "crash.h"
#include "dll_blocker.h"
#include "dev/commands.h"
#include "dev/lab_channel.h"
#include "dev/dump_game.h"
#include "dev/second_copy.h"
#include "coop/party_perks.h"
#include "game/addresses.h"
#include "game/far_cells.h"
#include "game/game.h"
#include "game/escape_menu.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "game/remove_all_items.h"
#include "game/inspector.h"
#include "game/mirror.h"
#include "game/mover.h"
#include "game/companions.h"
#include "game/containers.h"
#include "game/doors.h"
#include "game/objects.h"
#include "game/terminals.h"
#include "game/activations.h"
#include "game/mines.h"
#include "game/items.h"
#include "game/progress.h"
#include "game/dialogue.h"
#include "game/sleepwait.h"
#include "game/steam.h"
#include "game/quests.h"
#include "game/npcs.h"
#include "game/standins.h"
#include "log.h"
#include "version.h"
#include <algorithm>
#include <shlobj.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "plugin.h"
#include "number_box.h"

    uint32_t g_nextTeleporterLook = 0;
    int g_teleportersHad = -1;
    bool g_teleportAsked = false;
    std::vector<std::pair<uint8_t, bool>> g_teleportChoices;

    void GoToPlayer(uint8_t target, uint32_t now)
    {
        auto it = g_coop.Remotes().find(target);
        coop::Pose pose;
        std::string name = g_coop.NameOf(target);
        if (it == g_coop.Remotes().end() || !it->second.motion.Sample(now, pose) || !game::TravelTo(pose))
        {
            LOG_WARN("Teleporter: couldn't go to %s (no place known for them)", name.c_str());
            game::Notify(("Co-op: couldn't find " + name).c_str());
            return;
        }
        LOG_INFO("Teleporter: went to %s (%08X %.0f %.0f %.0f)", name.c_str(), pose.space, pose.x, pose.y, pose.z);
        game::Notify(("Co-op: teleported to " + name).c_str());
    }

    enum class GiftStep { None, Menu, Friend, Amount };
    constexpr int kCoopMenuQuestion = 16, kGiftFriendQuestion = 17, kGiftAmountQuestion = 18;
    GiftStep g_giftStep = GiftStep::None;
    uint8_t g_giftTo = 0;
    int32_t g_giftHave = 0;
    std::string g_giftShown;
    std::vector<uint8_t> g_giftFriends;
    coop::GiftSaveTimer g_giftSaveTimer;

    void AskTeleportTargets()
    {
        std::vector<std::string> buttons;
        g_teleportChoices.clear();
        for (const auto& [id, remote] : g_coop.Remotes())
            if (g_teleportChoices.size() < 8)
            {
                buttons.push_back("Go to " + g_coop.NameOf(id));
                g_teleportChoices.push_back({id, false});
                buttons.push_back("Bring " + g_coop.NameOf(id) + " here");
                g_teleportChoices.push_back({id, true});
            }
        buttons.push_back("Cancel");
        LOG_INFO("Teleporter: used (%zu others to pick from)", g_teleportChoices.size() / 2);
        g_teleportAsked = game::AskChoice(g_teleportChoices.empty() ? "Co-op Teleporter: nobody else is here" :
            "Co-op Teleporter", buttons, kTeleportQuestion);
    }

    void AskCoopMenu()
    {
        if (g_coop.Remotes().empty() || !g_config.gifts)
        {
            AskTeleportTargets();
            return;
        }
        if (game::AskChoice("Co-op Teleporter", {"Teleport", "Send caps", "Cancel"}, kCoopMenuQuestion))
            g_giftStep = GiftStep::Menu;
    }

    NumberBox g_giftAmount;

    std::string GiftAmountText(int32_t value, const std::string& typed)
    {
        return "Send caps to " + g_coop.NameOf(g_giftTo) + " (you have " + std::to_string(g_giftHave) + ")\n\n" +
            (typed.empty() ? std::to_string(value) : typed + "_") + " caps\n\nType how many, or step it with the "
            "buttons. Enter or Send sends; Esc or Cancel cancels.";
    }

    void StartGiftAmount(uint8_t to, TESObjectREFR* me)
    {
        g_giftStep = GiftStep::None;
        TESForm* caps = game::LookupForm(game::ids::kCaps);
        g_giftTo = to;
        g_giftHave = caps ? game::ItemCount(me, caps) : 0;
        if (g_giftHave < 1)
        {
            game::ShowMessage("You have no caps to send.");
            return;
        }
        NumberBox::Setup setup;
        setup.question = kGiftAmountQuestion;
        setup.confirm = "Send";
        setup.special = "All";
        setup.specialValue = g_giftHave;
        setup.last = "Cancel";
        setup.start = 1;
        setup.min = 1;
        setup.max = g_giftHave;
        setup.text = GiftAmountText;
        if (g_giftAmount.Start(setup))
            g_giftStep = GiftStep::Amount;
        else
            game::ShowMessage("Couldn't open the amount box (see NVCoop.log).");
    }

    void GiftSendCaps(uint8_t to, int32_t amount, TESObjectREFR* me)
    {
        TESForm* caps = game::LookupForm(game::ids::kCaps);
        std::string name = g_coop.NameOf(to);
        if (!caps || !g_coop.Remotes().count(to) || game::TradeMenuOpen())
        {
            game::ShowMessage("Couldn't send the caps to " + name + " right now.");
            return;
        }
        int before = game::ItemCount(me, caps);
        if (amount > before)
        {
            game::ShowMessage("You only have " + std::to_string(before) + " caps.");
            return;
        }
        game::ChangeItem(me, caps->refID, -amount);
        int after = game::ItemCount(me, caps);
        if (after != before - amount)
        {
            LOG_WARN("Gifts: only %d of %d caps came out; putting them back and not sending", before - after, amount);
            if (before != after)
                game::ChangeItem(me, caps->refID, before - after);
            game::ShowMessage("Couldn't take the caps out of your inventory, so nothing was sent.");
            return;
        }
        net::ItemCount kind;
        kind.item = caps->refID;
        kind.count = amount;
        if (!g_coop.SendGift(to, kind, caps->typeID))
        {
            LOG_WARN("Gifts: couldn't send to %s; putting %d caps back", name.c_str(), amount);
            game::ChangeItem(me, caps->refID, amount);
            game::ShowMessage("Couldn't send the caps to " + name + ".");
        }
        game::RefreshItemList();
    }

    void UpdateGiftAmount(TESObjectREFR* me)
    {
        int32_t amount = 0;
        switch (g_giftAmount.Update(amount))
        {
        case NumberBox::Result::Open:
            return;
        case NumberBox::Result::Confirmed:
            g_giftStep = GiftStep::None;
            GiftSendCaps(g_giftTo, amount, me);
            return;
        case NumberBox::Result::NotANumber:
            g_giftStep = GiftStep::None;
            game::ShowMessage("That isn't a number, so nothing was sent.");
            return;
        default:
            g_giftStep = GiftStep::None;
            LOG_INFO("Gifts: cancelled the amount box");
            return;
        }
    }

    void StopGiftMenus()
    {
        if (g_giftStep == GiftStep::Amount)
            g_giftAmount.Stop();
        if (g_giftStep != GiftStep::None)
        {
            game::DropQuestion(kCoopMenuQuestion);
            game::DropQuestion(kGiftFriendQuestion);
            game::DropQuestion(kGiftAmountQuestion);
            g_giftStep = GiftStep::None;
        }
    }

    void UpdateGiftMenus(TESObjectREFR* me)
    {
        if (g_giftStep == GiftStep::Menu)
        {
            int choice = game::TakeChoice(kCoopMenuQuestion);
            if (choice == -1)
                return;
            g_giftStep = GiftStep::None;
            if (choice == 0)
                AskTeleportTargets();
            else if (choice == 1)
            {
                g_giftFriends.clear();
                std::vector<std::string> buttons;
                for (const auto& [id, remote] : g_coop.Remotes())
                    if (g_giftFriends.size() < 9)
                    {
                        g_giftFriends.push_back(id);
                        buttons.push_back(g_coop.NameOf(id));
                    }
                if (g_giftFriends.size() == 1)
                    StartGiftAmount(g_giftFriends[0], me);
                else if (!g_giftFriends.empty())
                {
                    buttons.push_back("Cancel");
                    if (game::AskChoice("Send caps to who?", buttons, kGiftFriendQuestion))
                        g_giftStep = GiftStep::Friend;
                }
            }
        }
        else if (g_giftStep == GiftStep::Friend)
        {
            int choice = game::TakeChoice(kGiftFriendQuestion);
            if (choice == -1)
                return;
            g_giftStep = GiftStep::None;
            if (choice >= 0 && static_cast<size_t>(choice) < g_giftFriends.size())
                StartGiftAmount(g_giftFriends[choice], me);
        }
        else if (g_giftStep == GiftStep::Amount)
            UpdateGiftAmount(me);
    }

    void ApplyGiftOutcomes(TESObjectREFR* me)
    {
        bool changed = false;
        for (const coop::Coop::GiftOutcome& o : g_coop.TakeGiftOutcomes())
        {
            std::string name = g_coop.NameOf(o.to);
            std::string what = std::to_string(o.item.count) + " caps";
            if (o.reason == net::GiveReason::Ok)
            {
                LOG_INFO("Gifts: %s got %d of %08X (condition %d, mods %d)", name.c_str(), o.item.count, o.item.item,
                    o.item.health, o.item.mods);
                game::Notify(("Co-op: " + name + " got your " + what).c_str());
                continue;
            }
            LOG_INFO("Gifts: giving %d of %08X (condition %d, mods %d) back to us; %s didn't get it (reason %d)",
                o.item.count, o.item.item, o.item.health, o.item.mods, name.c_str(), static_cast<int>(o.reason));
            if (me)
                game::ChangeItem(me, o.item);
            changed = true;
            game::Notify(("Co-op: " + name + (o.reason == net::GiveReason::TurnedOff ? " has gifts turned off" :
                " didn't get the " + what) + ", so you have them back").c_str());
        }
        if (changed)
            game::RefreshItemList();
    }

    void ApplyGiftDeliveries(uint32_t now, TESObjectREFR* me)
    {
        if (!g_config.gifts && g_coop.InGame())
        {
            for (const coop::Coop::GiftDelivery& d : g_coop.TakeGiftDeliveries())
            {
                LOG_INFO("Gifts: refused gift %u from %s (gifts are turned off here)", d.id, g_coop.NameOf(d.from).c_str());
                g_coop.FinishGiftDelivery(d.id, net::GiveReason::TurnedOff);
            }
            return;
        }
        if (!me || !g_coop.InGame() || game::TradeMenuOpen() || game::QuantityPromptOpen())
            return;
        bool changed = false;
        for (const coop::Coop::GiftDelivery& d : g_coop.TakeGiftDeliveries())
        {
            std::string name = g_coop.NameOf(d.from);
            if (!game::IsItem(d.item.item) || game::FormTypeOf(d.item.item) != d.itemType)
            {
                LOG_WARN("Gifts: %s sent %08X, which we don't have as an item of type %u: refused", name.c_str(),
                    d.item.item, d.itemType);
                g_coop.FinishGiftDelivery(d.id, net::GiveReason::BadItem);
                continue;
            }
            game::ChangeItem(me, d.item);
            changed = true;
            LOG_INFO("Gifts: got %d of %08X (condition %d, mods %d) from %s (gift %u)", d.item.count, d.item.item,
                d.item.health, d.item.mods, name.c_str(), d.id);
            game::Notify(("Co-op: " + name + " sent you " + std::to_string(d.item.count) + " caps").c_str());
            g_coop.FinishGiftDelivery(d.id, net::GiveReason::Ok);
            if (g_config.giftSave)
                g_giftSaveTimer.Delivered(now);
        }
        if (changed)
            game::RefreshItemList();
    }

    void UpdateGifts(uint32_t now)
    {
        TESObjectREFR* me = game::Player();
        ApplyGiftOutcomes(me);
        if (!g_coop.InGame() || !me)
        {
            StopGiftMenus();
            g_giftSaveTimer.Forget();
            return;
        }
        UpdateGiftMenus(me);
        ApplyGiftDeliveries(now, me);
        if (g_giftSaveTimer.TakeDue(now))
        {
            std::string name = g_coop.GetRole() == coop::Role::Host ? coop::GiftSaveForHost(g_coop.NameOf(0)) :
                coop::GiftSaveForFriend(g_coop.NameOf(0));
            if (!name.empty())
            {
                LOG_INFO("Gifts: saving as \"%s\" so the gift can't be lost", name.c_str());
                BeginSave(name, now, "Co-op: couldn't save your game after the gift");
            }
        }
    }

    void Teleporter(uint32_t now)
    {
        TESObjectREFR* me = game::Player();
        TESForm* item = g_teleportersHad >= 0 || g_coop.InGame() ? game::TeleporterItem() : nullptr;
        if (!g_coop.InGame() || !me || !item)
        {
            if (g_teleportersHad > 0 && me && item)
                game::ChangeItem(me, item->refID, -game::ItemCount(me, item));
            g_teleportersHad = -1;
            g_teleportAsked = false;
            return;
        }
        for (uint8_t target : g_coop.TakeTeleports())
            GoToPlayer(target, now);
        if (g_teleportAsked)
        {
            int choice = game::TakeChoice(kTeleportQuestion);
            if (choice != -1)
            {
                g_teleportAsked = false;
                if (choice >= 0 && static_cast<size_t>(choice) < g_teleportChoices.size())
                {
                    auto [who, bring] = g_teleportChoices[choice];
                    LOG_INFO("Teleporter: %s %s", bring ? "bringing" : "going to", g_coop.NameOf(who).c_str());
                    if (bring)
                        game::Notify(("Co-op: bringing " + g_coop.NameOf(who) + " here").c_str());
                    g_coop.AskTeleport(bring ? who : g_coop.MyPlayerId(), bring ? g_coop.MyPlayerId() : who);
                }
            }
        }
        if (!coop::IsDue(now, g_nextTeleporterLook))
            return;
        g_nextTeleporterLook = now + 250;
        int have = game::ItemCount(me, item);
        if (g_teleportersHad > 0 && have < g_teleportersHad && !game::OpenContainer() && !g_teleportAsked &&
            g_giftStep == GiftStep::None)
            AskCoopMenu();
        if (have != 1)
        {
            game::ChangeItem(me, item->refID, 1 - have);
            if (g_teleportersHad < 0)
                LOG_INFO("Teleporter: given (we had %d)", have);
        }
        g_teleportersHad = 1;
    }
