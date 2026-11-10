// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "config.h"
#include "coop/chat_history.h"
#include "coop/coop.h"
#include "coop/game_mode.h"
#include "dev/lab_channel.h"
#include "speech.h"
#include "voice_out.h"
#include "game/game.h"
#include "game/standins.h"
#include "game/npcs.h"
#include "game/companions.h"
#include "game/doors.h"
#include "game/mines.h"
#include "game/containers.h"
#include "game/items.h"
#include "game/quests.h"
#include "game/objects.h"
#include "game/terminals.h"
#include "game/activations.h"
#include "game/progress.h"
#include "game/sleepwait.h"
#include "game/dialogue.h"
#include "game/inspector.h"
#include "game/mirror.h"
#include "coop/threat.h"
#include "coop/party_perks.h"
#include "coop/reinforcements.h"

#include <map>
#include <set>
#include <string>
#include <vector>

extern PluginHandle g_handle;
extern coop::Coop& g_coop;
extern Config g_config;
extern game::StandIns g_standIns;
extern Speech g_speech;
extern coop::SaveMode g_ourMode;
extern VoiceOut g_voiceOut;
extern game::Npcs g_npcs;
extern game::Companions g_companions;
extern game::Doors g_doors;
extern game::Mines g_mines;
extern game::Containers g_containers;
extern game::WorldItems g_items;
extern game::Quests g_quests;
extern game::Objects g_objects;
extern game::Terminals g_terminals;
extern game::Activations g_activations;
extern game::Progress g_progress;
extern game::SleepWait g_sleepWait;
extern game::Dialogue g_dialogue;
extern game::Inspector g_inspector;
extern game::Mirror g_mirror;
extern bool g_cutscene;
extern bool g_waitForHostLoad;
extern bool g_reportLoad;
extern bool g_loadWatch;
extern bool g_worldLoaded;
extern bool g_waitingForIntro;
extern bool g_startSoon;
extern uint32_t g_nextIntroCheck;
extern coop::Pose g_travel;
extern std::vector<uint32_t> g_bringFollowers;
extern uint32_t g_bringUntil;
extern uint8_t g_travelLeader;
extern bool g_hasTravel;
extern uint32_t g_travelAt;
extern int g_countdownLeft;
extern dev::LabChannel g_lab;
extern bool g_loadingSave;
extern std::vector<uint32_t> g_notedMade;
extern uint32_t g_nextMadeCheck;
extern coop::ThreatTable g_threats;
extern coop::KillCredits g_killCredits;
extern std::set<uint32_t> g_lootedBodies;
extern uint32_t g_nextPartyPerks;
extern uint8_t g_partyPerksLogged;
extern bool g_fovAfterLoad;
extern uint32_t g_nextThreatLook;
extern std::set<uint32_t> g_bodiesThinned;
extern uint32_t g_nextThinLook;
extern coop::Reinforcements g_reinforcements;
extern std::set<uint32_t> g_fightersSeen;
extern std::vector<uint32_t> g_extras;
extern uint32_t g_nextReinforceLook;

std::string PluginFolder();
void AskSwitches();
extern bool g_hardcoreAsked;
extern bool g_openLoadMenu;

std::string SavesFolder();
std::string OurMatchOf(const coop::SaveTag& hostTag);
bool HaveSaves();
void LoadOurMatch();
void LoadOwnSaves();
void SaveOwnSaves();
void ForgetLinksOf(const std::string& own);
bool MayLoad(const std::string& save);
uint64_t NewSaveTagId();
void StartNewGameTag();
uint64_t FileTimeNow();
void WriteSaveTag(const std::string& file, uint64_t written);
void ReadSaveTag(const std::string& file);
extern std::string g_expectedLoad, g_savedForHost, g_pendingLinkedSave, g_checkingSave;
extern coop::SaveTag g_thisGame;
void BeginSave(const std::string& name, uint32_t now, const char* giveUpNote);

void SetDowned(bool down, uint32_t now);
void UpdateReviveBar(uint32_t now);
bool UpdateHealAlly(uint32_t now, TESObjectREFR* player);
void UpdateDowned(uint32_t now, const coop::Pose& me, float health, float maxHealth, bool healthFresh);
extern bool g_downed, g_gameOver;
extern std::string g_reviveHint;
// Too Many Emotes: the emote we joined (a friend's; empty if none), and ending it.
const std::string& JoinedEmote();
bool JoinedAnEmote();
void StopJoinedEmote(const char* why);
extern uint32_t g_labHoldUntil;

void GoToPlayer(uint8_t target, uint32_t now);
void Teleporter(uint32_t now);
constexpr int kTeleportQuestion = 12;
extern int g_teleportersHad;
extern bool g_teleportAsked;
extern std::vector<std::pair<uint8_t, bool>> g_teleportChoices;
void UpdateGifts(uint32_t now);
void StopGiftMenus();
void TradeMenuPicked(TESObjectREFR* me);
void UpdateTrade(uint32_t now);
void StopTrade();
extern coop::GiftSaveTimer g_giftSaveTimer;

void UpdateChat();
void UpdatePings();
void UpdateVoice();
extern coop::ChatHistory g_chatHistory;
extern coop::EventFeed g_eventFeed;
extern coop::PartyWatch g_partyWatch;
void UpdateVoiceLines(uint32_t now);
void NoteMyAttack(uint8_t shots, bool release);
extern bool g_labTalking;
extern std::vector<std::string> g_labChat;

bool QuickStartBusy();
void UpdateQuickStart(uint32_t now);
