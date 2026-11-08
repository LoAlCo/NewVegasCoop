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

    constexpr float kReviveReach = 150;
    constexpr uint32_t kReviveHoldMs = 2000;
    bool g_downed = false;
    uint32_t g_downedAt = 0, g_nextBleedNotice = 0;
    bool g_gameOver = false;
    int g_reviving = -1;
    uint32_t g_reviveStart = 0, g_reviveAgainAt = 0;
    constexpr float kDragBehind = 70;
    constexpr float kDragFollowMax = 400;
    bool g_grabWas = false;
    int g_draggedBy = -1;
    uint32_t g_draggedSince = 0;
    std::string g_reviveHint;
    int g_healing = -1;
    uint32_t g_healStart = 0, g_healItem = 0;
    std::string g_healingName;
    uint32_t g_labHoldUntil = 0;
    constexpr uint32_t kHealFixDelayMs = 300;
    float g_healFix = 0;
    uint32_t g_healFixAt = 0;

    const char* HealItemName(uint32_t item) { return item == coop::kSuperStimpak ? "Super Stimpak" : "Stimpak"; }

    void ApplyHeals(TESObjectREFR* player, uint32_t now)
    {
        if (g_downed)
            g_healFix = 0;
        if (g_healFix != 0 && coop::IsDue(now, g_healFixAt))
        {
            float health = game::Health(player), max = game::MaxHealth(player), fix = g_healFix;
            if (fix > max - health)
                fix = max - health;
            if (fix < 1 - health)
                fix = 1 - health;
            game::ChangeHealth(player, fix);
            LOG_INFO("Heal: %+.0f for the giver's Medicine and perks (%+.0f wanted): health %.0f -> %.0f / %.0f", fix,
                g_healFix, health, game::Health(player), max);
            g_healFix = 0;
        }
        uint32_t back = 0;
        while (g_coop.TakeHealRefund(back))
        {
            game::ChangeItem(player, back, 1);
            LOG_INFO("Our %s came back (the heal was refused)", HealItemName(back));
            game::Notify((std::string("Your ") + HealItemName(back) + " came back: the heal didn't go through").c_str());
        }
        net::HealAlly heal;
        while (g_coop.TakeHeal(heal))
        {
            std::string giver = g_coop.NameOf(heal.giver);
            if (g_downed)
            {
                LOG_INFO("A heal from %s arrived while we're down: not applied", giver.c_str());
                continue;
            }
            float before = game::Health(player), max = game::MaxHealth(player);
            int myMedicine = static_cast<int>(game::PlayerMedicine());
            bool myFast = game::PlayerPerkRank(coop::kFastMetabolism) > 0;
            if (!game::CastOnSelf(player, heal.item))
            {
                LOG_WARN("A heal from %s: item %08X not found", giver.c_str(), heal.item);
                continue;
            }
            float fix = coop::HealCorrection(heal.item, heal.medicine, heal.fastMetabolism, myMedicine, myFast);
            g_healFix += fix;
            g_healFixAt = now + kHealFixDelayMs;
            LOG_INFO("%s gave us a %s (health %.0f / %.0f): its effect by our Medicine %d%s, then %+.0f for theirs "
                "(Medicine %u%s)", giver.c_str(), HealItemName(heal.item), before, max, myMedicine,
                myFast ? ", Fast Metabolism" : "", fix, heal.medicine, heal.fastMetabolism ? ", Fast Metabolism" : "");
            game::Notify((giver + " gave you a " + HealItemName(heal.item)).c_str());
        }
    }

    bool UpdateHealAlly(uint32_t now, TESObjectREFR* player)
    {
        TESObjectREFR* aimed = game::CrosshairRef();
        int target = -1;
        const coop::RemotePlayer* aimedAt = nullptr;
        for (auto& [id, remote] : g_coop.Remotes())
            if (aimed && g_standIns.Character(id) == aimed)
            {
                target = id;
                aimedAt = &remote;
                break;
            }
        if (target < 0 || aimedAt->Downed() || aimedAt->maxHealth <= 0 || aimedAt->health >= aimedAt->maxHealth)
        {
            g_healing = -1;
            return false;
        }
        TESForm* stim = game::LookupForm(coop::kStimpak);
        TESForm* super = game::LookupForm(coop::kSuperStimpak);
        int stims = stim ? game::ItemCount(player, stim) : 0, supers = super ? game::ItemCount(player, super) : 0;
        uint32_t item = stims > 0 ? coop::kStimpak : supers > 0 ? coop::kSuperStimpak : 0;
        if (!item)
        {
            g_reviveHint = aimedAt->name + " is hurt (no Stimpaks to give)";
            g_healing = -1;
            return true;
        }
        int left = item == coop::kStimpak ? stims : supers;
        g_reviveHint = "Hold E to give a " + std::string(HealItemName(item)) + " to " + aimedAt->name + " (" +
            std::to_string(left) + " left)";
        bool holding = coop::IsDue(now, g_reviveAgainAt) &&
            (game::IsControlPressed(game::layout::kControlActivate) || !coop::IsDue(now, g_labHoldUntil));
        if (!holding)
        {
            g_healing = -1;
            return true;
        }
        if (g_healing != target || g_healItem != item)
        {
            g_healing = target;
            g_healItem = item;
            g_healStart = now;
            g_healingName = aimedAt->name;
        }
        else if (now - g_healStart >= coop::kHealHoldMs)
        {
            game::ChangeItem(player, item, -1);
            g_coop.GiveHeal(static_cast<uint8_t>(target), item,
                static_cast<uint8_t>(std::clamp(game::PlayerMedicine(), 0.0f, 100.0f)),
                game::PlayerPerkRank(coop::kFastMetabolism) > 0);
            game::Notify(("You gave " + g_healingName + " a " + HealItemName(item)).c_str());
            g_healing = -1;
            g_reviveAgainAt = now + 3000;
            g_labHoldUntil = 0;
        }
        return true;
    }

    // Too Many Emotes: looking at a friend who's emoting shows "[E] Join Emote"; pressing E dances the same emote here,
    // at the same point of it as their character, and follows it (intro, then loop) until they stop, we move, or E
    // again. Only the animation: the song already plays from their character, and our own copy of the mod isn't told
    // (so it doesn't start the song again, or make people nearby dance). Friends see us dancing it, with no song.
    bool g_joinKeyWas = false;
    int g_joinedWith = -1;
    std::string g_joinedAnim;
    uint32_t g_joinedAt = 0, g_joinSyncAt = 0;
    bool g_joinCamera = false;
    constexpr uint32_t kJoinStopAfterMs = 500;
    constexpr uint32_t kJoinSyncMs = 100, kJoinSyncAgainMs = 2000;
    constexpr float kJoinSyncSlack = 0.1f;
    // moving, attacking, aiming, drawing a weapon or jumping ends it (as the mod's own emotes)
    constexpr int kJoinStopControls[] = {0, 1, 2, 3, game::layout::kControlAttack, game::layout::kControlAimBlock,
        game::layout::kControlReadyItem, game::layout::kControlJump};

    const std::string& JoinedEmote() { return g_joinedAnim; }
    bool JoinedAnEmote() { return g_joinedWith >= 0; }

    void StopJoinedEmote(const char* why)
    {
        if (g_joinedWith < 0)
            return;
        if (TESObjectREFR* player = game::Player(); player && !g_joinedAnim.empty())
            game::StopAnimFile(player, g_joinedAnim);
        if (g_joinCamera)
            game::EmoteCameraBack();
        LOG_INFO("Emotes: stopped dancing with %s (%s)", g_coop.NameOf(static_cast<uint8_t>(g_joinedWith)).c_str(), why);
        g_joinedWith = -1;
        g_joinedAnim.clear();
        g_joinCamera = false;
    }

    void FollowJoinedEmote(uint32_t now)
    {
        if (g_joinedWith < 0)
            return;
        uint8_t id = static_cast<uint8_t>(g_joinedWith);
        TESObjectREFR* player = game::Player();
        if (!player || !g_coop.InGame() || g_downed || g_gameOver || !g_config.emotes || !g_config.emoteJoin)
        {
            StopJoinedEmote("can't dance now");
            return;
        }
        auto remote = g_coop.Remotes().find(id);
        auto emote = g_coop.Emotes().find(id);
        TESObjectREFR* them = g_standIns.Character(id);
        if (remote == g_coop.Remotes().end() || remote->second.Downed() || emote == g_coop.Emotes().end() ||
            emote->second.path.empty() || !them)
        {
            StopJoinedEmote("they stopped");
            return;
        }
        if (now - g_joinedAt >= kJoinStopAfterMs)
            for (int control : kJoinStopControls)
                if (game::IsControlPressed(control))
                {
                    StopJoinedEmote("we moved");
                    return;
                }
        const std::string& want = emote->second.path;
        if (want != g_joinedAnim)
        {
            // joined, or their intro ended and the loop began
            if (!g_joinedAnim.empty())
                game::StopAnimFile(player, g_joinedAnim);
            game::PlayAnimFile(player, want);
            LOG_INFO("Emotes: dancing %s with %s", want.c_str(), remote->second.name.c_str());
            g_joinedAnim = want;
            g_joinSyncAt = now + kJoinSyncMs;
            return;
        }
        if (!coop::IsDue(now, g_joinSyncAt))
            return;
        g_joinSyncAt = now + kJoinSyncMs;
        if (!game::IsPlayingAnimFile(player, g_joinedAnim))
        {
            game::PlayAnimFile(player, g_joinedAnim);
            return;
        }
        // in step with their character: the same point of the animation (tried until both have started, then now and
        // then)
        float moved = 0;
        if (game::MatchAnimFileTime(player, them, g_joinedAnim, kJoinSyncSlack, &moved))
        {
            if (moved != 0)
                LOG_DEBUG("Emotes: in step with %s (moved %.2f s)", remote->second.name.c_str(), moved);
            g_joinSyncAt = now + kJoinSyncAgainMs;
        }
    }

    bool UpdateJoinEmote(uint32_t now)
    {
        bool key = game::IsControlPressed(game::layout::kControlActivate), pressed = key && !g_joinKeyWas;
        g_joinKeyWas = key;
        if (!g_config.emotes || !g_config.emoteJoin || !game::EmotesReady())
            return false;
        TESObjectREFR* aimed = game::CrosshairRef();
        if (!aimed)
            return false;
        for (auto& [id, emote] : g_coop.Emotes())
        {
            if (emote.path.empty() || g_standIns.Character(id) != aimed)
                continue;
            const auto remote = g_coop.Remotes().find(id);
            if (remote == g_coop.Remotes().end() || remote->second.Downed())
                return false;
            const std::string& name = remote->second.name;
            bool with = g_joinedWith == id;
            if (!with && g_joinedWith < 0 && !g_coop.MyEmote().path.empty())
            {
                // our own emote from the wheel is on: it has to end first
                g_reviveHint = name + " is dancing too";
                return true;
            }
            g_reviveHint = with ? "[E] Stop dancing with " + name : "[E] Join Emote (" + name + ")";
            if (pressed && with)
                StopJoinedEmote("E again");
            else if (pressed)
            {
                StopJoinedEmote("joined someone else");
                g_joinedWith = id;
                g_joinedAnim.clear();
                g_joinedAt = now;
                g_joinCamera = game::EmoteCameraOut();
                LOG_INFO("Emotes: joining %s's emote (%s)", name.c_str(), emote.path.c_str());
                FollowJoinedEmote(now);
            }
            return true;
        }
        return false;
    }

    const std::string kDownedAnim = "Characters\\_Male\\IdleAnims\\dlcpittlayingonfloorinpain.kf";

    void SetDowned(bool down, uint32_t now)
    {
        if (down == g_downed)
            return;
        g_downed = down;
        g_downedAt = g_nextBleedNotice = now;
        game::SetPlayerControls(!down);
        if (down)
        {
            g_chatHistory.Add("* You are down");
            game::PlayAnimFile(game::Player(), kDownedAnim);
        }
        else
            game::StopAnimFile(game::Player(), kDownedAnim);
        LOG_INFO("%s", down ? "We're down" : "We're back up");
    }

    std::string g_revivingName;

    void UpdateReviveBar(uint32_t now)
    {
        constexpr int kCells = 20;
        if (g_healing >= 0 && g_coop.InGame())
        {
            uint32_t held = now - g_healStart;
            int done = held >= coop::kHealHoldMs ? kCells : static_cast<int>(held * kCells / coop::kHealHoldMs);
            game::ShowReviveBar("Giving a " + std::string(HealItemName(g_healItem)) + " to " + g_healingName + "\n[" +
                std::string(done, '#') + std::string(kCells - done, '-') + "]");
            return;
        }
        if (g_reviving < 0 || !g_coop.InGame())
        {
            game::ShowReviveBar(g_coop.InGame() ? g_reviveHint : "");
            return;
        }
        uint32_t held = now - g_reviveStart;
        int done = held >= kReviveHoldMs ? kCells : static_cast<int>(held * kCells / kReviveHoldMs);
        game::ShowReviveBar("Reviving " + g_revivingName + "\n[" + std::string(done, '#') +
            std::string(kCells - done, '-') + "]");
    }

    void UpdateDowned(uint32_t now, const coop::Pose& me, float health, float maxHealth, bool healthFresh)
    {
        TESObjectREFR* player = game::Player();
        game::SetPlayerEssential(g_coop.InGame() && !g_gameOver);
        if (g_waitForHostLoad && !g_coop.InGame())
        {
            g_waitForHostLoad = false;
            game::SetPlayerControls(true);
        }
        int dragging = g_coop.InGame() ? g_coop.Dragging(g_coop.MyPlayerId()) : -1;
        static bool s_dragLimits = false;
        if (bool limits = dragging >= 0 && !g_downed && !g_gameOver; limits != s_dragLimits)
        {
            s_dragLimits = limits;
            game::SetDragging(limits);
        }
        bool grab = game::IsControlPressed(game::layout::kControlGrab), grabTapped = grab && !g_grabWas;
        g_grabWas = grab;
        g_reviveHint.clear();
        FollowJoinedEmote(now);
        if (!g_coop.InGame() || g_gameOver || g_waitForHostLoad)
        {
            SetDowned(false, now);
            return;
        }

        net::GameOver over;
        if (g_coop.TakeGameOver(over))
        {
            std::string text = over.reason == net::GameOverReason::BledOut ?
                g_coop.NameOf(over.playerId) + " bled out. Game over!" : over.reason == net::GameOverReason::Killed ?
                g_coop.NameOf(over.playerId) + " was killed. Game over!" : "Everyone is down. Game over!";
            LOG_INFO("%s", text.c_str());
            g_partyWatch.Mute();
            bool named = over.reason == net::GameOverReason::BledOut || over.reason == net::GameOverReason::Killed;
            if (!named || over.playerId == g_coop.MyPlayerId() || g_coop.Remotes().count(over.playerId))
                g_chatHistory.Add("* " + text);
            SetDowned(false, now);
            if (g_coop.GetRole() == coop::Role::Host)
            {
                game::Notify(text.c_str());
                g_gameOver = true;
                game::KillPlayer();
                return;
            }
            game::Notify((text + " Waiting for " + g_coop.NameOf(0) + " to load a save.").c_str());
            game::SetPlayerControls(false);
            g_waitForHostLoad = true;
            return;
        }
        if (g_coop.TakeRevived() && g_downed)
        {
            game::ChangeHealth(player, maxHealth * coop::kReviveHealth - game::Health(player));
            SetDowned(false, now);
            game::Notify("You've been revived!");
            g_chatHistory.Add("* You were revived");
            return;
        }
        ApplyHeals(player, now);

        if (game::IsKnockedOut(player))
        {
            LOG_INFO("Knocked out (life state %d, health %.1f, VATS mode %u), waking up", game::LifeState(player),
                game::Health(player), game::VatsMode());
            game::WakeUp(player);
            game::ChangeHealth(player, 1 - game::Health(player));
            SetDowned(true, now);
            return;
        }
        static uint32_t s_nextLifeLog = 0;
        if (healthFresh && (health < maxHealth * 0.25f || g_downed) && coop::IsDue(now, s_nextLifeLog))
        {
            s_nextLifeLog = now + 1000;
            LOG_DEBUG("Our health %.1f / %.1f, life state %d, VATS mode %u%s", game::Health(player), maxHealth,
                game::LifeState(player), game::VatsMode(), g_downed ? ", downed" : "");
        }
        int draggedBy = g_downed ? g_coop.DraggedBy(g_coop.MyPlayerId()) : -1;
        if (draggedBy != g_draggedBy)
        {
            if (draggedBy >= 0)
            {
                g_draggedSince = now;
                game::Notify((g_coop.NameOf(static_cast<uint8_t>(draggedBy)) + " is dragging you (your bleed-out is "
                    "paused)").c_str());
            }
            else if (g_draggedBy >= 0)
                g_downedAt += now - g_draggedSince;
            g_draggedBy = draggedBy;
        }
        if (g_downed && draggedBy >= 0)
        {
            auto it = g_coop.Remotes().find(static_cast<uint8_t>(draggedBy));
            coop::Pose them;
            if (it != g_coop.Remotes().end() && it->second.motion.Sample(now, them) && them.space == me.space)
            {
                coop::Pose to = me;
                to.x = them.x - kDragBehind * std::sin(them.heading);
                to.y = them.y - kDragBehind * std::cos(them.heading);
                to.heading = them.heading;
                if (std::fabs(them.z - me.z) > 60)
                    to.z = them.z;
                float dx = to.x - me.x, dy = to.y - me.y;
                if (dx * dx + dy * dy > 1 && dx * dx + dy * dy < kDragFollowMax * kDragFollowMax)
                    game::Move(player, to);
            }
        }
        if (g_downed)
        {
            static uint32_t s_nextPainCheck = 0;
            if (coop::IsDue(now, s_nextPainCheck))
            {
                s_nextPainCheck = now + 500;
                if (!game::IsPlayingAnimFile(player, kDownedAnim))
                    game::PlayAnimFile(player, kDownedAnim);
            }
            if (draggedBy < 0 && coop::IsDue(now, g_nextBleedNotice))
            {
                g_nextBleedNotice = now + 10000;
                uint32_t gone = now - g_downedAt;
                uint32_t left = gone < coop::kBleedOutMs ? (coop::kBleedOutMs - gone + 999) / 1000 : 0;
                std::string text = "You're down! " + std::to_string(left) + " s until you bleed out";
                game::Notify(text.c_str());
            }
            return;
        }

        int target = -1;
        std::string name;
        for (auto& [id, remote] : g_coop.Remotes())
        {
            coop::Pose at;
            if (!remote.Downed() || !game::GetPose(g_standIns.Character(id), at) || at.space != me.space)
                continue;
            float dx = at.x - me.x, dy = at.y - me.y, dz = at.z - me.z;
            if (dx * dx + dy * dy + dz * dz <= kReviveReach * kReviveReach)
            {
                target = id;
                name = remote.name;
                break;
            }
        }
        if (grabTapped && dragging >= 0)
            g_coop.Drag(static_cast<uint8_t>(dragging), false);
        else if (grabTapped && target >= 0 && g_coop.DraggedBy(static_cast<uint8_t>(target)) < 0)
            g_coop.Drag(static_cast<uint8_t>(target), true);
        if (dragging >= 0)
            g_reviveHint = "Dragging " + g_coop.NameOf(static_cast<uint8_t>(dragging)) + ": Z to let go";
        else if (target >= 0)
            g_reviveHint = "Hold E to revive " + name + ", Z to drag";
        if (dragging < 0 && target < 0 && UpdateJoinEmote(now))
        {
            g_reviving = g_healing = -1;
            return;
        }
        if (dragging < 0 && target < 0 && UpdateHealAlly(now, player))
        {
            g_reviving = -1;
            return;
        }
        g_healing = -1;
        bool holding = target >= 0 && coop::IsDue(now, g_reviveAgainAt) &&
            (game::IsControlPressed(game::layout::kControlActivate) || !coop::IsDue(now, g_labHoldUntil));
        if (!holding)
        {
            g_reviving = -1;
            return;
        }
        if (g_reviving != target)
        {
            g_reviving = target;
            g_reviveStart = now;
            g_revivingName = name;
        }
        else if (now - g_reviveStart >= kReviveHoldMs)
        {
            LOG_INFO("Reviving %s", name.c_str());
            g_coop.Revive(static_cast<uint8_t>(target));
            g_reviving = -1;
            g_reviveAgainAt = now + 3000;
            g_labHoldUntil = 0;
        }
    }
