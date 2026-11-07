// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "coop/gear_lockers.h"
#include "coop/activator_uses.h"
#include "coop/chat.h"
#include "coop/quests.h"
#include "coop/talk_log.h"
#include "coop/vats_feel.h"
#include "coop/balance.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <utility>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <deque>
#include <functional>
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
#include "game/internal.h"

namespace game
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
        std::map<uint32_t, BGSProjectile*> g_harmlessProjectiles;
        std::map<uint32_t, BGSExplosion*> g_harmlessExplosions;
        std::map<uint32_t, BGSExplosion*> g_quietExplosions;
        uint32_t g_myWeapon = 0;
        int g_myRounds = -1;
        int g_myAction = -1;
        coop::ShotCounter g_myShots;
        std::vector<uint32_t> g_standInBases;
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

        Script* g_notify = nullptr;
        Script* g_notifyFor = nullptr;
        Script* g_moveBy = nullptr;
        Script* g_health = nullptr;
        Script* g_medicine = nullptr;
        Script* g_level = nullptr;
        Script* g_hit = nullptr;
        Script* g_maxHealth = nullptr;
        Script* g_changeHealth = nullptr;
        Script* g_makeCompanion = nullptr;
        Script* g_stopFighting = nullptr;
        Script* g_kill = nullptr;
        Script* g_unprotect = nullptr;
        Script* g_killPlain = nullptr;
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
        Script* g_ghost = nullptr;
        Script* g_controls = nullptr;
        Script* g_controlPressed = nullptr;
        Script* g_menuMode = nullptr;
        Script* g_keyPressed = nullptr;
        Script* g_chatHud = nullptr;
        Script* g_chatShow = nullptr;
        Script* g_onScreen = nullptr;
        Script* g_chatAbove[kChatAboveSlots] = {};
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
        Script* g_talkText[kTalkSlots] = {};
        Script* g_talkPlace[kTalkSlots] = {};
        Script* g_talkBack[kTalkSlots] = {};
        Script* g_talkSide[kTalkSlots] = {};
        Script* g_feedText[kFeedSlots] = {};
        Script* g_feedPlace[kFeedSlots] = {};
        Script* g_feedBack[kFeedSlots] = {};
        Script* g_feedSide[kFeedSlots] = {};
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
        Script* g_pcTake[2][6] = {};
        Script* g_pcGive[2][6] = {};
        Script* g_sneak = nullptr;
        Script* g_rewardAdd = nullptr;
        Script* g_rewardFlag = nullptr;
        Script* g_rewardMark = nullptr;
        Script* g_pcEquip = nullptr;
        Script* g_pcUnequip = nullptr;
        Script* g_pcAddPerk = nullptr;
        Script* g_pcRemovePerk = nullptr;
        Script* g_perkRanks = nullptr;
        Script* g_creatureType = nullptr;
        Script* g_noteInject = nullptr;
        Script* g_noteShow = nullptr;
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

        std::vector<PendingHit> g_myHits;
        constexpr size_t kMaxPendingHits = 64;

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
        g_medicine = Compile("medicine",
            "float fSkill\n"
            "Begin Function {}\n"
            "  set fSkill to GetActorValue Medicine\n"
            "  SetFunctionValue fSkill\n"
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
        g_unprotect = Compile("unprotect",
            "Begin Function {}\n"
            "  SetActorRefEssential 0\n"
            "  SetGhost 0\n"
            "End\n");
        g_killPlain = Compile("kill plain",
            "Begin Function {}\n"
            "  Kill\n"
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
        g_ghost = Compile("ghost",
            "int iOn\n"
            "Begin Function {iOn}\n"
            "  SetGhost iOn\n"
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
        for (int slot = 0; slot < kFeedSlots; ++slot)
        {
            std::string n = std::to_string(slot);
            std::string text = "HUDMainMenu/NVCoopFeed" + n;
            std::string edge = "HUDMainMenu/NVCoopFeedEdge" + n + "x";
            std::string line = "HUDMainMenu/NVCoopFeedLine" + n;
            std::string bar = "HUDMainMenu/NVCoopFeedBar" + n;
            std::string setText = "  SetUIStringEx \"" + text + "/string\" \"%z\" sText\n"
                "  SetUIFloat \"" + text + "/justify\" fJustify\n"
                "  SetUIStringEx \"" + line + "/filename\" \"%z\" sLine\n";
            std::string placeEdges;
            for (int e = 0; e < kFeedEdges; ++e)
            {
                std::string path = edge + std::to_string(e);
                setText += "  SetUIStringEx \"" + path + "/string\" \"%z\" sText\n"
                    "  SetUIFloat \"" + path + "/justify\" fJustify\n";
                placeEdges += std::string("  set fEdgeX to ") + (e % 2 ? "fX + fOff" : "fX - fOff") + "\n"
                    "  set fEdgeY to fY " + (e / 2 ? "+" : "-") + " fOff\n"
                    "  SetUIFloat \"" + path + "/x\" fEdgeX\n"
                    "  SetUIFloat \"" + path + "/y\" fEdgeY\n"
                    "  SetUIFloat \"" + path + "/wrapwidth\" fWrap\n"
                    "  SetUIFloat \"" + path + "/alpha\" fEdge\n"
                    "  SetUIFloat \"" + path + "/visible\" " + (e == 3 ? "iLast" : "iShow") + "\n";
            }
            g_feedText[slot] = Compile("chat panel text",
                "string_var sText\n"
                "float fJustify\n"
                "string_var sLine\n"
                "Begin Function {sText, fJustify, sLine}\n" + setText +
                "End\n");
            g_feedPlace[slot] = Compile("chat panel place",
                "float fY\n"
                "float fWrap\n"
                "float fAlpha\n"
                "float fEdge\n"
                "float fOff\n"
                "int iEdges\n"
                "float fX\n"
                "float fEdgeX\n"
                "float fEdgeY\n"
                "int iShow\n"
                "int iLast\n"
                "Begin Function {fY, fWrap, fAlpha, fEdge, fOff, iEdges, fX}\n"
                "  set iShow to fAlpha > 0\n"
                "  SetUIFloat \"" + text + "/x\" fX\n"
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
            g_feedSide[slot] = Compile("chat panel side",
                "float fLineX\n"
                "float fBarX\n"
                "Begin Function {fLineX, fBarX}\n"
                "  SetUIFloat \"" + line + "/x\" fLineX\n"
                "  SetUIFloat \"" + bar + "/x\" fBarX\n"
                "End\n");
            g_feedBack[slot] = Compile("chat panel back",
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
        for (int slot = 0; slot < kTalkSlots; ++slot)
        {
            std::string n = std::to_string(slot);
            std::string text = "HUDMainMenu/NVCoopTalk" + n;
            std::string edge = "HUDMainMenu/NVCoopTalkEdge" + n + "x";
            std::string line = "HUDMainMenu/NVCoopTalkLine" + n;
            std::string bar = "HUDMainMenu/NVCoopTalkBar" + n;
            std::string setText = "  SetUIStringEx \"" + text + "/string\" \"%z\" sText\n";
            setText += "  SetUIFloat \"" + text + "/justify\" fJustify\n"
                "  SetUIStringEx \"" + line + "/filename\" \"%z\" sLine\n";
            for (int e = 0; e < kTalkEdges; ++e)
            {
                setText += "  SetUIStringEx \"" + edge + std::to_string(e) + "/string\" \"%z\" sText\n";
                setText += "  SetUIFloat \"" + edge + std::to_string(e) + "/justify\" fJustify\n";
            }
            g_talkText[slot] = Compile("talk log text",
                "string_var sText\n"
                "float fBright\n"
                "float fRed\n"
                "float fGreen\n"
                "float fBlue\n"
                "float fJustify\n"
                "string_var sLine\n"
                "Begin Function {sText, fBright, fRed, fGreen, fBlue, fJustify, sLine}\n" + setText +
                "  SetUIFloat \"" + text + "/brightness\" fBright\n"
                "  SetUIFloat \"" + text + "/red\" fRed\n"
                "  SetUIFloat \"" + text + "/green\" fGreen\n"
                "  SetUIFloat \"" + text + "/blue\" fBlue\n"
                "End\n");
            std::string placeEdges;
            for (int e = 0; e < kTalkEdges; ++e)
            {
                std::string path = edge + std::to_string(e);
                placeEdges += std::string("  set fX to ") + (e % 2 ? "fBaseX + fOff" : "fBaseX - fOff") + "\n"
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
                "float fBaseX\n"
                "float fX\n"
                "float fEdgeY\n"
                "int iShow\n"
                "int iLast\n"
                "Begin Function {fY, fWrap, fAlpha, fEdge, fOff, iEdges, fBaseX}\n"
                "  set iShow to fAlpha > 0\n"
                "  SetUIFloat \"" + text + "/x\" fBaseX\n"
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
            g_talkSide[slot] = Compile("talk log side",
                "float fLineX\n"
                "float fBarX\n"
                "Begin Function {fLineX, fBarX}\n"
                "  SetUIFloat \"" + line + "/x\" fLineX\n"
                "  SetUIFloat \"" + bar + "/x\" fBarX\n"
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
            "  ResurrectActor 1\n"
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
        {
            const char* const kArgs[5] = {"rTo", "iA", "iB", "iType", "rList"};
            for (int typed = 0; typed < 2; ++typed)
                for (int k = 1; k <= kMaxWholeArgs[typed]; ++k)
                {
                    const char* command = typed ? "RemoveAllTypedItems" : "RemoveAllItems";
                    std::string vars = "ref rTo\nint iA\nint iB\nint iType\nref rList\n", takeParams, giveParams,
                        takeArgs, giveArgs;
                    for (int i = 0; i < k; ++i)
                    {
                        takeParams += std::string(i ? ", " : "") + kArgs[i];
                        takeArgs += std::string(" ") + kArgs[i];
                        if (i)
                        {
                            giveParams += std::string(i > 1 ? ", " : "") + kArgs[i];
                            giveArgs += std::string(" ") + kArgs[i];
                        }
                    }
                    std::string name = std::string(typed ? "pc typed " : "pc all ") + std::to_string(k);
                    g_pcTake[typed][k] = Compile((name + " take").c_str(), vars + "Begin Function {" + takeParams +
                        "}\n  " + command + takeArgs + "\nEnd\n");
                    g_pcGive[typed][k] = Compile((name + " give").c_str(), vars + "Begin Function {" + giveParams +
                        "}\n  " + command + " player" + giveArgs + "\nEnd\n");
                }
        }
        g_rewardAdd = Compile("reward add",
            "ref rItem\n"
            "float fHealth\n"
            "Begin Function {rItem, fHealth}\n"
            "  AddItemHealthPercent rItem 1 fHealth\n"
            "End\n");
        g_rewardFlag = Compile("reward flag",
            "float fV\n"
            "Begin Function {}\n"
            "  set fV to AuxVarGetFlt \"_NVCoopHHReward\"\n"
            "  SetFunctionValue fV\n"
            "End\n");
        g_rewardMark = Compile("reward mark",
            "Begin Function {}\n"
            "  AuxVarSetFlt \"_NVCoopHHReward\" 1\n"
            "End\n");
        g_sneak = Compile("sneak",
            "float fV\n"
            "Begin Function {}\n"
            "  set fV to GetActorValue Sneak\n"
            "  SetFunctionValue fV\n"
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
        g_creatureType = Compile("creature type",
            "int iType\n"
            "int iIs\n"
            "Begin Function {iType}\n"
            "  let iIs := GetIsCreatureType iType\n"
            "  SetFunctionValue iIs\n"
            "End\n");
        {
            const std::string box = "\"LevelUpMenu/NOGLOW_BRANCH/LUM_MainRect/LUM_SelectionInfoRect";
            const std::string note = "LevelUpMenu/NOGLOW_BRANCH/LUM_MainRect/LUM_SelectionInfoRect/NVCoopPartyNote/";
            g_noteInject = Compile("level-up note",
                "float fR\n"
                "float fG\n"
                "float fB\n"
                "Begin Function {fR, fG, fB}\n"
                "  InjectUIComponent " + box + "\" \"<text name=%qNVCoopPartyNote%q><font>2</font><y>20</y>"
                "<wrapwidth><copy src=%qparent()%q trait=%qwidth%q/><sub>40</sub></wrapwidth>"
                "<brightness>-1</brightness><visible>0</visible></text>\"\n"
                "  SetUIFloat \"" + note + "red\" fR\n"
                "  SetUIFloat \"" + note + "green\" fG\n"
                "  SetUIFloat \"" + note + "blue\" fB\n"
                "  SetUIString \"" + note + "string\" \"" + std::string(kPartyPerkTakenText) + "\"\n"
                "End\n");
            g_noteShow = Compile("level-up note shown",
                "int iShow\n"
                "float fV\n"
                "Begin Function {iShow}\n"
                "  if iShow\n"
                "    SetUIFloat \"" + note + "visible\" 1\n"
                "    let fV := GetUIFloat \"" + note + "height\"\n"
                "    let fV := fV + 30\n"
                "    SetUIFloat \"LevelUpMenu/NOGLOW_BRANCH/LUM_MainRect/LUM_SelectionInfoRect/LUM_SelectionText/y\" fV\n"
                "  else\n"
                "    SetUIFloat \"" + note + "visible\" 0\n"
                "    SetUIFloat \"LevelUpMenu/NOGLOW_BRANCH/LUM_MainRect/LUM_SelectionInfoRect/LUM_SelectionText/y\" 20\n"
                "  endif\n"
                "End\n");
        }
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
            g_hit && g_maxHealth && g_changeHealth && g_makeCompanion && g_stopFighting && g_kill && g_unconscious && g_ghost && g_controls &&
            g_controlPressed && g_menuMode;
        if (g_compiled)
            LOG_INFO("Game scripts ready");
        return g_compiled;
    }

    bool RunConsoleLine(const char* line, TESObjectREFR* on)
    {
        if (!g_console || !line)
            return false;
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            return g_console->RunScriptLine2(line, on, false);
        return g_console->RunScriptLine(line, on);
    }

    std::string Evaluate(const char* expression, TESObjectREFR* on)
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
        if (!g_scripts->CallFunction(script, on ? on : Player(), nullptr, &result, 0))
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
}
