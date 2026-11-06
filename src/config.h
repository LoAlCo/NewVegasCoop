// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Config
{
    enum class Mode { Off, Menu, Host, Join, Auto } mode = Mode::Menu;
    std::string address = "127.0.0.1";
    uint16_t port = 27960;
    std::string name = "Player";
    std::vector<std::string> hostQuestMods;
    bool allowSecondCopy = false;
    bool secondCopyWaits = false;
    bool blockDiscordOverlay = true;
    bool borderless = true;
    bool desyncNotices = true;
    int fieldOfView = 0;
    bool friendMarkers = true;
    bool talkLog = true;
    int talkLogSize = 65;
    int talkLogX = 2, talkLogY = 25;
    int talkLogWidth = 600;
    int talkLogLines = 4;
    int talkLogSeconds = 15;
    int talkLogGap = 6;
    int talkLogRange = 1500;
    bool talkLogDividers = true;
    bool talkLogPreview = false;
    int talkLogStyle = 1;
    bool talkLogHudColor = true;
    int talkLogRed = 255, talkLogGreen = 255, talkLogBlue = 255;
    bool coopBalanced = false;
    bool friendlyFire = false;
    bool askMode = true;
    bool askHardcore = true;
    std::vector<std::string> patches;
    bool showAddress = false;
    bool syncRadio = true;
    bool chat = true;
    int chatKey = 21;
    bool chatSpeech = true;
    int chatSpeechVolume = 80;
    int voice = 0;
    int voiceVariant = 0;
    int voicePitch = 0;
    int voiceSpeed = 0;
    int voiceEffect = 0;
    bool voiceChat = true;
    int voiceChatKey = 48;
    int voiceChatMode = 0;
    int voiceChatVolume = 100;
    bool mouths = true;
    bool skipDlcPopups = true;
    bool labChannel = false;
    bool lanCoop = false;
    bool ipHosting = false;
    std::string joinAddress;
    uint16_t joinPort = 0;
    bool autoApproveJoins = false;

    static Config Load(const std::string& iniPath);
    void LoadPlayer(const std::string& path);
    void WriteSwitches(const std::string& path) const;
};
