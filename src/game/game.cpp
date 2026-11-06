// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/level_up.h"
#include "coop/chat.h"
#include "coop/talk_log.h"
#include "coop/balance.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <utility>
#include <array>
#include <cmath>
#include <mutex>
#include <deque>
#include <intrin.h>

#include "coop/attack_versions.h"
#include "coop/clock.h"
#include "coop/shot_counter.h"
#include "game/addresses.h"
#include "game/mover.h"
#include "log.h"

#include "nvse/PluginAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"

#include <map>
#include <set>
#include <string>

extern const _FormHeap_Free FormHeap_Free = reinterpret_cast<_FormHeap_Free>(game::addr::kFormHeapFree);

namespace game
{
    namespace
    {
        NVSEScriptInterface* g_scripts = nullptr;
        NVSEEventManagerInterface* g_events = nullptr;
        NVSEArrayVarInterface* g_arrays = nullptr;
        NVSEConsoleInterface* g_console = nullptr;
        bool g_compiled = false;
        Script* g_spawn = nullptr;
        Script* g_bringToPlayer = nullptr;
        Script* g_move = nullptr;
        Script* g_travel = nullptr;
        Script* g_remove = nullptr;
        Script* g_playerFlags = nullptr;
        Script* g_clone = nullptr;
        Script* g_noTalk = nullptr;
        Script* g_fire = nullptr;
        std::array<Script*, coop::kAttackVersions> g_attackAnim{};
        std::array<Script*, coop::kAttackVersions> g_throwAnim{};
        std::array<Script*, coop::kAttackVersions> g_mineAnim{};
        Script* g_placeAt = nullptr;
        Script* g_rounds = nullptr;
        std::map<uint32_t, TESForm*> g_harmless;
        constexpr size_t kMaxHarmless = 256;
        std::map<uint32_t, BGSProjectile*> g_harmlessProjectiles;
        std::map<uint32_t, BGSExplosion*> g_harmlessExplosions;
        std::map<uint32_t, BGSExplosion*> g_quietExplosions;
        uint32_t g_myWeapon = 0;
        int g_myRounds = -1;
        int g_myAction = -1;
        coop::ShotCounter g_myShots;
        std::vector<uint32_t> g_standInBases;
        constexpr size_t kMaxStandInBases = 32;
        Script* g_turn = nullptr;
        Script* g_equipped = nullptr;
        Script* g_removeAllItems = nullptr;
        Script* g_addAndEquip = nullptr;
        Script* g_removeItem = nullptr;
        Script* g_addItems = nullptr;
        Script* g_alert = nullptr;
        Script* g_restrain = nullptr;
        Script* g_evaluate = nullptr;
        Script* g_combatOff = nullptr;

        constexpr int kEquipSlots = 20;
        Script* g_notify = nullptr;
        Script* g_notifyFor = nullptr;
        Script* g_moveBy = nullptr;
        Script* g_health = nullptr;
        Script* g_level = nullptr;
        Script* g_hit = nullptr;
        Script* g_maxHealth = nullptr;
        Script* g_changeHealth = nullptr;
        Script* g_makeCompanion = nullptr;
        Script* g_stopFighting = nullptr;
        Script* g_kill = nullptr;
        Script* g_playedIdle = nullptr;
        Script* g_combatTarget = nullptr;
        Script* g_forceTarget = nullptr;
        Script* g_update3D = nullptr;
        Script* g_isLocked = nullptr;
        Script* g_pileKind = nullptr;
        Script* g_makePile = nullptr;
        Script* g_fovNow = nullptr;
        Script* g_placeExtra = nullptr;
        Script* g_thinAmmo = nullptr;
        Script* g_playIdle = nullptr;
        Script* g_setMaxHealth = nullptr;
        Script* g_stopIdle = nullptr;
        Script* g_npcRounds = nullptr;
        Script* g_wornGear = nullptr;
        Script* g_wearAt = nullptr;
        Script* g_idleGroup = nullptr;
        Script* g_forceStopIdle = nullptr;
        Script* g_madeClear = nullptr;
        Script* g_madeAdd = nullptr;
        Script* g_madeCount = nullptr;
        Script* g_madeGet = nullptr;
        Script* g_resurrect = nullptr;
        Script* g_unconscious = nullptr;
        Script* g_controls = nullptr;
        Script* g_controlPressed = nullptr;
        Script* g_menuMode = nullptr;
        Script* g_keyPressed = nullptr;
        Script* g_chatHud = nullptr;
        Script* g_chatShow = nullptr;
        Script* g_onScreen = nullptr;
        constexpr int kChatAboveSlots = 5;
        Script* g_chatAbove[kChatAboveSlots] = {};
        constexpr int kMarkSlots = 4;
        Script* g_mark[kMarkSlots] = {};
        Script* g_questStage = nullptr;
        Script* g_tutorialTimer = nullptr;
        Script* g_leaveTutorial = nullptr;
        Script* g_activateBy = nullptr;
        Script* g_starterGear = nullptr;
        Script* g_effectTrait = nullptr;
        Script* g_loadedOfType = nullptr;
        Script* g_showMap = nullptr;
        Script* g_questObject = nullptr;
        Script* g_itemCount = nullptr;
        Script* g_sitNearest = nullptr;
        Script* g_reviveBar = nullptr;
        constexpr int kTalkSlots = static_cast<int>(coop::TalkLog::kMostLines);
        constexpr int kTalkEdges = 4;
        Script* g_talkText[kTalkSlots] = {};
        Script* g_talkPlace[kTalkSlots] = {};
        Script* g_talkBack[kTalkSlots] = {};
        Script* g_speedMult = nullptr;
        Script* g_actorAlpha = nullptr;
        Script* g_collision = nullptr;
        TESForm* g_teleporter = nullptr;
        Script* g_armorDR = nullptr;
        Script* g_itemValue = nullptr;
        Script* g_bodyPartLists = nullptr;
        Script* g_bodyPartMult = nullptr;
        Script* g_keyDisabled = nullptr;
        Script* g_disableKey = nullptr;
        Script* g_controlDisabled = nullptr;
        Script* g_disableControl = nullptr;
        Script* g_loadingScreen = nullptr;
        Script* g_aiState = nullptr;
        Script* g_print = nullptr;
        Script* g_openState = nullptr;
        Script* g_setOpen = nullptr;
        Script* g_lock = nullptr;
        Script* g_contents = nullptr;
        Script* g_addItem = nullptr;
        Script* g_activate = nullptr;
        Script* g_refreshItems = nullptr;
        Script* g_loadOrder = nullptr;
        Script* g_objectiveShown = nullptr;
        Script* g_objectiveDone = nullptr;
        Script* g_completeQuest = nullptr;
        Script* g_enable = nullptr;
        Script* g_inDialogue = nullptr;
        Script* g_closeMenu = nullptr;
        Script* g_greet = nullptr;
        Script* g_xp = nullptr;
        Script* g_karma = nullptr;
        Script* g_giveXp = nullptr;
        Script* g_giveKarma = nullptr;
        std::vector<EnableChange> g_enables;
        constexpr size_t kMaxPendingEnables = 1024;

        void RecordEnable(void* parameters, bool enabled)
        {
            auto** args = static_cast<void**>(parameters);
            auto* ref = args ? static_cast<TESObjectREFR*>(args[0]) : nullptr;
            if (ref && (ref->refID >> 24) != 0xFF && g_enables.size() < kMaxPendingEnables)
                g_enables.push_back({ref->refID, enabled});
        }

        void OnEnable(TESObjectREFR*, void* parameters) { RecordEnable(parameters, true); }
        void OnDisable(TESObjectREFR*, void* parameters) { RecordEnable(parameters, false); }
        Script* g_placeItem = nullptr;
        Script* g_itemCondition = nullptr;
        Script* g_moveItem = nullptr;
        Script* g_grabbed = nullptr;
        Script* g_followers = nullptr;
        Script* g_isTeammate = nullptr;
        Script* g_crosshair = nullptr;
        Script* g_limbsGone = nullptr;
        NVSECommandTableInterface* g_cmdTable = nullptr;
        Script* g_pcAddItem = nullptr;
        Script* g_changeKind = nullptr;
        Script* g_pcRemoveItem = nullptr;
        Script* g_pcRemoveAll = nullptr;
        Script* g_pcRemoveAllTyped = nullptr;
        Script* g_pcGiveAll = nullptr;
        Script* g_pcEquip = nullptr;
        Script* g_pcUnequip = nullptr;
        Script* g_pcAddPerk = nullptr;
        Script* g_pcRemovePerk = nullptr;
        Script* g_perkRanks = nullptr;
        Script* g_pcAddNote = nullptr;
        Script* g_pcRemoveNote = nullptr;
        Script* g_pcAddImod = nullptr;
        Script* g_pcRemoveImod = nullptr;
        Script* g_pcDisable = nullptr;
        Script* g_pcEnable = nullptr;
        Script* g_pcCast = nullptr;
        Script* g_pcAddPackage = nullptr;
        Script* g_pcRemovePackage = nullptr;
        Script* g_teammates = nullptr;
        Script* g_isWaiting = nullptr;
        Script* g_setWaiting = nullptr;
        Script* g_followPackage = nullptr;
        Script* g_givePackage = nullptr;
        Script* g_dropPackage = nullptr;
        Script* g_currentPackage = nullptr;
        Script* g_inCombat = nullptr;
        Script* g_wheelOpen = nullptr;
        Script* g_isMobile = nullptr;
        Script* g_disable = nullptr;

        struct PendingHit { uint32_t refId; uint32_t attackerId; float healthAtHit; };
        std::vector<PendingHit> g_myHits;
        constexpr size_t kMaxPendingHits = 64;

        constexpr float kRadToDeg = 57.2957795f;
        constexpr int kPuppetAmmo = 500;

        std::set<const Script*> g_ourScripts;

        Script* Compile(const char* what, const std::string& text)
        {
            Script* s = g_scripts->CompileScript(text.c_str());
            if (!s)
                LOG_ERROR("Couldn't compile the %s script", what);
            else
                g_ourScripts.insert(s);
            return s;
        }

        std::array<Script*, coop::kAttackVersions> CompileVersions(const char* what, const char* group)
        {
            const char* const kSuffixes[coop::kAttackVersions] = {"", "Up", "Down"};
            std::array<Script*, coop::kAttackVersions> scripts{};
            for (int i = 0; i < coop::kAttackVersions; ++i)
                scripts[i] = Compile(what, std::string("Begin Function {}\n  PlayGroupAlt ") + group + kSuffixes[i] +
                    " 1\nEnd\n");
            return scripts;
        }

        uint32_t FloatArg(float f) { return *reinterpret_cast<uint32_t*>(&f); }

        float Number(Script* script, TESObjectREFR* ref)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_compiled || !ref || !g_scripts->CallFunction(script, ref, nullptr, &result, 0))
                return 0;
            return static_cast<float>(result.GetNumber());
        }

        void OnHit(TESObjectREFR*, void* parameters)
        {
            auto** args = static_cast<void**>(parameters);
            auto* victim = static_cast<TESObjectREFR*>(args[0]);
            auto* attacker = static_cast<TESForm*>(args[1]);
            if (!g_compiled || !IsActor(victim) || g_myHits.size() >= kMaxPendingHits)
                return;
            if (victim == Player())
            {
                if (!attacker || attacker == Player() || !IsActor(static_cast<TESObjectREFR*>(attacker)))
                    return;
            }
            else
            {
                bool onStandIn = victim->baseForm && std::find(g_standInBases.begin(), g_standInBases.end(),
                    victim->baseForm->refID) != g_standInBases.end();
                if (!onStandIn && (!attacker || attacker != Player()))
                    return;
            }
            for (const PendingHit& p : g_myHits)
                if (p.refId == victim->refID)
                    return;
            float health = Health(victim);
            if (health > 0)
                g_myHits.push_back({victim->refID, attacker ? attacker->refID : 0, health});
        }
    }

    bool Init(const NVSEInterface* nvse)
    {
        g_scripts = static_cast<NVSEScriptInterface*>(nvse->QueryInterface(kInterface_Script));
        if (!g_scripts)
            LOG_ERROR("xNVSE has no script interface (xNVSE too old?)");
        g_events = static_cast<NVSEEventManagerInterface*>(nvse->QueryInterface(kInterface_EventManager));
        if (!g_events)
            LOG_ERROR("xNVSE has no event interface (xNVSE too old?)");
        g_console = static_cast<NVSEConsoleInterface*>(nvse->QueryInterface(kInterface_Console));
        g_arrays = static_cast<NVSEArrayVarInterface*>(nvse->QueryInterface(kInterface_ArrayVar));
        g_cmdTable = static_cast<NVSECommandTableInterface*>(nvse->QueryInterface(kInterface_CommandTable));
        return g_scripts && g_events;
    }

    bool ListenForHits()
    {
        if (g_events && g_events->SetNativeEventHandler("OnHit", OnHit))
            return true;
        LOG_ERROR("Couldn't listen for hits");
        return false;
    }

    bool ListenForEnables()
    {
        if (g_events && g_events->SetNativeEventHandler("OnEnable", OnEnable) &&
            g_events->SetNativeEventHandler("OnDisable", OnDisable))
            return true;
        LOG_ERROR("Couldn't listen for things being switched on and off");
        return false;
    }

    bool CompileScripts()
    {
        if (g_compiled || !g_scripts)
            return g_compiled;

        using namespace ids;
        g_spawn = Compile("place",
            "ref rBase\n"
            "ref rNew\n"
            "Begin Function {rBase}\n"
            "  set rNew to PlaceAtMe rBase 1\n"
            "  SetFunctionValue rNew\n"
            "End\n");
        g_clone = Compile("copy form",
            "ref rBase\n"
            "ref rCopy\n"
            "Begin Function {rBase}\n"
            "  set rCopy to TempCloneForm rBase\n"
            "  SetFunctionValue rCopy\n"
            "End\n");
        g_noTalk = Compile("no talking",
            "Begin OnActivate\n"
            "  return\n"
            "End\n");
        if (g_noTalk)
            g_noTalk->info.type = Script::eType_Object;
        g_fire = Compile("fire weapon",
            "ref rWeapon\n"
            "Begin Function {rWeapon}\n"
            "  FireWeapon rWeapon\n"
            "End\n");
        g_throwAnim = CompileVersions("throw animation", "AttackThrow");
        g_mineAnim = CompileVersions("mine animation", "PlaceMine");
        g_placeAt = Compile("place at",
            "ref rForm\n"
            "ref rWhere\n"
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "Begin Function {rForm, rWhere, fX, fY, fZ}\n"
            "  PlaceAtCell rForm 1 rWhere fX fY fZ\n"
            "End\n");
        g_attackAnim = CompileVersions("attack animation", "AttackRight");
        g_rounds = Compile("rounds",
            "int iRounds\n"
            "Begin Function {}\n"
            "  set iRounds to GetPlayerCurrentAmmoRounds\n"
            "  SetFunctionValue iRounds\n"
            "End\n");
        g_npcRounds = Compile("NPC rounds",
            "int iRounds\n"
            "Begin Function {}\n"
            "  set iRounds to GetCurrentAmmoRounds\n"
            "  SetFunctionValue iRounds\n"
            "End\n");
        g_turn = Compile("turn",
            "float fAngle\n"
            "Begin Function {fAngle}\n"
            "  SetAngle z fAngle\n"
            "End\n");
        g_bringToPlayer = Compile("bring to player",
            "ref rTo\n"
            "Begin Function {rTo}\n"
            "  MoveTo rTo\n"
            "End\n");
        g_move = Compile("move",
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "float fAngle\n"
            "Begin Function {fX, fY, fZ, fAngle}\n"
            "  SetPos x fX\n"
            "  SetPos y fY\n"
            "  SetPos z fZ\n"
            "  SetAngle z fAngle\n"
            "End\n");
        g_travel = Compile("travel",
            "ref rPlace\n"
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "float fAngle\n"
            "Begin Function {rPlace, fX, fY, fZ, fAngle}\n"
            "  MoveToCell rPlace fX fY fZ\n"
            "  SetAngle z fAngle\n"
            "End\n");
        g_playerFlags = Compile("stance flags",
            "int iFlags\n"
            "Begin Function {}\n"
            "  set iFlags to 0\n"
            "  if IsWeaponOut\n"
            "    set iFlags to 1\n"
            "  endif\n"
            "  if IsSneaking\n"
            "    set iFlags to iFlags + 2\n"
            "  endif\n"
            "  set iFlags to iFlags + GetSitting * 4\n"
            "  SetFunctionValue iFlags\n"
            "End\n");
        g_equipped = Compile("equipped item",
            "int iSlot\n"
            "ref rItem\n"
            "Begin Function {iSlot}\n"
            "  set rItem to GetEquippedObject iSlot\n"
            "  SetFunctionValue rItem\n"
            "End\n");
        g_removeAllItems = Compile("remove all items",
            "Begin Function {}\n"
            "  RemoveAllItems\n"
            "End\n");
        g_addAndEquip = Compile("add and equip",
            "ref rItem\n"
            "Begin Function {rItem}\n"
            "  AddItem rItem 1 1\n"
            "  EquipItem rItem 1 1\n"
            "End\n");
        g_addItems = Compile("add items",
            "ref rItem\n"
            "int iCount\n"
            "Begin Function {rItem, iCount}\n"
            "  AddItem rItem iCount 1\n"
            "End\n");
        g_alert = Compile("alert",
            "int iAlert\n"
            "Begin Function {iAlert}\n"
            "  SetAlert iAlert\n"
            "End\n");
        g_restrain = Compile("restrain",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  SetRestrained iOn\n"
            "End\n");
        g_combatOff = Compile("combat off",
            "int iOff\n"
            "Begin Function {iOff}\n"
            "  SetCombatDisabled iOff\n"
            "  if iOff\n"
            "    SetTeammateUsingAmmo 0\n"
            "  else\n"
            "    SetTeammateUsingAmmo 1\n"
            "  endif\n"
            "  if iOff && IsInCombat\n"
            "    StopCombat\n"
            "  endif\n"
            "End\n");
        g_evaluate = Compile("evaluate package",
            "Begin Function {}\n"
            "  EvaluatePackage\n"
            "End\n");
        g_wornGear = Compile("worn gear",
            "array_var aItems\n"
            "ref rItem\n"
            "int i\n"
            "float fFull\n"
            "float fNow\n"
            "int iHealth\n"
            "Begin Function {}\n"
            "  let aItems := ar_Construct \"array\"\n"
            "  while i < 20\n"
            "    let rItem := GetEquippedObject i\n"
            "    if IsFormValid rItem\n"
            "      set fFull to GetHealth rItem\n"
            "      set iHealth to 255\n"
            "      if fFull > 0\n"
            "        set fNow to GetEquippedCurrentHealth i\n"
            "        set iHealth to fNow / fFull * 100 + 0.5\n"
            "        if iHealth > 100\n"
            "          set iHealth to 100\n"
            "        elseif iHealth < 0\n"
            "          set iHealth to 0\n"
            "        endif\n"
            "      endif\n"
            "      ar_Append aItems rItem\n"
            "      ar_Append aItems iHealth\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "  SetFunctionValue aItems\n"
            "End\n");
        g_wearAt = Compile("wear at condition",
            "ref rItem\n"
            "int iHealth\n"
            "ref rWorn\n"
            "int i\n"
            "float fHealth\n"
            "Begin Function {rItem, iHealth}\n"
            "  if GetEquipped rItem == 0\n"
            "    AddItem rItem 1 1\n"
            "    EquipItem rItem 1 1\n"
            "  endif\n"
            "  set fHealth to GetHealth rItem\n"
            "  set fHealth to fHealth * iHealth / 100\n"
            "  while i < 20\n"
            "    let rWorn := GetEquippedObject i\n"
            "    if eval (rWorn == rItem)\n"
            "      SetEquippedCurrentHealth fHealth i\n"
            "      break\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "End\n");
        g_removeItem = Compile("remove item",
            "ref rItem\n"
            "Begin Function {rItem}\n"
            "  RemoveItem rItem 1 1\n"
            "End\n");
        g_remove = Compile("remove",
            "Begin Function {}\n"
            "  Disable\n"
            "  MarkForDelete\n"
            "End\n");
        g_notify = Compile("notify",
            "string_var sText\n"
            "Begin Function {sText}\n"
            "  MessageEx \"%z\" sText\n"
            "End\n");
        g_notifyFor = Compile("notify for a while",
            "string_var sText\n"
            "float fSeconds\n"
            "Begin Function {sText, fSeconds}\n"
            "  MessageExAlt fSeconds \"%z\" sText\n"
            "End\n");
        g_moveBy = Compile("move by",
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "float fAngle\n"
            "Begin Function {fX, fY, fZ, fAngle}\n"
            "  MoveTo player fX fY fZ\n"
            "  SetAngle z fAngle\n"
            "End\n");
        g_health = Compile("health",
            "float fHealth\n"
            "Begin Function {}\n"
            "  set fHealth to GetActorValue Health\n"
            "  SetFunctionValue fHealth\n"
            "End\n");
        g_maxHealth = Compile("max health",
            "float fHealth\n"
            "Begin Function {}\n"
            "  set fHealth to GetBaseActorValue Health\n"
            "  SetFunctionValue fHealth\n"
            "End\n");
        g_level = Compile("level",
            "int iLevel\n"
            "Begin Function {}\n"
            "  set iLevel to GetLevel\n"
            "  SetFunctionValue iLevel\n"
            "End\n");
        g_changeHealth = Compile("change health",
            "float fAmount\n"
            "float fLoss\n"
            "Begin Function {fAmount}\n"
            "  if fAmount < 0\n"
            "    set fLoss to 0 - fAmount\n"
            "    DamageActorValue Health fLoss\n"
            "  else\n"
            "    RestoreActorValue Health fAmount\n"
            "  endif\n"
            "End\n");
        g_makeCompanion = Compile("make companion",
            "ref rFaction\n"
            "Begin Function {rFaction}\n"
            "  SetActorValue Health 10000\n"
            "  SetIgnoreFriendlyHits 1\n"
            "  SetActorValue Aggression 0\n"
            "  SetActorValue Assistance 0\n"
            "  SetActorValue Confidence 4\n"
            "  SetCombatDisabled 1\n"
            "  if rFaction\n"
            "    AddToFaction rFaction 0\n"
            "  endif\n"
            "End\n");
        g_stopFighting = Compile("stop fighting",
            "Begin Function {}\n"
            "  if IsInCombat\n"
            "    StopCombat\n"
            "  endif\n"
            "End\n");
        g_kill = Compile("kill",
            "int iLimb\n"
            "ref rSelf\n"
            "Begin Function {iLimb}\n"
            "  SetActorRefEssential 0\n"
            "  SetGhost 0\n"
            "  set rSelf to GetSelf\n"
            "  Kill rSelf iLimb\n"
            "End\n");
        g_limbsGone = Compile("limbs gone",
            "int i\n"
            "int iBit\n"
            "int iMask\n"
            "Begin Function {}\n"
            "  set iBit to 1\n"
            "  while i < 15\n"
            "    if IsLimbGone i\n"
            "      set iMask to iMask + iBit\n"
            "    endif\n"
            "    set iBit to iBit * 2\n"
            "    set i to i + 1\n"
            "  loop\n"
            "  SetFunctionValue iMask\n"
            "End\n");
        g_unconscious = Compile("unconscious",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  SetUnconscious iOn\n"
            "End\n");
        g_controls = Compile("player controls",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  if iOn\n"
            "    EnablePlayerControls\n"
            "  else\n"
            "    DisablePlayerControls 1 1 1 0 0 1 1\n"
            "  endif\n"
            "End\n");
        g_menuMode = Compile("menu mode",
            "int iMenu\n"
            "Begin Function {}\n"
            "  set iMenu to MenuMode\n"
            "  SetFunctionValue iMenu\n"
            "End\n");
        g_loadingScreen = Compile("loading screen",
            "int iMenu\n"
            "Begin Function {}\n"
            "  set iMenu to MenuMode 1007\n"
            "  SetFunctionValue iMenu\n"
            "End\n");
        g_keyPressed = Compile("key pressed",
            "int iKey\n"
            "int iPressed\n"
            "Begin Function {iKey}\n"
            "  if MenuMode == 0\n"
            "    set iPressed to IsKeyPressed iKey\n"
            "  endif\n"
            "  SetFunctionValue iPressed\n"
            "End\n");
        g_chatHud = Compile("chat hud",
            "string_var sXml\n"
            "Begin Function {sXml}\n"
            "  InjectUIComponent \"HUDMainMenu\" \"%z\" sXml\n"
            "End\n");
        g_chatShow = Compile("chat show",
            "string_var sText\n"
            "int iShow\n"
            "Begin Function {sText, iShow}\n"
            "  SetUIStringEx \"HUDMainMenu/NVCoopChat/NVCoopChatText/string\" \"%z\" sText\n"
            "  SetUIFloat \"HUDMainMenu/NVCoopChat/visible\" iShow\n"
            "End\n");
        for (const char* call : {"WorldToScreen fX fY fZ 0 0 150 2 rWho", "JGLegacyWorldToScreen \"fX\" \"fY\" \"fZ\" 0 0 150 2 rWho",
                 "WorldToScreen \"fX\" \"fY\" \"fZ\" 0 0 150 2 rWho"})
        {
            if (g_onScreen)
                break;
            g_onScreen = g_scripts->CompileScript((std::string(
                "ref rWho\n"
                "float fX\n"
                "float fY\n"
                "float fZ\n"
                "int iOn\n"
                "array_var aOut\n"
                "Begin Function {rWho}\n"
                "  set iOn to ") + call + "\n"
                "  let aOut := ar_List iOn fX fY\n"
                "  SetFunctionValue aOut\n"
                "End\n").c_str());
            if (g_onScreen)
            {
                g_ourScripts.insert(g_onScreen);
                LOG_INFO("Chat: places on screen with Johnny Guitar's \"%s\"", call);
            }
        }
        if (!g_onScreen)
            LOG_ERROR("Couldn't compile the on screen script (Johnny Guitar's WorldToScreen): no chat above heads");
        for (int slot = 0; slot < kMarkSlots; ++slot)
        {
            std::string path = "HUDMainMenu/NVCoopMark" + std::to_string(slot);
            g_mark[slot] = Compile("friend marker",
                "string_var sName\n"
                "float fX\n"
                "float fY\n"
                "int iShow\n"
                "int iRed\n"
                "Begin Function {sName, fX, fY, iShow, iRed}\n"
                "  if iShow == 2\n"
                "    SetUIStringEx \"" + path + "/string\" \"%z%rv\" sName\n"
                "    if iRed\n"
                "      SetUIFloat \"" + path + "/systemcolor\" 0\n"
                "      SetUIFloat \"" + path + "/red\" 255\n"
                "      SetUIFloat \"" + path + "/green\" 60\n"
                "      SetUIFloat \"" + path + "/blue\" 40\n"
                "    else\n"
                "      SetUIFloat \"" + path + "/systemcolor\" 1\n"
                "    endif\n"
                "  endif\n"
                "  if iShow\n"
                "    SetUIFloat \"" + path + "/user0\" fX\n"
                "    SetUIFloat \"" + path + "/user1\" fY\n"
                "  endif\n"
                "  SetUIFloat \"" + path + "/visible\" iShow\n"
                "End\n");
        }
        for (int slot = 0; slot < kChatAboveSlots; ++slot)
        {
            std::string path = "HUDMainMenu/NVCoopAbove" + std::to_string(slot);
            g_chatAbove[slot] = Compile("chat above",
                "string_var sText\n"
                "float fX\n"
                "float fY\n"
                "int iShow\n"
                "Begin Function {sText, fX, fY, iShow}\n"
                "  if iShow == 2\n"
                "    SetUIStringEx \"" + path + "/string\" \"%z\" sText\n"
                "  endif\n"
                "  if iShow\n"
                "    SetUIFloat \"" + path + "/user0\" fX\n"
                "    SetUIFloat \"" + path + "/user1\" fY\n"
                "  endif\n"
                "  SetUIFloat \"" + path + "/visible\" iShow\n"
                "End\n");
        }
        g_tutorialTimer = Compile("tutorial timer",
            "int iRun\n"
            "Begin Function {iRun}\n"
            "  set VCG01.bRunTimer to iRun\n"
            "End\n");
        g_questStage = Compile("quest stage",
            "ref rQuest\n"
            "int iStage\n"
            "Begin Function {rQuest}\n"
            "  set iStage to -1\n"
            "  if GetQuestRunning rQuest\n"
            "    set iStage to GetStage rQuest\n"
            "  endif\n"
            "  SetFunctionValue iStage\n"
            "End\n");
        g_leaveTutorial = Compile("leave tutorial",
            "ref rTutorial\n"
            "ref rSunny\n"
            "ref rDoor\n"
            "ref rPipBoy\n"
            "ref rGlove\n"
            "ref rSuit\n"
            "ref rNote\n"
            "Begin Function {rTutorial, rSunny, rDoor, rPipBoy, rGlove, rSuit, rNote}\n"
            "  player.RemoveScriptPackage\n"
            "  player.PlayGroup Idle 1\n"
            "  EnablePlayerControls\n"
            "  player.AddItem rPipBoy 1 1\n"
            "  player.AddItem rGlove 1 1\n"
            "  player.EquipItem rPipBoy 0 1\n"
            "  player.EquipItem rGlove 0 1\n"
            "  ResetPipBoyManager\n"
            "  player.AddItem rSuit 1 1\n"
            "  player.EquipItem rSuit 0 1\n"
            "  AddNote rNote\n"
            "  rDoor.SetDestroyed 0\n"
            "  SetStage rTutorial 200\n"
            "  SetStage rSunny 5\n"
            "End\n");
        g_activateBy = Compile("activate by player",
            "ref rWhat\n"
            "int iMoveHere\n"
            "Begin Function {rWhat, iMoveHere}\n"
            "  if iMoveHere\n"
            "    player.PlayGroup Idle 1\n"
            "    rWhat.MoveTo player\n"
            "    rWhat.Activate player 1\n"
            "  else\n"
            "    rWhat.Activate player\n"
            "  endif\n"
            "End\n");
        g_starterGear = Compile("starter gear",
            "ref rStimpak\n"
            "ref rCaps\n"
            "ref rLockpick\n"
            "ref r9mm\n"
            "ref r9mmAmmo\n"
            "ref rLaser\n"
            "ref rCells\n"
            "ref rDynamite\n"
            "ref rRazor\n"
            "ref rGloves\n"
            "Begin Function {rStimpak, rCaps, rLockpick, r9mm, r9mmAmmo, rLaser, rCells, rDynamite, rRazor, rGloves}\n"
            "  player.AddItem rStimpak 4\n"
            "  player.AddItem rCaps 18\n"
            "  if IsPlayerTagSkill Lockpick\n"
            "    player.AddItem rLockpick 12\n"
            "  else\n"
            "    player.AddItem rLockpick 6\n"
            "  endif\n"
            "  if IsPlayerTagSkill Guns\n"
            "    player.AddItem r9mm 1\n"
            "    player.AddItem r9mmAmmo 60\n"
            "  elseif IsPlayerTagSkill EnergyWeapons\n"
            "    player.AddItem rLaser 1\n"
            "    player.AddItem rCells 25\n"
            "  elseif IsPlayerTagSkill Explosives\n"
            "    player.AddItem rDynamite 8\n"
            "  else\n"
            "    player.AddItem r9mm 1\n"
            "    player.AddItem r9mmAmmo 30\n"
            "  endif\n"
            "  if IsPlayerTagSkill MeleeWeapons\n"
            "    player.AddItem rRazor 1\n"
            "  elseif IsPlayerTagSkill Unarmed\n"
            "    player.AddItem rGloves 1\n"
            "  endif\n"
            "End\n");
        g_effectTrait = Compile("effect trait",
            "ref rItem\n"
            "int iEffect\n"
            "int iTrait\n"
            "int iValue\n"
            "int iWas\n"
            "Begin Function {rItem, iEffect, iTrait, iValue}\n"
            "  set iWas to GetNthEffectTraitNumeric rItem iEffect iTrait\n"
            "  if iValue >= 0\n"
            "    SetNthEffectTraitNumeric rItem iEffect iTrait iValue\n"
            "  endif\n"
            "  SetFunctionValue iWas\n"
            "End\n");
        g_loadedOfType = Compile("loaded of type",
            "int iType\n"
            "array_var aForms\n"
            "Begin Function {iType}\n"
            "  let aForms := GetLoadedTypeArray iType\n"
            "  SetFunctionValue aForms\n"
            "End\n");
        g_showMap = Compile("show map",
            "ref rMarker\n"
            "Begin Function {rMarker}\n"
            "  ShowMap rMarker 1\n"
            "End\n");
        g_questObject = Compile("quest object",
            "ref rItem\n"
            "Begin Function {rItem}\n"
            "  SetQuestObject rItem 1\n"
            "End\n");
        g_itemCount = Compile("item count",
            "ref rItem\n"
            "int iCount\n"
            "Begin Function {rItem}\n"
            "  set iCount to GetItemCount rItem\n"
            "  SetFunctionValue iCount\n"
            "End\n");
        g_collision = Compile("collision",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  ToggleObjectCollision iOn\n"
            "End\n");
        g_speedMult = Compile("speed mult",
            "int iMult\n"
            "Begin Function {iMult}\n"
            "  SetSpeedMult iMult\n"
            "End\n");
        g_actorAlpha = Compile("actor alpha",
            "float fAlpha\n"
            "Begin Function {fAlpha}\n"
            "  SetActorAlpha fAlpha\n"
            "End\n");
        g_reviveBar = Compile("revive bar",
            "string_var sText\n"
            "int iShow\n"
            "Begin Function {sText, iShow}\n"
            "  if iShow\n"
            "    SetUIStringEx \"HUDMainMenu/NVCoopRevive/string\" \"%z\" sText\n"
            "  endif\n"
            "  SetUIFloat \"HUDMainMenu/NVCoopRevive/visible\" iShow\n"
            "End\n");
        for (int slot = 0; slot < kTalkSlots; ++slot)
        {
            std::string n = std::to_string(slot);
            std::string text = "HUDMainMenu/NVCoopTalk" + n;
            std::string edge = "HUDMainMenu/NVCoopTalkEdge" + n + "x";
            std::string line = "HUDMainMenu/NVCoopTalkLine" + n;
            std::string bar = "HUDMainMenu/NVCoopTalkBar" + n;
            std::string setText = "  SetUIStringEx \"" + text + "/string\" \"%z\" sText\n";
            for (int e = 0; e < kTalkEdges; ++e)
                setText += "  SetUIStringEx \"" + edge + std::to_string(e) + "/string\" \"%z\" sText\n";
            g_talkText[slot] = Compile("talk log text",
                "string_var sText\n"
                "float fBright\n"
                "float fRed\n"
                "float fGreen\n"
                "float fBlue\n"
                "Begin Function {sText, fBright, fRed, fGreen, fBlue}\n" + setText +
                "  SetUIFloat \"" + text + "/brightness\" fBright\n"
                "  SetUIFloat \"" + text + "/red\" fRed\n"
                "  SetUIFloat \"" + text + "/green\" fGreen\n"
                "  SetUIFloat \"" + text + "/blue\" fBlue\n"
                "End\n");
            std::string placeEdges;
            for (int e = 0; e < kTalkEdges; ++e)
            {
                std::string path = edge + std::to_string(e);
                placeEdges += std::string("  set fX to ") + (e % 2 ? "30 + fOff" : "30 - fOff") + "\n"
                    "  set fEdgeY to fY " + (e / 2 ? "+" : "-") + " fOff\n"
                    "  SetUIFloat \"" + path + "/x\" fX\n"
                    "  SetUIFloat \"" + path + "/y\" fEdgeY\n"
                    "  SetUIFloat \"" + path + "/wrapwidth\" fWrap\n"
                    "  SetUIFloat \"" + path + "/alpha\" fEdge\n"
                    "  SetUIFloat \"" + path + "/visible\" " + (e == 3 ? "iLast" : "iShow") + "\n";
            }
            g_talkPlace[slot] = Compile("talk log place",
                "float fY\n"
                "float fWrap\n"
                "float fAlpha\n"
                "float fEdge\n"
                "float fOff\n"
                "int iEdges\n"
                "float fX\n"
                "float fEdgeY\n"
                "int iShow\n"
                "int iLast\n"
                "Begin Function {fY, fWrap, fAlpha, fEdge, fOff, iEdges}\n"
                "  set iShow to fAlpha > 0\n"
                "  SetUIFloat \"" + text + "/y\" fY\n"
                "  SetUIFloat \"" + text + "/wrapwidth\" fWrap\n"
                "  SetUIFloat \"" + text + "/alpha\" fAlpha\n"
                "  SetUIFloat \"" + text + "/visible\" iShow\n"
                "  set iShow to 0\n"
                "  if fEdge > 0\n"
                "    if iEdges > 0\n"
                "      set iShow to 1\n"
                "    endif\n"
                "  endif\n"
                "  set iLast to iShow\n"
                "  if iEdges == 2\n"
                "    set iShow to 0\n"
                "  endif\n" + placeEdges +
                "End\n");
            g_talkBack[slot] = Compile("talk log back",
                "float fLineY\n"
                "float fLineWidth\n"
                "float fLine\n"
                "float fBarY\n"
                "float fBarHeight\n"
                "float fBarWidth\n"
                "float fBar\n"
                "int iShow\n"
                "Begin Function {fLineY, fLineWidth, fLine, fBarY, fBarHeight, fBarWidth, fBar}\n"
                "  set iShow to fLine > 0\n"
                "  SetUIFloat \"" + line + "/y\" fLineY\n"
                "  SetUIFloat \"" + line + "/width\" fLineWidth\n"
                "  SetUIFloat \"" + line + "/alpha\" fLine\n"
                "  SetUIFloat \"" + line + "/visible\" iShow\n"
                "  set iShow to fBar > 0\n"
                "  SetUIFloat \"" + bar + "/y\" fBarY\n"
                "  SetUIFloat \"" + bar + "/height\" fBarHeight\n"
                "  SetUIFloat \"" + bar + "/width\" fBarWidth\n"
                "  SetUIFloat \"" + bar + "/alpha\" fBar\n"
                "  SetUIFloat \"" + bar + "/visible\" iShow\n"
                "End\n");
        }
        g_sitNearest = Compile("sit nearest",
            "ref rActor\n"
            "ref rChair\n"
            "ref rBest\n"
            "float fBest\n"
            "float fDistance\n"
            "array_var aRefs\n"
            "int i\n"
            "Begin Function {rActor}\n"
            "  let aRefs := GetRefs 39 1\n"
            "  set fBest to 999999\n"
            "  while i < ar_Size aRefs\n"
            "    let rChair := aRefs[i]\n"
            "    set fDistance to rActor.GetDistance rChair\n"
            "    if fDistance < fBest\n"
            "      set fBest to fDistance\n"
            "      set rBest to rChair\n"
            "    endif\n"
            "    set i to i + 1\n"
            "  loop\n"
            "  if rBest\n"
            "    rBest.Activate rActor\n"
            "  endif\n"
            "  SetFunctionValue rBest\n"
            "End\n");
        g_armorDR = Compile("armor DR",
            "ref rArmor\n"
            "int iDR\n"
            "Begin Function {rArmor}\n"
            "  set iDR to GetArmorAR rArmor\n"
            "  SetFunctionValue iDR\n"
            "End\n");
        g_itemValue = Compile("item value",
            "ref rItem\n"
            "int iValue\n"
            "Begin Function {rItem}\n"
            "  set iValue to GetValue rItem\n"
            "  SetFunctionValue iValue\n"
            "End\n");
        g_bodyPartLists = Compile("body part lists",
            "array_var aLists\n"
            "Begin Function {}\n"
            "  let aLists := GetLoadedTypeArray 87\n"
            "  SetFunctionValue aLists\n"
            "End\n");
        g_bodyPartMult = Compile("body part damage",
            "ref rList\n"
            "int iPart\n"
            "float fValue\n"
            "float fWas\n"
            "Begin Function {rList, iPart, fValue}\n"
            "  set fWas to GetBodyPartTraitNumeric rList iPart 1\n"
            "  if fValue >= 0\n"
            "    SetBodyPartTraitNumeric rList iPart 1 fValue\n"
            "  endif\n"
            "  SetFunctionValue fWas\n"
            "End\n");
        g_keyDisabled = Compile("key disabled",
            "int iKey\n"
            "int iOff\n"
            "Begin Function {iKey}\n"
            "  set iOff to IsKeyDisabled iKey\n"
            "  SetFunctionValue iOff\n"
            "End\n");
        g_disableKey = Compile("disable key",
            "int iKey\n"
            "int iOff\n"
            "Begin Function {iKey, iOff}\n"
            "  if iOff\n"
            "    DisableKey iKey\n"
            "  else\n"
            "    EnableKey iKey\n"
            "  endif\n"
            "End\n");
        g_controlDisabled = Compile("control disabled",
            "int iControl\n"
            "int iOff\n"
            "Begin Function {iControl}\n"
            "  set iOff to IsControlDisabled iControl\n"
            "  SetFunctionValue iOff\n"
            "End\n");
        g_disableControl = Compile("disable control",
            "int iControl\n"
            "int iOff\n"
            "Begin Function {iControl, iOff}\n"
            "  if iOff\n"
            "    DisableControl iControl\n"
            "  else\n"
            "    EnableControl iControl\n"
            "  endif\n"
            "End\n");
        g_controlPressed = Compile("control pressed",
            "int iControl\n"
            "int iPressed\n"
            "Begin Function {iControl}\n"
            "  set iPressed to IsControlPressed iControl\n"
            "  SetFunctionValue iPressed\n"
            "End\n");
        g_hit = Compile("hit",
            "ref rTarget\n"
            "ref rBy\n"
            "float fDamage\n"
            "Begin Function {rTarget, rBy, fDamage}\n"
            "  rTarget.DamageActorValue Health fDamage\n"
            "  if rTarget.GetDead == 0 && rTarget.IsInCombat == 0\n"
            "    rTarget.StartCombat rBy\n"
            "  endif\n"
            "End\n");
        g_placeExtra = Compile("place extra enemy",
            "ref rBase\n"
            "ref rTarget\n"
            "ref rNew\n"
            "Begin Function {rBase, rTarget}\n"
            "  set rNew to PlaceAtMe rBase 1 600 1\n"
            "  if rNew\n"
            "    rNew.StartCombat rTarget\n"
            "  endif\n"
            "  SetFunctionValue rNew\n"
            "End\n");
        g_thinAmmo = Compile("thin ammo",
            "int iKept\n"
            "int iN\n"
            "int i\n"
            "ref rItem\n"
            "int iHave\n"
            "int iGone\n"
            "int iTotal\n"
            "Begin Function {iKept}\n"
            "  set iN to GetNumItems\n"
            "  set i to 0\n"
            "  while i < iN\n"
            "    set rItem to GetInventoryObject i\n"
            "    if GetObjectType rItem == 41\n"
            "      set iHave to GetItemCount rItem\n"
            "      set iGone to iHave - iHave * iKept / 100\n"
            "      if iGone > 0\n"
            "        RemoveItem rItem iGone 1\n"
            "        let iTotal += iGone\n"
            "      endif\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "  SetFunctionValue iTotal\n"
            "End\n");
        g_fovNow = Compile("field of view",
            "int iWhich\n"
            "float fFov\n"
            "Begin Function {iWhich}\n"
            "  set fFov to GetPlayerCamFOV iWhich\n"
            "  SetFunctionValue fFov\n"
            "End\n");
        g_update3D = Compile("update 3D",
            "Begin Function {}\n"
            "  Update3D\n"
            "End\n");
        g_forceTarget = Compile("force target",
            "ref rTarget\n"
            "ref rNow\n"
            "Begin Function {rTarget}\n"
            "  if rTarget\n"
            "    if GetDead\n"
            "      return\n"
            "    endif\n"
            "    set rNow to GetCombatTarget\n"
            "    if rNow != rTarget\n"
            "      if IsInCombat\n"
            "        StopCombat\n"
            "      endif\n"
            "      StartCombat rTarget\n"
            "    endif\n"
            "    SetCombatDisabled 1 rTarget\n"
            "  else\n"
            "    SetCombatDisabled 0\n"
            "  endif\n"
            "End\n");

        g_aiState = Compile("AI state",
            "int iBits\n"
            "Begin Function {}\n"
            "  set iBits to IsWeaponOut + IsSneaking * 2 + IsInCombat * 4 + GetRestrained * 8\n"
            "  set iBits to iBits + GetKnockedState * 16 + GetSitting * 128\n"
            "  set iBits to iBits + (GetCurrentAIPackage + 1) * 2048 + (GetCurrentAIProcedure + 1) * 262144\n"
            "  SetFunctionValue iBits\n"
            "End\n");
        g_combatTarget = Compile("combat target",
            "ref rTarget\n"
            "Begin Function {}\n"
            "  set rTarget to GetCombatTarget\n"
            "  SetFunctionValue rTarget\n"
            "End\n");
        g_playedIdle = Compile("played idle",
            "ref rIdle\n"
            "Begin Function {}\n"
            "  set rIdle to GetPlayedIdle\n"
            "  SetFunctionValue rIdle\n"
            "End\n");
        g_playIdle = Compile("play idle",
            "ref rIdle\n"
            "Begin Function {rIdle}\n"
            "  PlayIdleEx rIdle\n"
            "End\n");
        g_setMaxHealth = Compile("set max health",
            "float fHealth\n"
            "Begin Function {fHealth}\n"
            "  SetActorValue Health fHealth\n"
            "End\n");
        g_stopIdle = Compile("stop idle",
            "Begin Function {}\n"
            "  StopIdle\n"
            "End\n");
        g_idleGroup = Compile("idle group",
            "Begin Function {}\n"
            "  PlayGroupAlt Idle 1\n"
            "End\n");
        g_forceStopIdle = Compile("force stop idle",
            "Begin Function {}\n"
            "  ForceStopIdle\n"
            "End\n");
        g_madeClear = Compile("forget made characters",
            "Begin Function {}\n"
            "  AuxVarErase \"_NVCoopMade\"\n"
            "End\n");
        g_madeAdd = Compile("note a made character",
            "ref rMade\n"
            "Begin Function {rMade}\n"
            "  AuxVarSetRef \"_NVCoopMade\" rMade -1\n"
            "End\n");
        g_madeCount = Compile("count made characters",
            "int iCount\n"
            "Begin Function {}\n"
            "  set iCount to AuxVarSize \"_NVCoopMade\"\n"
            "  SetFunctionValue iCount\n"
            "End\n");
        g_madeGet = Compile("a made character",
            "int iIndex\n"
            "ref rMade\n"
            "Begin Function {iIndex}\n"
            "  set rMade to AuxVarGetRef \"_NVCoopMade\" iIndex\n"
            "  SetFunctionValue rMade\n"
            "End\n");
        g_resurrect = Compile("resurrect",
            "Begin Function {}\n"
            "  ResurrectActor 0\n"
            "End\n");

        g_print = Compile("console print",
            "string_var sText\n"
            "Begin Function {sText}\n"
            "  PrintToConsole \"%z\" sText\n"
            "End\n");

        g_openState = Compile("door open state",
            "int iState\n"
            "Begin Function {}\n"
            "  set iState to GetOpenState\n"
            "  SetFunctionValue iState\n"
            "End\n");
        g_setOpen = Compile("open door",
            "int iOpen\n"
            "Begin Function {iOpen}\n"
            "  SetOpenState iOpen\n"
            "End\n");
        g_pileKind = Compile("pile kind",
            "int iKind\n"
            "Begin Function {}\n"
            "  if HasMagicEffect GooificationEffect || HasMagicEffect GooificationEffectShader\n"
            "    set iKind to 1\n"
            "  elseif HasMagicEffect LaserDisintegrationEffect\n"
            "    set iKind to 2\n"
            "  elseif HasMagicEffect AlienDisintegrationEffect\n"
            "    set iKind to 3\n"
            "  endif\n"
            "  SetFunctionValue iKind\n"
            "End\n");
        g_makePile = Compile("make pile",
            "int iKind\n"
            "Begin Function {iKind}\n"
            "  if iKind == 1\n"
            "    CastImmediateOnSelf PlasmaEffect\n"
            "  elseif iKind == 2\n"
            "    CastImmediateOnSelf LaserDisintegrationFXSpell\n"
            "  elseif iKind == 3\n"
            "    CastImmediateOnSelf AlienDisintegrationFXSpell\n"
            "  endif\n"
            "End\n");
        g_isLocked = Compile("is locked",
            "int iLocked\n"
            "Begin Function {}\n"
            "  set iLocked to GetLocked\n"
            "  SetFunctionValue iLocked\n"
            "End\n");
        g_lock = Compile("lock door",
            "int iLocked\n"
            "int iLevel\n"
            "Begin Function {iLocked, iLevel}\n"
            "  if iLocked\n"
            "    Lock iLevel\n"
            "  else\n"
            "    Unlock\n"
            "  endif\n"
            "End\n");

        g_contents = Compile("contents",
            "array_var aItems\n"
            "array_var aRefs\n"
            "ref rItem\n"
            "ref rStack\n"
            "int i\n"
            "int n\n"
            "int j\n"
            "int m\n"
            "float fFull\n"
            "float fNow\n"
            "int iHealth\n"
            "Begin Function {}\n"
            "  let aItems := ar_Construct \"array\"\n"
            "  set n to GetNumItems\n"
            "  while i < n\n"
            "    let rItem := GetInventoryObject i\n"
            "    set fFull to GetHealth rItem\n"
            "    if fFull > 0\n"
            "      let aRefs := GetInvRefsForItem rItem\n"
            "      set m to ar_Size aRefs\n"
            "      set j to 0\n"
            "      while j < m\n"
            "        let rStack := aRefs[j]\n"
            "        set fNow to rStack.GetCurrentHealth\n"
            "        set iHealth to fNow / fFull * 100 + 0.5\n"
            "        if iHealth > 100\n"
            "          set iHealth to 100\n"
            "        elseif iHealth < 0\n"
            "          set iHealth to 0\n"
            "        endif\n"
            "        ar_Append aItems rItem\n"
            "        ar_Append aItems (rStack.GetRefCount)\n"
            "        ar_Append aItems iHealth\n"
            "        ar_Append aItems (rStack.GetWeaponRefModFlags)\n"
            "        let j += 1\n"
            "      loop\n"
            "    else\n"
            "      ar_Append aItems rItem\n"
            "      ar_Append aItems (GetItemCount rItem)\n"
            "      ar_Append aItems 255\n"
            "      ar_Append aItems 0\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "  SetFunctionValue aItems\n"
            "End\n");
        g_changeKind = Compile("change item kind",
            "ref rItem\n"
            "int iCount\n"
            "int iHealth\n"
            "int iMods\n"
            "array_var aRefs\n"
            "ref rStack\n"
            "int j\n"
            "int m\n"
            "int iHave\n"
            "int iStackHealth\n"
            "float fFull\n"
            "Begin Function {rItem, iCount, iHealth, iMods}\n"
            "  set fFull to GetHealth rItem\n"
            "  if iCount > 0\n"
            "    if iMods == 0\n"
            "      AddItemAlt rItem iCount iHealth 0 1\n"
            "    else\n"
            "      while iCount > 0\n"
            "        AddItemAlt rItem 1 iHealth 0 1\n"
            "        let aRefs := GetInvRefsForItem rItem\n"
            "        set m to ar_Size aRefs\n"
            "        set j to 0\n"
            "        while j < m\n"
            "          let rStack := aRefs[j]\n"
            "          set iStackHealth to rStack.GetCurrentHealth / fFull * 100 + 0.5\n"
            "          if rStack.GetWeaponRefModFlags == 0 && iStackHealth == iHealth && rStack.GetRefCount == 1\n"
            "            rStack.SetWeaponRefModFlags iMods\n"
            "            set j to m\n"
            "          endif\n"
            "          let j += 1\n"
            "        loop\n"
            "        let iCount -= 1\n"
            "      loop\n"
            "    endif\n"
            "  elseif iCount < 0\n"
            "    set iCount to 0 - iCount\n"
            "    let aRefs := GetInvRefsForItem rItem\n"
            "    set m to ar_Size aRefs\n"
            "    set j to 0\n"
            "    while j < m && iCount > 0\n"
            "      let rStack := aRefs[j]\n"
            "      set iStackHealth to rStack.GetCurrentHealth / fFull * 100 + 0.5\n"
            "      if iStackHealth > 100\n"
            "        set iStackHealth to 100\n"
            "      endif\n"
            "      if rStack.GetWeaponRefModFlags == iMods && iStackHealth == iHealth\n"
            "        set iHave to rStack.GetRefCount\n"
            "        if iHave > iCount\n"
            "          set iHave to iCount\n"
            "        endif\n"
            "        rStack.RemoveMeIRAlt iHave 0\n"
            "        let iCount -= iHave\n"
            "      endif\n"
            "      let j += 1\n"
            "    loop\n"
            "  endif\n"
            "End\n");
        g_addItem = Compile("add or remove item",
            "ref rItem\n"
            "int iCount\n"
            "Begin Function {rItem, iCount}\n"
            "  if iCount > 0\n"
            "    AddItem rItem iCount 1\n"
            "  elseif iCount < 0\n"
            "    set iCount to 0 - iCount\n"
            "    RemoveItem rItem iCount 1\n"
            "  endif\n"
            "End\n");
        g_activate = Compile("activate",
            "Begin Function {}\n"
            "  Activate player\n"
            "End\n");
        g_refreshItems = Compile("refresh items",
            "Begin Function {}\n"
            "  RefreshItemsList\n"
            "End\n");

        g_placeItem = Compile("place item",
            "ref rBase\n"
            "int iCount\n"
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "int iHealth\n"
            "ref rNew\n"
            "float fHealth\n"
            "Begin Function {rBase, iCount, fX, fY, fZ, iHealth}\n"
            "  set rNew to PlaceAtMe rBase 1\n"
            "  if iCount > 1\n"
            "    rNew.SetRefCount iCount\n"
            "  endif\n"
            "  if iHealth < 100\n"
            "    set fHealth to GetHealth rBase\n"
            "    set fHealth to fHealth * iHealth / 100\n"
            "    rNew.SetCurrentHealth fHealth\n"
            "  endif\n"
            "  rNew.SetPos x fX\n"
            "  rNew.SetPos y fY\n"
            "  rNew.SetPos z fZ\n"
            "  SetFunctionValue rNew\n"
            "End\n");
        g_itemCondition = Compile("item condition",
            "ref rBase\n"
            "float fFull\n"
            "float fNow\n"
            "int iHealth\n"
            "int iMods\n"
            "Begin Function {}\n"
            "  let rBase := GetBaseObject\n"
            "  set fFull to GetHealth rBase\n"
            "  set iHealth to 255\n"
            "  if fFull > 0\n"
            "    set fNow to GetCurrentHealth\n"
            "    set iHealth to fNow / fFull * 100 + 0.5\n"
            "    if iHealth > 100\n"
            "      set iHealth to 100\n"
            "    elseif iHealth < 0\n"
            "      set iHealth to 0\n"
            "    endif\n"
            "  endif\n"
            "  set iMods to GetWeaponRefModFlags\n"
            "  set iHealth to iHealth + iMods * 256\n"
            "  SetFunctionValue iHealth\n"
            "End\n");
        g_moveItem = Compile("move item",
            "float fX\n"
            "float fY\n"
            "float fZ\n"
            "float fRX\n"
            "float fRY\n"
            "float fRZ\n"
            "Begin Function {fX, fY, fZ, fRX, fRY, fRZ}\n"
            "  SetPosEx fX fY fZ\n"
            "  SetAngleEx fRX fRY fRZ\n"
            "End\n");
        g_followers = Compile("followers",
            "array_var aTeam\n"
            "array_var aOut\n"
            "array_var aEntry\n"
            "ref rMate\n"
            "Begin Function {}\n"
            "  let aOut := ar_Construct \"array\"\n"
            "  let aTeam := GetTeammates\n"
            "  if eval (ar_Size aTeam) > 0\n"
            "    foreach aEntry <- aTeam\n"
            "      let rMate := aEntry[\"value\"]\n"
            "      if eval (rMate.HasVariable \"Waiting\")\n"
            "        if eval (rMate.GetVariable \"Waiting\") != 0\n"
            "          continue\n"
            "        endif\n"
            "      endif\n"
            "      ar_Append aOut rMate\n"
            "    loop\n"
            "  endif\n"
            "  SetFunctionValue aOut\n"
            "End\n");
        g_crosshair = Compile("crosshair ref",
            "Begin Function {}\n"
            "  SetFunctionValue GetCrosshairRef\n"
            "End\n");
        g_isTeammate = Compile("is teammate",
            "Begin Function {}\n"
            "  SetFunctionValue GetPlayerTeammate\n"
            "End\n");
        g_teammates = Compile("teammates",
            "array_var aTeam\n"
            "Begin Function {}\n"
            "  let aTeam := GetTeammates\n"
            "  if eval (ar_Size aTeam) < 1\n"
            "    let aTeam := ar_Construct \"array\"\n"
            "  endif\n"
            "  SetFunctionValue aTeam\n"
            "End\n");
        g_isWaiting = Compile("is waiting",
            "int iOn\n"
            "Begin Function {}\n"
            "  if eval (HasVariable \"Waiting\")\n"
            "    if eval (GetVariable \"Waiting\") != 0\n"
            "      set iOn to 1\n"
            "    endif\n"
            "  endif\n"
            "  SetFunctionValue iOn\n"
            "End\n");
        g_setWaiting = Compile("set waiting",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  if eval (HasVariable \"Waiting\")\n"
            "    SetVariable \"Waiting\" iOn\n"
            "  endif\n"
            "  EVP\n"
            "End\n");
        g_followPackage = Compile("follow package",
            "ref rPack\n"
            "ref rTarget\n"
            "ref rCopy\n"
            "Begin Function {rPack, rTarget}\n"
            "  let rCopy := TempCloneForm rPack\n"
            "  if rCopy\n"
            "    SetPackageTargetReference rCopy rTarget\n"
            "  endif\n"
            "  SetFunctionValue rCopy\n"
            "End\n");
        g_givePackage = Compile("give package",
            "ref rPack\n"
            "Begin Function {rPack}\n"
            "  AddScriptPackage rPack\n"
            "  EVP\n"
            "End\n");
        g_dropPackage = Compile("drop package",
            "Begin Function {}\n"
            "  RemoveScriptPackage\n"
            "  EVP\n"
            "End\n");
        g_currentPackage = Compile("current package",
            "ref rPack\n"
            "Begin Function {}\n"
            "  let rPack := GetCurrentPackage\n"
            "  SetFunctionValue rPack\n"
            "End\n");
        g_inCombat = Compile("in combat",
            "Begin Function {}\n"
            "  SetFunctionValue IsInCombat\n"
            "End\n");
        g_wheelOpen = Compile("companion wheel open",
            "int iOpen\n"
            "Begin Function {}\n"
            "  set iOpen to MenuMode 1075\n"
            "  SetFunctionValue iOpen\n"
            "End\n");
        g_pcAddItem = Compile("pc add item",
            "ref rItem\n"
            "int iCount\n"
            "int iHide\n"
            "Begin Function {rItem, iCount, iHide}\n"
            "  AddItem rItem iCount iHide\n"
            "End\n");
        g_pcRemoveItem = Compile("pc remove item",
            "ref rItem\n"
            "int iCount\n"
            "int iHide\n"
            "Begin Function {rItem, iCount, iHide}\n"
            "  RemoveItem rItem iCount iHide\n"
            "End\n");
        g_pcRemoveAll = Compile("pc remove all",
            "ref rTo\n"
            "Begin Function {rTo}\n"
            "  RemoveAllItems rTo\n"
            "End\n");
        g_pcRemoveAllTyped = Compile("pc remove all typed",
            "ref rTo\n"
            "int iA\n"
            "int iB\n"
            "int iType\n"
            "Begin Function {rTo, iA, iB, iType}\n"
            "  RemoveAllTypedItems rTo iA iB iType\n"
            "End\n");
        g_pcGiveAll = Compile("pc give all",
            "Begin Function {}\n"
            "  RemoveAllItems player\n"
            "End\n");
        g_pcEquip = Compile("pc equip",
            "ref rItem\n"
            "int iA\n"
            "int iB\n"
            "Begin Function {rItem, iA, iB}\n"
            "  EquipItem rItem iA iB\n"
            "End\n");
        g_pcUnequip = Compile("pc unequip",
            "ref rItem\n"
            "int iA\n"
            "int iB\n"
            "Begin Function {rItem, iA, iB}\n"
            "  UnequipItem rItem iA iB\n"
            "End\n");
        g_pcAddPerk = Compile("pc add perk",
            "ref rPerk\n"
            "Begin Function {rPerk}\n"
            "  AddPerk rPerk\n"
            "End\n");
        g_pcRemovePerk = Compile("pc remove perk",
            "ref rPerk\n"
            "Begin Function {rPerk}\n"
            "  RemovePerk rPerk\n"
            "End\n");
        g_perkRanks = Compile("perk ranks",
            "ref rPerk\n"
            "int iRanks\n"
            "Begin Function {rPerk}\n"
            "  let iRanks := (HasPerkRank rPerk 0) + 256 * (HasPerkRank rPerk 1)\n"
            "  SetFunctionValue iRanks\n"
            "End\n");
        g_pcAddNote = Compile("pc add note",
            "ref rNote\n"
            "Begin Function {rNote}\n"
            "  AddNote rNote\n"
            "End\n");
        g_pcRemoveNote = Compile("pc remove note",
            "ref rNote\n"
            "Begin Function {rNote}\n"
            "  RemoveNote rNote\n"
            "End\n");
        g_pcAddImod = Compile("pc add imod",
            "ref rMod\n"
            "Begin Function {rMod}\n"
            "  ApplyImageSpaceModifier rMod\n"
            "End\n");
        g_pcRemoveImod = Compile("pc remove imod",
            "ref rMod\n"
            "Begin Function {rMod}\n"
            "  RemoveImageSpaceModifier rMod\n"
            "End\n");
        g_pcDisable = Compile("pc disable controls",
            "int i1\n"
            "int i2\n"
            "int i3\n"
            "int i4\n"
            "int i5\n"
            "int i6\n"
            "int i7\n"
            "Begin Function {i1, i2, i3, i4, i5, i6, i7}\n"
            "  DisablePlayerControls i1 i2 i3 i4 i5 i6 i7\n"
            "End\n");
        g_pcEnable = Compile("pc enable controls",
            "int i1\n"
            "int i2\n"
            "int i3\n"
            "int i4\n"
            "int i5\n"
            "int i6\n"
            "int i7\n"
            "Begin Function {i1, i2, i3, i4, i5, i6, i7}\n"
            "  EnablePlayerControls i1 i2 i3 i4 i5 i6 i7\n"
            "End\n");
        g_pcCast = Compile("pc cast on self",
            "ref rSpell\n"
            "Begin Function {rSpell}\n"
            "  CastImmediateOnSelf rSpell\n"
            "End\n");
        g_pcAddPackage = Compile("pc add package",
            "ref rPack\n"
            "Begin Function {rPack}\n"
            "  AddScriptPackage rPack\n"
            "End\n");
        g_pcRemovePackage = Compile("pc remove package",
            "Begin Function {}\n"
            "  RemoveScriptPackage\n"
            "End\n");
        g_isMobile = Compile("is mobile",
            "Begin Function {}\n"
            "  SetFunctionValue IsMobile\n"
            "End\n");
        g_grabbed = Compile("grabbed item",
            "ref rItem\n"
            "Begin Function {}\n"
            "  set rItem to GetPlayerGrabbedRef\n"
            "  SetFunctionValue rItem\n"
            "End\n");
        g_disable = Compile("disable",
            "Begin Function {}\n"
            "  Disable\n"
            "End\n");

        g_objectiveShown = Compile("objective shown",
            "ref rQuest\n"
            "int iObjective\n"
            "int iOn\n"
            "Begin Function {rQuest, iObjective, iOn}\n"
            "  SetObjectiveDisplayed rQuest iObjective iOn\n"
            "End\n");
        g_objectiveDone = Compile("objective done",
            "ref rQuest\n"
            "int iObjective\n"
            "int iOn\n"
            "Begin Function {rQuest, iObjective, iOn}\n"
            "  SetObjectiveCompleted rQuest iObjective iOn\n"
            "End\n");
        g_completeQuest = Compile("complete quest",
            "ref rQuest\n"
            "Begin Function {rQuest}\n"
            "  CompleteQuest rQuest\n"
            "End\n");
        g_enable = Compile("enable",
            "Begin Function {}\n"
            "  Enable\n"
            "End\n");
        g_inDialogue = Compile("in dialogue",
            "int iIn\n"
            "Begin Function {}\n"
            "  set iIn to MenuMode 1009\n"
            "  SetFunctionValue iIn\n"
            "End\n");
        g_closeMenu = Compile("close menu",
            "Begin Function {}\n"
            "  CloseActiveMenu\n"
            "End\n");
        g_greet = Compile("greet",
            "Begin Function {}\n"
            "  StartConversation player\n"
            "End\n");
        g_xp = Compile("xp",
            "float fV\n"
            "Begin Function {}\n"
            "  set fV to GetActorValue XP\n"
            "  SetFunctionValue fV\n"
            "End\n");
        g_karma = Compile("karma",
            "float fV\n"
            "Begin Function {}\n"
            "  set fV to GetActorValue Karma\n"
            "  SetFunctionValue fV\n"
            "End\n");
        g_giveXp = Compile("give xp",
            "int iXp\n"
            "Begin Function {iXp}\n"
            "  RewardXPExact iXp\n"
            "End\n");
        g_giveKarma = Compile("give karma",
            "int iKarma\n"
            "Begin Function {iKarma}\n"
            "  RewardKarma iKarma\n"
            "End\n");
        g_loadOrder = Compile("load order",
            "array_var aMods\n"
            "int i\n"
            "int n\n"
            "Begin Function {}\n"
            "  let aMods := ar_Construct \"array\"\n"
            "  set n to GetNumLoadedMods\n"
            "  while i < n\n"
            "    ar_Append aMods (GetNthModName i)\n"
            "    let i += 1\n"
            "  loop\n"
            "  SetFunctionValue aMods\n"
            "End\n");

        g_compiled = g_spawn && g_bringToPlayer && g_move && g_travel && g_moveBy && g_notifyFor && g_remove && g_notify && g_playerFlags && g_clone &&
            g_turn && g_equipped && g_removeAllItems && g_addAndEquip && g_removeItem && g_health &&
            g_hit && g_maxHealth && g_changeHealth && g_makeCompanion && g_stopFighting && g_kill && g_unconscious && g_controls &&
            g_controlPressed && g_menuMode;
        if (g_compiled)
            LOG_INFO("Game scripts ready");
        return g_compiled;
    }

    TESObjectREFR* Player()
    {
        return *reinterpret_cast<TESObjectREFR**>(addr::kThePlayer);
    }

    TESForm* LookupForm(uint32_t formId)
    {
        auto* forms = *reinterpret_cast<NiTPointerMap<TESForm>**>(addr::kFormMap);
        return forms ? forms->Lookup(formId) : nullptr;
    }

    TESObjectREFR* LookupRef(uint32_t refId)
    {
        TESForm* form = LookupForm(refId);
        return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
    }

    uint32_t RefId(const TESObjectREFR* ref)
    {
        return ref ? ref->refID : 0;
    }

    bool GetPose(const TESObjectREFR* ref, coop::Pose& out)
    {
        if (!ref || !ref->parentCell)
            return false;
        const TESObjectCELL* cell = ref->parentCell;
        out.space = cell->worldSpace ? cell->worldSpace->refID : cell->refID;
        out.x = ref->posX;
        out.y = ref->posY;
        out.z = ref->posZ;
        out.heading = ref->rotZ;
        out.pitch = ref->rotX;
        return true;
    }

    namespace
    {
        int StanceBits(TESObjectREFR* ref)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_compiled || !ref || !g_scripts->CallFunction(g_playerFlags, ref, nullptr, &result, 0))
                return 0;
            return static_cast<int>(result.GetNumber());
        }
    }

    uint8_t StanceFlags(TESObjectREFR* ref)
    {
        return static_cast<uint8_t>(StanceBits(ref)) & coop::Pose::kKnownFlags;
    }

    void Stance(TESObjectREFR* ref, uint8_t& flags, int& sitting)
    {
        int bits = StanceBits(ref);
        flags = static_cast<uint8_t>(bits) & coop::Pose::kKnownFlags;
        sitting = (bits >> 2) & 7;
    }

    std::vector<uint32_t> PlayerGear()
    {
        return Gear(Player(), false);
    }

    std::vector<uint32_t> Gear(TESObjectREFR* ref, bool armourOnly)
    {
        std::vector<uint32_t> items;
        if (!g_compiled || !IsActor(ref))
            return items;
        for (int slot = 0; slot < kEquipSlots; ++slot)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_scripts->CallFunction(g_equipped, ref, nullptr, &result, 1, static_cast<uint32_t>(slot)))
                continue;
            TESForm* item = result.GetTESForm();
            if (item && (!armourOnly || item->IsArmor()))
                items.push_back(item->refID);
        }
        return items;
    }

    std::vector<net::WornItem> WornGear(TESObjectREFR* ref)
    {
        std::vector<net::WornItem> items;
        NVSEArrayVarInterface::Element result;
        if (!g_wornGear || !g_arrays || !IsActor(ref) || !g_scripts->CallFunction(g_wornGear, ref, nullptr, &result, 0))
            return items;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (!size || size == static_cast<uint32_t>(-1) || size % 2 || size > 2 * kEquipSlots)
            return items;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (!g_arrays->GetElements(array, elements.data(), nullptr))
            return items;
        for (uint32_t i = 0; i < size; i += 2)
        {
            TESForm* item = elements[i].GetTESForm();
            double health = elements[i + 1].GetNumber();
            if (!item || !item->IsArmor() ||
                std::any_of(items.begin(), items.end(), [&](const net::WornItem& o) { return o.item == item->refID; }))
                continue;
            items.push_back({item->refID, health >= 0 && health <= 100 ? static_cast<uint8_t>(health) :
                net::kNoCondition});
        }
        std::sort(items.begin(), items.end(),
            [](const net::WornItem& a, const net::WornItem& b) { return a.item < b.item; });
        return items;
    }

    bool Wear(TESObjectREFR* ref, const net::WornItem& item)
    {
        TESForm* form = LookupForm(item.item);
        if (!g_wearAt || !IsActor(ref) || !form || !form->IsArmor())
            return false;
        uint32_t health = item.health <= 100 ? item.health : 100;
        g_scripts->CallFunctionAlt(g_wearAt, ref, 2, form, health);
        return true;
    }

    bool InDocMitchellsHouse()
    {
        TESObjectREFR* player = Player();
        return player && player->parentCell && player->parentCell->refID == ids::kDocMitchellHouse;
    }

    bool IsPastIntro()
    {
        TESObjectREFR* player = Player();
        if (!player || !player->parentCell || QuestStage(ids::kBennyIntroQuest) >= 0)
            return false;
        TESForm* quest = LookupForm(ids::kIntroQuest);
        bool tutorialDone = quest && quest->typeID == kFormType_TESQuest &&
            (static_cast<TESQuest*>(quest)->flags & layout::kQuestCompleted) != 0;
        return tutorialDone || !InDocMitchellsHouse();
    }

    namespace
    {
        TESObjectREFR* Place(TESForm* base)
        {
            if (!g_compiled || !Player())
                return nullptr;
            NVSEArrayVarInterface::Element result;
            if (!base || !g_scripts->CallFunction(g_spawn, Player(), nullptr, &result, 1, base))
                return nullptr;
            TESForm* form = result.GetTESForm();
            return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
        }
    }

    TESObjectREFR* PlaceActor(uint32_t baseId)
    {
        TESForm* base = LookupForm(baseId);
        if (!base || (base->typeID != kFormType_TESNPC && base->typeID != kFormType_TESCreature &&
            base->typeID != kFormType_TESLevCreature && base->typeID != kFormType_TESLevCharacter))
            return nullptr;
        return Place(base);
    }

    namespace
    {
        void SetName(String& s, const char* name)
        {
            size_t len = 0;
            while (name[len] && len < 63)
                ++len;
            auto* text = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(
                static_cast<UInt32>(len + 1)));
            if (!text)
                return;
            for (size_t i = 0; i < len; ++i)
                text[i] = static_cast<unsigned char>(name[i]) < 0x20 ? '?' : name[i];
            text[len] = 0;
            s.m_data = text;
            s.m_dataLen = s.m_bufLen = static_cast<UInt16>(len);
        }
    }

    namespace
    {
        bool LooksOntoBase(TESForm* base, const net::PlayerLooks& looks, int& shared);
    }

    TESObjectREFR* SpawnStandIn(const char* name, const net::PlayerLooks* looks)
    {
        TESForm* original = LookupForm(ids::kStandInBase);
        if (!g_compiled || !Player() || !name || !original || original->typeID != kFormType_TESNPC)
            return nullptr;
        NVSEArrayVarInterface::Element result;
        if (!g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, original))
            return nullptr;
        TESForm* copy = result.GetTESForm();
        if (!copy || copy->typeID != kFormType_TESNPC)
            return nullptr;
        auto* base = static_cast<TESActorBase*>(copy);
        SetName(base->fullName.name, name);
        base->container.formCountList.m_listHead.data = nullptr;
        base->container.formCountList.m_listHead.next = nullptr;
        if (g_noTalk)
            base->scriptable.script = g_noTalk;
        else
            LOG_WARN("Friends' characters can be talked to (the no-talking script didn't compile)");
        if (g_standInBases.size() >= kMaxStandInBases)
            g_standInBases.erase(g_standInBases.begin());
        g_standInBases.push_back(copy->refID);
        LOG_DEBUG("Character type %08X for %s", copy->refID, name);
        int shared = 0;
        if (looks && LooksOntoBase(copy, *looks, shared))
            LOG_INFO("Looks: %s's character made looking like them (%s)", name, looks->female ? "female" : "male");
        return Place(copy);
    }

    bool Is3DLoaded(TESObjectREFR* ref)
    {
        return ref && ref->GetNiNode();
    }

    void BringToPlayer(TESObjectREFR* ref)
    {
        MoveOnto(ref, Player());
    }

    void MoveOnto(TESObjectREFR* ref, TESObjectREFR* to)
    {
        if (g_compiled && ref && to)
            g_scripts->CallFunctionAlt(g_bringToPlayer, ref, 1, to);
    }

    void Move(TESObjectREFR* ref, const coop::Pose& pose)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_move, ref, 4, FloatArg(pose.x), FloatArg(pose.y), FloatArg(pose.z),
                FloatArg(pose.heading * kRadToDeg));
    }

    bool TravelTo(const coop::Pose& pose)
    {
        TESForm* place = LookupForm(pose.space);
        if (!g_compiled || !Player() || !place ||
            (place->typeID != kFormType_TESObjectCELL && place->typeID != kFormType_TESWorldSpace))
            return false;
        TESObjectREFR* player = Player();
        if (player->parentCell && player->parentCell->worldSpace && player->parentCell->worldSpace == place)
            g_scripts->CallFunctionAlt(g_moveBy, player, 4, FloatArg(pose.x - player->posX),
                FloatArg(pose.y - player->posY), FloatArg(pose.z - player->posZ), FloatArg(pose.heading * kRadToDeg));
        else
            g_scripts->CallFunctionAlt(g_travel, player, 5, place, FloatArg(pose.x), FloatArg(pose.y),
                FloatArg(pose.z), FloatArg(pose.heading * kRadToDeg));
        return true;
    }

    void Turn(TESObjectREFR* ref, float heading)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_turn, ref, 1, FloatArg(heading * kRadToDeg));
    }

    namespace
    {
        constexpr uintptr_t kAimSlots = 0x1C4;
        constexpr uintptr_t kAnimSequences = 0xE0;

        void** AimSlots(TESObjectREFR* ref)
        {
            BaseProcess* process = IsActor(ref) ? static_cast<Actor*>(ref)->baseProcess : nullptr;
            if (!process || process->processLevel > 1)
                return nullptr;
            return reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(process) + kAimSlots);
        }

        const char* SequenceName(const void* sequence)
        {
            const char* path = sequence ? *reinterpret_cast<const char* const*>(
                reinterpret_cast<const uint8_t*>(sequence) + 0x08) : nullptr;
            return path ? path : "-";
        }
    }

    void SetPitch(TESObjectREFR* ref, float pitch)
    {
        if (!IsActor(ref) || !std::isfinite(pitch))
            return;
        ref->rotX = pitch;
        void** slots = AimSlots(ref);
        AnimData* anim = slots ? ref->GetAnimData() : nullptr;
        if (!anim)
            return;
        void** playing = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(anim) + kAnimSequences);
        if (playing[4] && playing[5] && playing[6])
            for (int i = 0; i < 3; ++i)
                slots[i] = playing[4 + i];
    }

    void LogAimSlots(TESObjectREFR* ref)
    {
        void** slots = AimSlots(ref);
        AnimData* anim = slots ? ref->GetAnimData() : nullptr;
        if (!anim)
        {
            LOG_INFO("Aim slots: no high process or animation data");
            return;
        }
        void** playing = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(anim) + kAnimSequences);
        auto group = [anim](int layer) {
            return *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(anim) + process::kAnimGroupIds +
                2 * layer);
        };
        LOG_INFO("Aim slots: tilted %s | %s | %s; playing %s | %s | %s; pitch %.2f; groups %04X (idle layer), %04X %04X "
            "%04X (weapon layers); weapon %s", SequenceName(slots[0]), SequenceName(slots[1]), SequenceName(slots[2]),
            SequenceName(playing[4]), SequenceName(playing[5]), SequenceName(playing[6]), ref->rotX, group(0), group(4),
            group(5), group(6), (StanceFlags(ref) & coop::Pose::kWeaponOut) ? "out" : "away");
    }

    namespace
    {
        bool g_dragging = false;
        bool g_dragAlwaysRun = false;
        std::vector<int> g_dragControlsOff;
    }

    bool* PlayerAlwaysRun()
    {
        TESObjectREFR* player = Player();
        return player ? reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(player) + layout::kPlayerAlwaysRun) : nullptr;
    }

    void SetDragging(bool on)
    {
        TESObjectREFR* player = Player();
        bool* alwaysRun = PlayerAlwaysRun();
        if (on == g_dragging || !player || !alwaysRun || !g_controlDisabled || !g_disableControl)
            return;
        g_dragging = on;
        if (on)
        {
            SetWeaponOut(player, false);
            for (int control : {layout::kControlAttack, layout::kControlAimBlock, layout::kControlReadyItem,
                     layout::kControlRun, layout::kControlAlwaysRun, layout::kControlJump, layout::kControlVats})
            {
                NVSEArrayVarInterface::Element result;
                if (g_scripts->CallFunction(g_controlDisabled, player, nullptr, &result, 1, static_cast<uint32_t>(control)) &&
                    result.GetNumber() == 0)
                {
                    g_scripts->CallFunction(g_disableControl, player, nullptr, nullptr, 2, static_cast<uint32_t>(control), 1u);
                    g_dragControlsOff.push_back(control);
                }
            }
            g_dragAlwaysRun = *alwaysRun;
            *alwaysRun = false;
        }
        else
        {
            for (int control : g_dragControlsOff)
                g_scripts->CallFunction(g_disableControl, player, nullptr, nullptr, 2, static_cast<uint32_t>(control), 0u);
            g_dragControlsOff.clear();
            *alwaysRun = g_dragAlwaysRun;
        }
        LOG_INFO("Dragging: %s (always-run %s)", on ? "walking only, no weapon" : "back to normal",
            on ? (g_dragAlwaysRun ? "was on: off till we let go" : "was off") : (*alwaysRun ? "on again" : "off"));
    }

    void SetWeaponOut(TESObjectREFR* ref, bool out)
    {
        if (IsActor(ref))
            reinterpret_cast<void(__thiscall*)(TESObjectREFR*, bool)>(addr::kActorSetWeaponOut)(ref, out);
    }

    bool Jump(TESObjectREFR* ref)
    {
        if (!IsActor(ref) || !static_cast<Actor*>(ref)->baseProcess)
            return false;
        return reinterpret_cast<int(__thiscall*)(TESObjectREFR*)>(addr::kMobileObjectJump)(ref) != 0;
    }

    void SetAlert(TESObjectREFR* ref, bool alert)
    {
        if (g_alert && IsActor(ref))
            g_scripts->CallFunctionAlt(g_alert, ref, 1, static_cast<uint32_t>(alert ? 1 : 0));
    }

    void SetRestrained(TESObjectREFR* ref, bool on)
    {
        if (g_restrain && IsActor(ref))
            g_scripts->CallFunctionAlt(g_restrain, ref, 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    void SetCombatOff(TESObjectREFR* ref, bool off)
    {
        if (g_combatOff && IsActor(ref))
            g_scripts->CallFunctionAlt(g_combatOff, ref, 1, static_cast<uint32_t>(off ? 1 : 0));
    }

    void EvaluatePackage(TESObjectREFR* ref)
    {
        if (g_evaluate && IsActor(ref))
            g_scripts->CallFunctionAlt(g_evaluate, ref, 0);
    }

    uint16_t MovementFlags(TESObjectREFR* ref)
    {
        return static_cast<uint16_t>(ReadMover(ref).flags);
    }

    void SetMovementFlags(TESObjectREFR* ref, uint16_t flags)
    {
        AskMovementFlags(ref, flags);
    }

    void RemoveAllItems(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_removeAllItems, ref, 0);
    }

    namespace
    {
        TESForm* TempClone(TESForm* form)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_clone || !form || !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, form))
                return nullptr;
            return result.GetTESForm();
        }

        BGSExplosion* HarmlessExplosion(BGSExplosion* explosion, bool quiet = false)
        {
            if (!explosion)
                return nullptr;
            auto& made = quiet ? g_quietExplosions : g_harmlessExplosions;
            if (auto it = made.find(explosion->refID); it != made.end())
                return it->second;
            TESForm* copy = TempClone(explosion);
            if (!copy || copy->typeID != kFormType_BGSExplosion)
                return nullptr;
            auto* e = static_cast<BGSExplosion*>(copy);
            e->damage = 0;
            e->force = 0;
            e->RADlevel = 0;
            e->RADradius = 0;
            e->dissipationTime = 0;
            e->placedObj = nullptr;
            e->enchantable.enchantItem = nullptr;
            e->SetFlag(BGSExplosion::kFlags_KnockDownAlways, false);
            e->SetFlag(BGSExplosion::kFlags_KnockDownByFormula, false);
            if (quiet)
            {
                e->model.SetModelPath(const_cast<char*>(""));
                e->light = nullptr;
                e->sound1 = nullptr;
                e->sound2 = nullptr;
                e->impactDataSet = nullptr;
                e->imageSpaceModForm.unk04 = 0;
                e->ISradius = 0;
            }
            made[explosion->refID] = e;
            return e;
        }

        BGSProjectile* HarmlessProjectile(BGSProjectile* projectile)
        {
            if (!projectile)
                return nullptr;
            if (auto it = g_harmlessProjectiles.find(projectile->refID); it != g_harmlessProjectiles.end())
                return it->second;
            BGSExplosion* explosion = nullptr;
            if (projectile->explosionForm && !(explosion = HarmlessExplosion(projectile->explosionForm, true)))
                return nullptr;
            TESForm* copy = TempClone(projectile);
            if (!copy || copy->typeID != kFormType_BGSProjectile)
                return nullptr;
            auto* p = static_cast<BGSProjectile*>(copy);
            p->explosionForm = explosion;
            p->impactForce = 0;
            p->SetFlag(BGSProjectile::kFlags_CanBePicked, false);
            p->SetFlag(BGSProjectile::kFlags_CanBeDisabled, false);
            g_harmlessProjectiles[projectile->refID] = p;
            LOG_DEBUG("Harmless copy %08X of projectile %08X (explosion %08X)", p->refID, projectile->refID,
                explosion ? explosion->refID : 0);
            return p;
        }

        bool IsHarmlessProjectile(const BGSProjectile* projectile)
        {
            for (auto& [original, copy] : g_harmlessProjectiles)
                if (copy == projectile)
                    return true;
            return false;
        }

        TESForm* HarmlessCopy(TESForm* weapon)
        {
            auto it = g_harmless.find(weapon->refID);
            if (it != g_harmless.end())
                return it->second;
            for (auto& [original, copy] : g_harmless)
                if (copy == weapon)
                    return copy;
            NVSEArrayVarInterface::Element result;
            if (g_harmless.size() >= kMaxHarmless ||
                !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, weapon))
                return nullptr;
            TESForm* copy = result.GetTESForm();
            if (!copy || !copy->IsWeapon())
                return nullptr;
            auto* w = static_cast<TESObjectWEAP*>(copy);
            w->attackDmg.damage = 0;
            w->criticalDamage = 0;
            w->criticalPercent = 0;
            w->criticalEffect = nullptr;
            w->enchantable.enchantItem = nullptr;
            w->limbDamageMult = 0;
            if (w->projectile)
            {
                BGSProjectile* harmless = HarmlessProjectile(w->projectile);
                if (harmless)
                    w->projectile = harmless;
                else
                    LOG_WARN("No harmless copy of %08X's projectile %08X: its shots won't be shown", weapon->refID,
                        w->projectile->refID);
            }
            g_harmless[weapon->refID] = copy;
            LOG_DEBUG("Harmless copy %08X of weapon %08X", copy->refID, weapon->refID);
            return copy;
        }

        bool IsGun(const TESObjectWEAP* w)
        {
            return w->eWeaponType >= TESObjectWEAP::kWeapType_OneHandPistol &&
                w->eWeaponType <= TESObjectWEAP::kWeapType_TwoHandLauncher;
        }

        bool IsThrown(const TESObjectWEAP* w)
        {
            return w->eWeaponType >= TESObjectWEAP::kWeapType_OneHandGrenade &&
                w->eWeaponType <= TESObjectWEAP::kWeapType_OneHandThrown;
        }

        bool IsMine(const TESObjectWEAP* w)
        {
            return w->eWeaponType == TESObjectWEAP::kWeapType_OneHandMine ||
                w->eWeaponType == TESObjectWEAP::kWeapType_OneHandLunchboxMine;
        }

        bool CanFire(const TESObjectWEAP* weapon)
        {
            return weapon && g_fire && weapon->projectile && IsHarmlessProjectile(weapon->projectile);
        }

        int CurrentAction(TESObjectREFR* ref)
        {
            BaseProcess* process = IsActor(ref) ? static_cast<Actor*>(ref)->baseProcess : nullptr;
            if (!process || process->processLevel != 0)
                return -1;
            return static_cast<HighProcess*>(process)->currentAction;
        }

        TESObjectWEAP* HeldWeapon(TESObjectREFR* ref)
        {
            NVSEArrayVarInterface::Element result;
            TESForm* held = g_scripts->CallFunction(g_equipped, ref, nullptr, &result, 1, static_cast<uint32_t>(5)) ?
                result.GetTESForm() : nullptr;
            return held && held->IsWeapon() ? static_cast<TESObjectWEAP*>(held) : nullptr;
        }

        bool WeaponLayerPlays(TESObjectREFR* ref)
        {
            AnimData* anim = IsActor(ref) ? ref->GetAnimData() : nullptr;
            if (!anim)
                return false;
            auto* playing = reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(anim) +
                process::kAnimSequences);
            return playing[process::kWeaponLayer] != nullptr;
        }

        void PlayVersions(TESObjectREFR* ref, const std::array<Script*, coop::kAttackVersions>& anims)
        {
            int left = coop::PlayAttackVersions(
                [&](int version) {
                    if (anims[version])
                        g_scripts->CallFunctionAlt(anims[version], ref, 0);
                },
                [&] { return WeaponLayerPlays(ref); });
            if (left)
                LOG_DEBUG("%08X's attack: nothing in its weapon layer, so %d of its up and down versions left out",
                    ref->refID, left);
        }

        void PlayAttackWith(TESObjectREFR* ref, TESObjectWEAP* weapon, uint8_t shots)
        {
            if (weapon && IsThrown(weapon))
            {
                const auto& anims = IsMine(weapon) ? g_mineAnim : g_throwAnim;
                PlayVersions(ref, anims[0] ? anims : g_attackAnim);
                return;
            }
            PlayVersions(ref, g_attackAnim);
            if (!weapon || !IsGun(weapon) || !CanFire(weapon))
                return;
            for (uint8_t i = 0; i < shots; ++i)
                g_scripts->CallFunctionAlt(g_fire, ref, 1, weapon);
        }

        void ReleaseWith(TESObjectREFR* ref, TESObjectWEAP* weapon)
        {
            if (weapon && IsThrown(weapon) && CanFire(weapon))
                g_scripts->CallFunctionAlt(g_fire, ref, 1, weapon);
        }
    }

    bool AddAndEquip(TESObjectREFR* ref, uint32_t formId)
    {
        TESForm* item = LookupForm(formId);
        if (!g_compiled || !ref || !item || !(item->IsArmor() || item->IsWeapon()))
            return false;
        if (item->IsWeapon() && !(item = HarmlessCopy(item)))
            return false;
        g_scripts->CallFunctionAlt(g_addAndEquip, ref, 1, item);
        if (item->IsWeapon() && IsGun(static_cast<TESObjectWEAP*>(item)) && g_addItems)
        {
            TESForm* ammo = static_cast<TESObjectWEAP*>(item)->ammo.ammo;
            if (ammo && ammo->typeID == kFormType_BGSListForm)
                ammo = static_cast<BGSListForm*>(ammo)->GetNthForm(0);
            if (ammo && ammo->typeID == kFormType_TESAmmo)
                g_scripts->CallFunctionAlt(g_addItems, ref, 2, ammo, static_cast<uint32_t>(kPuppetAmmo));
        }
        return true;
    }

    void RemoveItem(TESObjectREFR* ref, uint32_t formId)
    {
        TESForm* item = LookupForm(formId);
        if (item && item->IsWeapon())
        {
            auto it = g_harmless.find(formId);
            item = it == g_harmless.end() ? nullptr : it->second;
        }
        if (g_compiled && ref && item)
            g_scripts->CallFunctionAlt(g_removeItem, ref, 1, item);
    }

    MyAttack TakeMyAttacks()
    {
        TESObjectREFR* player = Player();
        if (!g_compiled || !g_rounds || !player)
            return {};
        NVSEArrayVarInterface::Element result;
        TESForm* held = g_scripts->CallFunction(g_equipped, player, nullptr, &result, 1, static_cast<uint32_t>(5)) ?
            result.GetTESForm() : nullptr;
        auto* weapon = held && held->IsWeapon() ? static_cast<TESObjectWEAP*>(held) : nullptr;
        uint32_t weaponId = weapon ? weapon->refID : 0;
        int shots = 0;
        bool gun = weapon && IsGun(weapon);
        int action = CurrentAction(player);
        bool attackStarted = action == HighProcess::kAnimAction_Attack && g_myAction != HighProcess::kAnimAction_Attack;
        if (gun)
        {
            int rounds = static_cast<int>(Number(g_rounds, player));
            uint32_t gone = 0;
            if (weaponId == g_myWeapon && g_myRounds > rounds)
            {
                int perShot = weapon->ammoUse > 1 ? weapon->ammoUse : 1;
                gone = static_cast<uint32_t>((g_myRounds - rounds + perShot - 1) / perShot);
            }
            g_myRounds = rounds;
            bool reloadStarted = IsReloadAction(action) && !IsReloadAction(g_myAction);
            shots = static_cast<int>(g_myShots.Look(attackStarted, IsAttackPhase(action), gone, reloadStarted));
        }
        else
        {
            g_myRounds = -1;
            bool thrown = weapon && IsThrown(weapon);
            if (thrown ? IsAttackPhase(action) && !IsAttackPhase(g_myAction) : attackStarted)
                shots = 1;
        }
        bool release = weapon && IsThrown(weapon) && IsThrowRelease(action) && !IsThrowRelease(g_myAction);
        g_myWeapon = weaponId;
        g_myAction = action;
        if (shots > 0)
            LOG_DEBUG("We attacked: %d %s with %08X", shots, gun ? "shots" : "swing", weaponId);
        if (release)
            LOG_DEBUG("Our throw left our hand (%08X)", weaponId);
        return {static_cast<uint8_t>(shots < 255 ? shots : 255), release};
    }

    void PlayAttack(TESObjectREFR* ref, const std::vector<uint32_t>& worn, uint8_t shots)
    {
        if (!g_compiled || !g_attackAnim[0] || !ref)
            return;
        TESObjectWEAP* weapon = nullptr;
        for (uint32_t id : worn)
            if (auto it = g_harmless.find(id); it != g_harmless.end())
                weapon = static_cast<TESObjectWEAP*>(it->second);
        PlayAttackWith(ref, weapon, shots);
    }

    void PlayNpcAttack(TESObjectREFR* ref, uint8_t shots)
    {
        if (!g_compiled || !g_attackAnim[0] || !ref)
            return;
        TESObjectWEAP* held = HeldWeapon(ref);
        PlayAttackWith(ref, held ? static_cast<TESObjectWEAP*>(HarmlessCopy(held)) : nullptr, shots);
    }

    void ReleaseThrow(TESObjectREFR* ref, const std::vector<uint32_t>& worn)
    {
        if (!g_compiled || !ref)
            return;
        TESObjectWEAP* weapon = nullptr;
        for (uint32_t id : worn)
            if (auto it = g_harmless.find(id); it != g_harmless.end())
                weapon = static_cast<TESObjectWEAP*>(it->second);
        ReleaseWith(ref, weapon);
    }

    void ReleaseNpcThrow(TESObjectREFR* ref)
    {
        if (!g_compiled || !ref)
            return;
        TESObjectWEAP* held = HeldWeapon(ref);
        ReleaseWith(ref, held ? static_cast<TESObjectWEAP*>(HarmlessCopy(held)) : nullptr);
    }

    uint32_t HeldWeaponId(TESObjectREFR* ref)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        if (!held)
            return 0;
        for (auto& [original, copy] : g_harmless)
            if (copy == held)
                return original;
        return held->refID;
    }

    bool HoldsHarmless(TESObjectREFR* ref)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        for (auto& [original, copy] : g_harmless)
            if (held && copy == held)
                return true;
        return false;
    }

    int CountShots(TESObjectREFR* ref, GunState& state)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        uint32_t id = held ? held->refID : 0;
        for (auto& [original, copy] : g_harmless)
            if (held && copy == held)
                id = original;
        bool switched = id != state.weapon;
        state.weapon = id;
        if (!held || !IsGun(held) || !g_npcRounds)
        {
            state.rounds = -1;
            return -1;
        }
        int rounds = static_cast<int>(Number(g_npcRounds, ref));
        int shots = 0;
        if (!switched && state.rounds >= 0 && rounds < state.rounds)
        {
            int perShot = held->ammoUse > 1 ? held->ammoUse : 1;
            shots = (state.rounds - rounds + perShot - 1) / perShot;
        }
        state.rounds = rounds;
        return shots;
    }

    void StopIdle(TESObjectREFR* ref)
    {
        if (g_compiled && g_stopIdle && ref)
            g_scripts->CallFunctionAlt(g_stopIdle, ref, 0);
    }

    namespace
    {
        constexpr int kQuestions = 16;
        struct Question
        {
            bool open = false;
            volatile int button = -1;
            uint32_t askedAt = 0;
            char text[1024];
            std::string buttons[10];
        };
        Question g_questions[kQuestions];

        template <int N>
        void QuestionClosed()
        {
            g_questions[N].button = reinterpret_cast<uint8_t(__cdecl*)()>(addr::kTakeMessageBoxButton)();
        }
        template <int... N>
        constexpr std::array<void (*)(), sizeof...(N)> Closers(std::integer_sequence<int, N...>)
        {
            return {&QuestionClosed<N>...};
        }
        const std::array<void (*)(), kQuestions> kClosers = Closers(std::make_integer_sequence<int, kQuestions>());

        char g_message[1024];
        void MessageClosed() { reinterpret_cast<uint8_t(__cdecl*)()>(addr::kTakeMessageBoxButton)(); }

        using ShowFn = bool(__cdecl*)(const char*, uint32_t, uint32_t, void (*)(), uint32_t, uint32_t, float, float,
            ...);

        bool Show(int id)
        {
            Question& q = g_questions[id];
            const char* b[11] = {};
            for (size_t i = 0; i < 10 && !q.buttons[i].empty(); ++i)
                b[i] = q.buttons[i].c_str();
            return reinterpret_cast<ShowFn>(addr::kShowMessageBox)(q.text, 0, 0, kClosers[id], 0, 0x17, 0.f, 0.f,
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], nullptr);
        }

        bool ShowPlain(const std::string& text)
        {
            std::snprintf(g_message, sizeof(g_message), "%s", text.c_str());
            return reinterpret_cast<ShowFn>(addr::kShowMessageBox)(g_message, 0, 0, &MessageClosed, 0, 0x17, 0.f, 0.f,
                "OK", nullptr);
        }

        struct Waiting
        {
            int id;
            std::string text;
            uint32_t refusedSince = 0;
        };
        std::deque<Waiting> g_waiting;
        constexpr uint32_t kGiveUpMs = 10000;

        bool BoxFree()
        {
            auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
            return manager && *manager && !*reinterpret_cast<void* const*>(addr::kMessageMenu) &&
                !*reinterpret_cast<const uint8_t*>(addr::kMessageBoxBusy);
        }

        bool Ask(int id, const std::string& text, const std::vector<std::string>& buttons)
        {
            if (id < 0 || id >= kQuestions)
                return false;
            Question& q = g_questions[id];
            std::snprintf(q.text, sizeof(q.text), "%s", text.c_str());
            for (size_t i = 0; i < 10; ++i)
                q.buttons[i] = i < buttons.size() ? buttons[i] : std::string();
            q.button = -1;
            q.askedAt = GetTickCount();
            q.open = true;
            bool queued = false;
            for (const Waiting& w : g_waiting)
                queued |= w.id == id;
            if (!queued)
                g_waiting.push_back({id, ""});
            RetryMessageBoxes();
            return true;
        }
    }

    void RetryMessageBoxes()
    {
        if (g_waiting.empty() || !BoxFree())
            return;
        Waiting& w = g_waiting.front();
        bool shown = w.id >= 0 ? Show(w.id) : ShowPlain(w.text);
        if (shown)
        {
            g_waiting.pop_front();
            return;
        }
        uint32_t now = GetTickCount();
        if (!w.refusedSince)
            w.refusedSince = now;
        else if (now - w.refusedSince > kGiveUpMs)
        {
            LOG_WARN("A box (question %d) the game wouldn't show for %u s: given up", w.id, kGiveUpMs / 1000);
            if (w.id >= 0)
                g_questions[w.id].button = kNeverShown;
            g_waiting.pop_front();
        }
    }

    namespace
    {
        uint8_t* HookStart(uintptr_t function, const uint8_t* expect, size_t length, const void* to);
        uint8_t* g_loadSave = nullptr;
        bool (*g_mayLoad)(const std::string&) = nullptr;

        bool __fastcall LoadSaveHook(void* manager, void*, const char* name, uint32_t a, uint32_t b, uint32_t c)
        {
            if (g_mayLoad && name)
            {
                std::string save(name);
                size_t slash = save.find_last_of("\\/");
                if (slash != std::string::npos)
                    save.erase(0, slash + 1);
                if (save.size() > 4 && _stricmp(save.c_str() + save.size() - 4, ".fos") == 0)
                    save.resize(save.size() - 4);
                if (!g_mayLoad(save))
                    return false;
            }
            using Fn = bool(__thiscall*)(void*, const char*, uint32_t, uint32_t, uint32_t);
            return reinterpret_cast<Fn>(g_loadSave)(manager, name, a, b, c);
        }
    }

    bool InstallLoadCheck(bool (*mayLoad)(const std::string& save))
    {
        g_mayLoad = mayLoad;
        if (!g_loadSave)
        {
            static const uint8_t kStart[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x2C, 0x02, 0x00, 0x00};
            g_loadSave = HookStart(addr::kLoadSave, kStart, sizeof(kStart), reinterpret_cast<const void*>(&LoadSaveHook));
        }
        return g_loadSave != nullptr;
    }

    bool AskYesNo(const std::string& text, int id) { return Ask(id, text, {"Yes", "No"}); }

    void ShowMessage(const std::string& text)
    {
        g_waiting.push_back({-1, text});
        RetryMessageBoxes();
    }

    bool AskChoice(const std::string& text, const std::vector<std::string>& buttons, int id)
    {
        return Ask(id, text, buttons);
    }

    bool QuestionOpen()
    {
        for (const Question& q : g_questions)
            if (q.open && GetTickCount() - q.askedAt < 120000)
                return true;
        return false;
    }

    namespace
    {
        void SetTileText(void* tile, const std::string& text)
        {
            using SetString = void(__thiscall*)(void*, uint32_t, const char*, bool);
            reinterpret_cast<SetString>(addr::kTileSetString)(tile, addr::kTileValueString, text.c_str(), true);
        }
    }

    std::string MessageText()
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        void* tile = menu ? *reinterpret_cast<void* const*>(menu + addr::kMessageMenuText) : nullptr;
        if (!tile)
            return "";
        using GetValue = const uint8_t*(__thiscall*)(void*, uint32_t);
        const uint8_t* value = reinterpret_cast<GetValue>(addr::kTileGetValue)(tile, addr::kTileValueString);
        const char* now = value ? *reinterpret_cast<const char* const*>(value + 0xC) : nullptr;
        return now ? now : "";
    }

    bool MessageShows(const std::string& text) { return !text.empty() && MessageText() == text; }

    bool SetMessageText(const std::string& text)
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        void* tile = menu ? *reinterpret_cast<void* const*>(menu + addr::kMessageMenuText) : nullptr;
        if (!tile)
            return false;
        SetTileText(tile, text);
        return true;
    }

    bool SetMessageButton(int index, const std::string& label)
    {
        auto* menu = *reinterpret_cast<uint8_t* const*>(addr::kMessageMenu);
        if (!menu)
            return false;
        auto* node = menu + addr::kMessageMenuButtons + 4;
        for (int i = 0; node && i < index; ++i)
            node = *reinterpret_cast<uint8_t* const*>(node + 4);
        auto* item = node ? *reinterpret_cast<uint8_t* const*>(node) : nullptr;
        void* tile = item ? *reinterpret_cast<void* const*>(item) : nullptr;
        if (!tile)
            return false;
        SetTileText(tile, label);
        return true;
    }

    int TakeChoice(int id)
    {
        if (id < 0 || id >= kQuestions || !g_questions[id].open || g_questions[id].button == -1)
            return -1;
        int choice = g_questions[id].button;
        g_questions[id].open = false;
        g_questions[id].button = -1;
        LOG_DEBUG("Question %d answered with button %d", id, choice);
        return choice;
    }

    void DropQuestion(int id)
    {
        if (id < 0 || id >= kQuestions || !g_questions[id].open)
            return;
        Question& q = g_questions[id];
        q.open = false;
        q.button = -1;
        for (auto it = g_waiting.begin(); it != g_waiting.end(); ++it)
            if (it->id == id)
            {
                g_waiting.erase(it);
                return;
            }
        if (MessageShows(q.text))
            CloseMenu();
    }

    void OpenLoadMenu()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::kLoadOption)();
    }

    bool RunInBackground(bool on)
    {
        auto* value = reinterpret_cast<uint8_t*>(addr::kAlwaysActiveSetting + 4);
        bool was = *value != 0;
        *value = on ? 1 : 0;
        return was;
    }

    void QuitGame()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::kExitGameOption)();
    }

    int TakeAnswer(int id)
    {
        int choice = TakeChoice(id);
        return choice == kNeverShown ? 0 : choice < 0 ? -1 : choice == 0 ? 1 : 0;
    }

    bool RunConsoleLine(const char* line, TESObjectREFR* on)
    {
        if (!g_console || !line)
            return false;
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            return g_console->RunScriptLine2(line, on, false);
        return g_console->RunScriptLine(line, on);
    }

    std::string Evaluate(const char* expression)
    {
        if (!g_scripts || !expression || !*expression)
            return "nothing to work out";
        std::string text = std::string("Begin Function {}\n  SetFunctionValue ") + expression + "\nEnd\n";
        Script* script = g_scripts->CompileScript(text.c_str());
        if (script)
            g_ourScripts.insert(script);
        if (!script)
            return "couldn't compile it (the game's console says why)";
        NVSEArrayVarInterface::Element result;
        if (!g_scripts->CallFunction(script, Player(), nullptr, &result, 0))
            return "couldn't run it";
        char out[320];
        switch (result.GetType())
        {
        case NVSEArrayVarInterface::Element::kType_Numeric:
            std::snprintf(out, sizeof(out), "%g", result.GetNumber());
            break;
        case NVSEArrayVarInterface::Element::kType_Form:
        {
            TESForm* form = result.GetTESForm();
            const char* name = form && form->GetIsReference() ? Name(static_cast<TESObjectREFR*>(form)) : "";
            std::snprintf(out, sizeof(out), "form %08X%s%s", form ? form->refID : 0, *name ? " " : "", name);
            break;
        }
        case NVSEArrayVarInterface::Element::kType_String:
            std::snprintf(out, sizeof(out), "\"%.300s\"", result.GetString() ? result.GetString() : "");
            break;
        case NVSEArrayVarInterface::Element::kType_Array:
            return "an array (not shown)";
        default:
            return "no value";
        }
        return out;
    }

    void NoteMadeCharacters(const std::vector<uint32_t>& refIds)
    {
        if (!g_compiled || !g_madeClear || !g_madeAdd || !Player())
            return;
        g_scripts->CallFunctionAlt(g_madeClear, Player(), 0);
        for (uint32_t id : refIds)
            if (TESObjectREFR* ref = LookupRef(id))
                g_scripts->CallFunctionAlt(g_madeAdd, Player(), 1, ref);
    }

    std::vector<uint32_t> SavedMadeCharacters()
    {
        std::vector<uint32_t> ids;
        if (!g_compiled || !g_madeCount || !g_madeGet || !Player())
            return ids;
        int count = static_cast<int>(Number(g_madeCount, Player()));
        constexpr int kMaxSaved = 128;
        for (int i = 0; i < count && i < kMaxSaved; ++i)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_scripts->CallFunction(g_madeGet, Player(), nullptr, &result, 1, static_cast<uint32_t>(i)))
                continue;
            TESForm* form = result.GetTESForm();
            if (form && form->GetIsReference() && (form->refID >> 24) == 0xFF)
                ids.push_back(form->refID);
        }
        return ids;
    }

    void ResetAnimation(TESObjectREFR* ref, int how)
    {
        if (!g_compiled || !IsActor(ref))
            return;
        if (how == 1)
            StopIdle(ref);
        else if (how == 2 && g_idleGroup)
            g_scripts->CallFunctionAlt(g_idleGroup, ref, 0);
        else if (how == 3 && g_forceStopIdle)
            g_scripts->CallFunctionAlt(g_forceStopIdle, ref, 0);
    }

    bool Resurrect(TESObjectREFR* ref)
    {
        if (!g_compiled || !g_resurrect || !ref)
            return false;
        // Reviving with 3D loaded rebuilds the actor's animations on the spot, and that can crash the game.
        if (Is3DLoaded(ref))
        {
            LOG_WARN("Not reviving %08X while its 3D is loaded", ref->refID);
            return false;
        }
        g_scripts->CallFunctionAlt(g_resurrect, ref, 0);
        return true;
    }

    uintptr_t ReloadFunction(TESObjectREFR* ref)
    {
        if (!IsActor(ref))
            return 0;
        return (*reinterpret_cast<uintptr_t* const*>(ref))[addr::kActorReloadSlot];
    }

    bool Reload(TESObjectREFR* ref, int animType)
    {
        TESObjectWEAP* weapon = g_compiled && IsActor(ref) ? HeldWeapon(ref) : nullptr;
        uintptr_t function = ReloadFunction(ref);
        if (!weapon || function != addr::kActorReload)
            return false;
        using ReloadFn = int(__thiscall*)(TESObjectREFR*, TESObjectWEAP*, uint32_t, uint8_t);
        reinterpret_cast<ReloadFn>(function)(ref, weapon, static_cast<uint32_t>(animType), 0);
        return true;
    }

    bool InScriptAnimation(TESObjectREFR* ref)
    {
        return CurrentAction(ref) == HighProcess::kAnimAction_Force_Script_Anim;
    }

    bool IsAttackAction(int action)
    {
        return action == HighProcess::kAnimAction_Attack;
    }

    bool IsAttackPhase(int action)
    {
        return action >= HighProcess::kAnimAction_Attack && action <= HighProcess::kAnimAction_Attack_Throw_Release;
    }

    namespace
    {
        uintptr_t g_explosionInit = 0;
        std::vector<SeenExplosion> g_explosions;
        std::mutex g_explosionsLock;

        DWORD g_mainThread = 0;
        void NoteThread(const char* hook, std::atomic<int>& said)
        {
            bool main = GetCurrentThreadId() == g_mainThread;
            int which = main ? 1 : 2;
            if (!g_mainThread || (said.fetch_or(which) & which))
                return;
            if (main)
                LOG_DEBUG("%s runs on the game's main thread", hook);
            else
                LOG_WARN("%s ran on another thread than the game's main one", hook);
        }

        bool IsOurHarmless(const TESForm* form)
        {
            for (auto* list : {&g_harmlessExplosions, &g_quietExplosions})
                for (auto& [original, copy] : *list)
                    if (copy == form)
                        return true;
            return false;
        }

        void __fastcall ExplosionInitHook(TESObjectREFR* explosion)
        {
            bool fresh = explosion && !*reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(explosion) +
                addr::kExplosionProcess);
            reinterpret_cast<void(__fastcall*)(TESObjectREFR*)>(g_explosionInit)(explosion);
            static std::atomic<int> s_saidThread{0};
            NoteThread("The explosion hook", s_saidThread);
            if (!fresh || !explosion->baseForm || explosion->baseForm->typeID != kFormType_BGSExplosion ||
                IsOurHarmless(explosion->baseForm))
                return;
            coop::Pose at;
            if (!GetPose(explosion, at))
                return;
            auto* source = *reinterpret_cast<TESObjectREFR* const*>(reinterpret_cast<const uint8_t*>(explosion) +
                addr::kExplosionSource);
            std::lock_guard<std::mutex> lock(g_explosionsLock);
            if (g_explosions.size() >= 32)
                return;
            g_explosions.push_back({explosion->baseForm->refID, at.space, at.x, at.y, at.z, source && source == Player()});
            LOG_DEBUG("Explosion %08X (%08X) at %.0f, %.0f, %.0f by %08X", explosion->refID, explosion->baseForm->refID,
                at.x, at.y, at.z, source ? source->refID : 0);
        }
    }

    bool WatchExplosions()
    {
        if (g_explosionInit)
            return true;
        g_mainThread = GetCurrentThreadId();
        auto* slot = reinterpret_cast<uintptr_t*>(addr::kExplosionInitSlot);
        DWORD old;
        if (VirtualProtect(slot, 4, PAGE_READWRITE, &old))
        {
            g_explosionInit = *slot;
            *slot = reinterpret_cast<uintptr_t>(&ExplosionInitHook);
            VirtualProtect(slot, 4, old, &old);
        }
        if (g_explosionInit && g_explosionInit != addr::kExplosionInit)
            LOG_INFO("Explosions: another plugin hooks their start too (%08X); ours passes on to it",
                static_cast<unsigned>(g_explosionInit));
        return g_explosionInit != 0;
    }

    std::vector<SeenExplosion> TakeExplosions()
    {
        std::lock_guard<std::mutex> lock(g_explosionsLock);
        return std::move(g_explosions);
    }

    bool ShowExplosion(uint32_t formId, uint32_t space, float x, float y, float z)
    {
        TESForm* form = LookupForm(formId);
        TESForm* where = LookupForm(space);
        if (!g_compiled || !g_placeAt || !Player() || !form || form->typeID != kFormType_BGSExplosion || !where ||
            (where->typeID != kFormType_TESWorldSpace && where->typeID != kFormType_TESObjectCELL))
            return false;
        BGSExplosion* harmless = HarmlessExplosion(static_cast<BGSExplosion*>(form));
        if (!harmless)
            return false;
        g_scripts->CallFunctionAlt(g_placeAt, Player(), 5, harmless, where, FloatArg(x), FloatArg(y), FloatArg(z));
        return true;
    }

    bool IsThrowRelease(int action)
    {
        return action == HighProcess::kAnimAction_Attack_Throw_Release;
    }

    bool IsReloadAction(int action)
    {
        return action == HighProcess::kAnimAction_Reload;
    }

    void Remove(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_remove, ref, 0);
    }

    namespace
    {
        std::string Safe(const char* text)
        {
            std::string safe(text ? text : "");
            std::replace(safe.begin(), safe.end(), '|', '/');
            return safe;
        }
    }

    void Notify(const char* text)
    {
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_notify, Player(), 1, Safe(text).c_str());
    }

    void Notify(const char* text, float seconds)
    {
        if (!g_compiled || !Player())
            return;
        g_scripts->CallFunctionAlt(g_notifyFor, Player(), 2, Safe(text).c_str(), FloatArg(seconds));
    }

    bool AiStateBits(TESObjectREFR* ref, uint32_t& out)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_aiState || !IsActor(ref) || !g_scripts->CallFunction(g_aiState, ref, nullptr, &result, 0))
            return false;
        out = static_cast<uint32_t>(result.GetNumber());
        return true;
    }

    int AnimAction(TESObjectREFR* ref) { return CurrentAction(ref); }

    void ConsolePrint(const char* text)
    {
        if (g_print && Player())
            g_scripts->CallFunctionAlt(g_print, Player(), 1, text);
    }

    namespace
    {
        using TypeTest = bool (*)(uint8_t type, uint8_t wanted);

        bool SameType(uint8_t type, uint8_t wanted) { return type == wanted; }

        bool ItemType(uint8_t type, uint8_t)
        {
            using IsInventoryTypeFn = bool(__cdecl*)(UInt32 formType);
            return reinterpret_cast<IsInventoryTypeFn>(addr::kIsInventoryObjectType)(type);
        }

        void AddRefs(const TESObjectCELL* cell, TypeTest test, uint8_t baseType, std::vector<TESObjectREFR*>& out,
            const TESObjectREFR* around, float range, bool skipPersistent)
        {
            if (!cell)
                return;
            for (auto* node = &cell->objectList.m_listHead; node; node = node->next)
            {
                TESObjectREFR* ref = node->data;
                if (!ref || !ref->baseForm || !test(ref->baseForm->typeID, baseType))
                    continue;
                if (skipPersistent && (ref->flags & layout::kRefPersistent))
                    continue;
                if (around)
                {
                    float dx = ref->posX - around->posX, dy = ref->posY - around->posY;
                    if (dx * dx + dy * dy > range * range)
                        continue;
                }
                out.push_back(ref);
            }
        }

        template <typename T> T At(const void* base, uint32_t offset)
        {
            return *reinterpret_cast<const T*>(static_cast<const char*>(base) + offset);
        }
    }

    void RefsNear(TypeTest test, uint8_t baseType, std::vector<TESObjectREFR*>& out, float range)
    {
        out.clear();
        TESObjectREFR* player = Player();
        TESObjectCELL* cell = player ? player->parentCell : nullptr;
        if (!cell)
            return;
        const void* world = At<const void*>(cell, layout::kCellWorldSpace);
        if (!world)
        {
            AddRefs(cell, test, baseType, out, nullptr, 0, false);
            return;
        }
        const void* tes = *reinterpret_cast<const void* const*>(layout::kTesSingleton);
        const void* grid = tes ? At<const void*>(tes, layout::kTesGridCells) : nullptr;
        if (grid)
        {
            uint32_t size = At<uint32_t>(grid, layout::kGridSize);
            auto* cells = At<TESObjectCELL* const*>(grid, layout::kGridCellArray);
            if (cells && size <= 15)
                for (uint32_t i = 0; i < size * size; ++i)
                    AddRefs(cells[i], test, baseType, out, player, range, true);
        }
        AddRefs(At<const TESObjectCELL*>(world, layout::kWorldPersistentCell), test, baseType, out, player, range, false);
    }

    int LoadedGrid()
    {
        const void* tes = *reinterpret_cast<const void* const*>(layout::kTesSingleton);
        const void* grid = tes ? At<const void*>(tes, layout::kTesGridCells) : nullptr;
        uint32_t size = grid ? At<uint32_t>(grid, layout::kGridSize) : 0;
        return size >= 3 && size <= 15 && (size & 1) ? static_cast<int>(size) : 5;
    }

    bool InMenuMode()
    {
        return reinterpret_cast<bool(__cdecl*)()>(addr::kInMenuMode)();
    }

    namespace
    {
        bool ReadMenuStack(uint32_t (&ids)[addr::kMenuStackSlots])
        {
            __try
            {
                auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
                if (!manager)
                    return false;
                for (int i = 0; i < addr::kMenuStackSlots; ++i)
                    ids[i] = reinterpret_cast<const uint32_t*>(manager + addr::kMenuStack)[i];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        const char* MenuName(uint32_t id)
        {
            switch (id)
            {
            case 1001: return "message";
            case 1002: return "inventory";
            case 1003: return "stats";
            case 1004: return "HUD";
            case 1007: return "loading";
            case 1008: return "container";
            case 1009: return "dialogue";
            case 1012: return "sleep/wait";
            case 1013: return "pause menu";
            case 1014: return "lockpick";
            case 1016: return "quantity";
            case 1023: return "map";
            case 1027: return "level up";
            case 1053: return "barter";
            case 1055: return "hacking";
            case 1056: return "VATS";
            case 1057: return "computer";
            default: return nullptr;
            }
        }
    }

    void OpenMenus(char* out, size_t size)
    {
        if (!out || !size)
            return;
        out[0] = 0;
        uint32_t ids[addr::kMenuStackSlots] = {};
        if (!ReadMenuStack(ids))
        {
            std::snprintf(out, size, "?");
            return;
        }
        size_t used = 0;
        for (uint32_t id : ids)
        {
            if (!id || used + 1 >= size)
                continue;
            const char* name = MenuName(id);
            int n = name ? std::snprintf(out + used, size - used, "%s%s", used ? ", " : "", name)
                         : std::snprintf(out + used, size - used, "%s%u", used ? ", " : "", id);
            if (n > 0)
                used += static_cast<size_t>(n) < size - used ? static_cast<size_t>(n) : size - used - 1;
        }
    }

    bool QuantityPromptOpen()
    {
        uint32_t ids[addr::kMenuStackSlots] = {};
        if (!ReadMenuStack(ids))
            return false;
        for (uint32_t id : ids)
            if (id == 1016)
                return true;
        return false;
    }

    bool SetFieldOfView(int degrees)
    {
        static float s_world = 0;
        TESObjectREFR* player = Player();
        if (!player || !g_fovNow)
            return false;
        NVSEArrayVarInterface::Element world, hands;
        if (!g_scripts->CallFunction(g_fovNow, player, nullptr, &world, 1, 0u) ||
            !g_scripts->CallFunction(g_fovNow, player, nullptr, &hands, 1, 1u))
            return false;
        if (!s_world)
            s_world = static_cast<float>(world.GetNumber());
        float want = degrees ? static_cast<float>(degrees) : s_world;
        if (want < 30 || want > 150)
            return false;
        char line[64];
        std::snprintf(line, sizeof(line), "fov %.0f %.0f", want, hands.GetNumber());
        RunConsoleLine(line);
        LOG_INFO("Field of view: %.0f (the game's own: %.0f)", want, s_world);
        return true;
    }

    namespace
    {
        namespace npcl
        {
            constexpr uint32_t kFlags = 0x34, kBaseData = 0x30, kRace = 0x110, kFaceGen = 0x134, kFaceGenSize = 0x20,
                kFaceValues = 0x0C, kFaceCount = 0x18, kHair = 0x1B8, kHairLength = 0x1BC, kEyes = 0x1C0,
                kHairColor = 0x1D8, kHeadParts = 0x1DC;
            constexpr uint32_t kFaceCounts[3] = {50, 30, 50};
        }

        uint32_t IdAt(const void* npc, uint32_t offset)
        {
            const TESForm* form = At<const TESForm*>(npc, offset);
            return form ? form->refID : 0;
        }

        template <typename T>
        T* FormOfType(uint32_t id, uint8_t type)
        {
            TESForm* form = id ? LookupForm(id) : nullptr;
            return form && form->typeID == type ? reinterpret_cast<T*>(form) : nullptr;
        }
    }

    bool ReadMyLooks(net::PlayerLooks& out)
    {
        TESObjectREFR* player = Player();
        const TESForm* npc = player ? player->baseForm : nullptr;
        if (!npc || npc->typeID != kFormType_TESNPC)
            return false;
        out = net::PlayerLooks{};
        out.female = (At<uint32_t>(npc, npcl::kFlags) & 1) != 0;
        out.race = IdAt(npc, npcl::kRace);
        out.hair = IdAt(npc, npcl::kHair);
        out.eyes = IdAt(npc, npcl::kEyes);
        float length = At<float>(npc, npcl::kHairLength);
        out.hairLength = std::isfinite(length) && length >= 0 && length <= 10 ? length : 0;
        out.hairColor = At<uint32_t>(npc, npcl::kHairColor);
        size_t at = 0;
        for (int i = 0; i < 3; ++i)
        {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(npc) + npcl::kFaceGen + i * npcl::kFaceGenSize;
            const float* values = *reinterpret_cast<const float* const*>(data + npcl::kFaceValues);
            uint32_t count = *reinterpret_cast<const uint32_t*>(data + npcl::kFaceCount);
            for (uint32_t j = 0; j < npcl::kFaceCounts[i]; ++j, ++at)
            {
                float v = values && count == npcl::kFaceCounts[i] ? values[j] : 0;
                out.face[at] = std::isfinite(v) && v > -net::kMaxFaceValue && v < net::kMaxFaceValue ? v : 0;
            }
        }
        const uint8_t* node = reinterpret_cast<const uint8_t*>(npc) + npcl::kHeadParts;
        for (int n = 0; node && n < 32 && out.headParts.size() < net::kMaxHeadParts; ++n)
        {
            if (const TESForm* part = *reinterpret_cast<const TESForm* const*>(node))
                if ((part->refID >> 24) != 0xFF)
                    out.headParts.push_back(part->refID);
            node = *reinterpret_cast<const uint8_t* const*>(node + 4);
        }
        for (const char* c = Name(player); *c && out.name.size() < net::kMaxNameLength; ++c)
            out.name += static_cast<unsigned char>(*c) < 0x20 || *c == 0x7F ? '?' : *c;
        if ((out.race >> 24) == 0xFF) out.race = 0;
        if ((out.hair >> 24) == 0xFF) out.hair = 0;
        if ((out.eyes >> 24) == 0xFF) out.eyes = 0;
        return true;
    }

    namespace
    {
        bool LooksOntoBase(TESForm* base, const net::PlayerLooks& looks, int& shared)
        {
        if (!base || base->typeID != kFormType_TESNPC ||
            std::find(g_standInBases.begin(), g_standInBases.end(), base->refID) == g_standInBases.end())
            return false;
        auto* npc = reinterpret_cast<uint8_t*>(base);
        if (!looks.name.empty())
            SetName(static_cast<TESActorBase*>(base)->fullName.name, looks.name.c_str());
        if (TESForm* race = FormOfType<TESForm>(looks.race, kFormType_TESRace))
            if ((At<uint32_t>(race, addr::kRaceFlags) & (addr::kRacePlayable | addr::kRaceChild)) == addr::kRacePlayable)
                *reinterpret_cast<TESForm**>(npc + npcl::kRace) = race;
        if (((At<uint32_t>(npc, npcl::kFlags) & 1) != 0) != looks.female)
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(addr::kSetActorBaseFlag)(
                npc + npcl::kBaseData, 1, looks.female ? 1 : 0, 1);
        if (TESForm* hair = FormOfType<TESForm>(looks.hair, kFormType_TESHair))
            *reinterpret_cast<TESForm**>(npc + npcl::kHair) = hair;
        if (TESForm* eyes = FormOfType<TESForm>(looks.eyes, kFormType_TESEyes))
            *reinterpret_cast<TESForm**>(npc + npcl::kEyes) = eyes;
        *reinterpret_cast<float*>(npc + npcl::kHairLength) = looks.hairLength;
        *reinterpret_cast<uint32_t*>(npc + npcl::kHairColor) = looks.hairColor;
        const uint8_t* traveler = reinterpret_cast<const uint8_t*>(LookupForm(ids::kStandInBase));
        size_t at = 0;
        for (int i = 0; i < 3; ++i)
        {
            uint8_t* data = npc + npcl::kFaceGen + i * npcl::kFaceGenSize;
            float*& values = *reinterpret_cast<float**>(data + npcl::kFaceValues);
            uint32_t count = *reinterpret_cast<uint32_t*>(data + npcl::kFaceCount);
            if (count != npcl::kFaceCounts[i])
            {
                at += npcl::kFaceCounts[i];
                continue;
            }
            const float* theirs = traveler ? *reinterpret_cast<const float* const*>(
                traveler + npcl::kFaceGen + i * npcl::kFaceGenSize + npcl::kFaceValues) : nullptr;
            if (!values || values == theirs)
            {
                ++shared;
                values = static_cast<float*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(
                    count * sizeof(float)));
                if (!values)
                    return false;
            }
            for (uint32_t j = 0; j < count; ++j)
                values[j] = looks.face[at++];
        }
        uint8_t* node = npc + npcl::kHeadParts;
        *reinterpret_cast<void**>(node) = nullptr;
        *reinterpret_cast<void**>(node + 4) = nullptr;
        bool first = true;
        for (uint32_t id : looks.headParts)
        {
            TESForm* part = FormOfType<TESForm>(id, kFormType_BGSHeadPart);
            if (!part)
                continue;
            if (!first)
            {
                auto* next = static_cast<uint8_t*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(8));
                if (!next)
                    break;
                *reinterpret_cast<void**>(next + 4) = nullptr;
                *reinterpret_cast<void**>(node + 4) = next;
                node = next;
            }
            *reinterpret_cast<TESForm**>(node) = part;
            first = false;
        }
        return true;
        }
    }

    bool ApplyLooks(TESObjectREFR* standIn, const net::PlayerLooks& looks)
    {
        TESForm* base = standIn ? standIn->baseForm : nullptr;
        bool wasFemale = base && (At<uint32_t>(base, npcl::kFlags) & 1) != 0;
        int shared = 0;
        if (!LooksOntoBase(base, looks, shared))
            return false;
        if (wasFemale != looks.female && g_update3D)
        {
            g_scripts->CallFunctionAlt(g_update3D, standIn, 0);
            LOG_INFO("Looks: %08X is now %s: its body loaded again", standIn->refID, looks.female ? "female" : "male");
            return true;
        }
        bool& egt = *reinterpret_cast<bool*>(addr::kLoadFaceGenEgt);
        bool wasEgt = egt;
        egt = true;
        reinterpret_cast<void(__thiscall*)(TESObjectREFR*)>(addr::kRemakeHead)(standIn);
        egt = wasEgt;
        LOG_INFO("Looks: %08X made to look like its player (%s, race %08X, hair %08X, %zu head parts%s)", standIn->refID,
            looks.female ? "female" : "male", looks.race, looks.hair, looks.headParts.size(),
            shared ? "; its face's numbers were the Traveler's: given its own" : "");
        return true;
    }

    int ExteriorCellBuffer()
    {
        const auto* setting = reinterpret_cast<const uint8_t*>(addr::kSettingExteriorCellBuffer);
        const char* name = *reinterpret_cast<const char* const*>(setting + 8);
        auto at = reinterpret_cast<uintptr_t>(name);
        if (at < 0x00400000 || at >= 0x02000000 || std::strcmp(name, "uExterior Cell Buffer:General") != 0)
            return -1;
        return static_cast<int>(*reinterpret_cast<const uint32_t*>(setting + 4));
    }

    float LoadedReach()
    {
        return static_cast<float>((LoadedGrid() - 1) / 2) * 4096.0f;
    }

    namespace
    {
        bool MovableType(uint8_t type, uint8_t)
        {
            return type == kFormType_BGSMovableStatic || type == kFormType_TESObjectCONT;
        }

        std::map<uint32_t, bool> g_movable;

        bool IsMovable(TESObjectREFR* ref)
        {
            auto it = g_movable.find(ref->refID);
            if (it != g_movable.end())
                return it->second;
            NVSEArrayVarInterface::Element result;
            if (!g_isMobile || !ref->GetNiNode() || !g_scripts->CallFunction(g_isMobile, ref, nullptr, &result, 0))
                return false;
            if (g_movable.size() > 20000)
                g_movable.clear();
            return g_movable[ref->refID] = result.GetNumber() != 0;
        }
    }

    void MovablesNear(std::vector<TESObjectREFR*>& out, float range)
    {
        std::vector<TESObjectREFR*> candidates;
        RefsNear(MovableType, 0, candidates, range);
        out.clear();
        for (TESObjectREFR* ref : candidates)
            if ((ref->refID >> 24) != 0xFF && IsMovable(ref))
                out.push_back(ref);
    }

    bool Get3DPosition(TESObjectREFR* ref, float& x, float& y, float& z)
    {
        const auto* node = ref ? reinterpret_cast<const uint8_t*>(ref->GetNiNode()) : nullptr;
        if (!node)
            return false;
        const auto* p = reinterpret_cast<const float*>(node + layout::kNodeWorldTranslate);
        x = p[0];
        y = p[1];
        z = p[2];
        return true;
    }

    void DoorsNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_TESObjectDOOR, out, range);
    }

    void ContainersNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_TESObjectCONT, out, range);
    }

    namespace
    {
        struct ListNode { void* data; ListNode* next; };

        template <typename F> void ForEachIn(const void* list, F f)
        {
            for (auto* node = static_cast<const ListNode*>(list); node; node = node->next)
                if (node->data)
                    f(node->data);
        }

        TESForm* QuestById(uint32_t id)
        {
            TESForm* form = LookupForm(id);
            return form && form->typeID == kFormType_TESQuest ? form : nullptr;
        }

        void ReadQuestInto(const TESForm* quest, QuestInfo& q)
        {
            q.id = quest->refID;
            q.flags = At<uint8_t>(quest, layout::kQuestFlags);
            q.stage = At<uint8_t>(quest, layout::kQuestCurrentStage);
            q.delay = At<float>(quest, layout::kQuestDelay);
            const char* name = At<const char*>(quest, layout::kQuestEditorId);
            uint16_t length = At<uint16_t>(quest, layout::kQuestEditorId + 4);
            q.name = name && length < 200 ? std::string(name, length) : std::string();
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestStages, [&](void* stage)
            {
                if (At<uint8_t>(stage, layout::kStageDone))
                    q.done.push_back(At<uint8_t>(stage, layout::kStageId));
            });
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl)
                    q.objectives.push_back({At<uint32_t>(item, layout::kObjectiveId),
                        At<uint32_t>(item, layout::kObjectiveStatus)});
            });
        }
    }

    void ReadQuests(std::vector<QuestInfo>& out, bool all)
    {
        out.clear();
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            QuestInfo q;
            ReadQuestInto(static_cast<TESForm*>(quest), q);
            if (all || (q.flags & 0x63) || !q.done.empty())
                out.push_back(std::move(q));
        });
    }

    bool ReadQuest(uint32_t id, QuestInfo& out)
    {
        TESForm* quest = QuestById(id);
        if (!quest)
            return false;
        out = QuestInfo{};
        ReadQuestInto(quest, out);
        return true;
    }

    void SetQuestFlags(uint32_t id, uint8_t flags)
    {
        if (TESForm* quest = QuestById(id))
            *reinterpret_cast<uint8_t*>(reinterpret_cast<char*>(quest) + layout::kQuestFlags) = flags;
    }

    void SetQuestStageDone(uint32_t id, uint8_t stage, bool done)
    {
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestStages, [&](void* s)
            {
                if (At<uint8_t>(s, layout::kStageId) == stage)
                    static_cast<uint8_t*>(s)[layout::kStageDone] = done ? 1 : 0;
            });
    }

    void SetQuestObjective(uint32_t id, uint32_t objective, uint32_t status)
    {
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl &&
                    At<uint32_t>(item, layout::kObjectiveId) == objective)
                    *reinterpret_cast<uint32_t*>(static_cast<char*>(item) + layout::kObjectiveStatus) = status;
            });
    }

    void QuestFingerprints(std::vector<std::pair<uint32_t, uint32_t>>& out)
    {
        out.clear();
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            uint32_t h = 2166136261u;
            auto mix = [&](uint32_t v)
            {
                for (int i = 0; i < 4; ++i, v >>= 8)
                    h = (h ^ (v & 0xFF)) * 16777619u;
            };
            mix(At<uint8_t>(quest, layout::kQuestFlags));
            mix(At<uint8_t>(quest, layout::kQuestCurrentStage));
            ForEachIn(static_cast<char*>(quest) + layout::kQuestStages, [&](void* st)
            {
                mix(At<uint8_t>(st, layout::kStageId) | (At<uint8_t>(st, layout::kStageDone) << 8));
            });
            ForEachIn(static_cast<char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl)
                {
                    mix(At<uint32_t>(item, layout::kObjectiveId));
                    mix(At<uint32_t>(item, layout::kObjectiveStatus));
                }
            });
            out.push_back({static_cast<TESForm*>(quest)->refID, h});
        });
    }

    void WriteQuest(const QuestInfo& q)
    {
        TESForm* quest = QuestById(q.id);
        if (!quest)
            return;
        char* base = reinterpret_cast<char*>(quest);
        base[layout::kQuestFlags] = static_cast<char>(q.flags);
        base[layout::kQuestCurrentStage] = static_cast<char>(q.stage);
        ForEachIn(base + layout::kQuestStages, [&](void* st)
        {
            uint8_t id = At<uint8_t>(st, layout::kStageId);
            bool done = std::find(q.done.begin(), q.done.end(), id) != q.done.end();
            static_cast<uint8_t*>(st)[layout::kStageDone] = done ? 1 : 0;
        });
        ForEachIn(base + layout::kQuestObjectives, [&](void* item)
        {
            if (*static_cast<uintptr_t*>(item) != addr::kQuestObjectiveVtbl)
                return;
            uint32_t id = At<uint32_t>(item, layout::kObjectiveId);
            for (auto& [objective, status] : q.objectives)
                if (objective == id)
                    *reinterpret_cast<uint32_t*>(static_cast<char*>(item) + layout::kObjectiveStatus) = status;
        });
    }

    namespace
    {
        std::string GameString(const void* at)
        {
            const char* text = At<const char*>(at, 0);
            uint16_t length = At<uint16_t>(at, 4);
            return text && length < 1000 ? std::string(text, length) : std::string();
        }
    }

    std::string QuestName(uint32_t id)
    {
        TESForm* quest = QuestById(id);
        return quest ? GameString(reinterpret_cast<char*>(quest) + layout::kQuestName) : std::string();
    }

    std::string ObjectiveText(uint32_t id, uint32_t objective)
    {
        std::string text;
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl &&
                    At<uint32_t>(item, layout::kObjectiveId) == objective)
                    text = GameString(static_cast<char*>(item) + layout::kObjectiveText);
            });
        return text;
    }

    std::vector<std::pair<uint32_t, void*>> DetachQuestScripts(bool (*accept)(uint32_t id, const void* context),
        const void* context)
    {
        std::vector<std::pair<uint32_t, void*>> detached;
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return detached;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            void** slot = reinterpret_cast<void**>(static_cast<char*>(quest) + layout::kQuestScript);
            uint32_t id = static_cast<TESForm*>(quest)->refID;
            if (*slot && accept(id, context))
            {
                detached.push_back({id, *slot});
                *slot = nullptr;
            }
        });
        return detached;
    }

    void PutBackQuestScripts(const std::vector<std::pair<uint32_t, void*>>& scripts)
    {
        for (auto& [id, script] : scripts)
            if (TESForm* quest = QuestById(id))
            {
                void** slot = reinterpret_cast<void**>(reinterpret_cast<char*>(quest) + layout::kQuestScript);
                if (!*slot)
                    *slot = script;
            }
    }

    std::vector<std::pair<uint32_t, double>> QuestVariables(uint32_t id)
    {
        std::vector<std::pair<uint32_t, double>> out;
        TESForm* quest = QuestById(id);
        const void* locals = quest ? At<const void*>(quest, layout::kQuestLocals) : nullptr;
        const void* vars = locals ? At<const void*>(locals, layout::kLocalsVars) : nullptr;
        if (vars)
            ForEachIn(vars, [&](void* v)
            {
                out.push_back({At<uint32_t>(v, layout::kVarId), At<double>(v, layout::kVarValue)});
            });
        return out;
    }

    void* QuestScript(uint32_t id)
    {
        TESForm* quest = QuestById(id);
        return quest ? At<void*>(quest, layout::kQuestScript) : nullptr;
    }

    void* SwapQuestScript(uint32_t id, void* script)
    {
        TESForm* quest = QuestById(id);
        if (!quest)
            return nullptr;
        void** slot = reinterpret_cast<void**>(reinterpret_cast<char*>(quest) + layout::kQuestScript);
        void* old = *slot;
        *slot = script;
        return old;
    }

    bool ShowObjective(uint32_t quest, uint32_t objective, bool on)
    {
        TESForm* q = QuestById(quest);
        return g_objectiveShown && q && Player() &&
            g_scripts->CallFunctionAlt(g_objectiveShown, Player(), 3, q, objective, static_cast<uint32_t>(on ? 1 : 0));
    }

    bool CompleteObjective(uint32_t quest, uint32_t objective, bool on)
    {
        TESForm* q = QuestById(quest);
        return g_objectiveDone && q && Player() &&
            g_scripts->CallFunctionAlt(g_objectiveDone, Player(), 3, q, objective, static_cast<uint32_t>(on ? 1 : 0));
    }

    bool CompleteQuest(uint32_t quest)
    {
        TESForm* q = QuestById(quest);
        return g_completeQuest && q && Player() && g_scripts->CallFunctionAlt(g_completeQuest, Player(), 1, q);
    }

    bool InDialogue()
    {
        return g_inDialogue && Player() && Number(g_inDialogue, Player()) != 0;
    }

    int32_t PlayerXp() { return Player() ? static_cast<int32_t>(Number(g_xp, Player())) : 0; }
    int32_t PlayerKarma() { return Player() ? static_cast<int32_t>(Number(g_karma, Player())) : 0; }

    void GiveXp(int32_t xp)
    {
        if (g_giveXp && Player() && xp > 0)
            g_scripts->CallFunctionAlt(g_giveXp, Player(), 1, static_cast<uint32_t>(xp));
    }

    void GiveKarma(int32_t karma)
    {
        if (g_giveKarma && Player() && karma)
            g_scripts->CallFunctionAlt(g_giveKarma, Player(), 1, static_cast<uint32_t>(karma));
    }

    std::vector<ReputationInfo> ReadReputations()
    {
        std::vector<ReputationInfo> out;
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return out;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerReputations, [&](void* rep)
        {
            out.push_back({static_cast<TESForm*>(rep)->refID, GameString(static_cast<char*>(rep) + layout::kReputationName),
                At<float>(rep, layout::kReputationFame), At<float>(rep, layout::kReputationInfamy)});
        });
        return out;
    }

    void WriteReputation(uint32_t form, float fame, float infamy)
    {
        TESForm* rep = LookupForm(form);
        if (!rep || rep->typeID != kFormType_TESReputation)
            return;
        *reinterpret_cast<float*>(reinterpret_cast<char*>(rep) + layout::kReputationFame) = fame;
        *reinterpret_cast<float*>(reinterpret_cast<char*>(rep) + layout::kReputationInfamy) = infamy;
    }

    void Greet(TESObjectREFR* actor)
    {
        if (g_greet && actor)
            g_scripts->CallFunctionAlt(g_greet, actor, 0);
    }

    TESObjectREFR* DialoguePartner()
    {
        const void* menu = *reinterpret_cast<const void* const*>(addr::kDialogMenu);
        return menu ? At<TESObjectREFR*>(menu, layout::kDialogMenuPartner) : nullptr;
    }

    Spoken SpokenNow()
    {
        Spoken out;
        const void* menu = *reinterpret_cast<const void* const*>(addr::kDialogMenu);
        const void* const* const* topicAt = menu ? At<const void* const* const*>(menu, layout::kDialogMenuTopicNode) :
            nullptr;
        const void* const* topicNode = topicAt ? *topicAt : nullptr;
        const char* topic = topicNode ? static_cast<const char*>(*topicNode) : nullptr;
        if (!topic)
            return out;
        const void* const* next = At<const void* const*>(topic, layout::kMenuTopicResponseNode);
        const void* response = nullptr;
        int guard = 0;
        for (const void* const* node = reinterpret_cast<const void* const*>(topic + layout::kMenuTopicResponses);
             node && guard < 64; node = static_cast<const void* const*>(node[1]), ++guard)
        {
            if (node == next)
                break;
            response = node[0];
        }
        if (!response)
            return out;
        out.id = reinterpret_cast<uintptr_t>(response);
        const char* text = *static_cast<const char* const*>(response);
        out.text = text ? text : "";
        const TESForm* info = At<const TESForm*>(topic, layout::kMenuTopicInfo);
        const TESForm* topicForm = At<const TESForm*>(topic, layout::kMenuTopicTopic);
        out.info = info ? info->refID : 0;
        out.topic = topicForm ? topicForm->refID : 0;
        out.response = static_cast<int>(At<uint32_t>(response, layout::kResponseNumber));
        return out;
    }

    bool IsTalking(const TESObjectREFR* actor)
    {
        return actor && At<uint8_t>(actor, layout::kIsTalking) != 0;
    }

    namespace
    {
        struct Armed
        {
            const TESForm* topic = nullptr;
            const TESForm* info = nullptr;
            const TESObjectREFR* speaker = nullptr;
            int response = 0;
            uint32_t until = 0;
        };
        Armed g_armed;
        constexpr uint32_t kArmedMs = 3000;
        struct Said { const TESObjectREFR* speaker; const TESForm* info; uint32_t until; };
        std::vector<Said> g_said;
        constexpr uint32_t kSaidMs = 120000;
        uintptr_t g_pickLine = 0;

        bool IsArmed(const void* topic, const void* speaker)
        {
            return g_armed.info && topic == g_armed.topic && speaker == g_armed.speaker &&
                static_cast<int32_t>(GetTickCount() - g_armed.until) < 0;
        }

        std::map<uint32_t, RadioLineStart> g_radioNow;
        std::vector<RadioLineStart> g_radioStarts;
        bool g_radioFromHost = false;
        struct HostRadio
        {
            uint32_t info = 0, part = 0, seq = 0;
            std::vector<net::RadioPick> run;
            uint32_t hostStart = 0;
            uint32_t since = 0;
            uint32_t lastPush = 0;
            size_t handing = 0;
            bool handed = false;
            bool runUsed = false;
            int rebuilds = 0;
            bool aligned = false;
        };
        std::map<uint32_t, HostRadio> g_hostRadio;

        struct RadioNode { void* item; RadioNode* next; };
        struct RadioReceiver { TESObjectREFR* radio; Sound sound; Sound staticSound; bool shouldPlay, justStarted,
            inRange, isActor; };
        struct RadioConversation { RadioNode items; RadioNode* current; };
        struct RadioItem { RadioNode responses; RadioNode* current; TESForm* info; TESForm* topic; };
        struct RadioStation
        {
            TESObjectREFR* station;
            RadioConversation* conversation;
            void* nextConversation;
            uint32_t itemStart;
            int32_t itemDuration;
            uint8_t power, targetPower;
            uint8_t pad16[2];
            uint32_t flags;
            RadioNode users;
        };
        static_assert(offsetof(RadioReceiver, staticSound) == 0x10 && offsetof(RadioReceiver, shouldPlay) == 0x1C);
        static_assert(offsetof(RadioItem, info) == 0x0C && offsetof(RadioItem, topic) == 0x10);
        static_assert(offsetof(RadioStation, itemDuration) == 0x10 && offsetof(RadioStation, users) == 0x1C);

        bool ReadStation(const RadioStation* st, RadioLineStart& out)
        {
            const RadioConversation* conv = st->conversation;
            if (!conv || !conv->current || !conv->current->item)
                return false;
            out.station = st->station->refID;
            out.run.clear();
            out.line = 0;
            for (const RadioNode* n = &conv->items; n && out.run.size() < net::kMaxRadioRun; n = n->next)
            {
                auto* item = static_cast<const RadioItem*>(n->item);
                if (!item || !item->info)
                    continue;
                if (n == conv->current)
                    out.line = out.run.size();
                out.run.push_back({item->topic ? item->topic->refID : 0, item->info->refID});
            }
            auto* item = static_cast<const RadioItem*>(conv->current->item);
            if (!item->info)
                return false;
            out.info = item->info->refID;
            out.part = 0;
            if (item->current)
                for (const RadioNode* n = &item->responses; n && n != item->current && out.part < 64; n = n->next)
                    ++out.part;
            return true;
        }

        size_t FindInRun(const std::vector<net::RadioPick>& run, uint32_t info, size_t from = 0)
        {
            for (size_t i = from; i < run.size(); ++i)
                if (run[i].info == info)
                    return i;
            return std::string::npos;
        }

        void* __fastcall PickLineHook(void* topic, void*, void* a, void* b, void* c, uint32_t d, uint32_t e, uint32_t f)
        {
            if (IsArmed(topic, b))
                return const_cast<TESForm*>(g_armed.info);
            using Fn = void*(__thiscall*)(void*, void*, void*, void*, uint32_t, uint32_t, uint32_t);
            auto* speaker = static_cast<TESObjectREFR*>(b);
            if (g_radioFromHost && topic && speaker && speaker->baseForm &&
                speaker->baseForm->typeID == kFormType_BGSTalkingActivator)
            {
                auto it = g_hostRadio.find(speaker->refID);
                uint32_t id = static_cast<TESForm*>(topic)->refID;
                if (it != g_hostRadio.end() && !it->second.runUsed)
                {
                    HostRadio& h = it->second;
                    if (h.handed && h.handing < h.run.size() && h.run[h.handing].topic != id)
                    {
                        ++h.handing;
                        h.handed = false;
                    }
                    for (size_t i = h.handing; i < h.run.size(); ++i)
                        if (h.run[i].topic == id)
                            if (void* info = TopicInfo(h.run[i].info))
                            {
                                h.handing = i;
                                h.handed = true;
                                return info;
                            }
                }
            }
            return reinterpret_cast<Fn>(g_pickLine)(topic, a, b, c, d, e, f);
        }

        uint8_t* g_firstResponse = nullptr;
        uint8_t* g_setEndCallback = nullptr;
        std::map<uint32_t, Sound> g_voices;
        std::mutex g_voicesLock;

        void __fastcall SetEndCallbackHook(Sound* sound, void*, uintptr_t function, uint32_t argument)
        {
            if ((function == addr::kSayEnded || function == addr::kSpeechEnded) && sound)
            {
                static std::atomic<int> s_saidThread{0};
                NoteThread("The voice hook", s_saidThread);
                std::lock_guard<std::mutex> lock(g_voicesLock);
                if (g_voices.size() >= 256 && !g_voices.count(argument))
                    g_voices.clear();
                g_voices[argument] = *sound;
            }
            reinterpret_cast<void(__thiscall*)(Sound*, uintptr_t, uint32_t)>(g_setEndCallback)(sound, function, argument);
        }

        bool __fastcall FirstResponseHook(void* item)
        {
            bool any = reinterpret_cast<bool(__thiscall*)(void*)>(g_firstResponse)(item);
            const TESForm* info = item ? At<const TESForm*>(item, layout::kDialogueItemInfo) : nullptr;
            if (!any || !g_armed.info || info != g_armed.info || !g_armed.response ||
                static_cast<int32_t>(GetTickCount() - g_armed.until) >= 0)
                return any;
            using Current = void*(__thiscall*)(void*);
            auto current = reinterpret_cast<Current>(addr::kDialogueItemCurrentResponse);
            auto next = reinterpret_cast<bool(__thiscall*)(void*)>(addr::kDialogueItemNextResponse);
            void* response = current(item);
            for (int i = 0; response && i < 32 &&
                 At<uint32_t>(response, layout::kResponseNumber) != static_cast<uint32_t>(g_armed.response); ++i)
            {
                if (!next(item))
                    return reinterpret_cast<bool(__thiscall*)(void*)>(g_firstResponse)(item);
                response = current(item);
            }
            LOG_DEBUG("Dialogue: %s starts at response %u (wanted %d)", g_armed.speaker ?
                Name(g_armed.speaker) : "?", response ? At<uint32_t>(response, layout::kResponseNumber) : 0,
                g_armed.response);
            return response != nullptr;
        }

        uint8_t* HookStart(uintptr_t function, const uint8_t* expect, size_t length, const void* to)
        {
            auto* at = reinterpret_cast<uint8_t*>(function);
            if (length < 5 || length > 16 || std::memcmp(at, expect, length) != 0)
                return nullptr;
            auto* back = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE));
            if (!back)
                return nullptr;
            std::memcpy(back, at, length);
            back[length] = 0xE9;
            int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(at + length) -
                reinterpret_cast<uintptr_t>(back + length + 5));
            std::memcpy(back + length + 1, &rel, 4);
            DWORD old;
            if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old))
                return nullptr;
            at[0] = 0xE9;
            rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(at + 5));
            std::memcpy(at + 1, &rel, 4);
            for (size_t k = 5; k < length; ++k)
                at[k] = 0x90;
            VirtualProtect(at, length, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, length);
            return back;
        }

        uintptr_t HookCall(uintptr_t site, const void* to)
        {
            auto* at = reinterpret_cast<uint8_t*>(site);
            if (at[0] != 0xE8)
                return 0;
            int32_t rel;
            std::memcpy(&rel, at + 1, 4);
            uintptr_t was = site + 5 + rel;
            DWORD old;
            if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old))
                return 0;
            rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (site + 5));
            std::memcpy(at + 1, &rel, 4);
            VirtualProtect(at, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, 5);
            return was;
        }
    }

    namespace
    {
        void __fastcall SkipTimeChange(void*, void*, float, bool) {}
    }

    namespace
    {
        uint8_t* g_setSpeed = nullptr;

        void __fastcall WatchedSpeedChange(void* timer, void*, float speed, bool now)
        {
            static float lastSpeed = -1.0f;
            static uintptr_t lastCaller = 0;
            uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5;
            if (speed != lastSpeed || caller != lastCaller)
                LOG_INFO("Speed: %g asked by the game code at %08X%s", speed, static_cast<unsigned>(caller),
                    now ? " (now)" : "");
            lastSpeed = speed;
            lastCaller = caller;
            using Fn = void(__thiscall*)(void*, float, bool);
            reinterpret_cast<Fn>(g_setSpeed)(timer, speed, now);
        }
    }

    bool WatchSpeedChanges()
    {
        if (!g_setSpeed)
        {
            static const uint8_t kStart[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
            g_setSpeed = HookStart(addr::kSetTimeMultiplier, kStart, sizeof(kStart),
                reinterpret_cast<const void*>(&WatchedSpeedChange));
        }
        return g_setSpeed != nullptr;
    }

    void SetWorldSpeed(float speed)
    {
        using Fn = void(__thiscall*)(void*, float, bool);
        reinterpret_cast<Fn>(addr::kSetTimeMultiplier)(reinterpret_cast<void*>(addr::kTimeGlobal), speed, true);
    }

    int KeepVatsAtFullSpeed()
    {
        static int skipped = -1;
        if (skipped >= 0)
            return skipped;
        skipped = 0;
        for (uintptr_t site : addr::kVatsTimeCalls)
        {
            uintptr_t was = HookCall(site, reinterpret_cast<const void*>(&SkipTimeChange));
            if (was)
                ++skipped;
            if (was && was != addr::kSetTimeMultiplier)
                LOG_INFO("VATS: another plugin changes the speed at %08X too (%08X); co-op skips it",
                    static_cast<unsigned>(site), static_cast<unsigned>(was));
        }
        return skipped;
    }

    namespace
    {
        bool IsMine(const TESObjectREFR* ref)
        {
            if (!ref || ref->typeID != kFormType_GrenadeProjectile || !ref->baseForm ||
                ref->baseForm->typeID != kFormType_BGSProjectile)
                return false;
            uint16_t flags = static_cast<const BGSProjectile*>(ref->baseForm)->projFlags;
            return (flags & layout::kMineBaseMask) == layout::kMineBaseFlags;
        }

        constexpr size_t kMaxPuppets = 8;
        std::atomic<TESObjectREFR*> g_minePuppets[kMaxPuppets] = {};
        std::atomic<bool> g_mineFriendGame{false};
        uintptr_t g_mineReacts = 0;

        bool __fastcall MineReactsHook(TESObjectREFR* mine, void*, TESObjectREFR* target, TESObjectREFR* shooter,
            void* ownerFaction)
        {
            if (target)
            {
                for (const auto& puppet : g_minePuppets)
                    if (puppet.load(std::memory_order_relaxed) == target)
                    {
                        static std::atomic<uint32_t> s_logged{0};
                        if (mine && s_logged.exchange(mine->refID) != mine->refID)
                            LOG_DEBUG("Mines: %08X not set off by a friend's character (%08X)", mine->refID,
                                target->refID);
                        return false;
                    }
                if (g_mineFriendGame.load(std::memory_order_relaxed) && mine && (mine->refID >> 24) != 0xFF)
                    return false;
            }
            using Fn = bool(__thiscall*)(TESObjectREFR*, TESObjectREFR*, TESObjectREFR*, void*);
            uintptr_t original = g_mineReacts ? g_mineReacts : addr::kMineReactsToTarget;
            return reinterpret_cast<Fn>(original)(mine, target, shooter, ownerFaction);
        }
    }

    void MinesNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_BGSProjectile, out, range);
        out.erase(std::remove_if(out.begin(), out.end(), [](TESObjectREFR* ref) { return !IsMine(ref); }), out.end());
    }

    int MineState(const TESObjectREFR* ref)
    {
        if (!IsMine(ref))
            return -1;
        if (ref->flags & (layout::kRefDeleted | layout::kRefDisabled))
            return net::kMineGone;
        return (At<uint32_t>(ref, layout::kProjFlags) & layout::kProjTurnedOff) ? net::kMineOff : 0;
    }

    bool TurnOffMine(TESObjectREFR* mine)
    {
        TESObjectREFR* player = Player();
        if (MineState(mine) != 0 || !player)
            return false;
        using Fn = bool(__thiscall*)(TESObjectREFR*, TESObjectREFR*, bool);
        reinterpret_cast<Fn>(addr::kProjectileTurnOff)(mine, player, true);
        return MineState(mine) == net::kMineOff;
    }

    bool MineExplosion(TESObjectREFR* mine)
    {
        if (MineState(mine) < 0 || !g_compiled || !g_placeAt || !Player() || !mine->parentCell)
            return false;
        auto* explosion = static_cast<BGSProjectile*>(mine->baseForm)->explosionForm;
        const void* world = At<const void*>(mine->parentCell, layout::kCellWorldSpace);
        const void* where = world ? world : mine->parentCell;
        if (!explosion)
            return false;
        g_scripts->CallFunctionAlt(g_placeAt, Player(), 5, explosion, where, FloatArg(mine->posX), FloatArg(mine->posY),
            FloatArg(mine->posZ));
        return true;
    }

    namespace
    {
        std::atomic<bool> g_holdLevelUp{false};
        uintptr_t g_combatCount = 0;

        int __fastcall LevelUpCombatCount(void* player, void*)
        {
            uintptr_t original = g_combatCount ? g_combatCount : addr::kPlayerCombatCount;
            int count = reinterpret_cast<int(__thiscall*)(void*)>(original)(player);
            return count < 1 && g_holdLevelUp.load(std::memory_order_relaxed) ? 1 : count;
        }
    }

    bool HoldLevelUp(bool fighting)
    {
        g_holdLevelUp.store(fighting, std::memory_order_relaxed);
        static int hooked = -1;
        if (hooked >= 0)
            return hooked == 1;
        hooked = 0;
        for (uintptr_t site : addr::kLevelUpCombatCalls)
        {
            uintptr_t was = HookCall(site, reinterpret_cast<const void*>(&LevelUpCombatCount));
            if (!was)
            {
                LOG_WARN("Level up: %08X isn't a call; a friend's level-up screen may open mid-fight",
                    static_cast<unsigned>(site));
                return false;
            }
            if (was != addr::kPlayerCombatCount)
                LOG_INFO("Level up: another plugin hooks its combat check too (%08X); ours passes on to it",
                    static_cast<unsigned>(was));
            g_combatCount = was;
        }
        hooked = 1;
        return true;
    }

    bool SetMineRules(TESObjectREFR* const* puppets, size_t count, bool friendGame)
    {
        for (size_t i = 0; i < kMaxPuppets; ++i)
            g_minePuppets[i].store(i < count ? puppets[i] : nullptr, std::memory_order_relaxed);
        g_mineFriendGame.store(friendGame, std::memory_order_relaxed);
        if (g_mineReacts)
            return true;
        static bool tried = false;
        if (tried)
            return false;
        tried = true;
        uintptr_t was = HookCall(addr::kMineReactsCall, reinterpret_cast<const void*>(&MineReactsHook));
        if (!was)
        {
            LOG_WARN("Mines: couldn't hook their check (%08X isn't a call); friends' characters will set them off",
                static_cast<unsigned>(addr::kMineReactsCall));
            return false;
        }
        g_mineReacts = was;
        if (was != addr::kMineReactsToTarget)
            LOG_INFO("Mines: another plugin hooks their check too (%08X); ours passes on to it", static_cast<unsigned>(was));
        return true;
    }

    namespace
    {
        uintptr_t g_makeOptions = 0;
        bool g_optionsMade = false;
        void (*g_onHost)() = nullptr;
        void (*g_onJoin)() = nullptr;
        void __cdecl HostClicked() { if (g_onHost) g_onHost(); }
        void __cdecl JoinClicked() { if (g_onJoin) g_onJoin(); }

        struct SimpleArray
        {
            void* vtable;
            uint8_t** items;
            uint32_t count;
        };

        void* MakeOption(const char* name, void (*clicked)(), uint32_t flags = 1)
        {
            void* option = reinterpret_cast<void* (__cdecl*)(size_t)>(addr::kFormHeapAllocate)(0x10);
            if (!option)
                return nullptr;
            using Init = void*(__thiscall*)(void*, const char*, void (*)(), uint32_t);
            reinterpret_cast<Init>(addr::kStartMenuOptionInit)(option, name, clicked, flags);
            using Append = int(__thiscall*)(void*, void**);
            reinterpret_cast<Append>(addr::kSimpleArrayAppend)(reinterpret_cast<void*>(addr::kStartMenuOptions), &option);
            return option;
        }

        void __cdecl MakeOptions()
        {
            reinterpret_cast<void(__cdecl*)()>(g_makeOptions)();
            g_optionsMade = true;
            auto* list = reinterpret_cast<SimpleArray*>(addr::kStartMenuOptions);
            size_t load = list->count;
            for (uint32_t i = 0; i < list->count; ++i)
            {
                uint8_t* o = list->items[i];
                const char* name = o ? *reinterpret_cast<const char**>(o + 4) : nullptr;
                uintptr_t clicked = o ? *reinterpret_cast<uintptr_t*>(o + 8) : 0;
                LOG_DEBUG("Main menu option %u: %s (flags %X, function %08X)", i, name ? name : "?",
                    o ? *reinterpret_cast<uint32_t*>(o + 0xC) : 0, static_cast<unsigned>(clicked));
                if (clicked == addr::kLoadOption)
                    load = i + 1;
            }
            uint32_t before = list->count;
            MakeOption("Host Co-op", &HostClicked);
            MakeOption("Join Co-op", &JoinClicked);
            if (list->count != before + 2)
                return LOG_WARN("Main menu: couldn't add Host and Join");
            for (uint32_t k = 0; k < 2 && load < before; ++k)
            {
                uint8_t* ours = list->items[before + k];
                std::memmove(&list->items[load + k + 1], &list->items[load + k], (before - load) * sizeof(uint8_t*));
                list->items[load + k] = ours;
            }
            LOG_INFO("Main menu: added Host Co-op and Join Co-op (after option %u of %u)",
                static_cast<unsigned>(load < before ? load : before), before);
        }
    }

    bool MainMenuMade() { return g_optionsMade; }

    bool AddCoopToMainMenu(void (*onHost)(), void (*onJoin)())
    {
        if (g_makeOptions)
            return true;
        g_onHost = onHost;
        g_onJoin = onJoin;
        g_makeOptions = HookCall(addr::kMakeStartMenuOptionsCall, reinterpret_cast<const void*>(&MakeOptions));
        if (g_makeOptions && g_makeOptions != addr::kMakeStartMenuOptions)
            LOG_INFO("Main menu: another plugin makes its options too (%08X); ours are added after",
                static_cast<unsigned>(g_makeOptions));
        return g_makeOptions != 0;
    }

    bool InstallSayHooks()
    {
        if (g_pickLine)
            return true;
        uintptr_t pick = HookCall(addr::kPickLineCall, reinterpret_cast<const void*>(&PickLineHook));
        if (!pick)
            return false;
        g_pickLine = pick;
        if (pick != 0x0061A790)
            LOG_INFO("Dialogue: another plugin hooks the game's line picking too (%08X); ours passes on to it",
                static_cast<unsigned>(pick));
        static const uint8_t kStart[7] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC};
        g_firstResponse = HookStart(addr::kDialogueItemFirstResponse, kStart, sizeof(kStart),
            reinterpret_cast<const void*>(&FirstResponseHook));
        static const uint8_t kSoundStart[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C};
        if (!g_mainThread)
            g_mainThread = GetCurrentThreadId();
        g_setEndCallback = HookStart(addr::kSoundHandleSetEndCallback, kSoundStart, sizeof(kSoundStart),
            reinterpret_cast<const void*>(&SetEndCallbackHook));
        return g_firstResponse && g_setEndCallback;
    }

    std::vector<RadioLineStart> TakeRadioLineStarts() { return std::move(g_radioStarts); }

    bool TunePipboyRadio(TESObjectREFR* station)
    {
        if (!station || !station->baseForm || station->baseForm->typeID != kFormType_BGSTalkingActivator)
            return false;
        auto setOn = reinterpret_cast<void(__cdecl*)(bool)>(addr::kRadioSetOn);
        if (*reinterpret_cast<const uint8_t*>(addr::kRadioOn))
            setOn(false);
        setOn(true);
        reinterpret_cast<void(__cdecl*)(TESObjectREFR*, bool)>(addr::kRadioSetStation)(station, true);
        return true;
    }

    void RadioFromHost(bool on)
    {
        g_radioFromHost = on;
        if (!on)
            g_hostRadio.clear();
    }

    void HostRadioLine(const net::RadioLine& line)
    {
        if (g_hostRadio.size() >= 64 && !g_hostRadio.count(line.station))
            return;
        HostRadio& h = g_hostRadio[line.station];
        if (h.seq == line.seq && h.info == line.info)
            return;
        if (h.run.size() != line.run.size() ||
            !std::equal(h.run.begin(), h.run.end(), line.run.begin(),
                [](const net::RadioPick& x, const net::RadioPick& y) { return x.info == y.info && x.topic == y.topic; }))
        {
            h.run = line.run;
            h.handing = 0;
            h.handed = false;
            h.runUsed = false;
            h.rebuilds = 0;
        }
        h.info = line.info;
        h.part = line.part;
        h.seq = line.seq;
        h.hostStart = GetTickCount() - line.elapsed;
        h.aligned = false;
    }

    void UpdateRadios()
    {
        uint32_t now = GetTickCount();
        if (g_radioNow.size() > 128)
            g_radioNow.clear();
        auto seek = reinterpret_cast<void(__thiscall*)(Sound*, uint32_t)>(addr::kSoundHandleSeek);
        int stations = 0;
        for (auto* node = reinterpret_cast<RadioNode*>(addr::kActiveRadioStations); node && stations < 64;
             node = node->next, ++stations)
        {
            auto* st = static_cast<RadioStation*>(node->item);
            RadioLineStart read;
            if (!st || !st->station || !ReadStation(st, read))
                continue;
            RadioLineStart& was = g_radioNow[read.station];
            bool moved = read.info != was.info || read.part != was.part || read.line != was.line;
            if (moved)
            {
                was = read;
                if (g_radioStarts.size() < 64)
                    g_radioStarts.push_back(read);
            }
            if (!g_radioFromHost)
                continue;
            auto it = g_hostRadio.find(read.station);
            if (it == g_hostRadio.end())
                continue;
            HostRadio& h = it->second;
            if (moved)
                h.since = now;
            size_t hostLine = FindInRun(read.run, h.info);
            if (hostLine != std::string::npos && !h.runUsed)
            {
                h.runUsed = true;
                RadioLineStart got = read;
                got.fromHost = true;
                if (g_radioStarts.size() < 64)
                    g_radioStarts.push_back(got);
            }
            bool canPush = st->itemDuration > 0 && now - h.lastPush > 500;
            if (hostLine == std::string::npos)
            {
                if (canPush && h.rebuilds < 2)
                {
                    ++h.rebuilds;
                    h.lastPush = now;
                    h.handing = 0;
                    h.handed = false;
                    h.runUsed = false;
                    RadioNode* last = &st->conversation->items;
                    while (last->next && last->next->item)
                        last = last->next;
                    st->conversation->current = last;
                    st->itemDuration = 1;
                    st->itemStart = 0;
                    LOG_DEBUG("Radio: station %08X builds the host's run (%zu lines, %08X now)", read.station,
                        h.run.size(), h.info);
                }
                continue;
            }
            if (read.line < hostLine || (read.line == hostLine && read.part < h.part))
            {
                if (canPush)
                {
                    h.lastPush = now;
                    if (hostLine > read.line + 1)
                    {
                        size_t index = 0;
                        for (RadioNode* n = &st->conversation->items; n; n = n->next)
                            if (n->item && static_cast<const RadioItem*>(n->item)->info && index++ == hostLine - 1)
                            {
                                st->conversation->current = n;
                                break;
                            }
                    }
                    st->itemDuration = 1;
                    st->itemStart = 0;
                    LOG_DEBUG("Radio: station %08X goes on to the host's %08X part %u (from line %zu part %u)",
                        read.station, h.info, h.part, read.line, read.part);
                }
                continue;
            }
            if (read.line != hostLine || read.part != h.part || h.aligned || now - h.since < 200 ||
                st->itemDuration <= 1)
                continue;
            h.aligned = true;
            uint32_t target = now - h.hostStart;
            if (target < 1000 || target + 1000 >= static_cast<uint32_t>(st->itemDuration) || st->itemStart <= target)
                continue;
            st->itemStart -= target;
            int receivers = 0, sought = 0;
            bool pipboy = *reinterpret_cast<RadioStation**>(addr::kPipboyRadioStation) == st &&
                *reinterpret_cast<const uint8_t*>(addr::kRadioOn);
            if (pipboy)
                reinterpret_cast<void(__cdecl*)(TESObjectREFR*, bool)>(addr::kRadioSetStation)(st->station, true);
            for (RadioNode* u = &st->users; u && receivers < 32; u = u->next, ++receivers)
                if (auto* r = static_cast<RadioReceiver*>(u->item); r && IsPlaying(r->sound))
                {
                    seek(&r->sound, target);
                    ++sought;
                }
            LOG_DEBUG("Radio: station %08X caught up with the host's %08X part %u, %u ms in (Pip-Boy %d, %d radios)",
                read.station, h.info, h.part, target, pipboy, sought);
        }
    }

    bool SaidForOthers(const TESObjectREFR* speaker, const void* info)
    {
        uint32_t now = GetTickCount();
        for (const Said& s : g_said)
            if (s.speaker == speaker && s.info == info && static_cast<int32_t>(now - s.until) < 0)
                return true;
        return false;
    }

    bool SayLine(TESObjectREFR* speaker, uint32_t topicId, uint32_t infoId, int response, Sound& sound)
    {
        sound = Sound{};
        TESForm* topic = LookupForm(topicId);
        TESForm* info = static_cast<TESForm*>(TopicInfo(infoId));
        void* process = speaker ? At<void*>(speaker, layout::kActorProcess) : nullptr;
        if (!g_pickLine || !g_firstResponse || !process || !IsActor(speaker) ||
            !topic || topic->typeID != kFormType_TESTopic || !info || response < 0 || response > 255)
            return false;
        uint32_t now = GetTickCount();
        g_said.erase(std::remove_if(g_said.begin(), g_said.end(), [&](const Said& s) {
            return static_cast<int32_t>(now - s.until) >= 0 || s.speaker == speaker; }), g_said.end());
        g_said.push_back({speaker, info, now + kSaidMs});
        g_armed = Armed{topic, info, speaker, response, now + kArmedMs};
        using SayFn = void(__thiscall*)(void*, TESObjectREFR*, TESForm*, uint8_t, uint8_t, uint8_t, uint8_t);
        SayFn say = reinterpret_cast<SayFn>((*reinterpret_cast<void***>(process))[addr::kProcessSaySlot]);
        auto* general = reinterpret_cast<uint8_t*>(addr::kSettingGeneralSubtitles + 4);
        const auto* dialogue = reinterpret_cast<const uint8_t*>(addr::kSettingDialogueSubtitles + 4);
        uint8_t was = *general;
        *general = *dialogue;
        say(process, speaker, topic, 0, 0, 1, 1);
        *general = was;
        std::lock_guard<std::mutex> lock(g_voicesLock);
        auto voice = g_voices.find(speaker->refID);
        if (voice != g_voices.end())
            sound = voice->second;
        return true;
    }

    bool StopSpeaking(const TESObjectREFR* speaker)
    {
        Sound voice;
        {
            std::lock_guard<std::mutex> lock(g_voicesLock);
            auto it = speaker ? g_voices.find(speaker->refID) : g_voices.end();
            if (it == g_voices.end())
                return false;
            voice = it->second;
            g_voices.erase(it);
        }
        bool playing = IsPlaying(voice);
        StopSound(voice);
        return playing;
    }

    bool IsPlaying(Sound& sound)
    {
        return sound.id != 0xFFFFFFFF && reinterpret_cast<bool(__thiscall*)(Sound*)>(addr::kSoundHandleIsPlaying)(&sound);
    }

    void EndSubtitle(const std::string& text)
    {
        const char* hud = *reinterpret_cast<const char* const*>(addr::kHudMainMenu);
        if (!hud || text.empty())
            return;
        auto end = [&](char* subtitle) {
            if (subtitle && std::strncmp(subtitle, text.c_str(), 0x103) == 0)
                *reinterpret_cast<float*>(subtitle + layout::kSubtitleShowFor) = 0;
        };
        end(At<char*>(hud, layout::kHudSubtitleShown));
        char* const* data = At<char* const*>(hud, layout::kHudSubtitles + 4);
        uint32_t size = At<uint32_t>(hud, layout::kHudSubtitles + 8);
        for (uint32_t i = 0; data && i < size && i < 64; ++i)
            end(data[i]);
    }

    void StopSound(Sound& sound)
    {
        if (sound.id != 0xFFFFFFFF)
            reinterpret_cast<bool(__thiscall*)(Sound*)>(addr::kSoundHandleStop)(&sound);
        sound = Sound{};
    }

    void ShowSubtitle(TESObjectREFR* speaker, const char* text, bool always)
    {
        struct SoundHandle { uint32_t id = 0xFFFFFFFF; uint8_t assumeSuccess = 0; uint32_t state = 0; };
        struct Point { float x, y, z; };
        using Fn = bool(__cdecl*)(const char*, SoundHandle, Point, TESObjectREFR*, bool);
        if (!speaker || !text || !*text)
            return;
        auto* general = reinterpret_cast<uint8_t*>(addr::kSettingGeneralSubtitles + 4);
        const auto* dialogue = reinterpret_cast<const uint8_t*>(addr::kSettingDialogueSubtitles + 4);
        if (!*dialogue && !always)
            return;
        uint8_t was = *general;
        *general = 1;
        reinterpret_cast<Fn>(addr::kShowSubtitle)(text, SoundHandle{}, Point{speaker->posX, speaker->posY,
            speaker->posZ}, speaker, false);
        *general = was;
    }

    void* TopicInfo(uint32_t id)
    {
        TESForm* form = LookupForm(id);
        return form && form->typeID == kFormType_TESTopicInfo ? form : nullptr;
    }

    uint32_t TopicInfoId(const void* info)
    {
        return info ? static_cast<const TESForm*>(info)->refID : 0;
    }

    std::string TopicInfoPrompt(const void* info)
    {
        const char* text = info ? static_cast<const TESTopicInfo*>(info)->prompt.m_data : nullptr;
        return text ? text : "";
    }

    std::string PlayerReplyText(uint32_t topic, uint32_t info)
    {
        constexpr uint8_t kTopicTypeTopic = 0;
        const TESForm* form = LookupForm(topic);
        if (!form || form->typeID != kFormType_TESTopic ||
            static_cast<const TESTopic*>(form)->type != kTopicTypeTopic)
            return "";
        std::string prompt = TopicInfoPrompt(TopicInfo(info));
        if (!prompt.empty())
            return prompt;
        const char* name = static_cast<const TESTopic*>(form)->fullName.name.m_data;
        return name ? name : "";
    }

    bool HasResultScript(void* info, bool onEnd)
    {
        using Fn = void*(__thiscall*)(void*, uint32_t);
        return info && reinterpret_cast<Fn>(addr::kTopicInfoGetResultScript)(info, onEnd ? 1 : 0) != nullptr;
    }

    uint16_t ScreenCommandIn(void* info, bool onEnd, int* statements)
    {
        if (statements)
            *statements = 0;
        using Fn = Script*(__thiscall*)(void*, uint32_t);
        Script* script = info ? reinterpret_cast<Fn>(addr::kTopicInfoGetResultScript)(info, onEnd ? 1 : 0) : nullptr;
        if (!script || !script->data)
            return 0;
        const uint8_t* data = script->data;
        uint32_t size = script->info.dataLength;
        auto opensScreen = [](uint16_t op) {
            for (uint16_t o : opcodes::kOpensScreen)
                if (o == op)
                    return true;
            return false;
        };
        auto u16 = [&](uint32_t at) { return static_cast<uint16_t>(data[at] | (data[at + 1] << 8)); };
        std::string ops;
        int count = 0;
        uint32_t at = 0;
        while (at + 4 <= size)
        {
            uint16_t op = u16(at);
            if (op == opcodes::kOnReference)
            {
                at += 4;
                continue;
            }
            uint16_t length = u16(at + 2);
            char b[8];
            std::snprintf(b, sizeof(b), "%X ", op);
            ops += b;
            ++count;
            if (opensScreen(op))
                return op;
            at += 4 + length;
        }
        if (statements)
            *statements = at == size ? count : -1;
        if (at == size)
            return 0;
        LOG_DEBUG("Dialogue: line %08X's %s script: %u bytes, statements %s(didn't add up)",
            static_cast<TESForm*>(info)->refID, onEnd ? "end" : "start", size, ops.c_str());
        for (uint32_t i = 0; i + 1 < size; ++i)
            if (opensScreen(u16(i)))
                return u16(i);
        return 0;
    }

    void CloseMenu()
    {
        if (g_closeMenu && Player())
            g_scripts->CallFunctionAlt(g_closeMenu, Player(), 0);
    }

    std::vector<EnableChange> TakeEnables()
    {
        return std::move(g_enables);
    }

    void SetEnabled(TESObjectREFR* ref, bool enabled)
    {
        Script* s = enabled ? g_enable : g_disable;
        if (s && ref)
            g_scripts->CallFunctionAlt(s, ref, 0);
    }

    bool IsEnabled(const TESObjectREFR* ref)
    {
        return ref && !(ref->flags & layout::kRefDisabled);
    }

    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> PlayerObjectives(size_t& targets)
    {
        std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> out;
        targets = 0;
        TESObjectREFR* player = Player();
        if (!player)
            return out;
        ForEachIn(reinterpret_cast<char*>(player) + layout::kPlayerObjectives, [&](void* o)
        {
            const TESForm* quest = At<const TESForm*>(o, 0x10);
            out.emplace_back(quest ? quest->refID : 0, At<uint32_t>(o, layout::kObjectiveId),
                At<uint32_t>(o, layout::kObjectiveStatus));
        });
        ForEachIn(reinterpret_cast<char*>(player) + layout::kPlayerTargets, [&](void*) { ++targets; });
        return out;
    }

    uint32_t TrackedQuest()
    {
        TESObjectREFR* player = Player();
        TESForm* quest = player ? At<TESForm*>(player, layout::kPlayerActiveQuest) : nullptr;
        return quest ? quest->refID : 0;
    }

    void ItemsNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(ItemType, 0, out, range);
    }

    WorldItem ReadWorldItem(TESObjectREFR* ref)
    {
        WorldItem w;
        if (!ref || !ref->baseForm)
            return w;
        w.base = ref->baseForm->refID;
        w.flags = ref->flags;
        w.inCell = ref->parentCell != nullptr;
        w.x = ref->posX;
        w.y = ref->posY;
        w.z = ref->posZ;
        w.rx = std::remainder(ref->rotX, 6.2831853f);
        w.ry = std::remainder(ref->rotY, 6.2831853f);
        w.rz = std::remainder(ref->rotZ, 6.2831853f);
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        if (const void* count = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
            layout::kExtraCount))
            w.count = At<int16_t>(count, layout::kCountValue);
        return w;
    }

    TESObjectREFR* OpenContainer()
    {
        const void* menu = *reinterpret_cast<const void* const*>(addr::kContainerMenu);
        if (menu)
            return At<TESObjectREFR*>(menu, layout::kContainerMenuRef);
        const void* barter = *reinterpret_cast<const void* const*>(addr::kBarterMenu);
        TESObjectREFR* merchant = barter ? At<TESObjectREFR*>(barter, layout::kBarterMenuMerchant) : nullptr;
        if (!merchant)
            return nullptr;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&merchant->extraDataList,
            layout::kExtraMerchantContainer);
        return extra ? At<TESObjectREFR*>(extra, layout::kMerchantContainerRef) : nullptr;
    }

    TESObjectREFR* CrosshairRef()
    {
        NVSEArrayVarInterface::Element result;
        if (!g_crosshair || !Player() || !g_scripts->CallFunction(g_crosshair, Player(), nullptr, &result, 0))
            return nullptr;
        TESForm* at = result.GetTESForm();
        return at ? LookupRef(at->refID) : nullptr;
    }

    bool Contents(TESObjectREFR* ref, std::vector<Item>& out)
    {
        out.clear();
        NVSEArrayVarInterface::Element result;
        if (!g_contents || !g_arrays || !ref || !g_scripts->CallFunction(g_contents, ref, nullptr, &result, 0))
            return false;
        auto* array = result.GetArray();
        if (!array)
            return false;
        uint32_t size = g_arrays->GetArraySize(array);
        if (size == static_cast<uint32_t>(-1) || size % 4 || size > 4 * kMaxItemKinds)
            return false;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
            return false;
        for (uint32_t i = 0; i < size; i += 4)
        {
            uint32_t id = elements[i].GetFormID();
            double count = elements[i + 1].GetNumber(), health = elements[i + 2].GetNumber(),
                mods = elements[i + 3].GetNumber();
            if (!id || count < 1)
                continue;
            Item item{id, static_cast<int32_t>(count), health >= 0 && health <= 100 ? static_cast<uint8_t>(health) :
                net::kNoCondition, mods >= 0 && mods <= 7 ? static_cast<uint8_t>(mods) : uint8_t(0)};
            auto same = std::find_if(out.begin(), out.end(), [&](const Item& o) {
                return o.id == item.id && o.health == item.health && o.mods == item.mods; });
            if (same != out.end())
                same->count += item.count;
            else
                out.push_back(item);
        }
        return out.size() <= kMaxItemKinds;
    }

    void ChangeItem(TESObjectREFR* ref, uint32_t itemId, int32_t delta)
    {
        TESForm* item = LookupForm(itemId);
        if (g_addItem && ref && item && delta)
            g_scripts->CallFunctionAlt(g_addItem, ref, 2, item, static_cast<uint32_t>(delta));
    }

    void ChangeItem(TESObjectREFR* ref, const net::ItemCount& kind)
    {
        if (kind.health == net::kNoCondition || !g_changeKind)
            return ChangeItem(ref, kind.item, kind.count);
        TESForm* item = LookupForm(kind.item);
        if (ref && item && kind.count)
            g_scripts->CallFunctionAlt(g_changeKind, ref, 4, item, static_cast<uint32_t>(kind.count),
                static_cast<uint32_t>(kind.health), static_cast<uint32_t>(kind.mods));
    }

    void Activate(TESObjectREFR* ref)
    {
        if (g_activate && ref)
            g_scripts->CallFunctionAlt(g_activate, ref, 0);
    }

    bool IsItem(uint32_t formId)
    {
        TESForm* form = LookupForm(formId);
        using IsInventoryTypeFn = bool(__cdecl*)(UInt32 formType);
        return form && reinterpret_cast<IsInventoryTypeFn>(addr::kIsInventoryObjectType)(form->typeID);
    }

    bool IsLootable(TESObjectREFR* ref)
    {
        if (!ref || !ref->baseForm)
            return false;
        return ref->baseForm->typeID == kFormType_TESObjectCONT || (IsActor(ref) && IsDead(ref));
    }

    TESObjectREFR* TopEnableParent(TESObjectREFR* ref)
    {
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        TESObjectREFR* top = nullptr;
        for (int depth = 0; ref && depth < 8; ++depth)
        {
            const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
                layout::kExtraEnableStateParent);
            TESObjectREFR* parent = extra ? At<TESObjectREFR*>(extra, layout::kEnableParentRef) : nullptr;
            if (!parent || parent == ref)
                break;
            top = ref = parent;
        }
        return top;
    }

    bool IsThere(const WorldItem& item)
    {
        return item.base && !(item.flags & (layout::kRefDeleted | layout::kRefDisabled));
    }

    Condition ItemCondition(TESObjectREFR* ref)
    {
        Condition c;
        NVSEArrayVarInterface::Element result;
        if (!g_itemCondition || !ref || !ref->baseForm || (ref->baseForm->typeID != kFormType_TESObjectWEAP &&
            ref->baseForm->typeID != kFormType_TESObjectARMO) ||
            !g_scripts->CallFunction(g_itemCondition, ref, nullptr, &result, 0))
            return c;
        int n = static_cast<int>(result.GetNumber());
        if (n >= 0 && n % 256 <= 100)
            c.health = static_cast<uint8_t>(n % 256);
        if (n >= 0 && n / 256 <= 7)
            c.mods = static_cast<uint8_t>(n / 256);
        return c;
    }

    namespace
    {
        void SetWorldWeaponMods(TESObjectREFR* ref, uint8_t mods)
        {
            if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESObjectWEAP)
                return;
            for (uint32_t slot = 0; slot < 3; ++slot)
                if (!At<const void*>(ref->baseForm, layout::kWeaponMods + 4 * slot))
                    mods &= ~(1u << slot);
            if (!mods)
                return;
            using GetByTypeFn = void*(__thiscall*)(void* list, uint8_t type);
            if (void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
                layout::kExtraWeaponModFlags))
                *(static_cast<uint8_t*>(extra) + layout::kWeaponModFlagsValue) = mods;
            else
            {
                auto* made = static_cast<uint32_t*>(reinterpret_cast<void* (__cdecl*)(size_t)>(addr::kFormHeapAllocate)(
                    layout::kExtraWeaponModFlagsSize));
                if (!made)
                    return;
                made[0] = static_cast<uint32_t>(layout::kVtblExtraWeaponModFlags);
                made[1] = layout::kExtraWeaponModFlags;
                made[2] = 0;
                made[3] = 0;
                *(reinterpret_cast<uint8_t*>(made) + layout::kWeaponModFlagsValue) = mods;
                using AddFn = void*(__thiscall*)(void* list, void* extra);
                if (!reinterpret_cast<AddFn>(addr::kExtraListAdd)(&ref->extraDataList, made))
                    return;
            }
            if (g_update3D)
                g_scripts->CallFunctionAlt(g_update3D, ref, 0);
        }
    }

    TESObjectREFR* PlaceItem(uint32_t base, int count, float x, float y, float z, uint8_t health, uint8_t mods)
    {
        TESForm* form = LookupForm(base);
        NVSEArrayVarInterface::Element result;
        if (!g_placeItem || !Player() || !form || !IsItem(base) || count < 1 ||
            !g_scripts->CallFunction(g_placeItem, Player(), nullptr, &result, 6, form, static_cast<uint32_t>(count),
                FloatArg(x), FloatArg(y), FloatArg(z), static_cast<uint32_t>(health)))
            return nullptr;
        TESForm* made = result.GetTESForm();
        TESObjectREFR* ref = made ? LookupRef(made->refID) : nullptr;
        if (ref && mods)
            SetWorldWeaponMods(ref, mods);
        return ref;
    }

    void MoveItem(TESObjectREFR* ref, float x, float y, float z, float rx, float ry, float rz)
    {
        constexpr float kDegrees = 57.2957795f;
        if (g_moveItem && ref)
            g_scripts->CallFunctionAlt(g_moveItem, ref, 6, FloatArg(x), FloatArg(y), FloatArg(z), FloatArg(rx * kDegrees),
                FloatArg(ry * kDegrees), FloatArg(rz * kDegrees));
    }

    std::vector<uint32_t> Followers()
    {
        std::vector<uint32_t> out;
        NVSEArrayVarInterface::Element result;
        if (!g_followers || !g_arrays || !Player() || !g_scripts->CallFunction(g_followers, Player(), nullptr, &result, 0))
            return out;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (size == static_cast<uint32_t>(-1) || size > 64)
            return out;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
            return out;
        for (const auto& e : elements)
            if (uint32_t id = e.GetFormID())
                out.push_back(id);
        return out;
    }

    namespace
    {
        std::vector<uint32_t> FormList(Script* script, TESObjectREFR* on)
        {
            std::vector<uint32_t> out;
            NVSEArrayVarInterface::Element result;
            if (!script || !g_arrays || !on || !g_scripts->CallFunction(script, on, nullptr, &result, 0))
                return out;
            auto* array = result.GetArray();
            uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
            if (size == static_cast<uint32_t>(-1) || size > 64)
                return out;
            std::vector<NVSEArrayVarInterface::Element> elements(size);
            if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
                return out;
            for (const auto& e : elements)
                if (uint32_t id = e.GetFormID())
                    out.push_back(id);
            return out;
        }

        uint32_t FormResult(Script* script, TESObjectREFR* on)
        {
            NVSEArrayVarInterface::Element result;
            if (!script || !on || !g_scripts->CallFunction(script, on, nullptr, &result, 0))
                return 0;
            TESForm* form = result.GetTESForm();
            return form ? form->refID : 0;
        }
    }

    std::vector<uint32_t> Teammates() { return FormList(g_teammates, Player()); }

    namespace
    {
        using Kind = net::PlayerCommandKind;
        bool g_mirroring = false;
        bool g_screenFromHost = false;
        int g_inLine = 0;
        std::vector<net::PlayerCommand> g_playerCommands;
        std::set<const Script*> g_reportedScripts;

        struct Watched
        {
            const char* name;
            Kind kind;
            Cmd_Execute original = nullptr;
            bool kill = false;
            bool popup = false;
            uint32_t params = 0;
        };
        bool g_skipPopups = false;
        std::set<uint32_t> g_popupMessages;
        std::set<uint32_t> g_popupScripts;
        bool g_scriptKill = false;
        Watched g_watched[] = {
            {"AddItem", Kind::AddItem}, {"AddItemHealthPercent", Kind::AddItem}, {"RemoveItem", Kind::RemoveItem},
            {"RemoveAllItems", Kind::RemoveAllItems}, {"RemoveAllTypedItems", Kind::RemoveAllTypedItems},
            {"EquipItem", Kind::EquipItem}, {"UnequipItem", Kind::UnequipItem}, {"AddPerk", Kind::AddPerk},
            {"RemovePerk", Kind::RemovePerk}, {"AddNote", Kind::AddNote}, {"RemoveNote", Kind::RemoveNote},
            {"ApplyImageSpaceModifier", Kind::AddImageSpaceModifier},
            {"RemoveImageSpaceModifier", Kind::RemoveImageSpaceModifier},
            {"DisablePlayerControls", Kind::DisableControls}, {"EnablePlayerControls", Kind::EnableControls},
            {"CastImmediateOnSelf", Kind::CastOnSelf}, {"AddScriptPackage", Kind::AddPlayerPackage},
            {"RemoveScriptPackage", Kind::RemovePlayerPackage}, {"Kill", Kind::AddItem, nullptr, true},
            {"KillActor", Kind::AddItem, nullptr, true}, {"ShowMessage", Kind::AddItem, nullptr, false, true}};

        uint32_t FormIdOf(const TESForm* f) { return f ? f->refID : 0; }

        bool IsScreenOrControls(Kind k, const TESObjectREFR* on)
        {
            return k == Kind::AddImageSpaceModifier || k == Kind::RemoveImageSpaceModifier || k == Kind::DisableControls ||
                k == Kind::EnableControls ||
                ((k == Kind::AddPlayerPackage || k == Kind::RemovePlayerPackage) && on && on == Player());
        }
        bool TakesAway(Kind k)
        {
            return k == Kind::AddImageSpaceModifier || k == Kind::DisableControls || k == Kind::AddPlayerPackage;
        }

        bool IsWholeInventory(Kind k, const TESObjectREFR* on)
        {
            return (k == Kind::RemoveAllItems || k == Kind::RemoveAllTypedItems) && on && on == Player() && !g_inLine;
        }

        bool Note(const Watched& w, ParamInfo* paramInfo, void* scriptData, TESObjectREFR* thisObj, Script* scriptObj,
            ScriptEventList* eventList, UInt32* opcodeOffsetPtr, net::PlayerCommand& c)
        {
            if (g_mirroring && scriptObj && !g_ourScripts.count(scriptObj) && g_reportedScripts.size() < 256 &&
                g_reportedScripts.insert(scriptObj).second)
                LOG_DEBUG("Scripted: %s from script %08X (type %u)%s", w.name, scriptObj->refID, scriptObj->info.type,
                    g_inLine ? " in a dialogue line" : "");
            bool anyScript = IsScreenOrControls(w.kind, thisObj) || IsWholeInventory(w.kind, thisObj) ||
                (w.kind == Kind::RemoveAllItems && thisObj && thisObj != Player() && !g_inLine);
            if (!g_mirroring || !scriptObj || g_ourScripts.count(scriptObj) || g_playerCommands.size() >= 128 ||
                (!anyScript && (g_inLine || !(scriptObj->info.type == Script::eType_Quest || scriptObj->refID == 0 ||
                    (scriptObj->refID >> 24) == 0xFF))))
                return false;
            TESObjectREFR* player = Player();
            c = net::PlayerCommand{};
            c.kind = w.kind;
            UInt32 offset = *opcodeOffsetPtr;
            TESForm* form = nullptr;
            TESObjectREFR* ref = nullptr;
            UInt32 a = 0, b = 0, d = 0;
            float health = 1;
            switch (w.kind)
            {
            case Kind::AddItem:
            case Kind::RemoveItem:
            {
                a = 1;
                bool withHealth = std::strcmp(w.name, "AddItemHealthPercent") == 0;
                if (thisObj != player || !(withHealth ?
                    g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form, &a, &health, &b) :
                    g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form, &a, &b)))
                    return false;
                c.form = FormIdOf(form);
                c.n[0] = static_cast<int32_t>(a);
                c.n[1] = static_cast<int32_t>(b);
                break;
            }
            case Kind::RemoveAllItems:
                if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &ref, &a))
                    return false;
                if (thisObj == player && ref)
                    c.ref = RefId(ref);
                else if (ref == player && thisObj)
                {
                    c.kind = Kind::GiveAllItems;
                    c.ref = RefId(thisObj);
                }
                else
                    return false;
                break;
            case Kind::RemoveAllTypedItems:
                if (thisObj != player ||
                    !g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &ref, &a, &b, &d) || !ref)
                    return false;
                c.ref = RefId(ref);
                c.n[0] = static_cast<int32_t>(a);
                c.n[1] = static_cast<int32_t>(b);
                c.n[2] = static_cast<int32_t>(d);
                break;
            case Kind::EquipItem:
            case Kind::UnequipItem:
                if (thisObj != player ||
                    !g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form, &a, &b))
                    return false;
                c.form = FormIdOf(form);
                c.n[0] = static_cast<int32_t>(a);
                c.n[1] = static_cast<int32_t>(b);
                break;
            case Kind::AddPerk:
            case Kind::RemovePerk:
            case Kind::AddNote:
            case Kind::RemoveNote:
            case Kind::CastOnSelf:
                if (thisObj != player ||
                    !g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form, &a))
                    return false;
                c.form = FormIdOf(form);
                break;
            case Kind::AddImageSpaceModifier:
            case Kind::RemoveImageSpaceModifier:
                if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form, &a))
                    return false;
                c.form = FormIdOf(form);
                break;
            case Kind::AddPlayerPackage:
                if (thisObj != player || !g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &form))
                    return false;
                c.form = FormIdOf(form);
                break;
            case Kind::RemovePlayerPackage:
                if (thisObj != player)
                    return false;
                break;
            case Kind::DisableControls:
            case Kind::EnableControls:
            {
                bool off = w.kind == Kind::DisableControls;
                UInt32 v[7] = {1, 1, 1, 1, off ? 0u : 1u, off ? 0u : 1u, off ? 0u : 1u};
                if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &v[0], &v[1], &v[2],
                    &v[3], &v[4], &v[5], &v[6]))
                    return false;
                for (int i = 0; i < 7; ++i)
                    if (v[i])
                        c.flags |= static_cast<uint8_t>(1 << i);
                break;
            }
            }
            if (net::IsMadeWhilePlaying(c.form) || net::IsMadeWhilePlaying(c.ref))
                return false;
            return true;
        }

        void Relay(const Watched& w, const net::PlayerCommand& c)
        {
            LOG_DEBUG("Scripted: friends follow %s %08X %08X", w.name, c.form, c.ref);
            g_playerCommands.push_back(c);
        }

        int PerkRanks(uint32_t perk)
        {
            TESForm* form = LookupForm(perk);
            NVSEArrayVarInterface::Element result;
            if (!g_perkRanks || !form || !Player() ||
                !g_scripts->CallFunction(g_perkRanks, Player(), nullptr, &result, 1, form))
                return -1;
            return static_cast<int>(result.GetNumber());
        }

        template <int I>
        bool WatchedCommand(COMMAND_ARGS)
        {
            if (g_watched[I].popup)
            {
                if (g_skipPopups && scriptObj && !g_popupMessages.empty() && g_watched[I].params <= 10)
                {
                    UInt32 offset = *opcodeOffsetPtr;
                    TESForm* message = nullptr;
                    float v[9] = {};
                    if (g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &message, &v[0],
                            &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) &&
                        message && g_popupMessages.count(message->refID))
                    {
                        LOG_INFO("Popups: the intro box %08X skipped", message->refID);
                        *result = 0;
                        return true;
                    }
                }
                return g_watched[I].original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                    opcodeOffsetPtr);
            }
            if (g_skipPopups && g_watched[I].kind == Kind::AddItem && scriptObj && thisObj && thisObj == Player() &&
                g_popupScripts.count(scriptObj->refID) && g_pcAddItem && std::strcmp(g_watched[I].name, "AddItem") == 0)
            {
                UInt32 offset = *opcodeOffsetPtr, count = 1, hide = 0;
                TESForm* item = nullptr;
                if (g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &item, &count, &hide) &&
                    item)
                {
                    g_scripts->CallFunctionAlt(g_pcAddItem, thisObj, 3, item, count, 1u);
                    *result = 1;
                    return true;
                }
            }
            if (g_watched[I].kill)
            {
                if (thisObj && thisObj == Player() && scriptObj && !g_ourScripts.count(scriptObj))
                    g_scriptKill = true;
                return g_watched[I].original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                    opcodeOffsetPtr);
            }
            if (g_screenFromHost && ((IsScreenOrControls(g_watched[I].kind, thisObj) && TakesAway(g_watched[I].kind)) ||
                IsWholeInventory(g_watched[I].kind, thisObj)) && scriptObj && !g_ourScripts.count(scriptObj))
            {
                static std::set<const Script*> s_blocked;
                if (s_blocked.size() < 256 && s_blocked.insert(scriptObj).second)
                    LOG_INFO("Scripted: %s from script %08X not done here (the host's game decides that)",
                        g_watched[I].name, scriptObj->refID);
                *result = 0;
                return true;
            }
            net::PlayerCommand c;
            bool noted = Note(g_watched[I], paramInfo, scriptData, thisObj, scriptObj, eventList, opcodeOffsetPtr, c);
            bool perk = noted && (c.kind == Kind::AddPerk || c.kind == Kind::RemovePerk);
            int ranks = perk ? PerkRanks(c.form) : -1;
            if (noted && !perk)
                Relay(g_watched[I], c);
            bool done = g_watched[I].original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                opcodeOffsetPtr);
            if (perk && (ranks < 0 || PerkRanks(c.form) != ranks))
                Relay(g_watched[I], c);
            return done;
        }
        template <int... I>
        constexpr std::array<Cmd_Execute, sizeof...(I)> Wrappers(std::integer_sequence<int, I...>)
        {
            return {&WatchedCommand<I>...};
        }
        constexpr int kWatched = sizeof(g_watched) / sizeof(g_watched[0]);
    }

    bool WatchPlayerCommands()
    {
        static bool s_done = false;
        if (s_done)
            return true;
        if (!g_cmdTable)
            return false;
        static const std::array<Cmd_Execute, kWatched> kWrappers = Wrappers(std::make_integer_sequence<int, kWatched>());
        int watched = 0;
        for (int i = 0; i < kWatched; ++i)
        {
            auto* info = const_cast<CommandInfo*>(g_cmdTable->GetByName(g_watched[i].name));
            if (!info || !info->execute)
            {
                LOG_WARN("Scripted: no %s command to watch", g_watched[i].name);
                continue;
            }
            g_watched[i].original = info->execute;
            g_watched[i].params = info->numParams;
            if (g_watched[i].popup)
                LOG_INFO("Popups: ShowMessage has %u parameters%s", info->numParams,
                    info->numParams <= 10 ? "" : " (more than we read: the DLCs' boxes aren't skipped)");
            info->execute = kWrappers[i];
            ++watched;
        }
        s_done = true;
        LOG_INFO("Scripted: watching %d script commands (what quest scripts do to our player, for friends)", watched);
        return watched > 0;
    }

    void ScreenFromHost(bool on) { g_screenFromHost = on; }

    void SkipIntroPopups(bool on, const std::vector<std::string>& loadOrder)
    {
        g_skipPopups = on;
        g_popupMessages.clear();
        g_popupScripts.clear();
        if (!on)
            return;
        for (uint32_t id : ids::kPackPopups)
            g_popupMessages.insert(id);
        for (uint32_t id : ids::kPackScripts)
            g_popupScripts.insert(id);
        for (const ids::DlcPopups& dlc : ids::kDlcPopups)
            for (size_t i = 0; i < loadOrder.size() && i < 0xFF; ++i)
                if (_stricmp(loadOrder[i].c_str(), dlc.file) == 0)
                {
                    g_popupMessages.insert(static_cast<uint32_t>(i) << 24 | dlc.broadcast);
                    g_popupMessages.insert(static_cast<uint32_t>(i) << 24 | dlc.loaded);
                }
        LOG_INFO("Popups: the DLCs' intro boxes are skipped (%zu)", g_popupMessages.size());
    }

    bool TakeScriptKill()
    {
        bool killed = g_scriptKill;
        g_scriptKill = false;
        return killed;
    }

    void MirrorPlayerCommands(bool on)
    {
        g_mirroring = on;
        if (!on)
            g_playerCommands.clear();
    }

    void DialogueLineRunning(bool running) { g_inLine += running ? 1 : -1; }

    std::vector<net::PlayerCommand> TakePlayerCommands() { return std::move(g_playerCommands); }

    bool ApplyPlayerCommand(const net::PlayerCommand& c)
    {
        TESObjectREFR* player = Player();
        if (!g_compiled || !player)
            return false;
        TESForm* form = c.form ? LookupForm(c.form) : nullptr;
        TESObjectREFR* ref = c.ref ? LookupRef(c.ref) : nullptr;
        auto isA = [&](uint8_t type) { return form && form->typeID == type; };
        bool container = ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectCONT;
        auto n = [&](int i) { return static_cast<uint32_t>(c.n[i]); };
        switch (c.kind)
        {
        case Kind::AddItem:
            if (!form || !IsItem(c.form) || c.n[0] < 1)
                return false;
            g_scripts->CallFunctionAlt(g_pcAddItem, player, 3, form, n(0), n(1));
            return true;
        case Kind::RemoveItem:
            if (!form || !IsItem(c.form) || c.n[0] < 1)
                return false;
            g_scripts->CallFunctionAlt(g_pcRemoveItem, player, 3, form, n(0), n(1));
            return true;
        case Kind::RemoveAllItems:
            if (!container)
                return false;
            g_scripts->CallFunctionAlt(g_pcRemoveAll, player, 1, ref);
            return true;
        case Kind::RemoveAllTypedItems:
            if (!container)
                return false;
            g_scripts->CallFunctionAlt(g_pcRemoveAllTyped, player, 4, ref, n(0), n(1), n(2));
            return true;
        case Kind::GiveAllItems:
            if (!container)
                return false;
            g_scripts->CallFunctionAlt(g_pcGiveAll, ref, 0);
            return true;
        case Kind::EquipItem:
        case Kind::UnequipItem:
            if (!form || !IsItem(c.form))
                return false;
            g_scripts->CallFunctionAlt(c.kind == Kind::EquipItem ? g_pcEquip : g_pcUnequip, player, 3, form, n(0), n(1));
            return true;
        case Kind::AddPerk:
        case Kind::RemovePerk:
            if (!isA(kFormType_BGSPerk))
                return false;
            g_scripts->CallFunctionAlt(c.kind == Kind::AddPerk ? g_pcAddPerk : g_pcRemovePerk, player, 1, form);
            return true;
        case Kind::AddNote:
        case Kind::RemoveNote:
            if (!isA(kFormType_BGSNote))
                return false;
            g_scripts->CallFunctionAlt(c.kind == Kind::AddNote ? g_pcAddNote : g_pcRemoveNote, player, 1, form);
            return true;
        case Kind::AddImageSpaceModifier:
        case Kind::RemoveImageSpaceModifier:
            if (!isA(kFormType_TESImageSpaceModifier))
                return false;
            g_scripts->CallFunctionAlt(c.kind == Kind::AddImageSpaceModifier ? g_pcAddImod : g_pcRemoveImod, player, 1, form);
            return true;
        case Kind::DisableControls:
        case Kind::EnableControls:
        {
            uint32_t v[7];
            for (int i = 0; i < 7; ++i)
                v[i] = (c.flags >> i) & 1;
            g_scripts->CallFunctionAlt(c.kind == Kind::DisableControls ? g_pcDisable : g_pcEnable, player, 7, v[0], v[1],
                v[2], v[3], v[4], v[5], v[6]);
            return true;
        }
        case Kind::CastOnSelf:
            if (!isA(kFormType_SpellItem))
                return false;
            g_scripts->CallFunctionAlt(g_pcCast, player, 1, form);
            return true;
        case Kind::AddPlayerPackage:
            if (!isA(kFormType_TESPackage))
                return false;
            g_scripts->CallFunctionAlt(g_pcAddPackage, player, 1, form);
            return true;
        case Kind::RemovePlayerPackage:
            g_scripts->CallFunctionAlt(g_pcRemovePackage, player, 0);
            return true;
        }
        return false;
    }

    bool IsWaiting(TESObjectREFR* ref) { return ref && Number(g_isWaiting, ref) != 0; }

    void SetWaiting(TESObjectREFR* ref, bool waiting)
    {
        if (g_setWaiting && ref)
            g_scripts->CallFunctionAlt(g_setWaiting, ref, 1, static_cast<uint32_t>(waiting ? 1 : 0));
    }

    TESForm* FollowPackage(TESObjectREFR* target)
    {
        TESForm* original = LookupForm(ids::kDefaultFollowPlayerClose);
        NVSEArrayVarInterface::Element result;
        if (!g_followPackage || !original || !target || !Player() ||
            !g_scripts->CallFunction(g_followPackage, Player(), nullptr, &result, 2, original, target))
            return nullptr;
        return result.GetTESForm();
    }

    void GivePackage(TESObjectREFR* ref, TESForm* package)
    {
        if (g_givePackage && ref && package)
            g_scripts->CallFunctionAlt(g_givePackage, ref, 1, package);
    }

    void DropPackage(TESObjectREFR* ref)
    {
        if (g_dropPackage && ref)
            g_scripts->CallFunctionAlt(g_dropPackage, ref, 0);
    }

    uint32_t CurrentPackage(TESObjectREFR* ref) { return FormResult(g_currentPackage, ref); }

    bool InCombat(TESObjectREFR* ref) { return ref && Number(g_inCombat, ref) != 0; }

    bool CompanionWheelOpen() { return Player() && Number(g_wheelOpen, Player()) != 0; }

    bool IsTeammate(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        return g_isTeammate && ref && IsActor(ref) && g_scripts->CallFunction(g_isTeammate, ref, nullptr, &result, 0) &&
            result.GetNumber() != 0;
    }

    uint32_t GrabbedItem()
    {
        NVSEArrayVarInterface::Element result;
        if (!g_grabbed || !Player() || !g_scripts->CallFunction(g_grabbed, Player(), nullptr, &result, 0))
            return 0;
        TESForm* form = result.GetTESForm();
        return form ? form->refID : 0;
    }

    void RemoveWorldItem(TESObjectREFR* ref)
    {
        if (!ref)
            return;
        if ((ref->refID >> 24) == 0xFF)
            Remove(ref);
        else if (g_disable)
            g_scripts->CallFunctionAlt(g_disable, ref, 0);
    }

    std::vector<std::string> LoadedMods()
    {
        std::vector<std::string> mods;
        auto data = *reinterpret_cast<const uint8_t**>(addr::kDataHandler);
        if (!data)
            return mods;
        uint32_t count = *reinterpret_cast<const uint32_t*>(data + layout::kDataHandlerModCount);
        auto list = reinterpret_cast<const uint8_t* const*>(data + layout::kDataHandlerMods);
        for (uint32_t i = 0; i < count && i < 0xFF; ++i)
        {
            if (!list[i])
                return {};
            const char* name = reinterpret_cast<const char*>(list[i] + layout::kModFileName);
            mods.emplace_back(name, strnlen(name, 0x104));
        }
        return mods;
    }

    std::vector<std::string> LoadOrder()
    {
        std::vector<std::string> mods;
        NVSEArrayVarInterface::Element result;
        if (!g_loadOrder || !g_arrays || !Player() ||
            !g_scripts->CallFunction(g_loadOrder, Player(), nullptr, &result, 0) || !result.GetArray())
            return mods;
        uint32_t size = g_arrays->GetArraySize(result.GetArray());
        if (size == static_cast<uint32_t>(-1) || size > 255)
            return mods;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(result.GetArray(), elements.data(), nullptr))
            return mods;
        for (const auto& e : elements)
            mods.push_back(e.GetString() ? e.GetString() : "?");
        return mods;
    }

    void RefreshItemList()
    {
        if (g_refreshItems && Player())
            g_scripts->CallFunctionAlt(g_refreshItems, Player(), 0);
    }

    DoorInfo ReadDoor(TESObjectREFR* ref)
    {
        DoorInfo info;
        bool box = ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectCONT;
        if (!ref || !ref->baseForm || (ref->baseForm->typeID != kFormType_TESObjectDOOR && !box))
            return info;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        auto getByType = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType);
        if (box)
            info.open = 3;
        else if (g_openState)
            info.open = static_cast<int>(Number(g_openState, ref));
        info.leadsAway = getByType(&ref->extraDataList, layout::kExtraTeleport) != nullptr;
        if (const void* lock = getByType(&ref->extraDataList, layout::kExtraLock))
        {
            auto* data = *reinterpret_cast<const uint8_t* const*>(static_cast<const char*>(lock) + layout::kLockData);
            if (data)
            {
                info.lockLevel = data[layout::kLockLevel];
                info.locked = (data[layout::kLockFlags] & layout::kLockLocked) != 0;
            }
        }
        return info;
    }

    void SetDoorOpen(TESObjectREFR* ref, bool open)
    {
        if (g_setOpen && ref)
            g_scripts->CallFunctionAlt(g_setOpen, ref, 1, static_cast<uint32_t>(open ? 1 : 0));
    }

    uint8_t PileKind(TESObjectREFR* actor)
    {
        if (!g_pileKind || !actor || !IsActor(actor))
            return 0;
        int kind = static_cast<int>(Number(g_pileKind, actor));
        return kind >= net::kPileGoo && kind <= net::kPileLast ? static_cast<uint8_t>(kind) : 0;
    }

    void MakePile(TESObjectREFR* actor, uint8_t kind)
    {
        if (g_compiled && g_makePile && actor && IsActor(actor) && kind >= net::kPileGoo && kind <= net::kPileLast)
            g_scripts->CallFunctionAlt(g_makePile, actor, 1, static_cast<uint32_t>(kind));
    }

    int GameSaysLocked(TESObjectREFR* ref)
    {
        return g_isLocked && ref ? static_cast<int>(Number(g_isLocked, ref)) : -1;
    }

    void SetDoorLocked(TESObjectREFR* ref, bool locked, int level)
    {
        if (g_lock && ref)
            g_scripts->CallFunctionAlt(g_lock, ref, 2, static_cast<uint32_t>(locked ? 1 : 0),
                static_cast<uint32_t>(level));
    }

    bool IsActor(const TESObjectREFR* ref)
    {
        return ref && (ref->typeID == kFormType_Character || ref->typeID == kFormType_Creature);
    }

    void NearbyActors(std::vector<TESObjectREFR*>& out)
    {
        out.clear();
        auto* manager = reinterpret_cast<const uint8_t*>(addr::kProcessManager);
        auto* objects = *reinterpret_cast<TESObjectREFR* const* const*>(manager + process::kManagerObjects);
        uint32_t capacity = *reinterpret_cast<const uint16_t*>(manager + process::kManagerCapacity);
        uint32_t used = *reinterpret_cast<const uint16_t*>(manager + process::kManagerObjectCount);
        uint32_t begin = *reinterpret_cast<const uint32_t*>(manager + process::kManagerBegin);
        uint32_t end = *reinterpret_cast<const uint32_t*>(manager + process::kManagerEnd);
        static uint32_t s_nextWarn = 0;
        if ((begin > end || end > used) && coop::IsDue(GetTickCount(), s_nextWarn))
        {
            s_nextWarn = GetTickCount() + 10000;
            LOG_WARN("Nearby actors list looks odd: slots %u to %u, %u in use, room for %u", begin, end, used,
                capacity);
        }
        if (!objects || begin > end || end > capacity)
            return;
        for (uint32_t i = begin; i < end; ++i)
            if (IsActor(objects[i]))
                out.push_back(objects[i]);
    }

    const char* Name(const TESObjectREFR* ref)
    {
        if (!IsActor(ref) || !ref->baseForm ||
            (ref->baseForm->typeID != kFormType_TESNPC && ref->baseForm->typeID != kFormType_TESCreature))
            return "";
        const char* name = static_cast<const TESActorBase*>(ref->baseForm)->fullName.name.m_data;
        return name ? name : "";
    }

    BaseInfo DescribeBase(TESObjectREFR* ref)
    {
        BaseInfo info;
        if (!ref || !ref->baseForm)
            return info;
        info.base = ref->baseForm->refID;
        info.baseType = ref->baseForm->typeID;
        if (!IsActor(ref) || (info.base >> 24) != 0xFF)
            return info;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
            layout::kExtraLeveledCreature);
        if (!extra)
            return info;
        auto form = [&](uint32_t offset)
        {
            return *reinterpret_cast<TESForm* const*>(static_cast<const char*>(extra) + offset);
        };
        if (TESForm* f = form(layout::kLeveledBaseForm))
        {
            info.leveledBase = f->refID;
            info.leveledBaseType = f->typeID;
        }
        if (TESForm* f = form(layout::kLeveledActorBase))
        {
            info.leveledActor = f->refID;
            info.leveledActorType = f->typeID;
        }
        return info;
    }

    float Health(TESObjectREFR* ref) { return Number(g_health, ref); }
    int PlayerLevel() { return g_level ? static_cast<int>(Number(g_level, Player())) : 0; }
    float MaxHealth(TESObjectREFR* ref) { return Number(g_maxHealth, ref); }

    void ChangeHealth(TESObjectREFR* ref, float amount)
    {
        if (g_compiled && ref && amount != 0)
            g_scripts->CallFunctionAlt(g_changeHealth, ref, 1, FloatArg(amount));
    }

    void StopFighting(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_stopFighting, ref, 0);
    }

    namespace
    {
        bool SetBaseEssential(TESForm* form, bool on)
        {
            if (!form || form->typeID != kFormType_TESNPC)
                return false;
            UInt32& flags = static_cast<TESActorBase*>(form)->baseData.flags;
            if (((flags & layout::kActorBaseEssential) != 0) == on)
                return false;
            flags = on ? flags | layout::kActorBaseEssential : flags & ~layout::kActorBaseEssential;
            return true;
        }
    }

    void SetPlayerEssential(bool on)
    {
        if (SetBaseEssential(LookupForm(ids::kPlayerBase), on))
            LOG_INFO("Player %s", on ? "can be downed (essential)" : "is no longer essential");
    }

    void SetEssential(TESObjectREFR* ref, bool on)
    {
        if (ref && SetBaseEssential(ref->baseForm, on))
            LOG_DEBUG("Base %08X %s essential", ref->baseForm->refID, on ? "made" : "no longer");
    }

    void Kill(TESObjectREFR* ref, uint16_t limbs)
    {
        if (!g_compiled || !ref)
            return;
        if (!limbs)
            g_scripts->CallFunctionAlt(g_kill, ref, 1, static_cast<uint32_t>(-1));
        for (uint32_t i = 0; i < 15; ++i)
            if (limbs & (1u << i))
                g_scripts->CallFunctionAlt(g_kill, ref, 1, i);
    }

    uint16_t LimbsGone(TESObjectREFR* ref)
    {
        float mask = ref ? Number(g_limbsGone, ref) : 0;
        return mask > 0 && mask < 0x8000 ? static_cast<uint16_t>(mask) : 0;
    }

    void SetMaxHealth(TESObjectREFR* ref, float health)
    {
        if (g_compiled && g_setMaxHealth && ref && health > 0)
            g_scripts->CallFunctionAlt(g_setMaxHealth, ref, 1, FloatArg(health));
    }

    bool IsGeneric(TESObjectREFR* actor)
    {
        BaseInfo b = DescribeBase(actor);
        return net::IsMadeWhilePlaying(b.base) && b.leveledBase && !net::IsMadeWhilePlaying(b.leveledBase) &&
            (b.leveledBaseType == kFormType_TESNPC || b.leveledBaseType == kFormType_TESCreature ||
             b.leveledBaseType == kFormType_TESLevCharacter || b.leveledBaseType == kFormType_TESLevCreature);
    }

    int ThinAmmo(TESObjectREFR* body, int keptPercent)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_thinAmmo || !body ||
            !g_scripts->CallFunction(g_thinAmmo, body, nullptr, &result, 1, static_cast<uint32_t>(keptPercent)))
            return 0;
        return static_cast<int>(result.GetNumber());
    }

    TESObjectREFR* PlaceExtraEnemy(TESObjectREFR* like, TESObjectREFR* target)
    {
        BaseInfo b = DescribeBase(like);
        TESForm* base = b.leveledBase ? LookupForm(b.leveledBase) : nullptr;
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_placeExtra || !base || !target || !IsGeneric(like) ||
            !g_scripts->CallFunction(g_placeExtra, like, nullptr, &result, 2, base, target))
            return nullptr;
        TESForm* made = result.GetTESForm();
        return made ? LookupRef(made->refID) : nullptr;
    }

    void ForceTarget(TESObjectREFR* actor, TESObjectREFR* target)
    {
        if (g_compiled && g_forceTarget && actor)
            g_scripts->CallFunctionAlt(g_forceTarget, actor, 1, target);
    }

    uint32_t CombatTarget(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_combatTarget || !ref || !g_scripts->CallFunction(g_combatTarget, ref, nullptr, &result, 0))
            return 0;
        TESForm* target = result.GetTESForm();
        return target ? target->refID : 0;
    }

    uint32_t PlayedIdle(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_playedIdle || !ref || !g_scripts->CallFunction(g_playedIdle, ref, nullptr, &result, 0))
            return 0;
        TESForm* idle = result.GetTESForm();
        return idle ? idle->refID : 0;
    }

    bool PlayIdle(TESObjectREFR* ref, uint32_t idleId)
    {
        TESForm* idle = LookupForm(idleId);
        if (!g_compiled || !g_playIdle || !ref || !idle || idle->typeID != kFormType_TESIdleForm)
            return false;
        if (*reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(idle) + layout::kIdleChildren))
        {
            static std::set<uint32_t> s_skipped;
            if (s_skipped.size() < 256 && s_skipped.insert(idleId).second)
                LOG_INFO("Idles: not playing %08X on copies (it has a list of children, which crashed JIP's PlayIdleEx)",
                    idleId);
            return false;
        }
        g_scripts->CallFunctionAlt(g_playIdle, ref, 1, idle);
        return true;
    }

    void KillPlayer()
    {
        SetPlayerEssential(false);
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_kill, Player(), 1, static_cast<uint32_t>(-1));
    }

    void KnockOut(TESObjectREFR* ref)
    {
        ChangeHealth(ref, -(Health(ref) + 10));
    }

    void SetUnconscious(TESObjectREFR* ref, bool out)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_unconscious, ref, 1, static_cast<uint32_t>(out ? 1 : 0));
    }

    void WakeUp(TESObjectREFR* ref)
    {
        if (!IsKnockedOut(ref))
            return;
        SetUnconscious(ref, false);
        if (IsKnockedOut(ref))
        {
            LOG_DEBUG("SetUnconscious didn't wake them (life state %d); setting it to alive", LifeState(ref));
            static_cast<Actor*>(ref)->lifeState = 0;
        }
    }

    void SetPlayerControls(bool on)
    {
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_controls, Player(), 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    int LifeState(const TESObjectREFR* ref)
    {
        return IsActor(ref) ? static_cast<int>(static_cast<const Actor*>(ref)->lifeState) : -1;
    }

    bool IsDead(const TESObjectREFR* ref)
    {
        int state = LifeState(ref);
        return state == static_cast<int>(layout::kLifeStateDying) || state == static_cast<int>(layout::kLifeStateDead);
    }

    bool IsKnockedOut(const TESObjectREFR* ref)
    {
        int state = LifeState(ref);
        return state == static_cast<int>(layout::kLifeStateUnconscious) ||
            state == static_cast<int>(layout::kLifeStateEssentialDown);
    }

    bool InLoadingScreen()
    {
        return g_loadingScreen && Player() && Number(g_loadingScreen, Player()) != 0;
    }

    bool InMenu()
    {
        return Number(g_menuMode, Player()) != 0;
    }

    bool IsKeyPressed(int key)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_keyPressed || !Player() ||
            !g_scripts->CallFunction(g_keyPressed, Player(), nullptr, &result, 1, static_cast<uint32_t>(key)))
            return false;
        return result.GetNumber() != 0;
    }

    namespace
    {
        std::mutex g_chatMutex;
        coop::ChatDraft g_chatDraft;
        bool g_chatTyping = false;
        bool g_chatChanged = false;
        bool g_chatTalking = false;
        std::vector<std::string> g_chatSent;
        HWND g_gameWindow = nullptr;
        WNDPROC g_gameWndProc = nullptr;
        std::vector<int> g_chatKeysOff;
        bool g_entryTyping = false;
        coop::ChatDraft g_entryDraft;
        EntryState g_entryState = EntryState::None;
        bool g_chatHudMade = false;
        bool g_chatShown = false;

        LRESULT CALLBACK ChatWndProc(HWND window, UINT message, WPARAM w, LPARAM l)
        {
            if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSCHAR)
            {
                std::lock_guard<std::mutex> lock(g_chatMutex);
                if (g_entryTyping)
                {
                    if (message == WM_CHAR)
                    {
                        coop::ChatDraft::Done done = g_entryDraft.Type(static_cast<unsigned char>(w));
                        if (done != coop::ChatDraft::Done::No)
                        {
                            g_entryTyping = false;
                            g_entryState = done == coop::ChatDraft::Done::Send ? EntryState::Done : EntryState::Cancelled;
                        }
                    }
                    return 0;
                }
                if (g_chatTyping)
                {
                    if (message == WM_CHAR)
                    {
                        coop::ChatDraft::Done done = g_chatDraft.Type(static_cast<unsigned char>(w));
                        if (done == coop::ChatDraft::Done::Send)
                        {
                            std::string text = g_chatDraft.Take();
                            if (!text.empty() && g_chatSent.size() < 8)
                                g_chatSent.push_back(std::move(text));
                        }
                        if (done != coop::ChatDraft::Done::No)
                            g_chatTyping = false;
                        g_chatChanged = true;
                    }
                    return 0;
                }
            }
            return CallWindowProcA(g_gameWndProc, window, message, w, l);
        }

        BOOL CALLBACK FindGameWindow(HWND window, LPARAM)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
                return TRUE;
            g_gameWindow = window;
            return FALSE;
        }

        bool HookGameWindow()
        {
            if (g_gameWndProc)
                return true;
            EnumWindows(FindGameWindow, 0);
            if (!g_gameWindow)
                return false;
            g_gameWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChatWndProc)));
            if (g_gameWndProc)
                LOG_INFO("Chat: typing goes through the game's window");
            return g_gameWndProc != nullptr;
        }

        void MakeChatHud()
        {
            NVSEArrayVarInterface::Element result;
            if (g_chatHudMade || !g_chatHud || !Player())
                return;
            static const char kLine[] =
                "<rect name=\"NVCoopChat\"><visible>0</visible>"
                "<text name=\"NVCoopChatText\">"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div></x><y>600</y>"
                "<font>1</font><justify>&center;</justify><wrapwidth>1100</wrapwidth>"
                "<systemcolor>1</systemcolor><string> </string></text></rect>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kLine);
            for (int slot = 0; slot < kChatAboveSlots; ++slot)
            {
                std::string xml = "<text name=\"NVCoopAbove" + std::to_string(slot) + "\"><visible>0</visible>"
                    "<user0>0.5</user0><user1>0.5</user1>"
                    "<x><copy src=\"screen()\" trait=\"width\"/><mul src=\"me()\" trait=\"user0\"/></x>"
                    "<y><copy src=\"screen()\" trait=\"height\"/><mul src=\"me()\" trait=\"user1\"/><sub>30</sub></y>"
                    "<font>1</font><justify>&center;</justify><wrapwidth>600</wrapwidth>"
                    "<systemcolor>1</systemcolor><string> </string></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            static const char kRevive[] =
                "<text name=\"NVCoopRevive\"><visible>0</visible>"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div></x>"
                "<y><copy src=\"screen()\" trait=\"height\"/><mul>0.58</mul></y>"
                "<font>2</font><justify>&center;</justify><wrapwidth>900</wrapwidth>"
                "<systemcolor>1</systemcolor><string> </string></text>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kRevive);
            for (int slot = 0; slot < kTalkSlots; ++slot)
            {
                std::string n = std::to_string(slot);
                std::string text = "<locus>1</locus><y>24</y><alpha>0</alpha><font>3</font><justify>&left;</justify>"
                    "<wrapwidth>600</wrapwidth><brightness>-1</brightness><string> </string>";
                std::string xml = "<image name=\"NVCoopTalkBar" + n + "\"><visible>0</visible>"
                    "<filename>interface\\shared\\solid_black.dds</filename>"
                    "<x>22</x><y>24</y><depth>1</depth><width>616</width><height>20</height><zoom>-1</zoom>"
                    "<alpha>0</alpha></image>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                for (int e = 0; e < kTalkEdges; ++e)
                {
                    xml = "<text name=\"NVCoopTalkEdge" + n + "x" + std::to_string(e) + "\"><visible>0</visible>"
                        "<x>30</x><depth>2</depth>" + text + "<red>0</red><green>0</green><blue>0</blue></text>";
                    g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                }
                xml = "<text name=\"NVCoopTalk" + n + "\"><visible>0</visible><x>30</x><depth>3</depth>" + text +
                    "<systemcolor>1</systemcolor><red>255</red><green>255</green><blue>255</blue></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                xml = "<image name=\"NVCoopTalkLine" + n + "\"><visible>0</visible>"
                    "<filename>interface\\shared\\line\\glow_tip_line_horizontal.dds</filename>"
                    "<x>30</x><y>24</y><depth>4</depth><width>740</width><height>8</height><zoom>-1</zoom><alpha>0</alpha>"
                    "<brightness>-1</brightness><red>255</red><green>255</green><blue>255</blue></image>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            for (int slot = 0; slot < kMarkSlots; ++slot)
            {
                std::string xml = "<text name=\"NVCoopMark" + std::to_string(slot) + "\"><visible>0</visible>"
                    "<user0>0.5</user0><user1>0.5</user1>"
                    "<x><copy src=\"screen()\" trait=\"width\"/><mul src=\"me()\" trait=\"user0\"/></x>"
                    "<y><copy src=\"screen()\" trait=\"height\"/><mul src=\"me()\" trait=\"user1\"/><sub>75</sub></y>"
                    "<font>2</font><justify>&center;</justify><wrapwidth>400</wrapwidth>"
                    "<systemcolor>1</systemcolor><string> </string></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            g_chatHudMade = true;
        }

        struct ChatAbove
        {
            uint32_t who = 0;
            std::string text;
            uint32_t until = 0;
            bool changed = false;
            bool shown = false;
        };
        ChatAbove g_above[kChatAboveSlots];

        void ShowChatLine(const std::string& text, bool show)
        {
            NVSEArrayVarInterface::Element result;
            MakeChatHud();
            if (g_chatShow && Player())
                g_scripts->CallFunction(g_chatShow, Player(), nullptr, &result, 2, text.empty() ? " " : text.c_str(),
                    static_cast<uint32_t>(show ? 1 : 0));
        }

        void KeepKeysFromGame(bool keep)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_keyDisabled || !g_disableKey || !Player())
                return;
            if (keep)
            {
                for (int key = 1; key < 264; ++key)
                    if (g_scripts->CallFunction(g_keyDisabled, Player(), nullptr, &result, 1, static_cast<uint32_t>(key)) &&
                        result.GetNumber() == 0)
                    {
                        g_scripts->CallFunction(g_disableKey, Player(), nullptr, nullptr, 2, static_cast<uint32_t>(key), 1u);
                        g_chatKeysOff.push_back(key);
                    }
            }
            else
            {
                for (int key : g_chatKeysOff)
                    g_scripts->CallFunction(g_disableKey, Player(), nullptr, nullptr, 2, static_cast<uint32_t>(key), 0u);
                g_chatKeysOff.clear();
            }
        }
    }

    bool StartTextEntry()
    {
        if (!HookGameWindow())
            return false;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            if (g_chatTyping)
                return false;
            g_entryDraft = coop::ChatDraft();
            g_entryTyping = true;
            g_entryState = EntryState::Typing;
        }
        if (g_chatKeysOff.empty())
            KeepKeysFromGame(true);
        return true;
    }

    void StopTextEntry()
    {
        bool chat;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            g_entryTyping = false;
            g_entryDraft = coop::ChatDraft();
            g_entryState = EntryState::None;
            chat = g_chatTyping;
        }
        if (!chat && !g_chatKeysOff.empty())
            KeepKeysFromGame(false);
    }

    EntryState TextEntryState()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_entryState;
    }

    size_t TextEntryLength()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_entryDraft.Text().size();
    }

    std::string TakeTextEntry()
    {
        std::string text;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            if (g_entryState == EntryState::Done)
                text = g_entryDraft.Take();
        }
        StopTextEntry();
        return text;
    }

    void TypeIntoEntry(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (!g_entryTyping)
            return;
        for (char c : text)
            g_entryDraft.Type(static_cast<unsigned char>(c));
        g_entryTyping = false;
        g_entryState = EntryState::Done;
    }

    bool OpenChat()
    {
        if (!HookGameWindow())
            return false;
        std::lock_guard<std::mutex> lock(g_chatMutex);
        g_chatTyping = true;
        g_chatChanged = true;
        return true;
    }

    bool SetMouth(TESObjectREFR* actor, float opening)
    {
        if (!IsActor(actor))
            return false;
        auto* face = reinterpret_cast<uint8_t*(__thiscall*)(TESObjectREFR*)>(addr::kActorFaceAnimData)(actor);
        if (!face)
            return false;
        bool set = false;
        for (int group : {0x4C, 0xC8, 0xDC})
        {
            uint8_t* keyframes = face + group;
            auto* values = *reinterpret_cast<float**>(keyframes + 0x0C);
            uint32_t count = *reinterpret_cast<uint32_t*>(keyframes + 0x10);
            if (!values || count != 16)
                continue;
            values[1] = opening;
            set = true;
        }
        return set;
    }

    int QuestStage(uint32_t quest)
    {
        NVSEArrayVarInterface::Element result;
        TESForm* form = LookupForm(quest);
        if (!g_questStage || !form || !Player() ||
            !g_scripts->CallFunction(g_questStage, Player(), nullptr, &result, 1, form))
            return -1;
        return static_cast<int>(result.GetNumber());
    }

    void HoldTutorial(bool hold)
    {
        if (g_tutorialTimer && Player())
            g_scripts->CallFunctionAlt(g_tutorialTimer, Player(), 1, hold ? 0u : 1u);
    }

    bool LeaveTutorial()
    {
        using namespace ids;
        TESForm* forms[] = {LookupForm(kTutorialQuest), LookupForm(kSunnyQuest), LookupForm(kDocsDoor),
            LookupForm(kPipBoy), LookupForm(kPipBoyGlove), LookupForm(kVaultSuit21), LookupForm(kCourierNote)};
        for (TESForm* f : forms)
            if (!f)
                return false;
        if (!g_leaveTutorial || !Player())
            return false;
        g_scripts->CallFunction(g_leaveTutorial, Player(), nullptr, nullptr, 7, forms[0], forms[1], forms[2], forms[3],
            forms[4], forms[5], forms[6]);
        RemoveTutorialFades();
        return true;
    }

    void RemoveTutorialFades()
    {
        for (uint32_t fade : ids::kTutorialFades)
        {
            char line[64];
            std::snprintf(line, sizeof(line), "RemoveImageSpaceModifier %08X", fade);
            RunConsoleLine(line);
        }
    }

    bool ActivateByPlayer(uint32_t ref, bool moveHereFirst)
    {
        TESForm* what = LookupForm(ref);
        if (!what || !g_activateBy || !Player())
            return false;
        g_scripts->CallFunction(g_activateBy, Player(), nullptr, nullptr, 2, what, moveHereFirst ? 1u : 0u);
        return true;
    }

    bool GiveStarterGear()
    {
        using namespace ids;
        uint32_t ids[] = {kStimpak, kCaps, kLockpick, k9mmPistol, k9mmAmmo, kLaserPistol, kEnergyCells, kDynamite,
            kStraightRazor, kBoxingGloves};
        TESForm* f[10];
        for (int i = 0; i < 10; ++i)
            if (!(f[i] = LookupForm(ids[i])))
                return false;
        if (!g_starterGear || !Player())
            return false;
        g_scripts->CallFunction(g_starterGear, Player(), nullptr, nullptr, 10, f[0], f[1], f[2], f[3], f[4], f[5], f[6],
            f[7], f[8], f[9]);
        return true;
    }

    namespace
    {
        struct Tuned { const char* name; float original = 0; bool read = false; };
        Tuned g_tuned[] = {{"fDiffMultHPToPCVE"}, {"fDiffMultHPToPCE"}, {"fDiffMultHPToPCN"}, {"fDiffMultHPToPCH"},
            {"fDiffMultHPToPCVH"}, {"fDiffMultHPByPCVE"}, {"fDiffMultHPByPCE"}, {"fDiffMultHPByPCN"}, {"fDiffMultHPByPCH"},
            {"fDiffMultHPByPCVH"}, {"fAVDCarryWeightsBase"},
            {"iXPRewardKillOpponentVeryEasy"}, {"iXPRewardKillOpponentEasy"}, {"iXPRewardKillOpponentAverage"},
            {"iXPRewardKillOpponentHard"}, {"iXPRewardKillOpponentVeryHard"}, {"iXPRewardKillNPCVeryEasy"},
            {"iXPRewardKillNPCEasy"}, {"iXPRewardKillNPCAverage"}, {"iXPRewardKillNPCHard"}, {"iXPRewardKillNPCVeryHard"}};
        struct TunedEffect { uint32_t item; int magnitude = -1, duration = -1; };
        TunedEffect g_tunedEffects[] = {{ids::kStimpak}, {ids::kSuperStimpak}};

        struct PartMult { TESForm* list; int part; float original; };
        std::vector<PartMult> g_partMults;
        bool g_partsRead = false;

        float BodyPartMult(TESForm* list, int part, float value)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_bodyPartMult || !Player() || !g_scripts->CallFunction(g_bodyPartMult, Player(), nullptr, &result, 3,
                    list, static_cast<uint32_t>(part), *reinterpret_cast<uint32_t*>(&value)))
                return -1;
            return static_cast<float>(result.GetNumber());
        }

        void ReadBodyParts()
        {
            g_partsRead = true;
            NVSEArrayVarInterface::Element result;
            if (!g_bodyPartLists || !g_arrays || !Player() ||
                !g_scripts->CallFunction(g_bodyPartLists, Player(), nullptr, &result, 0) || !result.GetArray())
                return;
            uint32_t size = g_arrays->GetArraySize(result.GetArray());
            if (size == static_cast<uint32_t>(-1) || size > 4096)
                return;
            std::vector<NVSEArrayVarInterface::Element> lists(size);
            if (size && !g_arrays->GetElements(result.GetArray(), lists.data(), nullptr))
                return;
            for (const auto& e : lists)
                if (TESForm* list = e.GetTESForm())
                    for (int part = 1; part <= 12; ++part)
                    {
                        float mult = BodyPartMult(list, part, -1);
                        if (mult > 0)
                            g_partMults.push_back({list, part, mult});
                    }
            LOG_INFO("Balance: %u body part lists, %zu parts' damage read", size, g_partMults.size());
        }

        std::vector<TESForm*> LoadedOfType(uint8_t type)
        {
            std::vector<TESForm*> out;
            NVSEArrayVarInterface::Element result;
            if (!g_loadedOfType || !g_arrays || !Player() || !g_scripts->CallFunction(g_loadedOfType, Player(), nullptr,
                    &result, 1, static_cast<uint32_t>(type)) || !result.GetArray())
                return out;
            uint32_t size = g_arrays->GetArraySize(result.GetArray());
            if (size == static_cast<uint32_t>(-1) || size > 200000)
                return out;
            std::vector<NVSEArrayVarInterface::Element> forms(size);
            if (size && !g_arrays->GetElements(result.GetArray(), forms.data(), nullptr))
                return out;
            for (const auto& e : forms)
                if (TESForm* form = e.GetTESForm(); form && form->typeID == type)
                    out.push_back(form);
            return out;
        }

        struct ArmorDR { TESForm* armor; uint16_t original; bool heavy; };
        std::vector<ArmorDR> g_armors;
        struct SupplyList { TESForm* list; uint8_t original; };
        std::vector<SupplyList> g_supplyLists;
        std::vector<SupplyList> g_boxLists;
        struct AmmoValue { TESForm* ammo; uint32_t original; };
        std::vector<AmmoValue> g_ammoValues;
        struct AmmoCount { uint8_t* count; int32_t original; bool wide; };
        std::vector<AmmoCount> g_ammoCounts;
        bool g_armorAndLootRead = false;
        struct StyleWas { TESForm* style; float coverRadius, takeCover; uint8_t sideStep; uint16_t flags; };
        std::vector<StyleWas> g_styles;

        void ReadArmorAndLoot()
        {
            g_armorAndLootRead = true;
            int light = 0, checked = 0, differ = 0;
            for (TESForm* armor : LoadedOfType(layout::kFormTypeArmor))
            {
                uint32_t flags = At<uint32_t>(armor, layout::kArmorBipedFlags);
                uint16_t dr = At<uint16_t>(armor, layout::kArmorRating);
                if (checked < 20 && g_armorDR)
                {
                    NVSEArrayVarInterface::Element result;
                    if (g_scripts->CallFunction(g_armorDR, Player(), nullptr, &result, 1, armor))
                    {
                        ++checked;
                        differ += static_cast<int>(result.GetNumber()) != dr;
                    }
                }
                if (flags & (layout::kArmorMedium | layout::kArmorHeavy))
                    g_armors.push_back({armor, dr, (flags & layout::kArmorHeavy) != 0});
                else
                    ++light;
            }
            int food = 0, medicine = 0;
            for (TESForm* style : LoadedOfType(layout::kFormTypeCombatStyle))
                g_styles.push_back({style, At<float>(style, layout::kStyleCoverRadius),
                    At<float>(style, layout::kStyleTakeCover), At<uint8_t>(style, layout::kStyleSideStep),
                    At<uint16_t>(style, layout::kStyleFlags)});
            auto forEachNode = [](const uint8_t* node, auto&& use) {
                for (int n = 0; node && n < 1000; node = *reinterpret_cast<const uint8_t* const*>(node + 4), ++n)
                {
                    const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(node);
                    if (!data)
                        break;
                    use(data);
                }
            };
            std::set<const TESForm*> boxLists, kitLists;
            std::function<void(const TESForm*, std::set<const TESForm*>&)> follow =
                [&](const TESForm* list, std::set<const TESForm*>& into) {
                    if (!list || list->typeID != layout::kFormTypeLevItem || !into.insert(list).second)
                        return;
                    forEachNode(reinterpret_cast<const uint8_t*>(list) + layout::kLevListEntries, [&](const uint8_t* e) {
                        follow(*reinterpret_cast<const TESForm* const*>(e), into);
                    });
                };
            auto itemsOf = [&](uint8_t type, uint32_t offset, std::set<const TESForm*>& into) {
                for (TESForm* holder : LoadedOfType(type))
                    forEachNode(reinterpret_cast<const uint8_t*>(holder) + offset, [&](const uint8_t* e) {
                        follow(*reinterpret_cast<const TESForm* const*>(e + 4), into);
                    });
            };
            itemsOf(layout::kFormTypeContainer, layout::kContainerItemsOfBox, boxLists);
            itemsOf(layout::kFormTypeNpc, layout::kContainerItemsOfActor, kitLists);
            itemsOf(layout::kFormTypeCreature, layout::kContainerItemsOfActor, kitLists);
            for (TESForm* list : LoadedOfType(layout::kFormTypeLevItem))
            {
                if (boxLists.count(list) && !kitLists.count(list))
                {
                    g_boxLists.push_back({list, At<uint8_t>(list, layout::kLevListChanceNone)});
                    continue;
                }
                bool supplies = false, other = false;
                int entries = 0;
                for (auto* node = reinterpret_cast<const uint8_t*>(list) + layout::kLevListEntries; node && entries < 1000;
                     node = *reinterpret_cast<const uint8_t* const*>(node + 4), ++entries)
                {
                    const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(node);
                    if (!entry)
                        break;
                    const TESForm* item = *reinterpret_cast<const TESForm* const*>(entry);
                    uint8_t kind = item && item->typeID == layout::kFormTypeAlchemy ?
                        At<uint8_t>(item, layout::kAlchFlags) & (layout::kAlchFood | layout::kAlchMedicine) : 0;
                    food += (kind & layout::kAlchFood) != 0;
                    medicine += (kind & layout::kAlchMedicine) != 0;
                    (kind ? supplies : other) = true;
                }
                if (supplies && !other)
                    g_supplyLists.push_back({list, At<uint8_t>(list, layout::kLevListChanceNone)});
            }
            int ammoChecked = 0, ammoDiffer = 0;
            for (TESForm* ammo : LoadedOfType(layout::kFormTypeAmmo))
            {
                uint32_t value = At<uint32_t>(ammo, layout::kAmmoValue);
                if (ammoChecked < 20 && g_itemValue)
                {
                    NVSEArrayVarInterface::Element result;
                    if (g_scripts->CallFunction(g_itemValue, Player(), nullptr, &result, 1, ammo))
                    {
                        ++ammoChecked;
                        ammoDiffer += static_cast<uint32_t>(result.GetNumber()) != value;
                    }
                }
                g_ammoValues.push_back({ammo, value});
            }
            auto isAmmo = [](const TESForm* form) { return form && form->typeID == layout::kFormTypeAmmo; };
            for (const SupplyList& l : g_boxLists)
            {
                if (At<uint32_t>(l.list, layout::kLevListGlobal))
                    continue;
                forEachNode(reinterpret_cast<const uint8_t*>(l.list) + layout::kLevListEntries, [&](const uint8_t* e) {
                    if (isAmmo(*reinterpret_cast<const TESForm* const*>(e)))
                        g_ammoCounts.push_back({const_cast<uint8_t*>(e) + layout::kLevEntryCount,
                            *reinterpret_cast<const int16_t*>(e + layout::kLevEntryCount), false});
                });
            }
            size_t ammoInLists = g_ammoCounts.size();
            for (TESForm* box : LoadedOfType(layout::kFormTypeContainer))
                forEachNode(reinterpret_cast<const uint8_t*>(box) + layout::kContainerItemsOfBox, [&](const uint8_t* e) {
                    if (isAmmo(*reinterpret_cast<const TESForm* const*>(e + 4)))
                        g_ammoCounts.push_back({const_cast<uint8_t*>(e), *reinterpret_cast<const int32_t*>(e), true});
                });
            LOG_INFO("Balance: %zu kinds of ammo (value read as xNVSE does on %d of %d); %zu ammo entries in lists only "
                "boxes and vendors use, %zu in boxes' own items", g_ammoValues.size(), ammoChecked - ammoDiffer, ammoChecked,
                ammoInLists, g_ammoCounts.size() - ammoInLists);
            LOG_INFO("Balance: %zu medium and heavy armors (%d light; DR read as xNVSE does on %d of %d), %zu loot lists "
                "of only food, drink and medicine (%d food and %d medicine entries in loot lists), %zu only boxes and "
                "vendors use (of %zu they use; %zu kit out characters), %zu combat styles", g_armors.size(), light,
                checked - differ, checked, g_supplyLists.size(), food, medicine, g_boxLists.size(), boxLists.size(),
                kitLists.size(), g_styles.size());
        }

        int EffectTrait(uint32_t item, int trait, int value)
        {
            NVSEArrayVarInterface::Element result;
            TESForm* form = LookupForm(item);
            if (!g_effectTrait || !form || !Player() ||
                !g_scripts->CallFunction(g_effectTrait, Player(), nullptr, &result, 4, form, 0u,
                    static_cast<uint32_t>(trait), static_cast<uint32_t>(value)))
                return -1;
            return static_cast<int>(result.GetNumber());
        }
    }

    bool ApplyBalance(const coop::Balance* balance)
    {
        if (!Player())
            return false;
        for (Tuned& t : g_tuned)
            if (!t.read)
            {
                std::string value = Evaluate((std::string("GetNumericGameSetting ") + t.name).c_str());
                char* end = nullptr;
                t.original = std::strtof(value.c_str(), &end);
                t.read = end != value.c_str();
                if (!t.read)
                    LOG_WARN("Balance: couldn't read %s (%s)", t.name, value.c_str());
            }
        for (TunedEffect& e : g_tunedEffects)
            if (e.magnitude < 0)
            {
                e.magnitude = EffectTrait(e.item, 0, -1);
                e.duration = EffectTrait(e.item, 2, -1);
            }
        for (const Tuned& t : g_tuned)
        {
            if (!t.read)
                continue;
            float value = t.original;
            if (balance)
            {
                std::string name = t.name;
                if (name.compare(0, 15, "fDiffMultHPToPC") == 0)
                    value = t.original * balance->damageToPlayers;
                else if (name.compare(0, 15, "fDiffMultHPByPC") == 0)
                    value = t.original * balance->damageByPlayers;
                else if (name == "fAVDCarryWeightsBase")
                    value = balance->carryWeightBase;
                else if (name.compare(0, 13, "iXPRewardKill") == 0)
                    value = std::round(t.original * balance->killXpMult);
            }
            char line[128];
            if (t.name[0] == 'i')
                std::snprintf(line, sizeof(line), "SetNumericGameSetting %s %d", t.name, static_cast<int>(value));
            else
                std::snprintf(line, sizeof(line), "SetNumericGameSetting %s %.4f", t.name, value);
            RunConsoleLine(line);
        }
        if (!g_partsRead)
            ReadBodyParts();
        for (const PartMult& p : g_partMults)
        {
            bool head = p.part <= 2;
            float times = !balance ? 1.0f : head ? balance->headshotMult : balance->limbMult;
            BodyPartMult(p.list, p.part, p.original * times);
        }
        if (!g_armorAndLootRead)
            ReadArmorAndLoot();
        for (const ArmorDR& a : g_armors)
        {
            int extra = !balance ? 0 : a.heavy ? balance->heavyArmorDR : balance->mediumArmorDR;
            *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(a.armor) + layout::kArmorRating) =
                static_cast<uint16_t>(a.original + extra);
        }
        for (const StyleWas& s : g_styles)
        {
            auto* style = reinterpret_cast<uint8_t*>(s.style);
            bool usesCover = s.takeCover > 0;
            *reinterpret_cast<float*>(style + layout::kStyleTakeCover) = balance && usesCover ?
                (std::max)(s.takeCover, static_cast<float>(balance->takeCoverAtLeast)) : s.takeCover;
            *reinterpret_cast<float*>(style + layout::kStyleCoverRadius) = balance && usesCover ?
                s.coverRadius * balance->coverSearchMult : s.coverRadius;
            *(style + layout::kStyleSideStep) = balance && usesCover ?
                static_cast<uint8_t>((std::max)(static_cast<int>(s.sideStep), balance->sideStepChance)) : s.sideStep;
            *reinterpret_cast<uint16_t*>(style + layout::kStyleFlags) = balance ?
                static_cast<uint16_t>(s.flags | layout::kStyleFleeWhenHurt) : s.flags;
        }
        for (const SupplyList& l : g_supplyLists)
            *(reinterpret_cast<uint8_t*>(l.list) + layout::kLevListChanceNone) =
                balance ? coop::ScarceChanceNone(l.original, balance->suppliesKept) : l.original;
        for (const SupplyList& l : g_boxLists)
            *(reinterpret_cast<uint8_t*>(l.list) + layout::kLevListChanceNone) =
                balance ? coop::ScarceChanceNone(l.original, balance->boxLootKept) : l.original;
        for (const AmmoValue& a : g_ammoValues)
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(a.ammo) + layout::kAmmoValue) =
                balance ? coop::AmmoValue(a.original, *balance) : a.original;
        for (const AmmoCount& c : g_ammoCounts)
        {
            int32_t count = balance ? coop::AmmoFound(c.original, *balance) : c.original;
            if (c.wide)
                *reinterpret_cast<int32_t*>(c.count) = count;
            else
                *reinterpret_cast<int16_t*>(c.count) = static_cast<int16_t>(count);
        }
        for (const TunedEffect& e : g_tunedEffects)
        {
            if (e.magnitude < 0 || e.duration < 0)
                continue;
            bool super = e.item == ids::kSuperStimpak;
            int magnitude = !balance ? e.magnitude : super ? balance->superStimpakPerSecond : balance->stimpakPerSecond;
            int duration = !balance ? e.duration : super ? balance->superStimpakSeconds : balance->stimpakSeconds;
            EffectTrait(e.item, 0, magnitude);
            EffectTrait(e.item, 2, duration);
        }
        SetLevelUpBalance(balance);
        LOG_INFO("Balance: %s (damage to players x%.2f, by players x%.2f, carry weight %.0f, Stimpak %d/s for %d s, "
            "skill points a level %d + half of Intelligence, kill XP x%.2f, ammo worth x%.1f and found x%.2f)",
            balance ? "co-op balanced" : "the game's own", balance ? balance->damageToPlayers : 1.0f,
            balance ? balance->damageByPlayers : 1.0f, balance ? balance->carryWeightBase : g_tuned[10].original,
            balance ? balance->stimpakPerSecond : g_tunedEffects[0].magnitude,
            balance ? balance->stimpakSeconds : g_tunedEffects[0].duration,
            balance ? balance->skillPointsBase : coop::kGameSkillPointsBase, balance ? balance->killXpMult : 1.0f,
            balance ? balance->ammoValueMult : 1.0f, balance ? balance->ammoFindKept : 1.0f);
        return true;
    }

    bool KeyDown(int key)
    {
        HWND front = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId(front, &process);
        if (!front || process != GetCurrentProcessId() || key <= 0 || key > 255)
            return false;
        UINT vk = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VSC_TO_VK_EX);
        if (!vk)
            vk = MapVirtualKeyA(0xE000 | static_cast<UINT>(key), MAPVK_VSC_TO_VK_EX);
        return vk && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
    }

    void ShowTalking(bool talking)
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (g_chatTalking != talking)
        {
            g_chatTalking = talking;
            g_chatChanged = true;
        }
    }

    void CancelChat()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (!g_chatTyping)
            return;
        g_chatTyping = false;
        g_chatDraft.Type(0x1B);
        g_chatChanged = true;
    }

    bool ChatTyping()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_chatTyping;
    }

    void TypeChat(unsigned char c)
    {
        if (g_gameWindow)
            PostMessageA(g_gameWindow, WM_CHAR, c, 0);
    }

    void ShowChatAbove(int slot, const TESObjectREFR* who, const std::string& text)
    {
        if (slot < 0 || slot >= kChatAboveSlots || !who)
            return;
        uint32_t ms = 4000 + 70 * static_cast<uint32_t>(text.size());
        g_above[slot] = ChatAbove{who->refID, text, GetTickCount() + (ms < 12000 ? ms : 12000), true, g_above[slot].shown};
    }

    namespace
    {
        struct Mark { uint32_t who = 0; std::string name; bool downed = false, shown = false; };
        Mark g_marks[kMarkSlots];
    }

    void ShowFriendMarkers(const std::vector<FriendMark>& friends)
    {
        for (int slot = 0; slot < kMarkSlots; ++slot)
        {
            Mark& m = g_marks[slot];
            TESObjectREFR* who = slot < static_cast<int>(friends.size()) ? friends[slot].who : nullptr;
            const std::string name = who ? friends[slot].text : std::string();
            bool downed = who && friends[slot].downed;
            float x = 0, y = 0;
            bool show = false;
            NVSEArrayVarInterface::Element result;
            if (who && g_onScreen && g_arrays && g_mark[slot] && Player())
            {
                MakeChatHud();
                if (g_scripts->CallFunction(g_onScreen, who, nullptr, &result, 1, who) && result.GetArray() &&
                    g_arrays->GetArraySize(result.GetArray()) == 3)
                {
                    NVSEArrayVarInterface::Element e[3];
                    if (g_arrays->GetElements(result.GetArray(), e, nullptr) && e[0].GetNumber() != 0)
                    {
                        x = static_cast<float>(e[1].GetNumber());
                        y = static_cast<float>(e[2].GetNumber());
                        show = x >= 0 && x <= 1 && y >= 0 && y <= 1;
                    }
                }
            }
            if (!show && !m.shown)
                continue;
            bool changed = !m.shown || m.who != (who ? who->refID : 0) || m.name != name || m.downed != downed;
            if (Player() && g_mark[slot])
                g_scripts->CallFunction(g_mark[slot], Player(), nullptr, nullptr, 5, name.empty() ? " " : name.c_str(),
                    *reinterpret_cast<uint32_t*>(&x), *reinterpret_cast<uint32_t*>(&y),
                    static_cast<uint32_t>(!show ? 0 : changed ? 2 : 1), static_cast<uint32_t>(downed ? 1 : 0));
            m.who = show ? who->refID : 0;
            m.name = show ? name : std::string();
            m.downed = show && downed;
            m.shown = show;
        }
    }

    void UpdateChatAbove()
    {
        uint32_t now = GetTickCount();
        for (int slot = 0; slot < kChatAboveSlots; ++slot)
        {
            ChatAbove& a = g_above[slot];
            if (!a.who && !a.shown)
                continue;
            NVSEArrayVarInterface::Element result;
            TESObjectREFR* who = a.who ? LookupRef(a.who) : nullptr;
            float x = 0, y = 0;
            bool show = who && static_cast<int32_t>(now - a.until) < 0 && DistanceToPlayer(who) < 3500 && g_onScreen &&
                g_arrays && g_chatAbove[slot];
            if (show)
            {
                MakeChatHud();
                show = false;
                if (g_scripts->CallFunction(g_onScreen, who, nullptr, &result, 1, who) && result.GetArray() &&
                    g_arrays->GetArraySize(result.GetArray()) == 3)
                {
                    NVSEArrayVarInterface::Element e[3];
                    if (g_arrays->GetElements(result.GetArray(), e, nullptr) && e[0].GetNumber() != 0)
                    {
                        x = static_cast<float>(e[1].GetNumber());
                        y = static_cast<float>(e[2].GetNumber());
                        show = x >= 0 && x <= 1 && y >= 0 && y <= 1;
                    }
                }
            }
            else if (static_cast<int32_t>(now - a.until) >= 0)
                a.who = 0;
            if (!show && !a.shown)
                continue;
            if (Player() && g_chatAbove[slot])
                g_scripts->CallFunction(g_chatAbove[slot], Player(), nullptr, nullptr, 4, a.text.c_str(),
                    *reinterpret_cast<uint32_t*>(&x), *reinterpret_cast<uint32_t*>(&y),
                    static_cast<uint32_t>(!show ? 0 : a.changed || !a.shown ? 2 : 1));
            a.changed = a.changed && !show;
            a.shown = show;
        }
    }

    std::vector<std::string> UpdateChat()
    {
        bool typing, changed, talking;
        std::string text;
        std::vector<std::string> sent;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            typing = g_chatTyping;
            changed = g_chatChanged;
            g_chatChanged = false;
            talking = g_chatTalking;
            text = g_chatDraft.Text();
            sent = std::move(g_chatSent);
            g_chatSent.clear();
        }
        bool entry;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            entry = g_entryState == EntryState::Typing;
        }
        bool keep = typing || entry;
        if (keep != !g_chatKeysOff.empty() && (keep || !g_chatKeysOff.empty()))
            KeepKeysFromGame(keep);
        bool show = typing || talking;
        if (changed || show != g_chatShown)
        {
            ShowChatLine(typing ? "Say: " + text + "_" : std::string("((( Talking )))"), show);
            g_chatShown = show;
        }
        return sent;
    }

    float DistanceToPlayer(const TESObjectREFR* ref)
    {
        TESObjectREFR* player = Player();
        if (!ref || !player)
            return 1e9f;
        float dx = ref->posX - player->posX, dy = ref->posY - player->posY, dz = ref->posZ - player->posZ;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool IsControlPressed(int control)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !Player() ||
            !g_scripts->CallFunction(g_controlPressed, Player(), nullptr, &result, 1, static_cast<uint32_t>(control)))
            return false;
        return result.GetNumber() != 0;
    }

    void MakeCompanion(TESObjectREFR* ref)
    {
        if (!g_compiled || !ref)
            return;
        TESForm* faction = LookupForm(ids::kPlayerFaction);
        if (!faction || faction->typeID != kFormType_TESFaction)
        {
            LOG_WARN("PlayerFaction not found; stand-ins are teammates only");
            faction = nullptr;
        }
        g_scripts->CallFunctionAlt(g_makeCompanion, ref, 1, faction);
    }

    std::vector<Hit> TakeHits()
    {
        std::vector<Hit> hits;
        for (const PendingHit& p : g_myHits)
        {
            TESObjectREFR* ref = LookupRef(p.refId);
            float lost = ref ? p.healthAtHit - Health(ref) : 0;
            LOG_DEBUG("%08X hit %08X for %.1f", p.attackerId, p.refId, lost);
            if (lost > 0)
                hits.push_back({p.refId, p.attackerId, lost});
        }
        g_myHits.clear();
        return hits;
    }

    void ApplyHit(TESObjectREFR* target, TESObjectREFR* by, float damage)
    {
        if (g_compiled && target && by)
            g_scripts->CallFunctionAlt(g_hit, Player(), 3, target, by, FloatArg(damage));
    }

    namespace
    {
        const void* MarkerData(const TESObjectREFR* marker);
    }

    std::vector<TESObjectREFR*> MapMarkers()
    {
        std::vector<TESObjectREFR*> out;
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return out;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerWorldSpaces, [&](void* world)
        {
            const void* cell = At<const void*>(world, layout::kWorldSpaceCell);
            if (!cell)
                return;
            ForEachIn(static_cast<const char*>(cell) + layout::kCellObjects, [&](void* ref)
            {
                if (MarkerData(static_cast<TESObjectREFR*>(ref)))
                    out.push_back(static_cast<TESObjectREFR*>(ref));
            });
        });
        return out;
    }

    namespace
    {
        const void* MarkerData(const TESObjectREFR* marker)
        {
            using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
            const void* extra = marker ? reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(
                &marker->extraDataList, layout::kExtraMapMarker) : nullptr;
            return extra ? At<const void*>(extra, layout::kMapMarkerData) : nullptr;
        }
    }

    bool CanTravelTo(const TESObjectREFR* marker)
    {
        const void* data = MarkerData(marker);
        return data && (At<uint8_t>(data, layout::kMarkerFlags) & layout::kMarkerCanTravel);
    }

    std::string MarkerName(const TESObjectREFR* marker)
    {
        const void* data = MarkerData(marker);
        const char* name = data ? At<const char*>(data, layout::kMarkerName) : nullptr;
        return name ? name : "";
    }

    void UnlockTravel(TESObjectREFR* marker)
    {
        if (g_compiled && g_showMap && marker)
            g_scripts->CallFunctionAlt(g_showMap, Player(), 1, marker);
    }

    TESForm* TeleporterItem()
    {
        if (g_teleporter || !g_compiled || !Player())
            return g_teleporter;
        TESForm* original = LookupForm(ids::kStimpak);
        NVSEArrayVarInterface::Element result;
        if (!original || original->typeID != kFormType_AlchemyItem ||
            !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, original))
            return nullptr;
        TESForm* copy = result.GetTESForm();
        if (!copy || copy->typeID != kFormType_AlchemyItem)
            return nullptr;
        auto* item = static_cast<AlchemyItem*>(copy);
        SetName(item->fullName.name, "Co-op Teleporter");
        item->effects.list.m_listHead.data = nullptr;
        item->effects.list.m_listHead.next = nullptr;
        item->value = 0;
        if (g_questObject)
            g_scripts->CallFunctionAlt(g_questObject, Player(), 1, copy);
        g_teleporter = copy;
        LOG_INFO("Teleporter: item %08X made (a copy of %08X)", copy->refID, original->refID);
        return g_teleporter;
    }

    int ItemCount(TESObjectREFR* ref, TESForm* item)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_itemCount || !ref || !item || !g_scripts->CallFunction(g_itemCount, ref, nullptr, &result, 1, item))
            return 0;
        return static_cast<int>(result.GetNumber());
    }

    void SetRefMapMarker(TESObjectREFR* ref, bool on)
    {
        static const CommandInfo* add = g_cmdTable ? g_cmdTable->GetByName("AddRefMapMarker") : nullptr;
        static const CommandInfo* remove = g_cmdTable ? g_cmdTable->GetByName("RemoveRefMapMarker") : nullptr;
        static bool warned = false;
        const CommandInfo* cmd = on ? add : remove;
        if (!cmd || !cmd->execute)
        {
            if (!warned)
                LOG_WARN("Map: no JIP AddRefMapMarker / RemoveRefMapMarker: friends aren't shown on the map");
            warned = true;
            return;
        }
        if (!ref)
            return;
        double result = 0;
        UInt32 offset = 0;
        cmd->execute(cmd->params, nullptr, ref, nullptr, nullptr, nullptr, &result, &offset);
    }

    void SetOnCompass(TESObjectREFR* actor, bool on)
    {
        void* player = Player();
        if (!player || !actor || actor == player)
            return;
        if (on)
        {
            using SetFn = void(__thiscall*)(void* pc, TESObjectREFR* target, uint8_t hostile, uint8_t hidden);
            reinterpret_cast<SetFn>(addr::kCompassTargetSet)(player, actor, 0, 0);
        }
        else
        {
            using RemoveFn = void(__thiscall*)(void* pc, TESObjectREFR* target);
            reinterpret_cast<RemoveFn>(addr::kCompassTargetRemove)(player, actor);
        }
    }

    TESObjectREFR* SitInNearestChair(TESObjectREFR* actor)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_sitNearest || !actor || !g_scripts->CallFunction(g_sitNearest, Player(), nullptr, &result, 1, actor))
            return nullptr;
        TESForm* form = result.GetTESForm();
        return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
    }

    void PlayAnimFile(TESObjectREFR* actor, const std::string& path)
    {
        std::string file = _strnicmp(path.c_str(), "meshes\\", 7) == 0 ? path.substr(7) : path;
        if (!g_console || !actor || file.empty() || file.find('"') != std::string::npos)
            return;
        std::string line = "PlayAnimationPath \"" + file + "\" 0";
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            g_console->RunScriptLine2(line.c_str(), actor, true);
    }

    void SetCollision(TESObjectREFR* actor, bool on)
    {
        if (g_compiled && g_collision && actor)
            g_scripts->CallFunctionAlt(g_collision, actor, 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    void ShowReviveBar(const std::string& text)
    {
        static std::string s_shown;
        if (text == s_shown || !g_reviveBar || !Player())
            return;
        MakeChatHud();
        g_scripts->CallFunction(g_reviveBar, Player(), nullptr, nullptr, 2, text.empty() ? " " : text.c_str(),
            static_cast<uint32_t>(text.empty() ? 0 : 1));
        s_shown = text;
    }

    namespace
    {
        const void* ChildTile(const void* parent, const char* name)
        {
            int guard = 0;
            for (const void* node = parent ? At<const void*>(parent, layout::kTileFirstChild) : nullptr;
                 node && guard < 1024; node = At<const void*>(node, layout::kChildNodeNext), ++guard)
            {
                const void* child = At<const void*>(node, layout::kChildNodeTile);
                const char* childName = child ? At<const char*>(child, layout::kTileName) : nullptr;
                if (childName && _stricmp(childName, name) == 0)
                    return child;
            }
            return nullptr;
        }

        float TileValue(const void* tile, uint32_t trait)
        {
            using GetFloat = float(__thiscall*)(const void*, uint32_t);
            return tile ? reinterpret_cast<GetFloat>(addr::kTileGetValueFloat)(tile, trait) : 0;
        }

        void ScaleTile(const void* tile, float scale)
        {
            char* node = tile ? At<char*>(tile, layout::kTileNode) : nullptr;
            if (!node)
                return;
            *reinterpret_cast<float*>(node + layout::kNiLocalScale) = scale;
            char* const* children = At<char* const*>(node, layout::kNiNodeChildren);
            uint16_t slots = At<uint16_t>(node, layout::kNiNodeChildSlots);
            for (uint16_t i = 0; children && i < slots && i < 64; ++i)
                if (children[i])
                    std::fill_n(reinterpret_cast<float*>(children[i] + layout::kNiLocalTranslate), 3, 0.0f);
            struct { float time = 0; uint8_t flags[12] = {}; } update;
            using Fn = void(__thiscall*)(void*, void*);
            reinterpret_cast<Fn>(addr::kNiAVObjectUpdate)(node, &update);
        }

        struct TalkSlot
        {
            const void* tile = nullptr;
            uint32_t id = 0;
            uint32_t look = 0xFFFFFFFF;
            float place[6] = {-1, -1, -1, -1, -1, -1};
            float back[7] = {-1, -1, -1, -1, -1, -1, -1};
        };
        TalkSlot g_talkSlots[kTalkSlots];

        template <size_t N> bool Changed(float (&cached)[N], const float (&now)[N])
        {
            bool same = true;
            for (size_t i = 0; i < N; ++i)
                same = same && std::fabs(now[i] - cached[i]) < 0.5f;
            if (!same)
                std::copy(std::begin(now), std::end(now), cached);
            return !same;
        }

        void PlaceTalkSlot(int slot, const float (&place)[6], const float (&back)[7])
        {
            TalkSlot& s = g_talkSlots[slot];
            if (g_talkPlace[slot] && Changed(s.place, place))
                g_scripts->CallFunction(g_talkPlace[slot], Player(), nullptr, nullptr, 6, FloatArg(place[0]),
                    FloatArg(place[1]), FloatArg(place[2]), FloatArg(place[3]), FloatArg(place[4]),
                    static_cast<uint32_t>(place[5]));
            if (g_talkBack[slot] && Changed(s.back, back))
                g_scripts->CallFunction(g_talkBack[slot], Player(), nullptr, nullptr, 7, FloatArg(back[0]),
                    FloatArg(back[1]), FloatArg(back[2]), FloatArg(back[3]), FloatArg(back[4]), FloatArg(back[5]),
                    FloatArg(back[6]));
        }
    }

    void ShowTalkLog(const std::vector<coop::TalkLog::Shown>& lines, const TalkLogLook& look)
    {
        constexpr float kX = 30, kTop = 24, kWidth = 600, kAlpha = 235, kLineAlpha = 150;
        constexpr float kBarAlpha = 120, kBarPad = 6;
        constexpr float kDividerWidth = 740, kDividerHeight = 8, kTextLine = 32, kUnderLetters = 0.2f;
        constexpr uint32_t kHeight = 0xFB0;
        static bool s_showing = true;
        if (!Player() || !g_talkText[0] || !g_talkPlace[0] || !g_talkBack[0] || !g_chatHud ||
            (lines.empty() && !s_showing))
            return;
        MakeChatHud();
        auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
        const void* hud = ChildTile(manager ? At<const void*>(manager, layout::kInterfaceMenuRoot) : nullptr,
            "HUDMainMenu");
        if (!hud)
            return;
        const void* text[kTalkSlots] = {};
        const void* edges[kTalkSlots][kTalkEdges] = {};
        int guard = 0;
        for (const void* node = At<const void*>(hud, layout::kTileFirstChild); node && guard < 4096;
             node = At<const void*>(node, layout::kChildNodeNext), ++guard)
        {
            const void* child = At<const void*>(node, layout::kChildNodeTile);
            const char* name = child ? At<const char*>(child, layout::kTileName) : nullptr;
            if (!name || std::strncmp(name, "NVCoopTalk", 10) != 0)
                continue;
            bool isEdge = std::strncmp(name + 10, "Edge", 4) == 0;
            char* end = nullptr;
            const char* digits = name + (isEdge ? 14 : 10);
            if (*digits < '0' || *digits > '9')
                continue;
            long slot = std::strtol(digits, &end, 10);
            if (slot < 0 || slot >= kTalkSlots)
                continue;
            if (!isEdge)
                text[slot] = child;
            else if (*end == 'x' && end[1] >= '0' && end[1] < '0' + kTalkEdges)
                edges[slot][end[1] - '0'] = child;
        }

        float scale = std::clamp(look.scale, 0.2f, 3.0f);
        uint32_t lookKey = (look.hudColor ? 1u << 24 : 0) | look.red << 16 | look.green << 8 | look.blue;
        int style = std::clamp(look.style, 0, 3);
        bool used[kTalkSlots] = {};
        float hudHeight = static_cast<float>(*reinterpret_cast<const int32_t*>(addr::kDisplayHeightSetting + 4)) *
            *reinterpret_cast<const float*>(addr::kMenuUnitsPerPixel) * 0.5f;
        static bool s_heightLogged = false;
        if (!s_heightLogged)
        {
            s_heightLogged = true;
            LOG_INFO("Talk log: the screen is %.0f high in menu units; the log's top at %.0f", hudHeight,
                hudHeight > 200 ? hudHeight * 0.5f : kTop);
        }
        float y = hudHeight > 200 ? hudHeight * 0.5f : kTop;
        for (const coop::TalkLog::Shown& l : lines)
        {
            int slot = static_cast<int>(l.id % kTalkSlots);
            TalkSlot& s = g_talkSlots[slot];
            if (s.tile != text[slot])
                s = TalkSlot{text[slot]};
            if (!text[slot])
                continue;
            used[slot] = true;
            if (s.id != l.id || s.look != lookKey)
            {
                g_scripts->CallFunction(g_talkText[slot], Player(), nullptr, nullptr, 5,
                    l.text.empty() ? " " : l.text.c_str(), FloatArg(look.hudColor ? 255.0f : -1.0f),
                    FloatArg(look.red), FloatArg(look.green), FloatArg(look.blue));
                s.id = l.id;
                s.look = lookKey;
            }
            ScaleTile(text[slot], scale);
            for (const void* edge : edges[slot])
                ScaleTile(edge, scale);
            float measured = TileValue(text[slot], kHeight);
            float height = (measured > 0 ? measured : kTextLine) * scale;
            float alpha = kAlpha * l.alpha;
            const float place[6] = {y, kWidth / scale, alpha, style == 1 || style == 2 ? alpha : 0,
                style == 2 ? 2.0f : 1.0f, style == 1 ? 1.0f : style == 2 ? 2.0f : 0.0f};
            float divider = y + height + look.gap / 2 - kTextLine * kUnderLetters * scale;
            const float back[7] = {divider - kDividerHeight / 2, kDividerWidth,
                look.dividers ? kLineAlpha * l.alpha : 0,
                y - kBarPad / 2, height + kBarPad, kWidth + 2 * (kX - 22), style == 3 ? kBarAlpha * l.alpha : 0};
            PlaceTalkSlot(slot, place, back);
            y += height + look.gap;
        }
        for (int slot = 0; slot < kTalkSlots; ++slot)
            if (!used[slot])
            {
                g_talkSlots[slot].id = 0;
                const float hidden[6] = {kTop, kWidth, 0, 0, 1, 0};
                const float hiddenBack[7] = {kTop, kWidth, 0, kTop, 1, kWidth, 0};
                PlaceTalkSlot(slot, hidden, hiddenBack);
            }
        s_showing = !lines.empty();
    }

    void SetSpeedMult(TESObjectREFR* actor, int mult)
    {
        if (g_compiled && g_speedMult && actor && mult > 0)
            g_scripts->CallFunctionAlt(g_speedMult, actor, 1, static_cast<uint32_t>(mult));
    }

    void SetAnimOverride(TESObjectREFR* actor, const std::string& path, bool on)
    {
        std::string file = _strnicmp(path.c_str(), "meshes\\", 7) == 0 ? path.substr(7) : path;
        if (!g_console || !actor || file.empty() || file.find('"') != std::string::npos)
            return;
        std::string line = std::string("SetActorAnimationPath 0 ") + (on ? "1" : "0") + " \"" + file + "\"";
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            g_console->RunScriptLine2(line.c_str(), actor, true);
    }

    void SetActorAlpha(TESObjectREFR* actor, float alpha)
    {
        if (g_compiled && g_actorAlpha && actor)
            g_scripts->CallFunctionAlt(g_actorAlpha, actor, 1, FloatArg(alpha));
    }

    uint8_t PlayerDisabledControls()
    {
        const void* player = Player();
        return player ? At<uint8_t>(player, layout::kPlayerDisabledControls) : 0;
    }
}
