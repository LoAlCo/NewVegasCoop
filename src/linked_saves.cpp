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
#include <random>
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
    std::map<std::string, coop::SaveLink> g_ownSaves;
    void SaveOwnSaves();

    std::string OwnSaveKey(const std::string& host, const std::string& own)
    {
        return coop::PlainName(host, "Host") + "\t" + own;
    }

    std::string SavesFolder()
    {
        char documents[MAX_PATH];
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, documents)))
            return "";
        return std::string(documents) + "\\My Games\\FalloutNV\\Saves\\";
    }

    // By game ID alone. The host's name can't narrow it down: at the menu it's their Steam name, and in the game it's
    // their character's, which changes with the save they load.
    std::string OurMatchOf(const coop::SaveTag& hostTag)
    {
        std::string folder = SavesFolder();
        std::vector<coop::SaveOnDisk> ours;
        for (const auto& [key, link] : g_ownSaves)
        {
            size_t tab = key.find('\t');
            if (tab == std::string::npos)
                continue;
            std::string own = key.substr(tab + 1);
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (!GetFileAttributesExA((folder + own + ".fos").c_str(), GetFileExInfoStandard, &a) ||
                (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            ours.push_back({own, link.tag, (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime});
        }
        return coop::PickLinkedSave(ours, hostTag);
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
        std::string ours = OurMatchOf(host.tag);
        if (ours.empty())
        {
            LOG_INFO("Main menu: no save of ours belongs to the game %s is playing (\"%s\", game %016llx); the player "
                "picks one", host.hostName.c_str(), host.save.c_str(), static_cast<unsigned long long>(host.tag.game));
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
        LOG_INFO("Main menu: loading \"%s\", our save from the game %s is playing (\"%s\", game %016llx)", ours.c_str(),
            host.hostName.c_str(), host.save.c_str(), static_cast<unsigned long long>(host.tag.game));
        g_expectedLoad = ours;
        game::RunConsoleLine(("load \"" + ours + "\"").c_str());
    }

    std::string g_savedForHost;
    std::string g_linkHost;
    coop::SaveLink g_pendingLink;
    coop::SaveLink g_checkingLink;

    // The host is loading a save and we aren't following: they can carry on without us.
    void KeepOurCharacter(const std::string& note)
    {
        game::Notify(note.c_str());
        g_expectedLoad.clear();
        g_coop.RefundPendingGifts(net::GiveReason::PlayerLeft);
        g_coop.RefundPendingTrades();
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

    const char* g_saveGiveUpNote = "";

    void BeginSave(const std::string& name, uint32_t now, const char* giveUpNote)
    {
        g_checkingSave = name;
        g_saveGiveUpNote = giveUpNote;
        g_saveAskedAt = FileTimeNow();
        g_nextSaveCheck = now + 1500;
        g_saveGiveUpAt = now + 60000;
        g_saveTries = 1;
        g_checkingLink = {};
        game::RunConsoleLine(("save \"" + name + "\"").c_str());
    }

    void KeepTryingSave(uint32_t now)
    {
        if (g_checkingSave.empty() || g_loadingSave || !g_worldLoaded || !coop::IsDue(now, g_nextSaveCheck))
            return;
        if (SavedSince(g_checkingSave, g_saveAskedAt))
        {
            LOG_INFO("Saves: ours is saved (\"%s\"%s)", g_checkingSave.c_str(),
                g_saveTries > 1 ? (", try " + std::to_string(g_saveTries)).c_str() : "");
            if (!g_checkingLink.theirs.empty())
            {
                g_ownSaves[OwnSaveKey(g_linkHost, g_checkingSave)] = g_checkingLink;
                SaveOwnSaves();
            }
            g_checkingSave.clear();
        }
        else if (coop::IsDue(now, g_saveGiveUpAt))
        {
            LOG_WARN("Saves: the game wouldn't save ours (\"%s\") in a minute of trying; loading the host's "
                "save will keep our character as it is", g_checkingSave.c_str());
            game::Notify(g_saveGiveUpNote);
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

    void FollowHostSaves(uint32_t now)
    {
        for (const net::SaveOrder& order : g_coop.TakeSaveOrders())
        {
            if (!order.load)
            {
                std::string name = coop::LinkedSaveName(g_coop.NameOf(0), order.name);
                if (name.empty())
                    continue;
                g_pendingLinkedSave = name;
                g_linkHost = g_coop.NameOf(0);
                g_pendingLink = {order.name, order.tag};
                if (!g_savedForHost.empty())
                {
                    g_ownSaves[OwnSaveKey(g_linkHost, g_savedForHost)] = g_pendingLink;
                    SaveOwnSaves();
                }
                g_savedForHost.clear();
                continue;
            }
            // Saves we still meant to make or link belong to the game being left. Done after this load, they could
            // tie this character's save to that game, or another character's to the host's next save.
            g_pendingLinkedSave.clear();
            g_checkingSave.clear();
            g_savedForHost.clear();
            g_reportLoad = true;
            std::string ours = OurMatchOf(order.tag);
            if (ours.empty())
            {
                LOG_INFO("Saves: no save of ours goes with the one the host is loading (\"%s\", game %016llx); keeping "
                    "our character", order.name.c_str(), static_cast<unsigned long long>(order.tag.game));
                KeepOurCharacter("No save of yours goes with the host's (" + order.name + "): keeping your character");
                continue;
            }
            LOG_INFO("Saves: the host is loading \"%s\" (game %016llx); loading ours, \"%s\"", order.name.c_str(),
                static_cast<unsigned long long>(order.tag.game), ours.c_str());
            g_expectedLoad = ours;
            g_expectLoadUntil = now + 3000;
            game::RunConsoleLine(("load \"" + ours + "\"").c_str());
        }
        if (!g_expectedLoad.empty() && coop::IsDue(now, g_expectLoadUntil))
        {
            LOG_INFO("Saves: no \"%s\" here; keeping our character", g_expectedLoad.c_str());
            KeepOurCharacter("No save matching the host's here (" + g_expectedLoad + "): keeping your character");
        }
        if (g_coop.TakeLoadRefused())
        {
            game::Notify((g_coop.NameOf(0) + " said no to loading that save for everyone").c_str());
            LOG_INFO("Saves: the host said no to loading our save for everyone");
        }
        if (!g_pendingLinkedSave.empty() && !g_loadingSave && g_worldLoaded)
        {
            LOG_INFO("Saves: the host saved; saving ours as \"%s\"", g_pendingLinkedSave.c_str());
            BeginSave(g_pendingLinkedSave, now, "Co-op: couldn't save your game to match the host's save");
            g_checkingLink = g_pendingLink;
            g_pendingLinkedSave.clear();
        }
    }

    constexpr int kLoadQuestion = 2;
    struct { uint8_t playerId = 0; std::string save; bool open = false; } g_loadAsk;

    void ServeFriendSaves(uint32_t now)
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
                bool gift = coop::IsGiftSave(ask.name);
                std::string name = gift ? coop::GiftSaveForHost(g_coop.NameOf(0)) : coop::SaveForFriend(who, when);
                if (name.empty())
                    continue;
                LOG_INFO("Saves: %s saved; saving for everyone as \"%s\"", who.c_str(), name.c_str());
                BeginSave(name, now, "Co-op: couldn't save your game to match your friend's save");
                if (!gift)
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
            std::string host, own;
            coop::SaveLink link;
            if (coop::ParseSaveLink(text, host, own, link))
                g_ownSaves[OwnSaveKey(host, own)] = std::move(link);
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
        for (const auto& [key, link] : g_ownSaves)
        {
            size_t tab = key.find('\t');
            std::fprintf(f, "%s\n", coop::FormatSaveLink(key.substr(0, tab), key.substr(tab + 1), link).c_str());
        }
        std::fclose(f);
    }

    // A link describes a save as it was when linked, so any later write to that file ends it. The save the host asked
    // for is linked again once it's confirmed written.
    void ForgetLinksOf(const std::string& own)
    {
        bool changed = false;
        for (auto it = g_ownSaves.begin(); it != g_ownSaves.end();)
        {
            size_t tab = it->first.find('\t');
            if (tab != std::string::npos && it->first.compare(tab + 1, std::string::npos, own) == 0)
            {
                it = g_ownSaves.erase(it);
                changed = true;
            }
            else
                ++it;
        }
        if (changed)
            SaveOwnSaves();
    }

    coop::SaveTag g_thisGame;

    uint64_t NewSaveTagId()
    {
        std::random_device random;
        uint64_t id = 0;
        while (id == 0)
            id = (uint64_t(random()) << 32) | random();
        return id;
    }

    void StartNewGameTag()
    {
        g_thisGame = {NewSaveTagId(), NewSaveTagId()};
        g_coop.SetCurrentSaveTag(g_thisGame);
    }

    // Not in the co-save: xNVSE skips saving co-save data for a plugin on the default opcode base, which we are.
    void WriteSaveTag(const std::string& file, uint64_t written)
    {
        std::string path = SavesFolder() + coop::SaveTagFileName(file);
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "w") != 0 || !f)
        {
            LOG_WARN("Saves: couldn't write %s; this save won't be known as part of its game", path.c_str());
            return;
        }
        std::fputs(coop::FormatSaveTagFile(g_thisGame, written).c_str(), f);
        std::fclose(f);
    }

    void ReadSaveTag(const std::string& file)
    {
        std::string folder = SavesFolder();
        coop::SaveTag found;
        WIN32_FILE_ATTRIBUTE_DATA a;
        bool exists = GetFileAttributesExA((folder + file).c_str(), GetFileExInfoStandard, &a) != 0;
        uint64_t saveWritten = exists ? (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime : 0;
        FILE* f = nullptr;
        if (exists && fopen_s(&f, (folder + coop::SaveTagFileName(file)).c_str(), "r") == 0 && f)
        {
            char text[64];
            size_t size = std::fread(text, 1, sizeof(text), f);
            std::fclose(f);
            found = coop::SaveTagFromFile(std::string_view(text, size), saveWritten);
        }
        if (found.game == 0)
        {
            StartNewGameTag();
            LOG_INFO("Saves: this save has no game ID yet; it's now %016llx", static_cast<unsigned long long>(g_thisGame.game));
            // Kept beside it, so loading this save again is the same game and friends' saves still go with it.
            if (exists)
                WriteSaveTag(file, saveWritten);
            return;
        }
        g_thisGame = found;
        g_coop.SetCurrentSaveTag(g_thisGame);
        LOG_INFO("Saves: this save is from game %016llx", static_cast<unsigned long long>(g_thisGame.game));
    }

    bool MayLoad(const std::string& save)
    {
        if (g_coop.GetRole() != coop::Role::Friend || !g_coop.InGame() || save == g_expectedLoad)
            return true;
        std::string hostSave = coop::HostSaveOf(g_coop.NameOf(0), save);
        auto own = g_ownSaves.find(coop::PlainName(g_coop.NameOf(0), "Host") + "\t" + save);
        if (hostSave.empty() && own != g_ownSaves.end())
            hostSave = own->second.theirs;
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
