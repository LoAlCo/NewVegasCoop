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

    std::string g_pendingLinkedSave;
    std::string g_checkingSave;
    uint64_t g_saveAskedAt = 0;
    uint32_t g_nextSaveCheck = 0, g_saveGiveUpAt = 0, g_saveTries = 0;

    uint64_t FileTimeNow()
    {
        FILETIME t;
        GetSystemTimeAsFileTime(&t);
        return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    }

    std::string SavesFolder();

    bool SavedSince(const std::string& save, uint64_t since)
    {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExA((SavesFolder() + save + ".fos").c_str(), GetFileExInfoStandard, &a))
            return false;
        uint64_t written = (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
        return written + 20000000ull >= since;
    }
    std::string g_expectedLoad;
    uint32_t g_expectLoadUntil = 0;
    std::map<std::string, std::string> g_ownSaves;
    void SaveOwnSaves();

    std::string SavesFolder()
    {
        char documents[MAX_PATH];
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, documents)))
            return "";
        return std::string(documents) + "\\My Games\\FalloutNV\\Saves\\";
    }

    std::string OurMatchOf(const std::string& host, const std::string& hostSave)
    {
        if (hostSave.empty())
            return "";
        std::string folder = SavesFolder();
        auto exists = [&](const std::string& save)
        {
            DWORD a = GetFileAttributesA((folder + save + ".fos").c_str());
            return !save.empty() && a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        };
        std::string prefix = coop::PlainName(host, "Host") + "\t";
        for (const auto& [key, theirs] : g_ownSaves)
            if (key.compare(0, prefix.size(), prefix) == 0 && theirs == hostSave && exists(key.substr(prefix.size())))
                return key.substr(prefix.size());
        std::string linked = coop::LinkedSaveName(host, hostSave);
        return exists(linked) ? linked : "";
    }

    bool HaveSaves()
    {
        WIN32_FIND_DATAA found;
        HANDLE h = FindFirstFileA((SavesFolder() + "*.fos").c_str(), &found);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        FindClose(h);
        return true;
    }

    void LoadOurMatch()
    {
        const net::ModsMatch& host = g_coop.MatchedHost();
        std::string ours = OurMatchOf(host.hostName, host.save);
        if (ours.empty())
        {
            LOG_INFO("Main menu: no save of ours goes with %s's \"%s\"; the player picks one", host.hostName.c_str(),
                host.save.c_str());
            if (HaveSaves())
            {
                g_openLoadMenu = true;
                game::Notify(("Co-op: pick the character to bring into " + host.hostName + "'s game").c_str(), 8);
            }
            else
                game::ShowMessage("No saves yet: start a new game. You'll join " + host.hostName + "'s game once "
                    "you're out of Doc Mitchell's house.");
            return;
        }
        LOG_INFO("Main menu: loading \"%s\", our match of %s's \"%s\"", ours.c_str(), host.hostName.c_str(),
            host.save.c_str());
        g_expectedLoad = ours;
        game::RunConsoleLine(("load \"" + ours + "\"").c_str());
    }

    std::string g_savedForHost;

    void FollowHostSaves(uint32_t now)
    {
        for (const net::SaveOrder& order : g_coop.TakeSaveOrders())
        {
            std::string name = coop::LinkedSaveName(g_coop.NameOf(0), order.name);
            if (name.empty())
                continue;
            if (!order.load)
            {
                g_pendingLinkedSave = name;
                if (!g_savedForHost.empty())
                {
                    g_ownSaves[coop::PlainName(g_coop.NameOf(0), "Host") + "\t" + g_savedForHost] = order.name;
                    SaveOwnSaves();
                }
                g_savedForHost.clear();
                continue;
            }
            g_pendingLinkedSave.clear();
            g_reportLoad = true;
            LOG_INFO("Saves: the host is loading \"%s\"; loading ours, \"%s\"", order.name.c_str(), name.c_str());
            g_expectedLoad = name;
            g_expectLoadUntil = now + 3000;
            game::RunConsoleLine(("load \"" + name + "\"").c_str());
        }
        if (!g_expectedLoad.empty() && coop::IsDue(now, g_expectLoadUntil))
        {
            LOG_INFO("Saves: no \"%s\" here; keeping our character", g_expectedLoad.c_str());
            game::Notify(("No save matching the host's here (" + g_expectedLoad + "): keeping your character").c_str());
            g_expectedLoad.clear();
            if (g_reportLoad)
            {
                g_reportLoad = false;
                g_coop.LoadedHostSave();
            }
            if (g_waitForHostLoad)
            {
                g_waitForHostLoad = false;
                game::SetPlayerControls(true);
            }
        }
        if (g_coop.TakeLoadRefused())
        {
            game::Notify((g_coop.NameOf(0) + " said no to loading that save for everyone").c_str());
            LOG_INFO("Saves: the host said no to loading our save for everyone");
        }
        if (!g_pendingLinkedSave.empty() && !g_loadingSave && g_worldLoaded)
        {
            LOG_INFO("Saves: the host saved; saving ours as \"%s\"", g_pendingLinkedSave.c_str());
            g_checkingSave = g_pendingLinkedSave;
            g_saveAskedAt = FileTimeNow();
            g_nextSaveCheck = now + 1500;
            g_saveGiveUpAt = now + 60000;
            g_saveTries = 1;
            game::RunConsoleLine(("save \"" + g_pendingLinkedSave + "\"").c_str());
            g_pendingLinkedSave.clear();
        }
        else if (!g_checkingSave.empty() && !g_loadingSave && g_worldLoaded && coop::IsDue(now, g_nextSaveCheck))
        {
            if (SavedSince(g_checkingSave, g_saveAskedAt))
            {
                LOG_INFO("Saves: ours is saved (\"%s\"%s)", g_checkingSave.c_str(),
                    g_saveTries > 1 ? (", try " + std::to_string(g_saveTries)).c_str() : "");
                g_checkingSave.clear();
            }
            else if (coop::IsDue(now, g_saveGiveUpAt))
            {
                LOG_WARN("Saves: the game wouldn't save ours (\"%s\") in a minute of trying; loading the host's "
                    "save will keep our character as it is", g_checkingSave.c_str());
                game::Notify("Co-op: couldn't save your game to match the host's save");
                g_checkingSave.clear();
            }
            else if (game::InMenuMode())
            {
                g_saveGiveUpAt = now + 60000;
                g_nextSaveCheck = now + 1000;
            }
            else
            {
                if (g_saveTries == 1)
                    LOG_INFO("Saves: the game didn't save ours yet (enemies near?); trying again every 3 s");
                ++g_saveTries;
                g_nextSaveCheck = now + 3000;
                game::RunConsoleLine(("save \"" + g_checkingSave + "\"").c_str());
            }
        }
    }

    constexpr int kLoadQuestion = 2;
    struct { uint8_t playerId = 0; std::string save; bool open = false; } g_loadAsk;

    void ServeFriendSaves()
    {
        if (g_loadingSave || !g_worldLoaded)
            return;
        for (const coop::Coop::SaveAsk& ask : g_coop.TakeSaveAsks())
        {
            std::string who = g_coop.NameOf(ask.playerId);
            if (!ask.load)
            {
                SYSTEMTIME t;
                GetLocalTime(&t);
                char when[32];
                std::snprintf(when, sizeof(when), "%04u-%02u-%02u %02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                    t.wMinute, t.wSecond);
                std::string name = coop::SaveForFriend(who, when);
                if (name.empty())
                    continue;
                LOG_INFO("Saves: %s saved; saving for everyone as \"%s\"", who.c_str(), name.c_str());
                game::RunConsoleLine(("save \"" + name + "\"").c_str());
                game::Notify(("Saved for everyone (" + who + " saved)").c_str());
                continue;
            }
            g_loadAsk.playerId = ask.playerId;
            g_loadAsk.save = ask.name;
            g_loadAsk.open = game::AskYesNo(who + " loaded \"" + ask.name + "\". Load it for everyone?", kLoadQuestion);
            if (!g_loadAsk.open)
                g_coop.RefuseLoad(ask.playerId);
        }
        int answer = g_loadAsk.open ? game::TakeAnswer(kLoadQuestion) : -1;
        if (answer < 0)
            return;
        g_loadAsk.open = false;
        LOG_INFO("Saves: %s loading \"%s\" for everyone", answer == 1 ? "yes to" : "no to", g_loadAsk.save.c_str());
        if (answer == 1)
            game::RunConsoleLine(("load \"" + g_loadAsk.save + "\"").c_str());
        else
            g_coop.RefuseLoad(g_loadAsk.playerId);
    }

    std::string OwnSavesFile() { return PluginFolder() + "NVCoopLinkedSaves.txt"; }

    void LoadOwnSaves()
    {
        g_ownSaves.clear();
        FILE* f = nullptr;
        if (fopen_s(&f, OwnSavesFile().c_str(), "r") != 0 || !f)
            return;
        char line[512];
        while (std::fgets(line, sizeof(line), f) && g_ownSaves.size() < 1000)
        {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            size_t a = text.find('\t'), b = a == std::string::npos ? a : text.find('\t', a + 1);
            if (b == std::string::npos)
                continue;
            std::string host = text.substr(0, a), own = text.substr(a + 1, b - a - 1), theirs = text.substr(b + 1);
            if (coop::PlainName(host, "") == host && !host.empty() && coop::SaveNameOf(own) == own && !own.empty() &&
                coop::SaveNameOf(theirs) == theirs && !theirs.empty())
                g_ownSaves[host + "\t" + own] = theirs;
        }
        std::fclose(f);
        LOG_INFO("Saves: %zu of our saves are linked to hosts' saves", g_ownSaves.size());
    }

    void SaveOwnSaves()
    {
        FILE* f = nullptr;
        if (fopen_s(&f, OwnSavesFile().c_str(), "w") != 0 || !f)
        {
            LOG_WARN("Saves: couldn't write %s", OwnSavesFile().c_str());
            return;
        }
        for (const auto& [key, theirs] : g_ownSaves)
            std::fprintf(f, "%s\t%s\n", key.c_str(), theirs.c_str());
        std::fclose(f);
    }

    bool MayLoad(const std::string& save)
    {
        if (g_coop.GetRole() != coop::Role::Friend || !g_coop.InGame() || save == g_expectedLoad)
            return true;
        std::string hostSave = coop::HostSaveOf(g_coop.NameOf(0), save);
        auto own = g_ownSaves.find(coop::PlainName(g_coop.NameOf(0), "Host") + "\t" + save);
        if (hostSave.empty() && own != g_ownSaves.end())
            hostSave = own->second;
        if (hostSave.empty())
        {
            LOG_INFO("Saves: not loading \"%s\": it isn't linked to the host's game", save.c_str());
            game::Notify(("That save isn't linked to " + g_coop.NameOf(0) + "'s game. Leave co-op to load it.").c_str());
            return false;
        }
        LOG_INFO("Saves: not loading \"%s\" ourselves; asking the host to load \"%s\" for everyone", save.c_str(),
            hostSave.c_str());
        g_coop.AskHostToLoad(hostSave);
        game::Notify(("Asking " + g_coop.NameOf(0) + " to load this save for everyone...").c_str());
        return false;
    }
