// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "config.h"
#include "log.h"
#include "net/session.h"
#include <cstdlib>

#include <cstdlib>
#include <windows.h>
#include <algorithm>

namespace
{
    const char* kDefaultIni =
        "[Coop]\r\n"
        "; menu (Host Co-op and Join Co-op on the main menu), off, or for testing: host, join, or auto (first game on\r\n"
        "; this PC hosts, the next joins)\r\n"
        "Mode=menu\r\n"
        "; Host's address, used when joining\r\n"
        "Address=127.0.0.1\r\n"
        "Port=27960\r\n"
        "Name=Player\r\n"
        "; When hosting: quest mods whose quests your game runs for everyone, like the base game's, file names with\r\n"
        "; commas between (e.g. HostQuestMods=Some Quest Mod.esp, Another.esm). Other mods' quests run in each\r\n"
        "; player's game: leave menu mods like MCM out. TTW's and New California's are run for everyone on their own.\r\n"
        "HostQuestMods=\r\n"
        "; Developer-only: 1 lets a second copy of the game run on this PC for testing\r\n"
        "AllowSecondCopy=0\r\n"
        "; 1 keeps Discord's overlay out of the game (it crashes New Vegas, even when turned off in Discord)\r\n"
        "BlockDiscordOverlay=1\r\n"
        "; (The player's own settings (chat, speech, radio) are in Data\\config\\NVCoop.ini: the MCM menu's.)\r\n"
        "; Developer-only: 1 lets tools on this PC, for this Windows user only, run console commands in the game\r\n"
        "LabChannel=0\r\n";

    std::string Read(const std::string& ini, const char* key, const char* fallback, const char* section = "Coop")
    {
        char buf[4096];
        GetPrivateProfileStringA(section, key, fallback, buf, sizeof(buf), ini.c_str());
        return buf;
    }

    int Number(const std::string& text, int min, int max, int fallback)
    {
        char* end = nullptr;
        double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || value < min || value > max)
            return fallback;
        return static_cast<int>(value < 0 ? value - 0.5 : value + 0.5);
    }

    void Write(const std::string& ini, const char* key, int value)
    {
        WritePrivateProfileStringA("Settings", key, std::to_string(value).c_str(), ini.c_str());
    }
}

void Config::LoadPlayer(const std::string& path)
{
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        Write(path, "bChat", chat);
        Write(path, "iChatKey", chatKey);
        Write(path, "bChatSpeech", chatSpeech);
        Write(path, "iChatSpeechVolume", chatSpeechVolume);
        Write(path, "iVoice", voice);
        Write(path, "iVoiceVariant", voiceVariant);
        Write(path, "iVoicePitch", voicePitch);
        Write(path, "iVoiceSpeed", voiceSpeed);
        Write(path, "iVoiceEffect", voiceEffect);
        Write(path, "bVoiceChat", voiceChat);
        Write(path, "iVoiceChatKey", voiceChatKey);
        Write(path, "iVoiceChatMode", voiceChatMode);
        Write(path, "iVoiceChatVolume", voiceChatVolume);
        Write(path, "bMouths", mouths);
        Write(path, "bSkipDlcPopups", skipDlcPopups);
        Write(path, "bSyncRadio", syncRadio);
        Write(path, "bDesyncNotices", desyncNotices);
        Write(path, "iFieldOfView", fieldOfView);
        Write(path, "bFriendMarkers", friendMarkers);
        Write(path, "bTalkLog", talkLog);
        Write(path, "iTalkLogSize", talkLogSize);
        Write(path, "iTalkLogLines", talkLogLines);
        Write(path, "iTalkLogSeconds", talkLogSeconds);
        Write(path, "iTalkLogGap", talkLogGap);
        Write(path, "iTalkLogRange", talkLogRange);
        Write(path, "bTalkLogDividers", talkLogDividers);
        Write(path, "iTalkLogStyle", talkLogStyle);
        Write(path, "bTalkLogHudColor", talkLogHudColor);
        Write(path, "iTalkLogRed", talkLogRed);
        Write(path, "iTalkLogGreen", talkLogGreen);
        Write(path, "iTalkLogBlue", talkLogBlue);
        Write(path, "bShowAddress", showAddress);
        WriteSwitches(path);
    }
    auto flag = [&](const char* key, bool now) {
        return Number(Read(path, key, now ? "1" : "0", "Settings"), 0, 1, now ? 1 : 0) != 0;
    };
    auto number = [&](const char* key, int now, int min, int max) {
        return Number(Read(path, key, std::to_string(now).c_str(), "Settings"), min, max, now);
    };
    chat = flag("bChat", chat);
    chatKey = number("iChatKey", chatKey, 1, 255);
    chatSpeech = flag("bChatSpeech", chatSpeech);
    chatSpeechVolume = number("iChatSpeechVolume", chatSpeechVolume, 0, 100);
    voice = number("iVoice", voice, 0, 1);
    voiceVariant = number("iVoiceVariant", voiceVariant, 0, 3);
    voicePitch = number("iVoicePitch", voicePitch, -10, 10);
    voiceSpeed = number("iVoiceSpeed", voiceSpeed, -10, 10);
    voiceEffect = number("iVoiceEffect", voiceEffect, 0, 4);
    voiceChat = flag("bVoiceChat", voiceChat);
    voiceChatKey = number("iVoiceChatKey", voiceChatKey, 1, 255);
    if (number("iSettingsVersion", 0, 0, 1000) < 1)
    {
        if (voiceChatKey == 47)
        {
            voiceChatKey = 48;
            Write(path, "iVoiceChatKey", voiceChatKey);
        }
        Write(path, "iSettingsVersion", 1);
    }
    voiceChatMode = number("iVoiceChatMode", voiceChatMode, 0, 1);
    voiceChatVolume = number("iVoiceChatVolume", voiceChatVolume, 0, 100);
    mouths = flag("bMouths", mouths);
    skipDlcPopups = flag("bSkipDlcPopups", skipDlcPopups);
    syncRadio = flag("bSyncRadio", syncRadio);
    desyncNotices = flag("bDesyncNotices", desyncNotices);
    fieldOfView = number("iFieldOfView", fieldOfView, 0, 120);
    friendMarkers = flag("bFriendMarkers", friendMarkers);
    talkLog = flag("bTalkLog", talkLog);
    talkLogSize = number("iTalkLogSize", talkLogSize, 40, 150);
    talkLogLines = number("iTalkLogLines", talkLogLines, 1, 10);
    talkLogSeconds = number("iTalkLogSeconds", talkLogSeconds, 3, 60);
    talkLogGap = number("iTalkLogGap", talkLogGap, 0, 40);
    talkLogRange = number("iTalkLogRange", talkLogRange, 250, 8000);
    talkLogDividers = flag("bTalkLogDividers", talkLogDividers);
    talkLogStyle = number("iTalkLogStyle", talkLogStyle, 0, 3);
    talkLogHudColor = flag("bTalkLogHudColor", talkLogHudColor);
    talkLogRed = number("iTalkLogRed", talkLogRed, 0, 255);
    talkLogGreen = number("iTalkLogGreen", talkLogGreen, 0, 255);
    talkLogBlue = number("iTalkLogBlue", talkLogBlue, 0, 255);
    showAddress = flag("bShowAddress", showAddress);
    coopBalanced = flag("bCoopBalanced", coopBalanced);
    friendlyFire = flag("bFriendlyFire", friendlyFire);
    if (fieldOfView && fieldOfView < 60)
        fieldOfView = 60;
}

void Config::WriteSwitches(const std::string& path) const
{
    Write(path, "bCoopBalanced", coopBalanced);
    Write(path, "bFriendlyFire", friendlyFire);
}

Config Config::Load(const std::string& iniPath)
{
    if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        HANDLE f = CreateFileA(iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(f, kDefaultIni, static_cast<DWORD>(strlen(kDefaultIni)), &written, nullptr);
            CloseHandle(f);
        }
    }

    Config c;
    std::string mode = Read(iniPath, "Mode", "menu");
    if (_stricmp(mode.c_str(), "off") == 0) c.mode = Mode::Off;
    else if (_stricmp(mode.c_str(), "menu") == 0) c.mode = Mode::Menu;
    else if (_stricmp(mode.c_str(), "host") == 0) c.mode = Mode::Host;
    else if (_stricmp(mode.c_str(), "join") == 0) c.mode = Mode::Join;
    else if (_stricmp(mode.c_str(), "auto") == 0) c.mode = Mode::Auto;
    else
    {
        LOG_WARN("Settings: Mode=%s isn't one we know; using menu", mode.c_str());
        c.mode = Mode::Menu;
    }

    c.address = Read(iniPath, "Address", c.address.c_str());
    int port = std::atoi(Read(iniPath, "Port", "27960").c_str());
    if (port > 0 && port < 65536)
        c.port = static_cast<uint16_t>(port);
    c.name = Read(iniPath, "Name", c.name.c_str());
    std::string questMods = Read(iniPath, "HostQuestMods", "");
    for (size_t at = 0; at <= questMods.size();)
    {
        size_t comma = questMods.find(',', at);
        if (comma == std::string::npos)
            comma = questMods.size();
        std::string mod = questMods.substr(at, comma - at);
        while (!mod.empty() && (mod.front() == ' ' || mod.front() == '\t'))
            mod.erase(mod.begin());
        while (!mod.empty() && (mod.back() == ' ' || mod.back() == '\t'))
            mod.pop_back();
        if (!mod.empty())
            c.hostQuestMods.push_back(mod);
        at = comma + 1;
    }
    c.allowSecondCopy = Read(iniPath, "AllowSecondCopy", "0") == "1";
    c.autoApproveJoins = Read(iniPath, "AutoApproveJoins", "0") == "1";
    c.lanCoop = Read(iniPath, "LanCoop", "0") == "1";
    c.ipHosting = Read(iniPath, "IpHosting", "0") == "1";
    std::string joinAddress = Read(iniPath, "JoinAddress", "");
    c.joinPort = c.port;
    if (!joinAddress.empty() && !net::ParseHostAddress(joinAddress, c.joinAddress, c.joinPort))
        LOG_WARN("NVCoop.ini: JoinAddress isn't an address (an IP like 203.0.113.5, or 203.0.113.5:27960)");
    c.secondCopyWaits = Read(iniPath, "SecondCopyWaits", "0") == "1";
    c.blockDiscordOverlay = Read(iniPath, "BlockDiscordOverlay", "1") != "0";
    c.borderless = Read(iniPath, "Borderless", "1") != "0";
    c.desyncNotices = Read(iniPath, "DesyncNotices", "1") != "0";
    c.syncRadio = Read(iniPath, "SyncRadio", "1") != "0";
    c.chat = Read(iniPath, "Chat", "1") != "0";
    c.chatSpeech = Read(iniPath, "ChatSpeech", "1") != "0";
    c.chatSpeechVolume = Number(Read(iniPath, "ChatSpeechVolume", "80"), 0, 100, 80);
    c.chatKey = Number(Read(iniPath, "ChatKey", "21"), 1, 255, 21);
    c.labChannel = Read(iniPath, "LabChannel", "0") == "1";
    auto patchable = [&](const std::string& path) {
        c.askMode = Read(path, "AskMode", c.askMode ? "1" : "0") != "0";
        c.askHardcore = Read(path, "AskHardcore", c.askHardcore ? "1" : "0") != "0";
    };
    patchable(iniPath);
    size_t slash = iniPath.find_last_of("\\/");
    std::string folder = slash == std::string::npos ? "" : iniPath.substr(0, slash + 1);
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((folder + "NVCoop_*.ini").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && c.patches.size() < 16)
                c.patches.push_back(found.cFileName);
        while (FindNextFileA(find, &found));
        FindClose(find);
    }
    std::sort(c.patches.begin(), c.patches.end());
    for (const std::string& patch : c.patches)
        patchable(folder + patch);
    if (c.name.empty())
        c.name = "Player";
    return c;
}
