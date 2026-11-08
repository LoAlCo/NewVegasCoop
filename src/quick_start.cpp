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

    constexpr int kQuickStartQuestion = 9;
    constexpr int kHardcoreQuestion = 10;
    enum class QuickStart { Watching, Asked, HardcoreAsked, Leaving, Revising, Saving };
    QuickStart g_quickStart = QuickStart::Watching;
    bool QuickStartBusy() { return g_quickStart != QuickStart::Watching; }
    uint32_t g_quickStartAt = 0;
    uint32_t g_quickStartSpace = 0;
    void UpdateQuickStart(uint32_t now)
    {
        if (!g_worldLoaded || g_loadingSave)
            return;
        static int s_popupsSet = -1;
        if (s_popupsSet != static_cast<int>(g_config.skipDlcPopups))
        {
            s_popupsSet = g_config.skipDlcPopups;
            game::SkipIntroPopups(g_config.skipDlcPopups, game::LoadOrder());
        }
        static bool s_lockersSet = false;
        if (!s_lockersSet)
        {
            s_lockersSet = true;
            game::SetUpGearLockers(game::LoadOrder());
        }
        static uint32_t s_nextLook = 0;
        switch (g_quickStart)
        {
        case QuickStart::Watching:
        {
            if (!coop::IsDue(now, s_nextLook))
                return;
            s_nextLook = now + 500;
            int stage = game::QuestStage(game::ids::kTutorialQuest);
            static bool s_asked = false;
            if (stage >= 0 && stage < 10 && game::QuestStage(game::ids::kBennyIntroQuest) >= 0)
                return;
            if (stage < 0 || stage >= 10)
            {
                s_asked = false;
                return;
            }
            if (s_asked || !game::InDocMitchellsHouse())
                return;
            s_asked = true;
            if (int level = game::PlayerLevel(); level > 1)
                return LOG_INFO("Quick start: not asked: in Doc Mitchell's house with the tutorial at stage %d, but at "
                    "level %d (not a new game)", stage, level);
            game::HoldTutorial(true);
            AskSwitches();
            game::AskChoice("Quick start: skip Doc Mitchell's house?\n\nYou'll be outside his door with the game's "
                "\"finish your character\" menu (name, looks, S.P.E.C.I.A.L., tag skills, traits) and Doc's starting "
                "gear. Co-op starts once you're outside.", {"Quick start", "Play the intro"}, kQuickStartQuestion);
            g_quickStart = QuickStart::Asked;
            LOG_INFO("Quick start: a new game (tutorial stage %d); asked", stage);
            return;
        }
        case QuickStart::Asked:
        {
            int choice = game::TakeChoice(kQuickStartQuestion);
            if (choice == -1)
                return;
            if (choice != 0)
            {
                game::HoldTutorial(false);
                g_quickStart = QuickStart::Watching;
                return LOG_INFO("Quick start: no, the intro is played");
            }
            if (!game::LeaveTutorial())
            {
                g_quickStart = QuickStart::Watching;
                return LOG_ERROR("Quick start: the tutorial's forms aren't there; the intro is played");
            }
            g_hardcoreAsked = g_config.askHardcore;
            if (g_hardcoreAsked)
                game::AskChoice("Play in Hardcore mode?\n\nFood, water and sleep needs, healing over time, ammo has "
                    "weight, companions can die. You can change it later in the game's settings.", {"No", "Hardcore"},
                    kHardcoreQuestion);
            g_quickStart = QuickStart::HardcoreAsked;
            g_quickStartAt = now;
            return LOG_INFO("Quick start: Doc's farewell done; asking about Hardcore");
        }
        case QuickStart::HardcoreAsked:
        {
            static int s_choice = -1;
            if (s_choice == -1)
                s_choice = g_hardcoreAsked ? game::TakeChoice(kHardcoreQuestion) : 2;
            if (s_choice == -1 || now - g_quickStartAt < 4000)
                return;
            int choice = s_choice;
            s_choice = -1;
            if (choice != 2)
                game::RunConsoleLine(choice == 1 ? "SetHardcore 1" : "SetHardcore 0");
            coop::Pose inside;
            g_quickStartSpace = game::GetPose(game::Player(), inside) ? inside.space : 0;
            game::ActivateByPlayer(game::ids::kDocsDoor, false);
            g_quickStart = QuickStart::Leaving;
            g_quickStartAt = now;
            return LOG_INFO("Quick start: Hardcore %s; out the door", choice == 2 ? "as the game has it" :
                choice == 1 ? "on" : "off");
        }
        case QuickStart::Leaving:
        {
            coop::Pose me;
            if (now - g_quickStartAt < 3000 || !game::GetPose(game::Player(), me) || me.space == g_quickStartSpace)
            {
                if (now - g_quickStartAt > 30000)
                {
                    g_quickStart = QuickStart::Watching;
                    LOG_WARN("Quick start: still in Doc's house after 30 s; the rest is left to the player");
                }
                return;
            }
            static uint32_t s_outsideAt = 0;
            if (!s_outsideAt)
                s_outsideAt = now;
            if (now - s_outsideAt < 1000)
                return;
            s_outsideAt = 0;
            game::RemoveTutorialFades();
            game::ActivateByPlayer(game::ids::kReviseActivator, true);
            g_quickStart = QuickStart::Revising;
            g_quickStartAt = now;
            return LOG_INFO("Quick start: outside; the \"finish your character\" box");
        }
        case QuickStart::Revising:
            if (now - g_quickStartAt < 2000 || game::QuestStage(game::ids::kReviseQuest) >= 0)
                return;
            game::GiveStarterGear();
            g_quickStart = QuickStart::Saving;
            g_quickStartAt = now;
            return LOG_INFO("Quick start: character finished; Doc's starting gear given");
        case QuickStart::Saving:
        {
            if (now - g_quickStartAt < 1500)
                return;
            g_quickStart = QuickStart::Watching;
            std::string name;
            for (const char* c = game::Name(game::Player()); *c && name.size() < 40; ++c)
                if (std::isalnum(static_cast<unsigned char>(*c)) || *c == ' ' || *c == '-')
                    name += *c;
            std::string save = "Quick Start - " + (name.empty() ? std::string("Courier") : name);
            game::RunConsoleLine(("save \"" + save + "\"").c_str());
            return LOG_INFO("Quick start: saved as \"%s\"", save.c_str());
        }
        default:
            return;
        }
    }
