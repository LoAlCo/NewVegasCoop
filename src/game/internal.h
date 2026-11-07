// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "game/game.h"
#include "game/addresses.h"
#include "log.h"
#include "coop/attack_versions.h"
#include "coop/shot_counter.h"
#include "coop/chat_history.h"
#include "coop/talk_log.h"
#include "coop/gear_lockers.h"
#include "nvse/PluginAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"

#include <atomic>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game
{
    constexpr size_t kMaxHarmless = 256;
    constexpr size_t kMaxStandInBases = 32;
    constexpr int kEquipSlots = 20;
    constexpr int kChatAboveSlots = 5;
    constexpr int kMarkSlots = 4;
    constexpr int kTalkSlots = static_cast<int>(coop::TalkLog::kMostLines);
    constexpr int kTalkEdges = 4;
    constexpr int kFeedSlots = static_cast<int>(coop::kChatPanelMostRows);
    constexpr int kFeedEdges = 4;
    constexpr int kMaxWholeArgs[2] = {3, 5};
    struct PendingHit { uint32_t refId; uint32_t attackerId; float healthAtHit; };
    constexpr float kRadToDeg = 57.2957795f;
    constexpr int kPuppetAmmo = 500;

    template <typename T> T At(const void* base, uint32_t offset)
    {
        return *reinterpret_cast<const T*>(static_cast<const char*>(base) + offset);
    }

    Script* Compile(const char* what, const std::string& text);
    uint32_t FloatArg(float f);
    float Number(Script* script, TESObjectREFR* ref);

    uint8_t* HookStart(uintptr_t function, const uint8_t* expect, size_t length, const void* to);
    uintptr_t HookCall(uintptr_t site, const void* to);

    bool LooksOntoBase(TESForm* base, const net::PlayerLooks& looks, int& shared);

    extern coop::GearLockers g_gearLockers;
    extern uint32_t g_rewardBox, g_rewardQuest;
    extern std::vector<coop::RewardItem> g_rewardItems;
    bool IsContainerRef(const TESObjectREFR* ref);
    int PerkRanks(uint32_t perk);

    void SetName(String& s, const char* name);
    void NoteThread(const char* hook, std::atomic<int>& said);
    extern DWORD g_mainThread;
    using TypeTest = bool (*)(uint8_t type, uint8_t wanted);
    void RefsNear(TypeTest test, uint8_t baseType, std::vector<TESObjectREFR*>& out, float range);

    extern NVSEScriptInterface* g_scripts;
    extern NVSEEventManagerInterface* g_events;
    extern NVSEArrayVarInterface* g_arrays;
    extern NVSEConsoleInterface* g_console;
    extern bool g_compiled;
    extern Script* g_spawn;
    extern Script* g_bringToPlayer;
    extern Script* g_move;
    extern Script* g_travel;
    extern Script* g_remove;
    extern Script* g_playerFlags;
    extern Script* g_clone;
    extern Script* g_noTalk;
    extern Script* g_fire;
    extern std::array<Script*, coop::kAttackVersions> g_attackAnim;
    extern std::array<Script*, coop::kAttackVersions> g_throwAnim;
    extern std::array<Script*, coop::kAttackVersions> g_mineAnim;
    extern Script* g_placeAt;
    extern Script* g_rounds;
    extern std::map<uint32_t, TESForm*> g_harmless;
    extern std::map<uint32_t, BGSProjectile*> g_harmlessProjectiles;
    extern std::map<uint32_t, BGSExplosion*> g_harmlessExplosions;
    extern std::map<uint32_t, BGSExplosion*> g_quietExplosions;
    extern uint32_t g_myWeapon;
    extern int g_myRounds;
    extern int g_myAction;
    extern coop::ShotCounter g_myShots;
    extern std::vector<uint32_t> g_standInBases;
    extern Script* g_turn;
    extern Script* g_equipped;
    extern Script* g_removeAllItems;
    extern Script* g_addAndEquip;
    extern Script* g_removeItem;
    extern Script* g_addItems;
    extern Script* g_alert;
    extern Script* g_restrain;
    extern Script* g_evaluate;
    extern Script* g_combatOff;
    extern Script* g_notify;
    extern Script* g_notifyFor;
    extern Script* g_moveBy;
    extern Script* g_health;
    extern Script* g_medicine;
    extern Script* g_level;
    extern Script* g_hit;
    extern Script* g_maxHealth;
    extern Script* g_changeHealth;
    extern Script* g_makeCompanion;
    extern Script* g_stopFighting;
    extern Script* g_kill;
    extern Script* g_unprotect;
    extern Script* g_killPlain;
    extern Script* g_playedIdle;
    extern Script* g_combatTarget;
    extern Script* g_forceTarget;
    extern Script* g_update3D;
    extern Script* g_isLocked;
    extern Script* g_pileKind;
    extern Script* g_makePile;
    extern Script* g_fovNow;
    extern Script* g_placeExtra;
    extern Script* g_thinAmmo;
    extern Script* g_playIdle;
    extern Script* g_setMaxHealth;
    extern Script* g_stopIdle;
    extern Script* g_npcRounds;
    extern Script* g_wornGear;
    extern Script* g_wearAt;
    extern Script* g_idleGroup;
    extern Script* g_forceStopIdle;
    extern Script* g_madeClear;
    extern Script* g_madeAdd;
    extern Script* g_madeCount;
    extern Script* g_madeGet;
    extern Script* g_resurrect;
    extern Script* g_unconscious;
    extern Script* g_ghost;
    extern Script* g_controls;
    extern Script* g_controlPressed;
    extern Script* g_menuMode;
    extern Script* g_keyPressed;
    extern Script* g_chatHud;
    extern Script* g_chatShow;
    extern Script* g_onScreen;
    extern Script* g_chatAbove[kChatAboveSlots];
    extern Script* g_mark[kMarkSlots];
    extern Script* g_questStage;
    extern Script* g_tutorialTimer;
    extern Script* g_leaveTutorial;
    extern Script* g_activateBy;
    extern Script* g_starterGear;
    extern Script* g_effectTrait;
    extern Script* g_loadedOfType;
    extern Script* g_showMap;
    extern Script* g_questObject;
    extern Script* g_itemCount;
    extern Script* g_sitNearest;
    extern Script* g_reviveBar;
    extern Script* g_talkText[kTalkSlots];
    extern Script* g_talkPlace[kTalkSlots];
    extern Script* g_talkBack[kTalkSlots];
    extern Script* g_talkSide[kTalkSlots];
    extern Script* g_feedText[kFeedSlots];
    extern Script* g_feedPlace[kFeedSlots];
    extern Script* g_feedBack[kFeedSlots];
    extern Script* g_feedSide[kFeedSlots];
    extern Script* g_speedMult;
    extern Script* g_actorAlpha;
    extern Script* g_collision;
    extern TESForm* g_teleporter;
    extern Script* g_armorDR;
    extern Script* g_itemValue;
    extern Script* g_bodyPartLists;
    extern Script* g_bodyPartMult;
    extern Script* g_keyDisabled;
    extern Script* g_disableKey;
    extern Script* g_controlDisabled;
    extern Script* g_disableControl;
    extern Script* g_loadingScreen;
    extern Script* g_aiState;
    extern Script* g_print;
    extern Script* g_openState;
    extern Script* g_setOpen;
    extern Script* g_lock;
    extern Script* g_contents;
    extern Script* g_addItem;
    extern Script* g_activate;
    extern Script* g_refreshItems;
    extern Script* g_loadOrder;
    extern Script* g_objectiveShown;
    extern Script* g_objectiveDone;
    extern Script* g_completeQuest;
    extern Script* g_enable;
    extern Script* g_inDialogue;
    extern Script* g_closeMenu;
    extern Script* g_greet;
    extern Script* g_xp;
    extern Script* g_karma;
    extern Script* g_giveXp;
    extern Script* g_giveKarma;
    extern std::vector<EnableChange> g_enables;
    extern Script* g_placeItem;
    extern Script* g_itemCondition;
    extern Script* g_moveItem;
    extern Script* g_grabbed;
    extern Script* g_followers;
    extern Script* g_isTeammate;
    extern Script* g_crosshair;
    extern Script* g_limbsGone;
    extern NVSECommandTableInterface* g_cmdTable;
    extern Script* g_pcAddItem;
    extern Script* g_changeKind;
    extern Script* g_pcRemoveItem;
    extern Script* g_pcTake[2][6];
    extern Script* g_pcGive[2][6];
    extern Script* g_sneak;
    extern Script* g_rewardAdd;
    extern Script* g_rewardFlag;
    extern Script* g_rewardMark;
    extern Script* g_pcEquip;
    extern Script* g_pcUnequip;
    extern Script* g_pcAddPerk;
    extern Script* g_pcRemovePerk;
    extern Script* g_perkRanks;
    extern Script* g_creatureType;
    extern Script* g_noteInject;
    extern Script* g_noteShow;
    extern Script* g_pcAddNote;
    extern Script* g_pcRemoveNote;
    extern Script* g_pcAddImod;
    extern Script* g_pcRemoveImod;
    extern Script* g_pcDisable;
    extern Script* g_pcEnable;
    extern Script* g_pcCast;
    extern Script* g_pcAddPackage;
    extern Script* g_pcRemovePackage;
    extern Script* g_teammates;
    extern Script* g_isWaiting;
    extern Script* g_setWaiting;
    extern Script* g_followPackage;
    extern Script* g_givePackage;
    extern Script* g_dropPackage;
    extern Script* g_currentPackage;
    extern Script* g_inCombat;
    extern Script* g_wheelOpen;
    extern Script* g_isMobile;
    extern Script* g_disable;
    extern std::vector<PendingHit> g_myHits;
    extern std::set<const Script*> g_ourScripts;
}
