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
        if (g_teleportersHad > 0 && have < g_teleportersHad && !game::OpenContainer() && !g_teleportAsked)
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
        if (have != 1)
        {
            game::ChangeItem(me, item->refID, 1 - have);
            if (g_teleportersHad < 0)
                LOG_INFO("Teleporter: given (we had %d)", have);
        }
        g_teleportersHad = 1;
    }
