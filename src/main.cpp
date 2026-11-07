// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "nvse/PluginAPI.h"

#include "config.h"
#include "net/session.h"
#include "coop/clock.h"
#include "coop/coop.h"
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
#include "game/addresses.h"
#include "game/game.h"
#include "game/escape_menu.h"
#include "game/level_up.h"
#include "game/remove_all_items.h"
#include "game/inspector.h"
#include "game/mirror.h"
#include "game/mover.h"
#include "game/companions.h"
#include "game/containers.h"
#include "game/doors.h"
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
#include <utility>
#include <vector>

namespace
{
    PluginHandle g_handle = kPluginHandle_Invalid;
    coop::Coop& g_coop = *new coop::Coop;
    Config g_config;
    game::StandIns g_standIns;
    Speech g_speech;
    coop::SaveMode g_ourMode;
    VoiceOut g_voiceOut;
    game::Npcs g_npcs;
    game::Companions g_companions;
    game::Doors g_doors;
    game::Mines g_mines;
    game::Containers g_containers;
    game::WorldItems g_items;
    game::Quests g_quests;
    game::Progress g_progress;
    game::SleepWait g_sleepWait;
    game::Dialogue g_dialogue;
    game::Inspector g_inspector;
    game::Mirror g_mirror;
    bool g_cutscene = false;
    bool g_waitForHostLoad = false;
    void FollowHostSaves(uint32_t now);
    void ServeFriendSaves();
    bool g_reportLoad = false;
    bool g_loadWatch = false;
    bool g_worldLoaded = false;
    bool g_waitingForIntro = false;
    bool g_startSoon = false;
    uint32_t g_nextIntroCheck = 0;
    coop::Pose g_travel;
    std::vector<uint32_t> g_bringFollowers;
    uint32_t g_bringUntil = 0;
    uint8_t g_travelLeader = 0;
    bool g_hasTravel = false;
    uint32_t g_travelAt = 0;
    int g_countdownLeft = 0;
    constexpr int kFollowSeconds = 4;
    dev::LabChannel g_lab;
    bool g_loadingSave = false;

    std::vector<uint32_t> g_notedMade;
    uint32_t g_nextMadeCheck = 0;
    float MaxHitDistance() { return game::LoadedReach() * 1.5f; }

    coop::ThreatTable g_threats;
    bool g_fovAfterLoad = false;

    struct HeldTo { uint8_t player; TESObjectREFR* target; };
    std::map<uint32_t, HeldTo> g_forced;
    uint32_t g_nextThreatLook = 0;
    std::set<uint32_t> g_bodiesThinned;
    uint32_t g_nextThinLook = 0;
    coop::Reinforcements g_reinforcements;
    std::set<uint32_t> g_fightersSeen;
    std::vector<uint32_t> g_extras;
    uint32_t g_nextReinforceLook = 0;

    struct Tally { int count = 0; float damage = 0; void Add(float d) { ++count; damage += d; } };
    struct CombatTally
    {
        Tally sent;
        Tally applied;
        int ignored = 0;
        Tally hurt;
        Tally taken;
        Tally undone;
    } g_tally;
    uint32_t g_nextTallyAt = 0;

    void LogCombat(uint32_t now)
    {
        if (!coop::IsDue(now, g_nextTallyAt))
            return;
        g_nextTallyAt = now + 10000;
        const CombatTally& t = g_tally;
        if (t.sent.count || t.applied.count || t.ignored || t.hurt.count || t.taken.count || t.undone.count)
        {
            TESObjectREFR* player = game::Player();
            LOG_INFO("Combat: sent %d hits (%.0f damage); applied %d friend hits (%.0f), ignored %d; passed on %d "
                "hits to friends (%.0f); took %d times (%.0f) from the host; undid %d hits (%.0f) from our copies; "
                "our health %.0f / %.0f", t.sent.count, t.sent.damage, t.applied.count, t.applied.damage, t.ignored,
                t.hurt.count, t.hurt.damage, t.taken.count, t.taken.damage, t.undone.count, t.undone.damage,
                game::Health(player), game::MaxHealth(player));
        }
        g_tally = {};
    }

    std::string PluginFolder()
    {
        char path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&PluginFolder), &self);
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string folder(path);
        return folder.substr(0, folder.find_last_of("\\/") + 1);
    }

    std::string g_pausingMenus;

    std::string g_missingPlugins;

    bool PluginsMissing()
    {
        if (g_missingPlugins.empty())
            return false;
        game::ShowMessage("Co-op can't start: " + g_missingPlugins + (g_missingPlugins.find(',') == std::string::npos ?
            " isn't" : " aren't") + " installed (or didn't load). Co-op needs JIP LN NVSE, kNVSE and Real Time Menus "
            "in Fallout New Vegas\\Data\\NVSE\\Plugins. Install them, then restart the game.");
        return true;
    }

    void CheckRealTimeMenus()
    {
        const std::string ini = PluginFolder() + "RealTimeMenus.ini";
        const char* kIni = ini.c_str();
        const char* kKeys[] = {"bPauseContainers", "bPauseBarter", "bPauseServiceRepair", "bPauseRecipeMenu",
            "bPauseLockPickMenu", "bPauseComputerMenu", "bPauseCompanionWheel", "bPausePipBoy", "bPauseDialogue",
            "bPauseVATS"};
        g_pausingMenus.clear();
        for (const char* key : kKeys)
        {
            char value[16] = {};
            GetPrivateProfileStringA("Menus", key, "0", value, sizeof(value), kIni);
            if (value[0] != '0')
                g_pausingMenus += std::string(g_pausingMenus.empty() ? "" : ", ") + key;
        }
        if (!g_pausingMenus.empty())
            LOG_WARN("Real Time Menus pauses menus co-op needs running (%s): set them to 0 in %s",
                g_pausingMenus.c_str(), kIni);
    }

    std::future<coop::ModList> g_modScan;
    constexpr int kModQuestion = 3;
    bool g_modAsked = false;

    void LogLoadOrder(const char* what, const std::vector<std::string>& plugins)
    {
        const size_t kPerLine = 20;
        for (size_t from = 0; from < plugins.size() || from == 0; from += kPerLine)
        {
            std::string list;
            for (size_t i = from; i < plugins.size() && i < from + kPerLine; ++i)
                list += (i > from ? ", " : "") + plugins[i];
            if (from == 0)
                LOG_INFO("%s (%zu): %s", what, plugins.size(), list.c_str());
            else
                LOG_INFO("  ... from %zu: %s", from + 1, list.c_str());
        }
    }

    coop::ModFolders ModFoldersNow()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string game(path);
        game = game.substr(0, game.find_last_of("\\/") + 1);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
            t.wMinute, t.wSecond);
        char* local = nullptr;
        size_t length = 0;
        std::string pluginsTxt;
        if (_dupenv_s(&local, &length, "LOCALAPPDATA") == 0 && local)
            pluginsTxt = std::string(local) + "\\FalloutNV\\plugins.txt";
        free(local);
        std::string manager = GetModuleHandleA("usvfs_x86.dll") ? "Mod Organizer 2" : "";
        return {game + "Data\\", game + "NVCoop Downloads\\", game + "NVCoop Backup\\" + stamp + "\\", pluginsTxt,
            manager};
    }

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

    struct JoinTarget
    {
        net::SteamId steam = 0;
        std::string address;
        std::string label;
        uint16_t port = 0;
    };
    JoinTarget g_joinTarget;

    std::string MyName()
    {
        if (g_config.name == "Player" && game::SteamReady() && !game::SteamName().empty())
            return game::SteamName();
        return g_config.name;
    }

    bool ConnectToHost(bool checkModsOnly)
    {
        if (g_joinTarget.steam)
        {
            if (!game::SteamReady())
                return false;
            auto steam = std::make_unique<net::SteamSession>(game::Steam());
            if (!steam->StartClient(g_joinTarget.steam))
                return false;
            g_coop.JoinOn(std::move(steam), MyName(), checkModsOnly);
            return true;
        }
        std::string address = g_joinTarget.address.empty() ? g_config.address : g_joinTarget.address;
        uint16_t port = g_joinTarget.port ? g_joinTarget.port : g_config.port;
        return g_coop.Join(address, port, MyName(), checkModsOnly);
    }

    enum class MenuCheck { NotYet, Scanning, Comparing, Done } g_menuCheck = MenuCheck::NotYet;
    std::future<coop::ModList> g_menuScan;
    coop::ModList g_menuMods;
    std::vector<std::string> g_menuPlugins;
    uint32_t g_nextMenuCheck = 0;
    bool g_menuMatched = false;

    void LoadOurMatch();
    bool g_joinClicked = false;
    bool g_toldUnreachable = false;

    void CheckModsAtMenu(uint32_t now)
    {
        if (g_config.mode != Config::Mode::Join || g_worldLoaded || g_loadingSave || g_menuCheck == MenuCheck::Done ||
            !coop::IsDue(now, g_nextMenuCheck))
            return;
        switch (g_menuCheck)
        {
        case MenuCheck::NotYet:
            if (g_menuMods.empty())
            {
                std::vector<std::string> plugins = game::LoadedMods();
                if (plugins.empty())
                    return;
                LogLoadOrder("Load order from the game's list", plugins);
                g_menuPlugins = plugins;
                std::string folder = PluginFolder();
                g_menuScan = std::async(std::launch::async, [plugins = std::move(plugins), folder,
                    data = ModFoldersNow().data]
                    { return coop::ScanMods(data, plugins, folder + "NVCoopModHashes.txt", "NVCoop.dll"); });
                g_menuCheck = MenuCheck::Scanning;
                return;
            }
            g_coop.SetMods(g_menuMods);
            g_coop.SetModFolders(ModFoldersNow());
            if (!ConnectToHost(true))
            {
                LOG_ERROR("Couldn't start comparing mods with %s", g_joinTarget.label.c_str());
                g_menuCheck = MenuCheck::Done;
                return;
            }
            g_menuCheck = MenuCheck::Comparing;
            return;
        case MenuCheck::Scanning:
            if (g_menuScan.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            g_menuMods = g_menuScan.get();
            g_menuCheck = MenuCheck::NotYet;
            return;
        case MenuCheck::Comparing:
            if (g_coop.ModsMatched())
            {
                g_menuCheck = MenuCheck::Done;
                g_menuMatched = true;
                if (g_joinClicked)
                    LoadOurMatch();
                else
                    game::Notify("Co-op: your mods match the host's. Load a save to join.", 5);
                return;
            }
            if (g_coop.GetFriendState() == coop::FriendState::Rejected)
            {
                g_coop.Stop();
                g_menuCheck = MenuCheck::Done;
                return;
            }
            if (g_coop.GetRole() != coop::Role::None && g_coop.GetFriendState() != coop::FriendState::Disconnected)
                return;
            g_coop.Stop();
            if (g_joinClicked && !g_toldUnreachable)
            {
                g_toldUnreachable = true;
                game::ShowMessage("Couldn't reach " + (g_joinTarget.label.empty() ? g_config.address : g_joinTarget.label) +
                    " yet. Trying again every 15 seconds: they need to be in a game with Host Co-op.");
            }
            LOG_INFO("Mods: comparing again in 15 s from the main menu");
            g_nextMenuCheck = now + 15000;
            g_menuCheck = MenuCheck::NotYet;
            return;
        case MenuCheck::Done:
            return;
        }
    }

    bool g_openLoadMenu = false;
    bool g_warnNoLargeAddress = false;
    bool HaveSaves();

    constexpr int kHostWhoQuestion = 15;
    bool g_askedHostWho = false;
    void HostFor(bool byIp);

    void HostClicked()
    {
        if (PluginsMissing())
            return;
        g_askedHostWho = game::AskChoice("Who can join your game?", {"Steam friends", "Steam friends, and by IP address",
            "Cancel"}, kHostWhoQuestion);
    }

    void HostFor(bool byIp)
    {
        g_config.ipHosting = byIp;
        if (g_coop.GetRole() != coop::Role::None && !g_coop.InGame())
            g_coop.Stop();
        g_config.mode = Config::Mode::Host;
        g_menuCheck = MenuCheck::Done;
        g_joinClicked = false;
        LOG_INFO("Main menu: Host Co-op%s", byIp ? " (by IP too)" : "");
        if (HaveSaves())
            g_openLoadMenu = true;
        else
            game::ShowMessage("You're hosting co-op: start a new game. Friends can join once you're out of Doc "
                "Mitchell's house.");
    }

    void ShowAddresses()
    {
        if (!g_config.ipHosting || g_coop.GetRole() != coop::Role::Host)
        {
            game::ShowMessage("Joining by IP is off. To let friends join by IP address, pick \"Steam friends, and by IP "
                "address\" when you click Host Co-op.");
            return;
        }
        std::string list;
        for (const std::string& a : net::LocalAddresses())
            list += "\n" + a;
        game::ShowMessage("Friends joining by IP put one of these in JoinAddress (NVCoop.ini), port " +
            std::to_string(g_config.port) + " (UDP):" + (list.empty() ? std::string("\n(none found)") : list) +
            "\n\nOver the internet with port forwarding, it's your router's public address instead (search "
            "\"what is my IP\"). Don't show this on stream.");
    }

    constexpr int kJoinWhoQuestion = 5;
    constexpr int kJoinHowQuestion = 13;
    constexpr int kAddressQuestion = 14;
    bool g_askedWhoToJoin = false, g_askedHowToJoin = false, g_typingAddress = false;
    std::vector<JoinTarget> g_joinChoices;
    enum class JoinHow { SteamFriend, TypeAddress, LastAddress, LanCoop };
    constexpr const char* kLanCoopLabel = "LanCoop (NVCoop.ini's Address)";
    std::vector<JoinHow> g_howChoices;
    size_t g_addressShown = 0;

    void JoinClicked()
    {
        LOG_INFO("Main menu: Join Co-op");
        if (PluginsMissing())
            return;
        std::vector<std::string> buttons;
        g_howChoices.clear();
        if (g_config.lanCoop)
        {
            buttons.push_back(kLanCoopLabel);
            g_howChoices.push_back(JoinHow::LanCoop);
        }
        buttons.push_back("A Steam friend's game");
        g_howChoices.push_back(JoinHow::SteamFriend);
        buttons.push_back("By IP address");
        g_howChoices.push_back(JoinHow::TypeAddress);
        if (!g_config.joinAddress.empty())
        {
            buttons.push_back("The last IP address");
            g_howChoices.push_back(JoinHow::LastAddress);
        }
        buttons.push_back("Cancel");
        g_askedHowToJoin = game::AskChoice("Join how?", buttons, kJoinHowQuestion);
    }

    void JoinSteamFriend()
    {
        g_joinChoices.clear();
        for (const game::SteamFriend& f : game::HostingFriends())
            if (g_joinChoices.size() < 9)
                g_joinChoices.push_back({f.id, "", f.mode.empty() ? f.name : f.name + " (" + f.mode + ")"});
        if (g_joinChoices.empty())
        {
            game::ShowMessage(game::SteamReady() ? "None of your Steam friends are hosting co-op right now. (They "
                "need to be in a game, with Host Co-op.)" : "Joining a Steam friend needs Steam running and logged in. "
                "You can join by IP address instead.");
            return;
        }
        std::vector<std::string> buttons;
        for (const JoinTarget& t : g_joinChoices)
            buttons.push_back(t.label);
        buttons.push_back("Cancel");
        g_askedWhoToJoin = game::AskChoice("Join whose game?", buttons, kJoinWhoQuestion);
    }

    std::string AddressBoxText(size_t typed)
    {
        return "The host's address (an IP like 203.0.113.5, or 203.0.113.5:27960), hidden as you type:\n\n" +
            (typed ? std::string(typed, '*') : std::string("_")) + "\n\nEnter joins, Esc cancels.";
    }

    void StartAddressEntry()
    {
        if (!game::StartTextEntry())
        {
            game::ShowMessage("Couldn't take typing in the game (see NVCoop.log).");
            return;
        }
        g_typingAddress = true;
        g_addressShown = 0;
        game::AskChoice(AddressBoxText(0), {"Cancel"}, kAddressQuestion);
    }

    void JoinHost(const JoinTarget& target);

    void UpdateAddressEntry()
    {
        game::EntryState state = game::TextEntryState();
        int box = game::TakeChoice(kAddressQuestion);
        if (state == game::EntryState::Typing)
        {
            if (box != -1)
            {
                game::StopTextEntry();
                g_typingAddress = false;
                return;
            }
            size_t typed = game::TextEntryLength();
            if (typed != g_addressShown && game::MessageShows(AddressBoxText(g_addressShown)))
            {
                game::SetMessageText(AddressBoxText(typed));
                g_addressShown = typed;
            }
            return;
        }
        g_typingAddress = false;
        game::DropQuestion(kAddressQuestion);
        if (state != game::EntryState::Done)
        {
            game::StopTextEntry();
            return;
        }
        std::string text = game::TakeTextEntry(), host;
        uint16_t port = g_config.port;
        if (!net::ParseHostAddress(text, host, port))
        {
            game::ShowMessage("That isn't an address: type an IP like 203.0.113.5, or 203.0.113.5:27960.");
            return;
        }
        g_config.joinAddress = host;
        g_config.joinPort = port;
        std::string saved = port == g_config.port ? host : host + ":" + std::to_string(port);
        WritePrivateProfileStringA("Coop", "JoinAddress", saved.c_str(), (PluginFolder() + "NVCoop.ini").c_str());
        JoinHost({0, host, "the host by IP", port});
    }

    void JoinHost(const JoinTarget& target)
    {
        if (g_coop.GetRole() != coop::Role::None && !g_coop.InGame() && !g_coop.DownloadingMods())
            g_coop.Stop();
        g_config.mode = Config::Mode::Join;
        g_joinTarget = target;
        g_menuCheck = MenuCheck::NotYet;
        g_nextMenuCheck = 0;
        g_joinClicked = true;
        g_toldUnreachable = false;
        LOG_INFO("Main menu: joining %s", target.label.c_str());
        game::Notify(("Co-op: connecting to " + target.label + "...").c_str(), 3);
    }

    void MainMenuClicks()
    {
        if (g_warnNoLargeAddress && !g_worldLoaded && game::MainMenuMade() && !game::QuestionOpen())
        {
            g_warnNoLargeAddress = false;
            game::ShowMessage("New Vegas is running without the 4GB patch, so it can run out of memory and crash "
                "(most often when alt-tabbing). Install the \"FNV 4GB Patcher\" (Nexus Mods), then start the game "
                "as before.");
        }
        if (g_openLoadMenu && !g_worldLoaded && !g_loadingSave)
        {
            g_openLoadMenu = false;
            game::OpenLoadMenu();
        }
        if (g_askedHostWho)
        {
            int who = game::TakeChoice(kHostWhoQuestion);
            if (who != -1)
            {
                g_askedHostWho = false;
                if (who == 0 || who == 1)
                    HostFor(who == 1);
            }
        }
        if (g_askedHowToJoin)
        {
            int how = game::TakeChoice(kJoinHowQuestion);
            if (how != -1)
            {
                g_askedHowToJoin = false;
                if (how >= 0 && how < static_cast<int>(g_howChoices.size()))
                    switch (g_howChoices[how])
                    {
                    case JoinHow::SteamFriend: JoinSteamFriend(); break;
                    case JoinHow::TypeAddress: StartAddressEntry(); break;
                    case JoinHow::LastAddress:
                        JoinHost({0, g_config.joinAddress, "the host by IP", g_config.joinPort});
                        break;
                    case JoinHow::LanCoop: JoinHost({0, g_config.address, kLanCoopLabel}); break;
                    }
            }
        }
        if (g_typingAddress)
            UpdateAddressEntry();
        int choice = g_askedWhoToJoin ? game::TakeChoice(kJoinWhoQuestion) : -1;
        if (choice < 0)
            return;
        g_askedWhoToJoin = false;
        if (choice < static_cast<int>(g_joinChoices.size()))
            JoinHost(g_joinChoices[choice]);
    }

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

    constexpr int kJoinQuestion = 4;
    std::vector<coop::JoinAsk> g_joinAsks;
    uint32_t g_openJoinAsk = 0;

    void AnswerJoinAsk(bool yes)
    {
        g_coop.AnswerJoin(g_openJoinAsk, yes);
        g_openJoinAsk = 0;
    }

    void AskAboutJoins()
    {
        for (coop::JoinAsk& ask : g_coop.TakeJoinAsks())
            g_joinAsks.push_back(std::move(ask));
        if (g_openJoinAsk)
        {
            int answer = game::TakeAnswer(kJoinQuestion);
            if (answer >= 0)
                AnswerJoinAsk(answer == 1);
        }
        if (g_openJoinAsk || g_joinAsks.empty() || game::QuestionOpen())
            return;
        coop::JoinAsk ask = g_joinAsks.front();
        g_joinAsks.erase(g_joinAsks.begin());
        if (!g_coop.StillAsking(ask.id))
            return;
        std::string text = ask.name + (ask.wantsFiles ? " wants to join, and needs some of your mod files first. Send "
            "them and let " + ask.name + " in?" : " wants to join. Let them in?");
        if (game::AskYesNo(text, kJoinQuestion))
            g_openJoinAsk = ask.id;
        else
            g_coop.AnswerJoin(ask.id, false);
    }

    constexpr int kQuitQuestion = 6;
    bool g_askedToQuit = false;
    bool g_quitSoon = false;

    constexpr int kProgressQuestion = 7;

    constexpr int kCompanionQuestion = 8;
    uint32_t g_companionAsked = 0;

    void AskAboutCompanion(uint32_t now)
    {
        if (uint32_t ref = g_dialogue.TakeCompanionAsk(); ref && !g_companionAsked)
        {
            uint8_t leader = 0;
            TESObjectREFR* ours = g_npcs.OurRefOf(ref);
            if (g_coop.CompanionLeader(ref, leader))
            {
                std::string name = ours && *game::Name(ours) ? game::Name(ours) : "Companion";
                std::string doing = leader == net::kCompanionWaiting ? "waiting" : leader == g_coop.MyPlayerId() ?
                    "following you" : "following " + g_coop.NameOf(leader);
                if (game::AskChoice(name + " (" + doing + ")", {"Follow me", "Wait here", "Talk", "Cancel"},
                    kCompanionQuestion))
                    g_companionAsked = ref;
            }
        }
        if (!g_companionAsked)
            return;
        int choice = game::TakeChoice(kCompanionQuestion);
        if (choice == -1)
            return;
        uint32_t ref = g_companionAsked;
        g_companionAsked = 0;
        if (choice == 0)
            g_coop.OrderCompanion(ref, net::CompanionOrderKind::FollowMe);
        else if (choice == 1)
            g_coop.OrderCompanion(ref, net::CompanionOrderKind::Wait);
        else if (choice == 2)
        {
            g_dialogue.AllowTalk(ref, now + 15000);
            if (TESObjectREFR* ours = g_npcs.OurRefOf(ref))
                game::Activate(ours);
        }
    }
    constexpr int kTeleportQuestion = 12;
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

    uint32_t g_nextAnimLook = 0;

    void ShareFurnitureAnim(uint32_t now)
    {
        if (!coop::IsDue(now, g_nextAnimLook))
            return;
        g_nextAnimLook = now + 100;
        TESObjectREFR* me = game::Player();
        g_coop.SetMyAnim(g_coop.InGame() && me ? game::FurnitureAnim(me) : std::string());
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

    bool g_progressShown = false;
    std::string g_progressText;
    uint32_t g_nextProgress = 0;
    std::string g_pendingResult;

    std::string ProgressText(bool widest, bool done)
    {
        const coop::ModDownload& d = g_coop.Download();
        uint64_t total = d.BytesTotal(), got = widest || done ? total : d.BytesDone();
        size_t files = d.FilesTotal(), file = widest || done ? files : d.Finished().size() + 1;
        if (file > files)
            file = files;
        char text[160];
        std::snprintf(text, sizeof(text), "%s the host's mods\n%.1f of %.1f MB (%u%%), file %zu of %zu",
            done ? "Got" : "Getting", got / 1048576.0, total / 1048576.0,
            total ? static_cast<unsigned>(got * 100 / total) : 100u, file, files);
        return text;
    }

    void StartedFixing()
    {
        if (!g_coop.DownloadingMods() || g_coop.Download().FilesTotal() == 0)
            return;
        g_progressText = ProgressText(true, false);
        g_progressShown = game::AskChoice(g_progressText, {"Cancel"}, kProgressQuestion);
        g_nextProgress = 0;
    }

    void ShowFixResult(const std::string& result)
    {
        if (result.find("Restart the game") != std::string::npos)
            g_askedToQuit = game::AskChoice(result, {"Quit the game", "Later"}, kQuitQuestion);
        else
            game::ShowMessage(result);
    }

    void FixMods(uint32_t now)
    {
        std::string question = g_coop.TakeModQuestion();
        if (!question.empty())
            g_modAsked = game::AskYesNo(question, kModQuestion);
        if (g_quitSoon)
        {
            g_quitSoon = false;
            LOG_INFO("Quitting, to load the mods just fetched");
            game::QuitGame();
        }
        int quit = g_askedToQuit ? game::TakeChoice(kQuitQuestion) : -1;
        if (quit >= 0)
        {
            g_askedToQuit = false;
            g_quitSoon = quit == 0;
        }
        int answer = g_modAsked ? game::TakeAnswer(kModQuestion) : -1;
        if (answer >= 0)
        {
            g_modAsked = false;
            g_coop.AnswerModFix(answer == 1);
            StartedFixing();
        }
        if (g_progressShown)
        {
            int clicked = game::TakeChoice(kProgressQuestion);
            if (clicked == game::kNeverShown)
            {
                g_progressShown = false;
                if (!g_pendingResult.empty())
                    ShowFixResult(std::exchange(g_pendingResult, std::string()));
            }
            else if (clicked >= 0)
            {
                g_progressShown = false;
                if (g_coop.DownloadingMods())
                {
                    LOG_INFO("Mods: the download was cancelled");
                    g_coop.CancelModFix();
                }
                else if (!g_pendingResult.empty())
                    ShowFixResult(std::exchange(g_pendingResult, std::string()));
            }
            else if (!g_pendingResult.empty() && game::MessageShows(g_progressText))
            {
                g_progressText = g_pendingResult.find("Restart the game") != std::string::npos ?
                    ProgressText(false, true) : "Couldn't get the host's mods";
                game::SetMessageText(g_progressText);
                game::SetMessageButton(0, "OK");
            }
            else if (g_coop.DownloadingMods() && coop::IsDue(now, g_nextProgress) &&
                game::MessageShows(g_progressText))
            {
                g_nextProgress = now + 250;
                g_progressText = ProgressText(false, false);
                game::SetMessageText(g_progressText);
            }
        }
        std::string result = g_coop.TakeModFixResult();
        if (!result.empty())
        {
            if (g_progressShown && game::MessageShows(g_progressText))
            {
                g_pendingResult = result;
                g_progressText = result.find("Restart the game") != std::string::npos ? ProgressText(false, true) :
                    "Couldn't get the host's mods";
                game::SetMessageText(g_progressText);
                game::SetMessageButton(0, "OK");
            }
            else if (g_progressShown)
            {
                g_pendingResult = result;
            }
            else
            {
                g_progressShown = false;
                ShowFixResult(result);
            }
            if (g_menuCheck == MenuCheck::Comparing)
                g_menuCheck = MenuCheck::Done;
        }
        if (answer == 0 && g_menuCheck == MenuCheck::Comparing)
            g_menuCheck = MenuCheck::Done;
    }

    void StartCoop()
    {
        if (PluginsMissing())
        {
            LOG_ERROR("Co-op not started: %s missing", g_missingPlugins.c_str());
            return;
        }
        if (!g_pausingMenus.empty())
            game::Notify("Co-op: Real Time Menus still pauses some menus (see NVCoop.log)");
        game::RunConsoleLine("SetGameSetting fVATSTimeMult 1");
        LOG_INFO("VATS time while aiming: %s (1 = full speed)", game::Evaluate("GetGameSetting fVATSTimeMult").c_str());
        LOG_INFO("VATS: %d of its speed changes skipped (5 expected)", game::KeepVatsAtFullSpeed());
        std::vector<std::string> plugins = game::LoadOrder();
        LogLoadOrder("Load order", plugins);
        LOG_INFO("Load order from the game's list %s the script's", game::LoadedMods() == plugins ? "matches" : "DIFFERS from");
        std::string folder = PluginFolder();
        coop::ModFolders modFolders = ModFoldersNow();
        g_coop.SetModFolders(modFolders);
        if (!modFolders.manager.empty())
            LOG_INFO("Mods: run by %s (it sets the load order; co-op doesn't change it)", modFolders.manager.c_str());
        if (!g_menuMods.empty() && plugins == g_menuPlugins)
            g_modScan = std::async(std::launch::async, [mods = g_menuMods] { return mods; });
        else
            g_modScan = std::async(std::launch::async, [plugins = std::move(plugins), folder, data = modFolders.data]
                { return coop::ScanMods(data, plugins, folder + "NVCoopModHashes.txt", "NVCoop.dll"); });
    }

    void ConnectCoop()
    {
        coop::ModList mods = g_modScan.get();
        size_t hashed = 0;
        for (const net::ModFile& f : mods)
            hashed += f.hashed;
        LOG_INFO("Mods: %zu files (%zu fingerprinted; the base game's by size)", mods.size(), hashed);
        g_coop.SetMods(std::move(mods));
        g_coop.SetQuestMods(coop::WithStoryMods(g_config.hostQuestMods, game::LoadOrder()));
        g_coop.SetAutoApprove(g_config.autoApproveJoins);
        const Config& config = g_config;
        switch (config.mode)
        {
        case Config::Mode::Off:
            LOG_INFO("Co-op is off (NVCoop.ini)");
            break;
        case Config::Mode::Menu:
            break;
        case Config::Mode::Host:
        {
            bool byIp = config.ipHosting || config.lanCoop;
            std::unique_ptr<net::SteamSession> steam;
            if (game::SteamReady())
            {
                steam = std::make_unique<net::SteamSession>(game::Steam());
                steam->StartHost(coop::kMaxFriends + 2);
            }
            if (steam && !byIp)
                g_coop.HostOn(std::move(steam), MyName());
            else if (steam)
            {
                if (!g_coop.HostBoth(config.port, std::move(steam), MyName()))
                {
                    LOG_WARN("Port %u is taken: hosting through Steam only", config.port);
                    steam = std::make_unique<net::SteamSession>(game::Steam());
                    steam->StartHost(coop::kMaxFriends + 2);
                    g_coop.HostOn(std::move(steam), MyName());
                }
            }
            else if (!byIp || !g_coop.Host(config.port, MyName()))
            {
                LOG_ERROR("Couldn't host: %s", byIp ? "the port is taken" :
                    "Steam isn't running or logged in (IpHosting=1 lets friends join by IP instead)");
                game::Notify("Co-op: couldn't host (see NVCoop.log)", 8);
            }
            if (config.ipHosting && g_coop.GetRole() == coop::Role::Host)
            {
                LOG_INFO("Joining by IP is on: port %u (UDP)", config.port);
                game::Notify("Co-op: friends can join by IP (your address: MCM > New Vegas Co-op > Show My Address)", 8);
            }
            break;
        }
        case Config::Mode::Join:
            if (!ConnectToHost(false))
                LOG_ERROR("Couldn't start joining %s", g_joinTarget.label.c_str());
            break;
        case Config::Mode::Auto:
            if (!g_coop.Host(config.port, config.name))
            {
                LOG_INFO("Port %u is taken, so joining that game instead", config.port);
                if (!g_coop.Join("127.0.0.1", config.port, config.name + " 2"))
                    LOG_ERROR("Couldn't start joining");
            }
            break;
        }
    }

    bool QuickStartBusy();

    void TryStartCoop()
    {
        if (g_coop.GetRole() != coop::Role::None || g_modScan.valid() || g_config.mode == Config::Mode::Off ||
            g_config.mode == Config::Mode::Menu)
        {
            g_waitingForIntro = false;
            return;
        }
        if (!game::IsPastIntro() || QuickStartBusy())
        {
            if (!g_waitingForIntro)
            {
                LOG_INFO("Co-op waits until the character is made and out of Doc Mitchell's house");
                game::Notify("Co-op starts once you've left Doc Mitchell's house");
            }
            g_waitingForIntro = true;
            return;
        }
        g_waitingForIntro = false;
        StartCoop();
    }

    void OnWorldLoaded()
    {
        if (g_teleportersHad > 0)
            g_teleportersHad = 0;
        if (int dragged = g_coop.Dragging(g_coop.MyPlayerId()); g_coop.InGame() && dragged >= 0)
            g_coop.Drag(static_cast<uint8_t>(dragged), false);
        g_fovAfterLoad = true;
        g_forced.clear();
        g_threats.Clear();
        g_fightersSeen.clear();
        g_bodiesThinned.clear();
        g_extras.clear();
        g_reinforcements.Reset();
        static int s_loggedGrid = 0;
        if (int grid = game::LoadedGrid(); grid != s_loggedGrid)
        {
            s_loggedGrid = grid;
            int buffer = game::ExteriorCellBuffer(), needed = (grid + 1) * (grid + 1);
            LOG_INFO("Range: the game loads %dx%d cells around the player (uGridsToLoad), so other players, NPCs, "
                "doors and items show within %.0f units (%.0f m); uExterior Cell Buffer %d", grid, grid,
                game::LoadedReach(), game::LoadedReach() * 0.01428f, buffer);
            if (buffer >= 0 && buffer < needed)
            {
                LOG_WARN("Range: uExterior Cell Buffer is %d; with uGridsToLoad %d it should be at least %d (in "
                    "Fallout.ini and FalloutPrefs.ini), or the game can crash", buffer, grid, needed);
                game::Notify(("Co-op: uGridsToLoad is " + std::to_string(grid) + " but uExterior Cell Buffer only " +
                    std::to_string(buffer) + "; set it to " + std::to_string(needed) + " or the game can crash").c_str(),
                    15);
            }
        }
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
        CheckCrashReporter();
        if (!game::CompileScripts())
        {
            game::Notify("NVCoop couldn't start, see NVCoop.log");
            return;
        }
        game::CheckMoverMap();
        std::vector<uint32_t> leftover = game::SavedMadeCharacters();
        size_t removed = 0;
        for (uint32_t id : leftover)
        {
            TESObjectREFR* ref = game::LookupRef(id);
            if (ref && game::IsActor(ref) && ref != game::Player())
            {
                game::Remove(ref);
                ++removed;
            }
            else if (ref)
                LOG_INFO("Not removing %08X: noted in the save as ours, but now it's something else", id);
        }
        if (!leftover.empty())
            LOG_INFO("Removed %zu co-op character(s) left in the save (friends', copies of the host's NPCs; %zu "
                "listed)", removed, leftover.size());
        game::NoteMadeCharacters({});
        g_notedMade.clear();
        g_worldLoaded = true;
        g_startSoon = true;
        g_nextIntroCheck = GetTickCount() + 1000;
    }

    void FollowGroup(uint32_t now)
    {
        if (!g_bringFollowers.empty())
        {
            coop::Pose me;
            if (game::GetPose(game::Player(), me) && me.space == g_travel.space)
            {
                for (uint32_t id : g_bringFollowers)
                    if (TESObjectREFR* follower = game::LookupRef(id); follower && !game::IsDead(follower))
                    {
                        game::MoveOnto(follower, game::Player());
                        LOG_INFO("Companions: brought %s (%08X) along", game::Name(follower), id);
                    }
                g_bringFollowers.clear();
            }
            else if (coop::IsDue(now, g_bringUntil))
                g_bringFollowers.clear();
        }
        coop::Pose to;
        uint8_t leader;
        if (g_coop.TakeTravel(to, leader))
        {
            g_travel = to;
            g_travelLeader = leader;
            g_hasTravel = true;
            g_travelAt = now + kFollowSeconds * 1000;
            g_countdownLeft = kFollowSeconds;
            LOG_INFO("%s went to place %08X; following in %d s", g_coop.NameOf(leader).c_str(), to.space,
                kFollowSeconds);
        }
        if (!g_hasTravel)
            return;
        std::string name = g_coop.NameOf(g_travelLeader);
        if (g_countdownLeft > 0 && coop::IsDue(now, g_travelAt - g_countdownLeft * 1000))
        {
            game::Notify(("Following " + name + " in " + std::to_string(g_countdownLeft)).c_str(), 0.9f);
            --g_countdownLeft;
        }
        if (!coop::IsDue(now, g_travelAt) || game::InMenu())
            return;
        g_hasTravel = false;
        if (!game::TravelTo(g_travel))
        {
            LOG_WARN("%s went to place %08X, which isn't a place in this game (mods differ?)", name.c_str(),
                g_travel.space);
            return;
        }
        LOG_INFO("Following %s to place %08X (%.0f, %.0f, %.0f)", name.c_str(), g_travel.space, g_travel.x,
            g_travel.y, g_travel.z);
        if (g_coop.GetRole() == coop::Role::Host)
        {
            g_bringFollowers = game::Followers();
            g_bringUntil = now + 30000;
        }
    }

    void ApplyFriendHit(const coop::HitReport& hit)
    {
        TESObjectREFR* by = g_standIns.Character(hit.playerId);
        TESObjectREFR* target = game::LookupRef(hit.targetRef);
        coop::Pose at, from;
        bool ok = by && game::IsActor(target) && target != game::Player() && !g_standIns.IsStandIn(target) &&
            game::GetPose(target, at) && game::GetPose(by, from) && at.space == from.space;
        if (ok && game::IsTeammate(target))
        {
            LOG_DEBUG("Player %u hit our companion %08X: no friendly fire in the party", hit.playerId, hit.targetRef);
            return;
        }
        float dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
        if (!ok || dx * dx + dy * dy + dz * dz > MaxHitDistance() * MaxHitDistance())
        {
            LOG_INFO("Ignored a hit from player %u on %08X", hit.playerId, hit.targetRef);
            ++g_tally.ignored;
            return;
        }
        g_tally.applied.Add(hit.damage);
        LOG_DEBUG("Player %u hit %08X for %.1f", hit.playerId, hit.targetRef, hit.damage);
        game::ApplyHit(target, by, hit.damage);
        g_threats.Hurt(hit.targetRef, hit.playerId, hit.damage, GetTickCount());
    }

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

    const std::string kDownedAnim = "Characters\\_Male\\IdleAnims\\dlcpittlayingonfloorinpain.kf";

    void SetDowned(bool down, uint32_t now)
    {
        if (down == g_downed)
            return;
        g_downed = down;
        g_downedAt = g_nextBleedNotice = now;
        game::SetPlayerControls(!down);
        if (down)
            game::PlayAnimFile(game::Player(), kDownedAnim);
        else
            game::StopAnimFile(game::Player(), kDownedAnim);
        LOG_INFO("%s", down ? "We're down" : "We're back up");
    }

    std::string g_revivingName;

    void UpdateReviveBar(uint32_t now)
    {
        if (g_reviving < 0 || !g_coop.InGame())
        {
            game::ShowReviveBar(g_coop.InGame() ? g_reviveHint : "");
            return;
        }
        constexpr int kCells = 20;
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
            return;
        }

        if (game::IsKnockedOut(player))
        {
            LOG_INFO("Knocked out (life state %d, health %.1f), waking up", game::LifeState(player), game::Health(player));
            game::WakeUp(player);
            game::ChangeHealth(player, 1 - game::Health(player));
            SetDowned(true, now);
            return;
        }
        static uint32_t s_nextLifeLog = 0;
        if (healthFresh && (health < maxHealth * 0.25f || g_downed) && coop::IsDue(now, s_nextLifeLog))
        {
            s_nextLifeLog = now + 1000;
            LOG_DEBUG("Our health %.1f / %.1f, life state %d%s", game::Health(player), maxHealth,
                game::LifeState(player), g_downed ? ", downed" : "");
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
        bool holding = target >= 0 && coop::IsDue(now, g_reviveAgainAt) &&
            game::IsControlPressed(game::layout::kControlActivate);
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
        }
    }

    std::string LabStatus()
    {
        char out[400];
        coop::Pose me;
        TESObjectREFR* player = game::Player();
        bool here = g_worldLoaded && game::GetPose(player, me);
        const char* role = g_coop.GetRole() == coop::Role::Host ? "hosting" :
            g_coop.GetRole() == coop::Role::Friend ? (g_coop.InGame() ? "joined" : "joining") : "off";
        if (!here)
        {
            std::snprintf(out, sizeof(out), "world %s; co-op %s", g_loadingSave ? "loading" : "not loaded", role);
            return out;
        }
        std::snprintf(out, sizeof(out), "world loaded%s; co-op %s (%zu others); player at %.0f %.0f %.0f facing "
            "%.0f deg in %08X; health %.0f / %.0f", game::InMenu() ? " (a menu is open)" : "", role,
            g_coop.Remotes().size(), me.x, me.y, me.z, me.heading * 57.2957795f, me.space, game::Health(player),
            game::MaxHealth(player));
        if (uint8_t locked = game::PlayerDisabledControls())
            std::snprintf(out + std::strlen(out), sizeof(out) - std::strlen(out), "; controls locked %02X", locked);
        for (const auto& [id, remote] : g_coop.Remotes())
        {
            coop::Pose at;
            if (game::GetPose(g_standIns.Character(id), at))
                std::snprintf(out + std::strlen(out), sizeof(out) - std::strlen(out), "; player %u at %.0f %.0f %.0f", id,
                    at.x, at.y, at.z);
        }
        return out;
    }

    std::vector<std::string> g_labChat;
    std::string PlayerSettingsPath() { return PluginFolder() + "..\\..\\config\\NVCoop.ini"; }
    void FollowPlayerSettings(uint32_t now)
    {
        static uint32_t s_nextLook = 0;
        static FILETIME s_written = {};
        if (!coop::IsDue(now, s_nextLook))
            return;
        s_nextLook = now + 2000;
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExA(PlayerSettingsPath().c_str(), GetFileExInfoStandard, &data) ||
            CompareFileTime(&data.ftLastWriteTime, &s_written) == 0)
            return;
        bool first = s_written.dwLowDateTime == 0 && s_written.dwHighDateTime == 0;
        s_written = data.ftLastWriteTime;
        if (first)
            return;
        bool showedAddress = g_config.showAddress;
        g_config.LoadPlayer(PlayerSettingsPath());
        if (g_config.showAddress && !showedAddress)
            ShowAddresses();
        coop::SaveMode switches = coop::ModeFromSwitches(g_config.coopBalanced, g_config.friendlyFire);
        if (!(switches == g_ourMode))
        {
            g_ourMode = switches;
            LOG_INFO("Mode: %s (changed in the menu)", coop::Describe(g_ourMode).c_str());
        }
        LOG_INFO("Settings: changed in the menu (chat %d, key %d, speech %d at %d%%, voice %d/%d pitch %d speed %d "
            "effect %d, voice chat %d key %d mode %d at %d%%, radio %d, desync notices %d, pings %d key %d sound %d radius %d)", g_config.chat,
            g_config.chatKey, g_config.chatSpeech, g_config.chatSpeechVolume, g_config.voice, g_config.voiceVariant,
            g_config.voicePitch, g_config.voiceSpeed, g_config.voiceEffect, g_config.voiceChat, g_config.voiceChatKey,
            g_config.voiceChatMode, g_config.voiceChatVolume, g_config.syncRadio, g_config.desyncNotices, g_config.pings,
            g_config.pingKey, g_config.pingSound, g_config.pingRadius);
    }

    void UpdateChat()
    {
        static bool s_keyWas = false;
        bool on = g_config.chat && g_coop.InGame() && g_worldLoaded;
        if (!on && game::ChatTyping())
            game::CancelChat();
        bool key = on && !game::ChatTyping() && game::IsKeyPressed(g_config.chatKey);
        if (s_keyWas && !key && on && !game::ChatTyping())
            game::OpenChat();
        s_keyWas = key;
        std::vector<std::string> typed = game::UpdateChat();
        for (std::string& t : g_labChat)
            typed.push_back(std::move(t));
        g_labChat.clear();
        for (const std::string& text : typed)
        {
            std::string line = coop::CleanChat(text);
            if (!on || line.empty())
                continue;
            LOG_INFO("Chat: we say \"%s\"", line.c_str());
            coop::VoiceStyle style{static_cast<uint8_t>(g_config.voice), static_cast<uint8_t>(g_config.voiceVariant),
                static_cast<int8_t>(g_config.voicePitch), static_cast<int8_t>(g_config.voiceSpeed),
                static_cast<uint8_t>(g_config.voiceEffect)};
            g_coop.SendChat(line, style);
            if (g_config.chatSpeech)
                g_speech.Say(line, g_coop.MyPlayerId(), style, g_config.chatSpeechVolume / 100.0f, 0);
        }
        for (const coop::Coop::ChatHeard& h : g_coop.TakeChat())
        {
            LOG_INFO("Chat: %s says \"%s\"", h.name.c_str(), h.text.c_str());
            if (!g_config.chat)
                continue;
            TESObjectREFR* who = g_standIns.Character(h.player);
            if (who && game::DistanceToPlayer(who) < 2500)
                game::ShowChatAbove(h.player, who, h.text);
            else
                game::Notify((h.name + ": " + h.text).c_str(), 6);
            coop::Pose me, them;
            if (g_config.chatSpeech && who && game::GetPose(game::Player(), me) && game::GetPose(who, them) &&
                me.space == them.space)
            {
                coop::SpeechMix mix = coop::MixSpeech(them.x - me.x, them.y - me.y, them.z - me.z, me.heading);
                if (mix.volume > 0.02f)
                    g_speech.Say(h.text, h.player, h.style, mix.volume * g_config.chatSpeechVolume / 100.0f, mix.pan);
            }
        }
        game::UpdateChatAbove();
        static std::map<uint8_t, uint32_t> s_downedSince;
        static uint32_t s_lastMarks = 0;
        uint32_t now = GetTickCount(), step = s_lastMarks ? now - s_lastMarks : 0;
        s_lastMarks = now;
        std::vector<game::FriendMark> friends;
        for (const auto& [id, remote] : g_coop.InGame() ? g_coop.Remotes() : std::map<uint8_t, coop::RemotePlayer>())
        {
            bool dragged = g_coop.DraggedBy(id) >= 0;
            if (!remote.Downed())
                s_downedSince.erase(id);
            else if (!s_downedSince.count(id))
                s_downedSince[id] = now;
            else if (dragged)
                s_downedSince[id] += step;
            TESObjectREFR* who = g_standIns.Character(id);
            if (!who || g_cutscene || (!g_config.friendMarkers && !remote.Downed()))
                continue;
            if (!remote.Downed())
            {
                friends.push_back({who, g_coop.NameOf(id), false});
                continue;
            }
            uint32_t gone = now - s_downedSince[id];
            uint32_t left = gone < coop::kBleedOutMs ? (coop::kBleedOutMs - gone + 999) / 1000 : 0;
            friends.push_back({who, g_coop.NameOf(id) + (dragged ? std::string("\nDOWNED, dragged") :
                "\nDOWNED " + std::to_string(left) + "s"), true});
        }
        if (!g_coop.InGame())
            s_downedSince.clear();
        game::ShowFriendMarkers(friends);
    }

    bool g_labTalking = false;
    double QpcMs(const LARGE_INTEGER& from, const LARGE_INTEGER& to)
    {
        static LARGE_INTEGER s_freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        return 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(s_freq.QuadPart);
    }

    void LetGo(uint32_t enemy)
    {
        if (g_forced.erase(enemy))
            game::ForceTarget(game::LookupRef(enemy), nullptr);
    }

    void UpdateFighting(uint32_t now)
    {
        bool friendGame = g_coop.InGame() && g_coop.GetRole() == coop::Role::Friend;
        bool fighting = friendGame && g_coop.HostSaysFighting();
        static bool s_held = false;
        if (fighting != s_held)
            LOG_INFO("Level up: %s", fighting ? "enemies are going for us in the host's game; any level-up waits" :
                "no enemies going for us; a level-up can show");
        s_held = fighting;
        game::HoldLevelUp(fighting);

        static uint32_t s_nextLook = 0;
        if (g_coop.GetRole() != coop::Role::Host || !g_coop.InGame() || !coop::IsDue(now, s_nextLook))
            return;
        s_nextLook = now + 500;
        std::map<uint8_t, int> enemies;
        std::vector<TESObjectREFR*> actors;
        game::NearbyActors(actors);
        for (TESObjectREFR* a : actors)
        {
            if (a == game::Player() || g_standIns.IsStandIn(a) || game::IsDead(a) || game::IsTeammate(a))
                continue;
            if (uint32_t target = game::CombatTarget(a))
                if (int id = g_standIns.PlayerIdOf(game::LookupRef(target)); id >= 0)
                    ++enemies[static_cast<uint8_t>(id)];
        }
        for (const auto& [id, remote] : g_coop.Remotes())
            g_coop.SetFighting(id, enemies.count(id) > 0);
    }

    void UpdateThreats(uint32_t now, const coop::Pose& me)
    {
        bool hosting = g_coop.GetRole() == coop::Role::Host && g_coop.InGame();
        for (auto it = g_forced.begin(); it != g_forced.end();)
        {
            uint8_t player = it->second.player;
            TESObjectREFR* character = !hosting ? nullptr : player == coop::kHostPlayerId ? game::Player() :
                g_coop.Remotes().count(player) ? g_standIns.Character(player) : nullptr;
            if (character && character == it->second.target)
            {
                ++it;
                continue;
            }
            game::ForceTarget(game::LookupRef(it->first), nullptr);
            it = g_forced.erase(it);
        }
        if (!hosting)
        {
            g_threats.Clear();
            return;
        }
        if (!coop::IsDue(now, g_nextThreatLook))
            return;
        g_nextThreatLook = now + 1000;
        for (uint32_t enemy : g_threats.Stale(now))
        {
            LetGo(enemy);
            g_threats.Forget(enemy);
        }
        for (uint32_t enemy : g_threats.Enemies())
        {
            TESObjectREFR* e = game::LookupRef(enemy);
            coop::Pose at;
            if (!e || game::IsDead(e) || !game::GetPose(e, at))
            {
                LetGo(enemy);
                g_threats.Forget(enemy);
                continue;
            }
            if (game::IsTeammate(e) || !game::CombatTarget(e))
            {
                LetGo(enemy);
                continue;
            }
            std::vector<coop::ThreatTable::Player> players;
            if (me.space == at.space)
                players.push_back({coop::kHostPlayerId, std::hypot(me.x - at.x, me.y - at.y), g_downed});
            for (const auto& [id, remote] : g_coop.Remotes())
            {
                coop::Pose them;
                TESObjectREFR* who = g_standIns.Character(id);
                if (who && game::GetPose(who, them) && them.space == at.space)
                    players.push_back({id, std::hypot(them.x - at.x, them.y - at.y), remote.Downed()});
            }
            int pick = g_threats.Pick(enemy, players, now);
            if (pick < 0)
            {
                LetGo(enemy);
                continue;
            }
            auto held = g_forced.find(enemy);
            if (held != g_forced.end() && held->second.player == pick)
                continue;
            TESObjectREFR* target = pick == coop::kHostPlayerId ? game::Player() :
                g_standIns.Character(static_cast<uint8_t>(pick));
            if (!target)
                continue;
            game::ForceTarget(e, target);
            g_forced[enemy] = HeldTo{static_cast<uint8_t>(pick), target};
            LOG_DEBUG("Threat: %08X %s goes for %s", enemy, game::Name(e),
                pick == coop::kHostPlayerId ? "the host" : g_coop.NameOf(static_cast<uint8_t>(pick)).c_str());
        }
    }

    void UpdatePings()
    {
        static bool s_keyWas = false;
        static coop::PingGate s_gate(coop::kPingSendGapMs);
        static coop::PingBoard s_board;
        uint32_t now = GetTickCount();
        bool on = g_config.pings && g_coop.InGame() && g_worldLoaded && !g_loadingSave;
        if (!on)
            s_board.Clear();
        coop::Pose me;
        bool placed = on && game::GetPose(game::Player(), me);
        bool key = placed && !g_cutscene && !game::ChatTyping() && game::IsKeyPressed(g_config.pingKey);
        if (key && !s_keyWas && s_gate.Allow(now))
        {
            game::CrosshairPick aim;
            bool hit = game::CrosshairPoint(aim) && aim.hit;
            coop::PingLook look = coop::ClassifyPing(
                coop::ChosenPingThing(hit ? aim.thing : coop::PingThing::Nothing, g_config.pingObjects), aim.name);
            net::Ping ping;
            ping.kind = look.kind;
            ping.space = me.space;
            ping.x = hit ? aim.at[0] : me.x;
            ping.y = hit ? aim.at[1] : me.y;
            ping.z = hit ? aim.at[2] : me.z;
            ping.name = look.name;
            float at[3] = {ping.x, ping.y, ping.z}, sx = 0, sy = 0;
            bool inView = false;
            game::SpotOnScreen(at, sx, sy, inView);
            LOG_INFO("Pings: we mark %.0f %.0f %.0f in place %08X (%s, kind %u, ref %08X thing %u, %.0f away on \"%s\"), %.3f %.3f on screen",
                ping.x, ping.y, ping.z, ping.space, ping.name.c_str(), static_cast<unsigned>(ping.kind), aim.ref,
                static_cast<unsigned>(aim.thing), aim.distance, aim.what, sx, sy);
            g_coop.SendPing(ping);
            s_board.Add(g_coop.MyPlayerId(), "You", ping.name, ping.x, ping.y, ping.z, now, static_cast<uint8_t>(ping.kind));
            game::PlayPingSound(g_config.pingSound);
        }
        s_keyWas = key;
        const auto& remotes = g_coop.Remotes();
        for (coop::Coop::PingHeard& h : g_coop.TakePings())
        {
            coop::Pose sender;
            auto remote = remotes.find(h.ping.player);
            bool known = remote != remotes.end() && remote->second.motion.Newest(sender);
            bool inRange = placed && h.ping.space == me.space && coop::PingInRange(&me, known ? &sender : nullptr, g_config.pingRadius);
            LOG_INFO("Pings: %s marks %.0f %.0f %.0f in place %08X (%s), %s", h.who.c_str(), h.ping.x, h.ping.y, h.ping.z,
                h.ping.space, h.ping.name.c_str(), inRange ? "shown" : "out of range");
            if (!inRange)
                continue;
            s_board.Add(h.ping.player, h.who, h.ping.name, h.ping.x, h.ping.y, h.ping.z, now, static_cast<uint8_t>(h.ping.kind));
            game::PlayPingSound(g_config.pingSound);
        }
        s_board.Expire(now);
        std::array<game::PingMark, coop::kPingSlots> marks;
        for (int slot = 0; slot < coop::kPingSlots; ++slot)
        {
            const coop::ActivePing& p = s_board.Slots()[slot];
            if (!p.used)
                continue;
            if (p.player != g_coop.MyPlayerId() && !remotes.count(p.player))
            {
                s_board.Remove(p.player);
                continue;
            }
            marks[slot].active = true;
            marks[slot].text = coop::PingLine(p.who, p.name, p.x - me.x, p.y - me.y);
            marks[slot].at[0] = p.x;
            marks[slot].at[1] = p.y;
            marks[slot].at[2] = p.z;
            marks[slot].kind = p.kind;
        }
        game::ShowPings(marks, static_cast<coop::PingIcons>(g_config.pingIcons));
    }

    void UpdateVoice()
    {
        bool on = g_config.voiceChat && g_coop.InGame() && g_worldLoaded;
        bool talk = on && !game::ChatTyping() &&
            (g_config.voiceChatMode == 1 || g_labTalking || game::KeyDown(g_config.voiceChatKey));
        static bool s_talking = false, s_warned = false;
        if (talk != s_talking)
        {
            if (!game::VoiceRecording(talk) && talk && !s_warned)
            {
                s_warned = true;
                LOG_WARN("Voice: Steam's voice isn't there; no voice chat");
            }
            s_talking = talk;
            LOG_DEBUG("Voice: %s", talk ? "talking" : "stopped talking");
        }
        game::ShowTalking(talk && g_config.voiceChatMode == 0);
        net::Bytes recorded;
        for (int i = 0; i < 4 && game::TakeVoice(recorded); ++i)
            g_coop.SendVoice(recorded);
        std::vector<int16_t> samples;
        for (const coop::Coop::VoiceHeard& v : g_coop.TakeVoice())
            if (g_config.voiceChat && game::DecompressVoice(v.data, coop::kVoiceRate, samples))
                g_voiceOut.Push(v.player, samples);
        coop::Pose me;
        bool here = game::GetPose(game::Player(), me);
        for (const auto& [id, remote] : g_coop.Remotes())
        {
            coop::Pose them;
            TESObjectREFR* who = g_standIns.Character(id);
            coop::SpeechMix mix{0, 0};
            if (here && who && game::GetPose(who, them) && them.space == me.space)
                mix = coop::MixSpeech(them.x - me.x, them.y - me.y, them.z - me.z, me.heading);
            g_voiceOut.SetMix(id, mix.volume * g_config.voiceChatVolume / 100.0f, mix.pan);
            static std::map<uint8_t, bool> s_moving;
            float opening = g_config.mouths ? coop::MouthOpening((std::max)(g_voiceOut.Level(id), g_speech.Level(id))) : 0;
            bool& moving = s_moving[id];
            if (who && (opening > 0 || moving))
                game::SetMouth(who, opening);
            moving = opening > 0;
        }
    }

    constexpr int kModeQuestion = 11;
    bool g_modeAsked = false;
    bool g_hardcoreAsked = false;
    void AskSwitches()
    {
        if (!g_config.askMode)
            return LOG_INFO("Mode: not asked (AskMode=0): %s", coop::Describe(g_ourMode).c_str());
        game::AskChoice("A new game: turn on any co-op settings?\n\nCo-op balanced: harder and made for a group "
            "(smarter enemies, scarcer supplies, slower leveling). Leave it off if you use mods that change the game's "
            "balance.\nFriendly fire: players can hurt each other.\n\nThe host's settings count for everyone. Change "
            "them any time in the Mod Configuration Menu (New Vegas Co-op).", coop::SaveModeButtons(), kModeQuestion);
        g_modeAsked = true;
        LOG_INFO("Mode: asked (a new game; now %s)", coop::Describe(g_ourMode).c_str());
    }
    void PickSwitches(const coop::SaveMode& picked)
    {
        g_config.coopBalanced = picked.mode == coop::GameMode::Balanced;
        g_config.friendlyFire = picked.friendlyFire;
        g_config.WriteSwitches(PlayerSettingsPath());
        g_ourMode = picked;
        LOG_INFO("Mode: %s (picked; kept in the MCM page's settings)", coop::Describe(g_ourMode).c_str());
    }
    coop::SaveMode EffectiveMode()
    {
        return g_coop.GetRole() == coop::Role::Friend && g_coop.InGame() ? g_coop.SessionMode() : g_ourMode;
    }

    void UpdateLootThinning(uint32_t now)
    {
        if (g_coop.GetRole() == coop::Role::Friend || EffectiveMode().mode != coop::GameMode::Balanced ||
            !coop::IsDue(now, g_nextThinLook))
            return;
        g_nextThinLook = now + 1000;
        if (g_bodiesThinned.size() > 4096)
            g_bodiesThinned.clear();
        std::vector<TESObjectREFR*> actors;
        game::NearbyActors(actors);
        for (TESObjectREFR* a : actors)
        {
            uint32_t id = game::RefId(a);
            if (a == game::Player() || !game::IsDead(a) || g_standIns.IsStandIn(a) || g_bodiesThinned.count(id))
                continue;
            g_bodiesThinned.insert(id);
            if (game::IsGeneric(a))
            {
                int gone = game::ThinAmmo(a, static_cast<int>(coop::CoopBalance().bodyAmmoKept * 100 + 0.5f));
                LOG_DEBUG("Balance: %08X %s's body keeps %.0f%% of its ammo (%d rounds taken)", id, game::Name(a),
                    coop::CoopBalance().bodyAmmoKept * 100, gone);
            }
        }
    }

    void UpdateReinforcements(uint32_t now)
    {
        if (g_coop.GetRole() != coop::Role::Host || !g_coop.InGame() || !coop::IsDue(now, g_nextReinforceLook))
            return;
        int players = 1 + static_cast<int>(g_coop.Remotes().size());
        g_nextReinforceLook = now + 1000;
        if (g_fightersSeen.size() > 4096)
            g_fightersSeen.clear();
        constexpr float kClearDistance = 6000;
        int alive = 0;
        for (auto it = g_extras.begin(); it != g_extras.end();)
        {
            TESObjectREFR* e = game::LookupRef(*it);
            if (!e)
            {
                it = g_extras.erase(it);
                continue;
            }
            coop::Pose at, p;
            bool seen = false;
            if (game::GetPose(e, at))
            {
                seen = game::GetPose(game::Player(), p) && p.space == at.space &&
                    std::hypot(p.x - at.x, p.y - at.y) < kClearDistance;
                for (const auto& [pid, remote] : g_coop.Remotes())
                {
                    TESObjectREFR* who = g_standIns.Character(pid);
                    seen = seen || (who && game::GetPose(who, p) && p.space == at.space &&
                        std::hypot(p.x - at.x, p.y - at.y) < kClearDistance);
                }
            }
            if (!seen)
            {
                LetGo(*it);
                g_threats.Forget(*it);
                LOG_DEBUG("Bigger fights: extra %08X cleared away (%s)", *it, game::IsDead(e) ? "dead" : "left behind");
                game::RemoveWorldItem(e);
                it = g_extras.erase(it);
                continue;
            }
            alive += !game::IsDead(e);
            ++it;
        }
        if (players < 2 || EffectiveMode().mode != coop::GameMode::Balanced)
            return;
        std::vector<TESObjectREFR*> actors;
        game::NearbyActors(actors);
        TESObjectREFR *copyOf = nullptr, *copyTarget = nullptr;
        for (TESObjectREFR* a : actors)
        {
            uint32_t id = game::RefId(a);
            if (a == game::Player() || g_standIns.IsStandIn(a) || game::IsDead(a))
                continue;
            bool seen = g_fightersSeen.count(id) != 0;
            if (seen && (copyOf || !g_reinforcements.Owed()))
                continue;
            uint32_t targetId = game::CombatTarget(a);
            TESObjectREFR* target = targetId ? game::LookupRef(targetId) : nullptr;
            if (!target || (target != game::Player() && !g_standIns.IsStandIn(target)))
                continue;
            bool generic = game::IsGeneric(a);
            if (!seen)
            {
                g_fightersSeen.insert(id);
                LOG_DEBUG("Bigger fights: %08X %s fights one of us (%s)", id, game::Name(a),
                    generic ? "generic" : "named: no extras");
                if (generic)
                    g_reinforcements.EnemyJoined(players);
            }
            if (generic)
            {
                copyOf = a;
                copyTarget = target;
            }
        }
        if (copyOf && g_reinforcements.Take(alive))
            if (TESObjectREFR* made = game::PlaceExtraEnemy(copyOf, copyTarget))
            {
                g_extras.push_back(game::RefId(made));
                g_fightersSeen.insert(game::RefId(made));
                LOG_INFO("Bigger fights: an extra %s (%08X) joins, behind %08X (%d players, %d extras alive)",
                    game::Name(made), game::RefId(made), game::RefId(copyOf), players, alive + 1);
            }
    }

    void UpdateBalance()
    {
        static int s_applied = -1;
        if (!g_worldLoaded || g_loadingSave)
            return;
        int balanced = EffectiveMode().mode == coop::GameMode::Balanced ? 1 : 0;
        if (balanced == s_applied || (s_applied == -1 && !balanced))
        {
            s_applied = s_applied == -1 ? 0 : s_applied;
            return;
        }
        if (game::ApplyBalance(balanced ? &coop::CoopBalance() : nullptr))
            s_applied = balanced;
    }

    void UpdateSaveMode()
    {
        if (g_modeAsked)
        {
            int choice = game::TakeChoice(kModeQuestion);
            if (choice != -1)
            {
                g_modeAsked = false;
                coop::SaveMode picked = coop::SaveModeFromButton(choice);
                if (picked.mode != coop::GameMode::Unset)
                    PickSwitches(picked);
            }
        }
        if (g_coop.GetRole() == coop::Role::Host)
            g_coop.SetSessionMode(g_ourMode);
        static coop::SaveMode s_hostMode;
        static bool s_wasIn = false;
        bool in = g_coop.GetRole() == coop::Role::Friend && g_coop.InGame();
        if (in && s_wasIn && !(g_coop.SessionMode() == s_hostMode))
        {
            game::Notify(("Co-op: the host's settings are now " + coop::Describe(g_coop.SessionMode())).c_str());
            LOG_INFO("Mode: shown to the player: the host's settings are now %s",
                coop::Describe(g_coop.SessionMode()).c_str());
        }
        s_hostMode = g_coop.SessionMode();
        s_wasIn = in;
    }

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

    void CrashAtExit()
    {
        LOG_WARN("Lab: crashing as the game closes; Steam's steamclient.dll is %s",
            GetModuleHandleA("steamclient.dll") ? "still loaded" : "gone");
        *reinterpret_cast<volatile int*>(0x10) = 1;
    }

    std::string RunLabLine(std::string line)
    {
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        while (!line.empty() && line.front() == ' ')
            line.erase(line.begin());
        LOG_INFO("Lab: %s", _strnicmp(line.c_str(), "typeaddress ", 12) == 0 ? "typeaddress (hidden)" : line.c_str());
        if (line.empty())
            return "empty line";
        if (_stricmp(line.c_str(), "status") == 0)
            return LabStatus();
        if (line.compare(0, 11, "killcopies ") == 0 && g_coop.GetRole() == coop::Role::Friend)
        {
            int wanted = std::atoi(line.c_str() + 11);
            std::vector<TESObjectREFR*> actors;
            game::NearbyActors(actors);
            int killed = 0;
            for (TESObjectREFR* a : actors)
                if (killed < wanted && a != game::Player() && !game::IsDead(a) &&
                    std::strstr(game::Name(a), "Powder Ganger") && g_npcs.HostRefOf(game::RefId(a)))
                {
                    game::Kill(a);
                    ++killed;
                }
            return "killed " + std::to_string(killed) + " copies here";
        }
        if (line.compare(0, 10, "friendhit ") == 0 && g_coop.GetRole() == coop::Role::Friend)
        {
            std::vector<TESObjectREFR*> actors;
            game::NearbyActors(actors);
            TESObjectREFR* nearest = nullptr;
            for (TESObjectREFR* a : actors)
                if (a != game::Player() && !game::IsDead(a) && std::strstr(game::Name(a), "Powder Ganger") &&
                    (!nearest || game::DistanceToPlayer(a) < game::DistanceToPlayer(nearest)))
                    nearest = a;
            uint32_t hostRef = nearest ? g_npcs.HostRefOf(game::RefId(nearest)) : 0;
            if (!hostRef)
                return "no Powder Ganger copy near us";
            g_coop.ReportHit(hostRef, static_cast<float>(std::atof(line.c_str() + 10)));
            char out[64];
            std::snprintf(out, sizeof(out), "hit the host's %08X", hostRef);
            return out;
        }
        if (line.compare(0, 4, "say ") == 0)
        {
            g_labChat.push_back(line.substr(4));
            return "said";
        }
        if (line.size() == 6 && line.compare(0, 5, "mode ") == 0 && line[5] >= '0' && line[5] <= '3')
        {
            if (!g_modeAsked)
                return "no mode question open";
            g_modeAsked = false;
            game::DropQuestion(kModeQuestion);
            PickSwitches(coop::SaveModeFromButton(line[5] - '0'));
            return coop::Describe(g_ourMode);
        }
        if (line == "talk on" || line == "talk off")
        {
            g_labTalking = line == "talk on";
            return g_labTalking ? "talking" : "not talking";
        }
        if (line == "chatbox")
            return game::OpenChat() ? "opened" : "can't open";
        if (line.compare(0, 5, "type ") == 0)
        {
            std::string what = line.substr(5);
            if (what == "enter" || what == "esc")
                game::TypeChat(what == "enter" ? '\r' : 0x1B);
            else
                for (char c : what)
                    game::TypeChat(static_cast<unsigned char>(c));
            return "typed";
        }
        if (line == "modfix yes" || line == "modfix no")
        {
            if (g_coop.GetFriendState() != coop::FriendState::FixingMods)
                return "no mod question open";
            g_modAsked = false;
            game::DropQuestion(kModQuestion);
            g_coop.AnswerModFix(line == "modfix yes");
            StartedFixing();
            return "answered";
        }
        if (line == "approve yes" || line == "approve no")
        {
            if (!g_openJoinAsk)
                return "nobody is asking";
            game::DropQuestion(kJoinQuestion);
            AnswerJoinAsk(line == "approve yes");
            return "answered";
        }
        if (line == "sit" || _strnicmp(line.c_str(), "sit ", 4) == 0)
        {
            TESObjectREFR* who = line == "sit" ? game::Player() :
                game::LookupRef(static_cast<uint32_t>(std::strtoul(line.c_str() + 4, nullptr, 16)));
            if (!who || !g_worldLoaded)
                return "no such actor";
            TESObjectREFR* chair = game::SitInNearestChair(who);
            char out[96];
            std::snprintf(out, sizeof(out), "furniture %08X %s", chair ? game::RefId(chair) : 0,
                chair ? game::Name(chair) : "(none near)");
            return out;
        }
        if (line == "teleporter")
        {
            TESForm* item = game::TeleporterItem();
            if (!item || !g_coop.InGame() || !game::Player())
                return "no teleporter";
            game::ChangeItem(game::Player(), item->refID, -1);
            return "used";
        }
        if (line.size() == 10 && line.compare(0, 9, "teleport ") == 0 && line[9] >= '0' && line[9] <= '9')
        {
            if (!g_teleportAsked)
                return "nothing to pick";
            g_teleportAsked = false;
            game::DropQuestion(kTeleportQuestion);
            size_t choice = static_cast<size_t>(line[9] - '0');
            if (choice >= g_teleportChoices.size())
                return "cancelled";
            auto [who, bring] = g_teleportChoices[choice];
            g_coop.AskTeleport(bring ? who : g_coop.MyPlayerId(), bring ? g_coop.MyPlayerId() : who);
            return std::string(bring ? "bringing " : "going to ") + g_coop.NameOf(who);
        }
        if (_strnicmp(line.c_str(), "typeaddress ", 12) == 0)
        {
            if (!g_typingAddress)
                return "no address box";
            game::TypeIntoEntry(line.substr(12));
            return "typed";
        }
        if (line.size() == 6 && line.compare(0, 5, "pick ") == 0 && line[5] >= '0' && line[5] <= '9' &&
            (g_askedHowToJoin || g_askedHostWho))
        {
            int id = g_askedHowToJoin ? kJoinHowQuestion : kHostWhoQuestion;
            game::DropQuestion(id);
            int choice = line[5] - '0';
            if (id == kHostWhoQuestion)
            {
                g_askedHostWho = false;
                if (choice > 1)
                    return "cancelled";
                HostFor(choice == 1);
                return choice == 1 ? "hosting, by IP too" : "hosting";
            }
            g_askedHowToJoin = false;
            if (choice >= static_cast<int>(g_howChoices.size()))
                return "cancelled";
            switch (g_howChoices[choice])
            {
            case JoinHow::SteamFriend: JoinSteamFriend(); return "Steam friends";
            case JoinHow::TypeAddress: StartAddressEntry(); return "address box";
            case JoinHow::LastAddress:
                JoinHost({0, g_config.joinAddress, "the host by IP", g_config.joinPort});
                return "picked the last IP address";
            case JoinHow::LanCoop:
                JoinHost({0, g_config.address, kLanCoopLabel});
                return std::string("picked ") + kLanCoopLabel;
            }
            return "cancelled";
        }
        if (line.size() == 6 && line.compare(0, 5, "pick ") == 0 && line[5] >= '0' && line[5] <= '9')
        {
            if (!g_askedWhoToJoin)
                return "nothing to pick";
            g_askedWhoToJoin = false;
            game::DropQuestion(kJoinWhoQuestion);
            size_t choice = static_cast<size_t>(line[5] - '0');
            if (choice >= g_joinChoices.size())
                return "cancelled";
            JoinHost(g_joinChoices[choice]);
            return "picked " + g_joinChoices[choice].label;
        }
        if (line == "menu host" || line == "menu join")
        {
            (line == "menu host" ? HostClicked : JoinClicked)();
            return "clicked";
        }
        if (line.compare(0, 17, "companion follow ") == 0 || line.compare(0, 15, "companion wait ") == 0)
        {
            bool follow = line[10] == 'f';
            uint32_t ref = static_cast<uint32_t>(std::strtoul(line.c_str() + (follow ? 17 : 15), nullptr, 10));
            uint8_t leader = 0;
            if (!g_coop.CompanionLeader(ref, leader))
                return "not one of the group's companions";
            g_coop.OrderCompanion(ref, follow ? net::CompanionOrderKind::FollowMe : net::CompanionOrderKind::Wait);
            return "told";
        }
        if (line == "crashtest")
        {
            LOG_WARN("Lab: crashing on purpose");
            *reinterpret_cast<volatile int*>(0x10) = 1;
            return "";
        }
        if (line == "crashtest exit")
        {
            std::atexit(CrashAtExit);
            LOG_WARN("Lab: the game will crash as it closes, on purpose");
            return "it will crash as it closes";
        }
        if (line == "crashtest param")
        {
            char tiny[2];
            errno_t error = strcpy_s(tiny, sizeof(tiny), line.c_str());
            return "the runtime said invalid parameter (" + std::to_string(error) + ") and the game went on";
        }
        if (line == "freezetest")
        {
            LOG_WARN("Lab: stopping the game for 10 s on purpose");
            Sleep(10000);
            return "stopped 10 s";
        }
        if (line == "modfix cancel")
        {
            if (!g_coop.DownloadingMods())
                return "not downloading";
            g_coop.CancelModFix();
            return "cancelled";
        }
        if (line == "quitgame")
        {
            g_quitSoon = true;
            return "quitting";
        }
        if (line == "loadwatch")
        {
            g_loadWatch = !g_loadWatch;
            return g_loadWatch ? "watching loads" : "not watching loads";
        }
        if (_strnicmp(line.c_str(), "eval ", 5) == 0)
            return g_worldLoaded ? game::Evaluate(line.c_str() + 5) : "no world loaded";
        if (_strnicmp(line.c_str(), "note ", 5) == 0)
            return "noted";
        if (_strnicmp(line.c_str(), "on ", 3) == 0 && g_worldLoaded)
        {
            char* rest = nullptr;
            TESObjectREFR* on = game::LookupRef(std::strtoul(line.c_str() + 3, &rest, 16));
            if (!on || *rest != ' ')
                return "on <ref id, hex> <console line>: no such reference here";
            return game::RunConsoleLine(rest + 1, on) ? "ran" : "the console didn't take it";
        }
        return game::RunConsoleLine(line.c_str()) ? "ran" : "the console didn't take it";
    }

    void RunLabLines()
    {
        uint32_t id;
        std::string line;
        for (int i = 0; i < 4 && !g_loadingSave && g_lab.TakeCommand(id, line); ++i)
            g_lab.Reply(id, RunLabLine(std::move(line)));
    }

    bool g_deferredInitDone = false;

    struct FrameCost
    {
        double sumMs = 0, mostMs = 0;
        uint32_t frames = 0, nextLogAt = 0;
    } g_frameCost;

    enum FramePart
    {
        kPartMenus, kPartNetwork, kPartPlayer, kPartPlayers, kPartNpcs, kPartDoors, kPartContainers, kPartItems,
        kPartQuests, kPartDialogue, kPartCompanions, kPartRadio, kPartSettings, kPartModes, kPartChat, kPartVoice,
        kPartScripted, kPartSaves, kPartCount
    };
    const char* const kPartNames[kPartCount] = {"menus", "network", "player", "players", "NPCs", "doors", "containers",
        "items", "quests", "dialogue", "companions", "radio", "settings", "modes", "chat", "voice", "scripted", "saves"};
    coop::FrameParts g_frameParts;

    struct PartClock
    {
        void Done(FramePart part)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double ms = QpcMs(m_last, now);
            g_frameParts.Add(part, ms);
            m_lastFrame[part] += ms;
            m_last = now;
        }
        double m_lastFrame[kPartCount] = {};
        LARGE_INTEGER m_last{};
    } g_partClock;

    void EveryFrameWork();

    struct Hitches { uint32_t count = 0; double longestMs = 0; uint32_t nextLineAt = 0; } g_hitches;
    double g_lastOursMs = 0;

    void WatchHitches()
    {
        static LARGE_INTEGER s_last{};
        LARGE_INTEGER at;
        QueryPerformanceCounter(&at);
        double gap = s_last.QuadPart ? QpcMs(s_last, at) : 0;
        s_last = at;
        if (gap < 50 || !g_worldLoaded || g_loadingSave || game::InLoadingScreen())
            return;
        Hitches& h = g_hitches;
        ++h.count;
        h.longestMs = gap > h.longestMs ? gap : h.longestMs;
        uint32_t now = GetTickCount();
        if (!coop::IsDue(now, h.nextLineAt))
            return;
        h.nextLineAt = now + 1000;
        int biggest = 0;
        for (int i = 1; i < kPartCount; ++i)
            if (g_partClock.m_lastFrame[i] > g_partClock.m_lastFrame[biggest])
                biggest = i;
        char open[96], menus[128] = "";
        game::OpenMenus(open, sizeof(open));
        if (game::InMenuMode())
            std::snprintf(menus, sizeof(menus), "; a menu is up (%s)", open);
        LOG_INFO("Hitch: a frame took %.0f ms; NVCoop %.1f ms of it (most: %s, %.1f ms)%s", gap, g_lastOursMs,
            kPartNames[biggest], g_partClock.m_lastFrame[biggest], menus);
    }

    void EveryFrame()
    {
        WatchHitches();
        if (g_lab.Running())
            RunLabLines();
        static bool s_escapeHooked = false;
        if (g_deferredInitDone && !s_escapeHooked)
        {
            s_escapeHooked = true;
            game::KeepWorldRunningInEscapeMenu();
            game::TakeOverLevelUpSkillPoints();
            game::FixRemoveAllItemsTakeOff();
        }
        game::EscapeMenuLive(g_coop.InGame());
        if (g_config.borderless)
            display::Update();
        dev::EveryFrame();
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        g_partClock.m_last = start;
        for (double& part : g_partClock.m_lastFrame)
            part = 0;
        EveryFrameWork();
        QueryPerformanceCounter(&end);
        double ms = QpcMs(start, end);
        g_lastOursMs = ms;
        FrameCost& c = g_frameCost;
        c.sumMs += ms;
        c.mostMs = ms > c.mostMs ? ms : c.mostMs;
        ++c.frames;
        uint32_t now = GetTickCount();
        if (!coop::IsDue(now, c.nextLogAt))
            return;
        if (c.nextLogAt && g_coop.InGame())
        {
            AddressSpace a = GetAddressSpace();
            LOG_INFO("Frame cost: NVCoop took %.2f ms a frame on average (most %.2f) over %u frames; %u frames over 50 "
                "ms in all (longest %.0f); address space %u MB free of %u", c.sumMs / c.frames, c.mostMs, c.frames,
                g_hitches.count, g_hitches.longestMs, a.freeMb, a.totalMb);
            g_hitches.count = 0;
            g_hitches.longestMs = 0;
            std::string parts = g_frameParts.Report(kPartNames, kPartCount, c.frames, 0.02, 1.0);
            if (!parts.empty())
                LOG_INFO("Frame cost by part (ms a frame): %s", parts.c_str());
        }
        else
            g_frameParts = coop::FrameParts{};
        c = FrameCost{0, 0, 0, now + 30000};
    }

    void WatchLoading()
    {
        static uint32_t last = 0;
        static bool wasLoading = false;
        uint32_t now = GetTickCount();
        bool loading = g_worldLoaded && game::InLoadingScreen();
        if (last && now - last > 300)
            LOG_INFO("Load watch: %u ms since the last frame (world %d, loading screen %d)", now - last, g_worldLoaded,
                loading);
        if (loading != wasLoading)
            LOG_INFO("Load watch: loading screen %s", loading ? "up" : "gone");
        wasLoading = loading;
        last = now;
    }

    bool g_frozen = false;
    uint32_t g_nextFreezeNotice = 0;

    void UpdateFreeze(uint32_t now)
    {
        bool frozen = g_coop.InGame() && g_coop.Frozen();
        if (frozen && coop::IsDue(now, g_nextFreezeNotice))
        {
            g_nextFreezeNotice = now + 3000;
            game::SetWorldSpeed(0.0001f);
            game::Notify(("Waiting for " + g_coop.NameOf(g_coop.FrozenFor()) + " to finish loading...").c_str(), 3);
        }
        if (frozen == g_frozen)
            return;
        g_frozen = frozen;
        LOG_INFO("Loading: %s", frozen ? "the world waits" : "the world goes on");
        if (!frozen)
        {
            game::SetWorldSpeed(1.0f);
            g_nextFreezeNotice = 0;
        }
    }

    void EveryFrameWork()
    {
        if (g_loadWatch)
            WatchLoading();
        g_coop.Update();
        g_partClock.Done(kPartNetwork);
        std::string refused = g_coop.TakeRejectReason();
        if (!refused.empty())
            game::Notify(("Couldn't join: " + refused).c_str(), 10);
        std::string modReport = g_coop.TakeModReport();
        if (!modReport.empty() && !(g_menuMatched && g_worldLoaded))
            game::ShowMessage(modReport);
        if (g_modScan.valid() && g_modScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            if (g_worldLoaded)
                ConnectCoop();
            else
            {
                g_modScan.get();
                LOG_INFO("Mods read, but the world they were read for is gone: co-op starts with the next one");
            }
        }
        game::RetryMessageBoxes();
        FixMods(GetTickCount());
        CheckModsAtMenu(GetTickCount());
        AskAboutJoins();
        MainMenuClicks();
        static bool s_inCoop = false, s_wasRunning = false;
        if ((g_coop.GetRole() != coop::Role::None) != s_inCoop)
        {
            s_inCoop = !s_inCoop;
            if (s_inCoop)
                s_wasRunning = game::RunInBackground(true);
            else
                game::RunInBackground(s_wasRunning);
            LOG_INFO("Background: the game %s when it isn't the active window", s_inCoop || s_wasRunning ?
                "keeps running" : "pauses again");
        }
        static bool s_shownHosting = false;
        bool hosting = g_coop.GetRole() == coop::Role::Host;
        static coop::SaveMode s_shownMode;
        if ((hosting != s_shownHosting || (hosting && !(s_shownMode == g_ourMode))) && game::SteamReady())
        {
            s_shownHosting = hosting;
            s_shownMode = g_ourMode;
            game::SetHostingOnSteam(hosting, coop::Describe(g_ourMode));
        }
        if (g_coop.TakeAskingHost())
            game::Notify("Co-op: asking the host to let you in...", 5);
        g_partClock.Done(kPartMenus);
        if (!g_worldLoaded)
            return;

        uint32_t now = GetTickCount();
        UpdateFreeze(now);
        g_mirror.Update(now);
        g_inspector.Update(now);
        if ((g_startSoon || g_waitingForIntro) && coop::IsDue(now, g_nextIntroCheck))
        {
            g_startSoon = false;
            g_nextIntroCheck = now + 2000;
            TryStartCoop();
        }

        for (const game::Hit& hit : game::TakeHits())
        {
            TESObjectREFR* target = game::LookupRef(hit.targetRef);
            TESObjectREFR* attacker = game::LookupRef(hit.attackerRef);
            int friendId = g_standIns.PlayerIdOf(target);
            if (target && target == game::Player())
            {
                if (g_coop.GetRole() == coop::Role::Friend && g_coop.InGame())
                {
                    game::ChangeHealth(target, hit.damage);
                    g_tally.undone.Add(hit.damage);
                    LOG_DEBUG("Undid a hit on us by our copy %08X (%.1f)", hit.attackerRef, hit.damage);
                }
                continue;
            }
            if (friendId < 0)
            {
                if (attacker == game::Player())
                {
                    if (g_coop.GetRole() == coop::Role::Host && game::IsActor(target) && !game::IsTeammate(target))
                        g_threats.Hurt(hit.targetRef, coop::kHostPlayerId, hit.damage, now);
                    uint32_t hostRef = g_npcs.HostRefOf(hit.targetRef);
                    if (!hostRef)
                        continue;
                    g_coop.ReportHit(hostRef, hit.damage);
                    if (g_coop.GetRole() == coop::Role::Friend)
                        g_tally.sent.Add(hit.damage);
                }
                continue;
            }
            game::ChangeHealth(target, hit.damage);
            if (attacker == game::Player() && g_coop.SessionMode().friendlyFire)
            {
                LOG_DEBUG("Friendly fire: we hit player %d for %.1f", friendId, hit.damage);
                g_coop.ReportPlayerHit(static_cast<uint8_t>(friendId), hit.damage);
            }
            bool theirOwnMine = !game::IsActor(attacker) && g_mines.SparedFrom(friendId, now);
            if (g_coop.GetRole() == coop::Role::Host && attacker != game::Player() && !g_standIns.IsStandIn(attacker) &&
                !game::IsTeammate(attacker) && !theirOwnMine)
            {
                LOG_DEBUG("%08X hurt player %d for %.1f", hit.attackerRef, friendId, hit.damage);
                g_coop.DamagePlayer(static_cast<uint8_t>(friendId), hit.damage);
                g_tally.hurt.Add(hit.damage);
            }
        }
        for (const coop::HitReport& hit : g_coop.TakeHits())
            ApplyFriendHit(hit);
        if (float damage = g_coop.TakeDamage(); damage > 0)
        {
            damage = coop::DamageTaken(damage, EffectiveMode().mode == coop::GameMode::Balanced);
            game::ChangeHealth(game::Player(), -damage);
            g_tally.taken.Add(damage);
        }
        if (g_coop.InGame())
            LogCombat(now);
        static uint32_t s_nextHealthCheck = 0;
        static float s_health = 0, s_maxHealth = 0;
        bool healthFresh = coop::IsDue(now, s_nextHealthCheck);
        if (healthFresh)
        {
            s_nextHealthCheck = now + 100;
            s_health = game::Health(game::Player());
            s_maxHealth = game::MaxHealth(game::Player());
            if (g_downed)
                s_health = 0;
            g_coop.SetMyHealth(s_health, s_maxHealth);
        }

        coop::Pose me;
        if (!game::GetPose(game::Player(), me))
            return;
        me.flags = game::StanceFlags(game::Player());
        me.moves = game::MovementFlags(game::Player());
        if (game::IsJumping(game::Player()))
            me.moves |= coop::moves::kJump;
        static uint32_t s_nextModAnimLook = 0;
        static uint16_t s_runSpeed = 0;
        if (coop::IsDue(now, s_nextModAnimLook))
        {
            s_nextModAnimLook = now + 100;
            float run = game::CachedRunSpeed(game::Player());
            if (run > 0)
                s_runSpeed = static_cast<uint16_t>(run < 65535 ? run + 0.5f : 65535);
            g_coop.SetMyModAnims(game::ModAnimFiles(game::Player()));
        }
        me.runSpeed = s_runSpeed;
        g_coop.SetMyPose(me);
        if (g_coop.InGame())
            FollowGroup(now);
        static uint32_t s_nextGearCheck = 0;
        if (coop::IsDue(now, s_nextGearCheck))
        {
            s_nextGearCheck = now + 500;
            g_coop.SetMyGear(game::PlayerGear());
        }
        for (const game::SeenExplosion& e : game::TakeExplosions())
            if (g_coop.InGame() && (g_coop.GetRole() == coop::Role::Host || e.ours))
                g_coop.ShareExplosion({e.form, e.space, e.x, e.y, e.z});
        for (const net::ExplosionAt& e : g_coop.TakeExplosions())
            if (e.space == me.space && std::hypot(e.x - me.x, e.y - me.y) < MaxHitDistance() &&
                !g_mines.BlewNear(e.x, e.y, e.z, now))
            {
                bool shown = game::ShowExplosion(e.form, e.space, e.x, e.y, e.z);
                LOG_DEBUG("Explosions: %s %08X at %.0f, %.0f, %.0f", shown ? "showing" : "couldn't show", e.form, e.x,
                    e.y, e.z);
            }
        game::MyAttack mine = game::TakeMyAttacks();
        if (mine.shots)
        {
            g_coop.Attack(mine.shots);
            g_mirror.Attack(mine.shots);
        }
        if (mine.release)
        {
            g_coop.Attack(0, true);
            g_mirror.Attack(0, true);
        }
        g_partClock.Done(kPartPlayer);
        UpdateThreats(now, me);
        UpdateFighting(now);
        static int s_fovSet = 0;
        if (g_fovAfterLoad || g_config.fieldOfView != s_fovSet)
        {
            if ((!g_config.fieldOfView && !s_fovSet) || game::SetFieldOfView(g_config.fieldOfView))
            {
                s_fovSet = g_config.fieldOfView;
                g_fovAfterLoad = false;
            }
        }
        static uint32_t s_nextLooksLook = 0;
        if (g_coop.InGame() && coop::IsDue(now, s_nextLooksLook))
        {
            s_nextLooksLook = now + 5000;
            net::PlayerLooks looks;
            if (game::ReadMyLooks(looks))
                g_coop.SetMyLooks(looks);
        }
        UpdateReinforcements(now);
        UpdateLootThinning(now);
        for (const coop::AttackReport& attack : g_coop.TakeAttacks())
            g_standIns.Attack(attack.playerId, attack.shots, attack.release);
        if (g_coop.InGame())
        {
            g_cutscene = (game::PlayerDisabledControls() & 1) && !g_downed && !g_waitForHostLoad;
            g_standIns.Update(g_coop, now, me, g_cutscene);
        }
        else
        {
            g_cutscene = false;
            g_standIns.RemoveAll();
        }
        g_partClock.Done(kPartPlayers);
        if (g_coop.GetRole() == coop::Role::Host)
            g_npcs.Share(g_coop, now, me, g_standIns);
        else if (g_coop.InGame())
        {
            g_npcs.Update(g_coop, now, me, g_standIns);
            g_npcs.CheckDesyncs(g_coop, now, me);
            for (const net::NpcAttack& attack : g_coop.TakeNpcAttacks())
                g_npcs.Attack(attack.ref, attack.shots, attack.reload, attack.release);
        }
        else
            g_npcs.ReleaseAll();
        g_partClock.Done(kPartNpcs);
        static uint32_t s_nextDesyncNotice = 0;
        for (const std::string& text : g_coop.TakeDesyncNotices())
        {
            if (g_config.desyncNotices && coop::IsDue(now, s_nextDesyncNotice))
            {
                s_nextDesyncNotice = now + 20000;
                game::Notify(text.c_str(), 6);
            }
        }
        g_doors.Update(g_coop, now);
        g_mines.Update(g_coop, g_standIns, now);
        g_partClock.Done(kPartDoors);
        g_containers.Update(g_coop, now, g_npcs);
        g_partClock.Done(kPartContainers);
        g_items.Update(g_coop, now);
        g_partClock.Done(kPartItems);
        g_quests.Update(g_coop, now);
        g_partClock.Done(kPartQuests);
        game::Dialogue::TalkLogSettings talkLog;
        talkLog.on = g_config.talkLog;
        talkLog.size = g_config.talkLogSize;
        talkLog.lines = g_config.talkLogLines;
        talkLog.seconds = g_config.talkLogSeconds;
        talkLog.gap = g_config.talkLogGap;
        talkLog.range = g_config.talkLogRange;
        talkLog.dividers = g_config.talkLogDividers;
        talkLog.style = g_config.talkLogStyle;
        talkLog.hudColor = g_config.talkLogHudColor;
        talkLog.red = static_cast<uint8_t>(g_config.talkLogRed);
        talkLog.green = static_cast<uint8_t>(g_config.talkLogGreen);
        talkLog.blue = static_cast<uint8_t>(g_config.talkLogBlue);
        g_dialogue.SetTalkLog(talkLog);
        g_dialogue.Update(now);
        g_partClock.Done(kPartDialogue);
        if (uint8_t player = 0; g_coop.GetRole() == coop::Role::Host)
        {
            uint32_t at = 0;
            if (g_dialogue.LastFriendLine(player, at) && now - at < 1000)
                g_companions.FriendLineRan(player, at);
        }
        g_companions.Update(g_coop, g_standIns, now);
        g_partClock.Done(kPartCompanions);
        if (game::TakeScriptKill())
            g_coop.KilledByScript();
        bool radioFromHost = g_coop.GetRole() == coop::Role::Friend && g_coop.InGame() && g_config.syncRadio;
        game::RadioFromHost(radioFromHost);
        for (const auto& [station, line] : g_coop.TakeRadioLines())
            if (radioFromHost)
                game::HostRadioLine(line);
        game::UpdateRadios();
        g_partClock.Done(kPartRadio);
        FollowPlayerSettings(now);
        g_partClock.Done(kPartSettings);
        UpdateSaveMode();
        UpdateBalance();
        UpdateQuickStart(now);
        g_partClock.Done(kPartModes);
        UpdateChat();
        UpdatePings();
        g_partClock.Done(kPartChat);
        UpdateVoice();
        g_partClock.Done(kPartVoice);
        for (const game::RadioLineStart& p : game::TakeRadioLineStarts())
            if (g_coop.GetRole() == coop::Role::Host)
            {
                LOG_DEBUG("Radio: station %08X plays %08X part %u (line %zu of %zu)", p.station, p.info, p.part,
                    p.line + 1, p.run.size());
                g_coop.ShareRadioLine(net::RadioLine{p.station, p.info, 0, 0, static_cast<uint8_t>(p.part), p.run}, now);
            }
            else if (p.fromHost)
                LOG_DEBUG("Radio: station %08X plays the host's %08X", p.station, p.info);
        game::MirrorPlayerCommands(g_coop.GetRole() == coop::Role::Host && g_coop.InGame());
        game::ScreenFromHost(g_coop.GetRole() == coop::Role::Friend && g_coop.InGame());
        for (const net::PlayerCommand& c : game::TakePlayerCommands())
            g_coop.SharePlayerCommand(c);
        for (const net::PlayerCommand& c : g_coop.TakePlayerCommands())
            if (!game::ApplyPlayerCommand(c))
                LOG_INFO("Scripted: couldn't do the host's command %u (%08X %08X) here", static_cast<unsigned>(c.kind),
                    c.form, c.ref);
        g_partClock.Done(kPartScripted);
        if (g_coop.GetRole() == coop::Role::Friend)
            AskAboutCompanion(now);
        g_sleepWait.Update(now);
        if (g_coop.GetRole() == coop::Role::Friend)
            FollowHostSaves(now);
        else if (g_coop.GetRole() == coop::Role::Host)
            ServeFriendSaves();
        g_progress.Update(g_coop, now);
        Teleporter(now);
        ShareFurnitureAnim(now);
        UpdateDowned(now, me, s_health, s_maxHealth, healthFresh);
        UpdateReviveBar(now);

        if (coop::IsDue(now, g_nextMadeCheck))
        {
            g_nextMadeCheck = now + 1000;
            std::vector<uint32_t> made = g_standIns.RefIds();
            for (uint32_t id : g_npcs.MadeRefIds())
                made.push_back(id);
            for (uint32_t id : g_extras)
                made.push_back(id);
            std::sort(made.begin(), made.end());
            if (made != g_notedMade)
            {
                game::NoteMadeCharacters(made);
                g_notedMade = std::move(made);
            }
        }
        g_partClock.Done(kPartSaves);
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

    std::string MessageText(const NVSEMessagingInterface::Message* msg)
    {
        return msg->data ? std::string(static_cast<const char*>(msg->data), strnlen(static_cast<const char*>(msg->data),
            msg->dataLen)) : std::string();
    }

    void OnNVSEMessage(NVSEMessagingInterface::Message* msg)
    {
        switch (msg->type)
        {
        case NVSEMessagingInterface::kMessage_DeferredInit:
            g_deferredInitDone = true;
            break;
        case NVSEMessagingInterface::kMessage_PostLoad:

        {
            LOG_INFO("All plugins loaded");
            AddressSpace a = GetAddressSpace();
            LOG_INFO("Address space: %u MB free of %u (4 GB patch: %s)", a.freeMb, a.totalMb, a.large ? "yes" :
                "NO: the game can run out of memory and crash");
            g_warnNoLargeAddress = !a.large;
            const std::pair<const wchar_t*, const char*> kNeeded[] = {{L"jip_nvse.dll", "JIP LN NVSE"},
                {L"kNVSE.dll", "kNVSE"}, {L"RealTimeMenus.dll", "Real Time Menus"}};
            for (const auto& [dll, name] : kNeeded)
                if (GetModuleHandleW(dll))
                    LOG_INFO("Found %ls", dll);
                else
                {
                    LOG_ERROR("%ls is missing: install it (JIP LN NVSE, kNVSE and Real Time Menus are needed for "
                        "co-op)", dll);
                    g_missingPlugins += std::string(g_missingPlugins.empty() ? "" : ", ") + name;
                }
            if (!g_missingPlugins.empty() && g_config.mode != Config::Mode::Off)
                PluginsMissing();
            CheckRealTimeMenus();
            if (g_config.mode != Config::Mode::Off && !game::AddCoopToMainMenu(&HostClicked, &JoinClicked))
                LOG_WARN("Main menu: couldn't add Host Co-op and Join Co-op (another plugin changed it?)");
            g_dialogue.Install(g_coop, g_npcs, g_progress);
            g_sleepWait.Install(g_coop);
            LoadOwnSaves();
            if (!game::InstallLoadCheck(&MayLoad))
                LOG_WARN("Saves: couldn't hook loading (another plugin changed it?); friends' loads won't wait for the host");
            game::ListenForHits();
            if (!game::WatchExplosions())
                LOG_WARN("Explosions: couldn't watch them; they won't line up on others' screens");
            if (!game::WatchPlayerCommands())
                LOG_WARN("Scripted: couldn't watch script commands; friends won't follow scripted sequences");
            game::ListenForEnables();
            CheckCrashReporter();
            break;
        }
        case NVSEMessagingInterface::kMessage_MainGameLoop:
            FreezeBeat(g_coop.InGame() && g_worldLoaded && !g_loadingSave);
            EveryFrame();
            break;
        case NVSEMessagingInterface::kMessage_PreLoadGame:
        case NVSEMessagingInterface::kMessage_ExitToMainMenu:
            if (msg->type == NVSEMessagingInterface::kMessage_PreLoadGame)
            {
                std::string file = MessageText(msg);
                LOG_INFO("Loading a save: %s", file.c_str());
                g_coop.SetCurrentSave(coop::SaveNameOf(file));
                if (g_coop.GetRole() == coop::Role::Host && g_coop.InGame())
                    g_coop.ShareSave(file, true);
                g_expectedLoad.clear();
                if (g_menuCheck != MenuCheck::Done && !g_coop.DownloadingMods())
                {
                    if (g_menuCheck == MenuCheck::Comparing)
                        g_coop.Stop();
                    g_menuCheck = MenuCheck::Done;
                }
            }
            else
            {
                LOG_INFO("Back to the main menu");
                if (g_coop.GetRole() != coop::Role::None)
                    g_coop.Stop();
            }
            g_loadingSave = msg->type == NVSEMessagingInterface::kMessage_PreLoadGame;
            g_worldLoaded = false;
            game::ForgetHolds();
            g_gameOver = false;
            g_standIns.Forget();
            g_npcs.Forget();
            g_doors.Forget();
            g_mines.Forget();
            g_progress.Forget();
            g_quests.Forget();
            g_coop.ForgetMyPlace();
            g_hasTravel = false;
            g_inspector.Forget();
            g_mirror.Stop();
            break;
        case NVSEMessagingInterface::kMessage_SaveGame:
        {
            std::string file = MessageText(msg);
            LOG_INFO("Saved: %s", file.c_str());
            g_coop.SetCurrentSave(coop::SaveNameOf(file));
            if (g_coop.GetRole() == coop::Role::Host && g_coop.InGame())
                g_coop.ShareSave(file, false);
            if (g_coop.GetRole() == coop::Role::Friend && g_coop.InGame())
            {
                std::string name = coop::SaveNameOf(file);
                if (!name.empty() && name.compare(0, 8, "Co-op - ") != 0 && !coop::IsAutosave(name))
                {
                    LOG_INFO("Saves: we saved \"%s\"; the host saves for everyone", name.c_str());
                    g_coop.AskHostToSave(name);
                    g_savedForHost = name;
                }
            }
            break;
        }
        case NVSEMessagingInterface::kMessage_PostLoadGame:
            g_loadingSave = false;
            if (msg->data)
                LOG_INFO("Save loaded");
            else
                LOG_WARN("The save didn't load (the game says it failed)");
            OnWorldLoaded();
            break;
        case NVSEMessagingInterface::kMessage_NewGame:
            g_loadingSave = false;
            LOG_INFO("New game started");
            OnWorldLoaded();
            break;
        case NVSEMessagingInterface::kMessage_ExitGame:
        case NVSEMessagingInterface::kMessage_ExitGame_Console:
            g_coop.Stop();
            g_lab.Stop();
            game::StopVoice();
            Log::Flush();
            break;
        default:
            break;
        }
    }
}

extern "C" {

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "NVCoop";
    info->version = NVCOOP_VERSION_NUMBER;

    if (nvse->isEditor)
        return false;

    Log::Open("");
    LOG_INFO("NVCoop %s starting (xNVSE %08X, game %08X)",
        NVCOOP_VERSION_STRING, nvse->nvseVersion, nvse->runtimeVersion);

    if (nvse->runtimeVersion != RUNTIME_VERSION_1_4_0_525)
    {
        LOG_ERROR("Unsupported game version %08X: needs the Steam version 1.4.0.525", nvse->runtimeVersion);
        return false;
    }
    if (GetPrivateProfileIntA("Coop", "DumpGameCode", 0, (PluginFolder() + "NVCoop.ini").c_str()) == 1 &&
        !DumpGameImage("NVCoop-game-memory.exe"))
        LOG_ERROR("Couldn't save the game's program from memory");
    return true;
}

bool NVSEPlugin_Load(NVSEInterface* nvse)
{
    g_handle = nvse->GetPluginHandle();
    InstallCrashReporter();
    g_config = Config::Load(PluginFolder() + "NVCoop.ini");
    g_config.LoadPlayer(PlayerSettingsPath());
    g_ourMode = coop::ModeFromSwitches(g_config.coopBalanced, g_config.friendlyFire);
    LOG_INFO("Mode: %s (the MCM page's settings)", coop::Describe(g_ourMode).c_str());
    for (const std::string& patch : g_config.patches)
        LOG_INFO("Patch: %s read (co-op settings asked %d, Hardcore asked %d)", patch.c_str(),
            g_config.askMode ? 1 : 0, g_config.askHardcore ? 1 : 0);
    if (g_config.allowSecondCopy)
        AllowSecondCopy(g_config.secondCopyWaits);
    if (g_config.borderless)
        display::Install();
    if (g_config.blockDiscordOverlay)
        BlockCrashingDlls();
    if (!game::Init(nvse))
        return false;

    auto* messaging = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
    if (!messaging || !messaging->RegisterListener(g_handle, "NVSE", OnNVSEMessage))
    {
        LOG_ERROR("Couldn't listen for xNVSE messages");
        return false;
    }

    dev::RegisterCommands(nvse, &g_inspector, &g_mirror);
    if (g_config.labChannel && g_lab.Start(L"\\\\.\\pipe\\NVCoopLab"))
        LOG_INFO("Lab channel open (\\\\.\\pipe\\NVCoopLab): tools on this PC, for this Windows user only, can run "
            "console lines in the game");
    LOG_INFO("Hello from NVCoop! Plugin loaded");
    return true;
}

}
