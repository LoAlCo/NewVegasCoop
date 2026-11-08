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
    coop::GearLockers g_gearLockers;
    uint32_t g_rewardBox = 0, g_rewardQuest = 0;
    std::vector<coop::RewardItem> g_rewardItems;

    bool IsContainerRef(const TESObjectREFR* ref)
    {
        return ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectCONT;
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

        bool g_replayingLine = false;
        std::vector<net::PlayerCommand> g_replayGiveBacks;
        constexpr size_t kMaxReplayGiveBacks = 8;

        bool WholeInventoryCommand(const Watched& w, ParamInfo* paramInfo, void* scriptData, TESObjectREFR* thisObj,
            Script* scriptObj, ScriptEventList* eventList, UInt32* opcodeOffsetPtr, net::PlayerCommand& c)
        {
            if (w.kind != Kind::RemoveAllItems && w.kind != Kind::RemoveAllTypedItems)
                return false;
            bool typed = w.kind == Kind::RemoveAllTypedItems;
            UInt32 offset = *opcodeOffsetPtr;
            int given = *reinterpret_cast<const UInt16*>(static_cast<const uint8_t*>(scriptData) + offset);
            if (w.params > 8 || given < 1 || given > static_cast<int>(w.params) || given > kMaxWholeArgs[typed])
                return false;
            UInt32 v[8] = {};
            if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &v[0], &v[1], &v[2],
                    &v[3], &v[4], &v[5], &v[6], &v[7]))
                return false;
            TESObjectREFR* player = Player();
            auto* target = reinterpret_cast<TESObjectREFR*>(v[0]);
            c = net::PlayerCommand{};
            if (thisObj && thisObj == player && IsContainerRef(target))
            {
                c.kind = w.kind;
                c.ref = RefId(target);
            }
            else if (target && target == player && IsContainerRef(thisObj))
            {
                c.kind = typed ? Kind::GiveAllTypedItems : Kind::GiveAllItems;
                c.ref = RefId(thisObj);
            }
            else
                return false;
            for (int i = 0; i < 3; ++i)
                c.n[i] = i + 1 < given && (typed || i < 2) ? static_cast<int32_t>(v[i + 1]) : 0;
            if (typed && given >= 5)
                c.form = FormIdOf(reinterpret_cast<TESForm*>(v[4]));
            for (int32_t n : c.n)
                if (n > net::kMaxPlayerCommandNumber || n < -net::kMaxPlayerCommandNumber)
                    return false;
            c.flags = static_cast<uint8_t>(given << net::kCommandArgsShift);
            return true;
        }

        bool ControlSwitches(Kind kind, ParamInfo* paramInfo, void* scriptData, Script* scriptObj,
            ScriptEventList* eventList, UInt32* opcodeOffsetPtr, uint8_t& flags)
        {
            bool off = kind == Kind::DisableControls;
            UInt32 v[7] = {1, 1, 1, 1, off ? 0u : 1u, off ? 0u : 1u, off ? 0u : 1u};
            UInt32 offset = *opcodeOffsetPtr;
            if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &v[0], &v[1], &v[2],
                &v[3], &v[4], &v[5], &v[6]))
                return false;
            flags = 0;
            for (int i = 0; i < 7; ++i)
                if (v[i])
                    flags |= static_cast<uint8_t>(1 << i);
            return true;
        }

        bool Note(const Watched& w, ParamInfo* paramInfo, void* scriptData, TESObjectREFR* thisObj, Script* scriptObj,
            ScriptEventList* eventList, UInt32* opcodeOffsetPtr, net::PlayerCommand& c)
        {
            if (g_mirroring && scriptObj && !g_ourScripts.count(scriptObj) && g_reportedScripts.size() < 256 &&
                g_reportedScripts.insert(scriptObj).second)
                LOG_DEBUG("Scripted: %s from script %08X (type %u)%s", w.name, scriptObj->refID, scriptObj->info.type,
                    g_inLine ? " in a dialogue line" : "");
            if (w.kind == Kind::RemoveAllItems || w.kind == Kind::RemoveAllTypedItems)
            {
                if (!g_mirroring || !scriptObj || g_ourScripts.count(scriptObj) || g_playerCommands.size() >= 128 ||
                    g_replayingLine ||
                    !WholeInventoryCommand(w, paramInfo, scriptData, thisObj, scriptObj, eventList, opcodeOffsetPtr, c))
                    return false;
                if (g_inLine && !(net::IsTakeKind(c.kind) && g_gearLockers.TakenInLine(c.ref)))
                    return false;
                return !net::IsMadeWhilePlaying(c.form) && !net::IsMadeWhilePlaying(c.ref);
            }
            bool anyScript = IsScreenOrControls(w.kind, thisObj);
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
            case Kind::RemoveAllTypedItems:
            case Kind::GiveAllItems:
            case Kind::GiveAllTypedItems:
                return false;
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
                if (!ControlSwitches(w.kind, paramInfo, scriptData, scriptObj, eventList, opcodeOffsetPtr, c.flags))
                    return false;
                break;
            }
            if (net::IsMadeWhilePlaying(c.form) || net::IsMadeWhilePlaying(c.ref))
                return false;
            return true;
        }

        void Relay(const Watched& w, const net::PlayerCommand& c)
        {
            LOG_DEBUG("Scripted: friends follow %s %08X %08X", w.name, c.form, c.ref);
            if (net::IsTakeKind(c.kind))
            {
                if (!g_gearLockers.Is(c.ref))
                    LOG_INFO("Scripted: %08X holds our gear now: a gear locker this session", c.ref);
                g_gearLockers.Add(c.ref, false);
                g_gearLockers.Taken(c);
                LOG_INFO("Gear: our gear into locker %08X (%s, %d arguments, keeps %08X); friends do theirs", c.ref,
                    w.name, c.flags >> net::kCommandArgsShift, c.form);
            }
            else if (net::IsWholeInventoryKind(c.kind) && g_gearLockers.Is(c.ref))
            {
                g_gearLockers.GivenBack(c.ref);
                LOG_INFO("Gear: locker %08X gave our gear back (%s); friends get theirs", c.ref, w.name);
            }
            g_playerCommands.push_back(c);
        }

        int g_holdPlayerEffects = 0;
        const TESObjectREFR* g_ownUseRef = nullptr;
        coop::OwnUseLock g_ownUseLock;
        std::map<const TESObjectREFR*, uint32_t> g_sentByReplay;
        constexpr uint32_t kSentByReplayMs = 60000;

        template <int I>
        bool WatchedCommand(COMMAND_ARGS)
        {
            const Watched& w = g_watched[I];
            coop::UseCommandFacts facts;
            facts.replaying = g_holdPlayerEffects != 0;
            facts.onOurPlayer = !w.popup && thisObj && thisObj == Player();
            facts.screenOrControls = !w.kill && !w.popup && IsScreenOrControls(w.kind, thisObj);
            facts.controls = facts.screenOrControls && (w.kind == Kind::DisableControls || w.kind == Kind::EnableControls);
            facts.opensScreen = w.popup;
            facts.ownUse = g_ownUseRef && thisObj == g_ownUseRef && scriptObj && !g_ourScripts.count(scriptObj) &&
                !g_inLine;
            facts.friendGame = g_screenFromHost;
            coop::UseCommand use = coop::ForUse(facts);
            if (use == coop::UseCommand::NotHere)
            {
                LOG_INFO("Activations: %s%s not done (another player's use, replayed here)", w.name,
                    facts.onOurPlayer ? " on our player" : facts.opensScreen ? " (their screen)" : " (their controls)");
                *result = 0;
                return true;
            }
            if (g_holdPlayerEffects && w.kind == Kind::AddPlayerPackage && thisObj && thisObj != Player() &&
                IsActor(thisObj) && g_sentByReplay.size() < 64)
                g_sentByReplay[thisObj] = GetTickCount() + kSentByReplayMs;
            if (use == coop::UseCommand::OursAlone)
            {
                uint8_t switches = 0x0F;
                if (w.kind == Kind::DisableControls)
                    ControlSwitches(w.kind, paramInfo, scriptData, scriptObj, eventList, opcodeOffsetPtr, switches);
                bool done = w.original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                    opcodeOffsetPtr);
                if (w.kind == Kind::DisableControls && PlayerDisabledControls())
                    g_ownUseLock.Locked(GetTickCount(), switches);
                LOG_INFO("Activations: %s from %08X's script is our own use's: done here, not for the others", w.name,
                    thisObj->refID);
                return done;
            }
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
            if (g_screenFromHost && g_watched[I].kind == Kind::AddItem && thisObj && g_rewardBox &&
                thisObj->refID == g_rewardBox && scriptObj && !g_ourScripts.count(scriptObj))
            {
                LOG_INFO("Gear: %s into our reward box from script %08X not done (ours is filled once, by us)",
                    g_watched[I].name, scriptObj->refID);
                *result = 0;
                return true;
            }
            net::PlayerCommand c;
            if (g_holdPlayerEffects && WholeInventoryCommand(g_watched[I], paramInfo, scriptData, thisObj, scriptObj,
                    eventList, opcodeOffsetPtr, c))
            {
                if (g_gearLockers.ForReplayedUse(c) == coop::GearLockers::InReplay::OursAfter &&
                    g_replayGiveBacks.size() < kMaxReplayGiveBacks)
                {
                    g_replayGiveBacks.push_back(c);
                    LOG_INFO("Gear: locker %08X gives back in another player's use: ours too, after the replay",
                        c.ref);
                }
                else
                    LOG_INFO("Gear: %s (container %08X) in another player's use not done to our player (theirs)",
                        g_watched[I].name, c.ref);
                *result = 0;
                return true;
            }
            if (g_replayingLine && WholeInventoryCommand(g_watched[I], paramInfo, scriptData, thisObj, scriptObj,
                    eventList, opcodeOffsetPtr, c))
            {
                LOG_INFO("Gear: %s (locker %08X) in another player's line not done to our player", g_watched[I].name,
                    c.ref);
                *result = 0;
                return true;
            }
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

    namespace
    {
        Cmd_Execute g_playGroupOriginal = nullptr;
        Cmd_Execute g_setDestroyedOriginal = nullptr;
        bool g_watchObjects = false;
        std::vector<ObjectChange> g_objectChanges;
        constexpr size_t kMaxPendingObjectChanges = 256;
        Script* g_setDestroyedScript = nullptr;
        Script* g_damageObjectScript = nullptr;
        Cmd_Execute g_rewardXpOriginal = nullptr;
        std::map<const TESObjectREFR*, uint32_t> g_heldRewards;
        uint32_t g_rewardsHeld = 0;

        bool RewardXpWatched(COMMAND_ARGS)
        {
            if (thisObj && !g_heldRewards.empty())
            {
                uint32_t now = GetTickCount();
                for (auto it = g_heldRewards.begin(); it != g_heldRewards.end();)
                    it = static_cast<int32_t>(now - it->second) >= 0 ? g_heldRewards.erase(it) : std::next(it);
                if (g_heldRewards.count(thisObj))
                {
                    ++g_rewardsHeld;
                    LOG_INFO("Objects: RewardXP from %08X's script not given here (the game that did it gave it, "
                        "shared)", thisObj->refID);
                    *result = 0;
                    return true;
                }
            }
            return g_rewardXpOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                opcodeOffsetPtr);
        }
        std::map<uint32_t, Script*> g_playGroupScripts;

        bool WatchedObject(TESObjectREFR* ref, Script* script)
        {
            return g_watchObjects && ref && script && !g_ourScripts.count(script) && (ref->refID >> 24) != 0xFF &&
                !IsActor(ref) && g_objectChanges.size() < kMaxPendingObjectChanges;
        }

        bool PlayGroupWatched(COMMAND_ARGS)
        {
            UInt32 group = net::kNoAnimGroup, init = 0;
            bool read = false;
            if (WatchedObject(thisObj, scriptObj))
            {
                UInt32 offset = *opcodeOffsetPtr;
                read = g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &group, &init);
            }
            bool done = g_playGroupOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                opcodeOffsetPtr);
            if (read && group < net::kAnimGroupCount)
                g_objectChanges.push_back({thisObj->refID, true, static_cast<uint16_t>(group), init != 0,
                    IsDestroyed(thisObj)});
            return done;
        }

        bool SetDestroyedWatched(COMMAND_ARGS)
        {
            bool watched = WatchedObject(thisObj, scriptObj);
            bool done = g_setDestroyedOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList,
                result, opcodeOffsetPtr);
            if (watched)
                g_objectChanges.push_back({thisObj->refID, false, net::kNoAnimGroup, false, IsDestroyed(thisObj)});
            return done;
        }
    }

    bool WatchObjectCommands()
    {
        static bool s_done = false;
        if (s_done)
            return true;
        if (!g_cmdTable || !g_scripts)
            return false;
        auto* play = const_cast<CommandInfo*>(g_cmdTable->GetByName("PlayGroup"));
        auto* destroyed = const_cast<CommandInfo*>(g_cmdTable->GetByName("SetDestroyed"));
        auto* reward = const_cast<CommandInfo*>(g_cmdTable->GetByName("RewardXP"));
        if (!play || !play->execute || !destroyed || !destroyed->execute || !reward || !reward->execute)
        {
            LOG_WARN("Objects: no PlayGroup, SetDestroyed or RewardXP command to watch");
            return false;
        }
        g_rewardXpOriginal = reward->execute;
        reward->execute = &RewardXpWatched;
        g_playGroupOriginal = play->execute;
        play->execute = &PlayGroupWatched;
        g_setDestroyedOriginal = destroyed->execute;
        destroyed->execute = &SetDestroyedWatched;
        s_done = true;
        LOG_INFO("Objects: watching PlayGroup, SetDestroyed and RewardXP");
        return true;
    }

    void WatchObjects(bool on)
    {
        g_watchObjects = on;
        if (!on)
            g_objectChanges.clear();
    }

    std::vector<ObjectChange> TakeObjectChanges()
    {
        return std::move(g_objectChanges);
    }

    bool IsDestroyed(const TESObjectREFR* ref)
    {
        return ref && (ref->flags & layout::kRefDestroyed) != 0;
    }

    void SetDestroyedFlag(TESObjectREFR* ref, bool destroyed)
    {
        if (!g_setDestroyedScript && ref)
            g_setDestroyedScript = Compile("set destroyed", "int iOn\nBegin Function {iOn}\n  SetDestroyed iOn\nEnd\n");
        if (g_setDestroyedScript && ref)
            g_scripts->CallFunctionAlt(g_setDestroyedScript, ref, 1, static_cast<uint32_t>(destroyed ? 1 : 0));
    }

    const char* PlayableGroupName(uint16_t group)
    {
        if (group >= net::kAnimGroupCount)
            return nullptr;
        const char* name = *reinterpret_cast<const char* const*>(addr::kAnimGroupInfos + group * addr::kAnimGroupInfoSize);
        if (!name || !*name)
            return nullptr;
        for (size_t i = 0; name[i]; ++i)
            if (i >= 64 || !(std::isalnum(static_cast<unsigned char>(name[i])) || name[i] == '_'))
                return nullptr;
        return name;
    }

    bool PlayObjectGroup(TESObjectREFR* ref, uint16_t group, bool init)
    {
        const char* name = PlayableGroupName(group);
        if (!ref || !name)
            return false;
        uint32_t key = (static_cast<uint32_t>(group) << 1) | (init ? 1 : 0);
        auto it = g_playGroupScripts.find(key);
        if (it == g_playGroupScripts.end())
            it = g_playGroupScripts.emplace(key, Compile("play group", std::string("Begin Function {}\n  PlayGroup ") +
                name + (init ? " 1" : " 0") + "\nEnd\n")).first;
        if (!it->second)
            return false;
        g_scripts->CallFunctionAlt(it->second, ref, 0);
        return true;
    }

    const void* Model(TESObjectREFR* ref)
    {
        return ref ? ref->GetNiNode() : nullptr;
    }

    float ObjectFullHealth(const TESObjectREFR* ref)
    {
        const TESForm* base = ref ? ref->baseForm : nullptr;
        if (!base || (base->typeID != kFormType_TESObjectACTI && base->typeID != kFormType_BGSTalkingActivator) ||
            !(ref->flags & layout::kRefDestructible))
            return 0;
        const void* data = At<const void*>(base, layout::kActivatorDestructible + layout::kDestructibleData);
        return data ? static_cast<float>(At<uint32_t>(data, layout::kDestructibleHealth)) : 0;
    }

    float ObjectSelfDamage(const TESObjectREFR* ref)
    {
        if (ObjectFullHealth(ref) <= 0)
            return 0;
        const void* data = At<const void*>(ref->baseForm, layout::kActivatorDestructible + layout::kDestructibleData);
        uint8_t count = At<uint8_t>(data, layout::kDestructibleStageCount);
        auto* const* stages = At<const void* const*>(data, layout::kDestructibleStages);
        uint32_t most = 0;
        for (uint8_t i = 0; stages && i < count; ++i)
            if (stages[i] && At<uint32_t>(stages[i], layout::kStageSelfDamage) > most)
                most = At<uint32_t>(stages[i], layout::kStageSelfDamage);
        return static_cast<float>(most);
    }

    float ObjectHealth(const TESObjectREFR* ref)
    {
        float full = ObjectFullHealth(ref);
        if (full <= 0)
            return net::kNoHealth;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
            layout::kExtraObjectHealth);
        if (!extra)
            return full;
        float health = At<float>(extra, layout::kObjectHealthValue);
        return std::isfinite(health) ? (health < 0 ? 0 : health) : full;
    }

    namespace
    {
        bool Activator(uint8_t type, uint8_t)
        {
            return type == kFormType_TESObjectACTI || type == kFormType_BGSTalkingActivator;
        }
    }

    void DestructiblesNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(Activator, 0, out, range);
        out.erase(std::remove_if(out.begin(), out.end(), [](TESObjectREFR* r) {
            return ObjectFullHealth(r) <= 0 || (r->refID >> 24) == 0xFF; }), out.end());
    }

    void DamageObjectBy(TESObjectREFR* ref, float damage)
    {
        if (!g_damageObjectScript && ref)
            g_damageObjectScript = Compile("damage object", "float fDamage\nBegin Function {fDamage}\n  DamageObject fDamage\nEnd\n");
        if (g_damageObjectScript && ref && damage > 0)
            g_scripts->CallFunctionAlt(g_damageObjectScript, ref, 1, FloatArg(damage));
    }

    namespace
    {
        bool g_keepActivations = false;
        int g_scriptActivating = 0;
        std::vector<uint32_t> g_activations;
        constexpr size_t kMaxPendingActivations = 16;
        Cmd_Execute g_activateOriginal = nullptr;
        Cmd_Execute g_moveToOriginal = nullptr;
        Script* g_activateAs = nullptr;

        bool IsActivator(const TESObjectREFR* ref)
        {
            uint8_t type = ref && ref->baseForm ? ref->baseForm->typeID : 0;
            return type == kFormType_TESObjectACTI || type == kFormType_BGSTalkingActivator;
        }

        bool ScriptedContainerOrDoor(const TESObjectREFR* ref)
        {
            const TESForm* base = ref ? ref->baseForm : nullptr;
            const Script* script = nullptr;
            if (base && base->typeID == kFormType_TESObjectCONT)
                script = static_cast<const TESObjectCONT*>(base)->scriptForm.script;
            else if (base && base->typeID == kFormType_TESObjectDOOR)
                script = static_cast<const TESObjectDOOR*>(base)->scriptForm.script;
            return script && coop::ScriptHasBlock(script->data, script->info.dataLength, coop::kBlockOnActivate);
        }

        bool LockedAgainstPlayer(TESObjectREFR* ref);

        bool DoorTripEnding()
        {
            TESObjectREFR* player = Player();
            void* process = player ? At<void*>(player, layout::kActorProcess) : nullptr;
            if (!process || At<uint32_t>(process, process::kLevel) != 0)
                return false;
            return At<uint32_t>(process, process::kFadeState) == process::kFadeTeleportOut;
        }

        void OnActivate(TESObjectREFR*, void* parameters)
        {
            auto** args = static_cast<void**>(parameters);
            auto* ref = args ? static_cast<TESObjectREFR*>(args[0]) : nullptr;
            auto* who = args ? static_cast<TESObjectREFR*>(args[1]) : nullptr;
            if (!g_keepActivations || g_scriptActivating || g_holdPlayerEffects || !ref || !who || who != Player() ||
                (ref->refID >> 24) == 0xFF || !(IsActivator(ref) || ScriptedContainerOrDoor(ref)) ||
                LockedAgainstPlayer(ref))
                return;
            if (DoorTripEnding())
            {
                LOG_DEBUG("Activations: %08X activated again as our door trip ends: not a use", ref->refID);
                return;
            }
            g_ownUseRef = ref;
            if (ref->baseForm->typeID != kFormType_BGSTalkingActivator && g_activations.size() < kMaxPendingActivations)
                g_activations.push_back(ref->refID);
        }

        bool LockedAgainstPlayer(TESObjectREFR* ref)
        {
            using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
            const void* lock = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
                layout::kExtraLock);
            auto* data = lock ? *reinterpret_cast<const uint8_t* const*>(static_cast<const char*>(lock) +
                layout::kLockData) : nullptr;
            if (!data || !(data[layout::kLockFlags] & layout::kLockLocked))
                return false;
            auto* key = *reinterpret_cast<TESForm* const*>(data + layout::kLockKey);
            return !(key && ItemCount(Player(), key) > 0);
        }

        struct QuestWatched
        {
            const char* name;
            net::QuestCommandKind kind;
            Cmd_Execute original;
        };
        QuestWatched g_questWatched[] = {
            {"SetStage", net::QuestCommandKind::SetStage, nullptr},
            {"SetObjectiveDisplayed", net::QuestCommandKind::ShowObjective, nullptr},
            {"SetObjectiveCompleted", net::QuestCommandKind::CompleteObjective, nullptr},
            {"CompleteQuest", net::QuestCommandKind::CompleteQuest, nullptr}};
        constexpr int kQuestWatched = sizeof(g_questWatched) / sizeof(g_questWatched[0]);
        std::function<bool(uint32_t)> g_hostRunsQuest;
        std::vector<net::QuestCommand> g_questCommands;
        constexpr size_t kMaxPendingQuestCommands = 32;
        coop::RecentQuestCommands g_sentQuestCommands;
        Script* g_setStageScript = nullptr;

        template <int I>
        bool QuestCommandWatched(COMMAND_ARGS)
        {
            const QuestWatched& w = g_questWatched[I];
            auto run = [&] {
                return w.original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                    opcodeOffsetPtr);
            };
            if (!g_screenFromHost || !g_hostRunsQuest || !scriptObj)
                return run();
            coop::QuestCommandFacts f;
            f.friendGame = true;
            f.objectScript = scriptObj->info.type == Script::eType_Object && scriptObj->refID != 0 &&
                (scriptObj->refID >> 24) != 0xFF;
            f.oursOrLine = g_ourScripts.count(scriptObj) || g_inLine;
            if (!f.objectScript || f.oursOrLine)
                return run();
            UInt32 offset = *opcodeOffsetPtr, number = 0, on = 0;
            TESForm* quest = nullptr;
            bool read = w.kind == net::QuestCommandKind::CompleteQuest ?
                g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &quest) :
                w.kind == net::QuestCommandKind::SetStage ?
                g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &quest, &number) :
                g_scripts->ExtractArgsEx(paramInfo, scriptData, &offset, scriptObj, eventList, &quest, &number, &on);
            if (!read || !quest || quest->typeID != kFormType_TESQuest)
                return run();
            f.hostQuest = g_hostRunsQuest(quest->refID);
            f.replaying = g_holdPlayerEffects != 0;
            f.sentAsUse = thisObj && std::find(g_activations.begin(), g_activations.end(), thisObj->refID) !=
                g_activations.end();
            switch (coop::ForQuestCommand(f))
            {
            case coop::QuestCommandHere::AsUsual:
                return run();
            case coop::QuestCommandHere::NotHere:
                LOG_INFO("Quests: %s %08X %u %u from script %08X not done here (%s; the host's game decides quests)",
                    w.name, quest->refID, number, on, scriptObj->refID,
                    f.replaying ? "another player's use, replayed" : "our use, sent whole to the host");
                break;
            case coop::QuestCommandHere::ToHost:
            {
                net::QuestCommand c{w.kind, quest->refID, number, static_cast<uint8_t>(on ? 1 : 0)};
                if (w.kind == net::QuestCommandKind::CompleteQuest)
                    c.number = c.on = 0;
                if (w.kind == net::QuestCommandKind::SetStage)
                    c.on = 0;
                bool fits = w.kind == net::QuestCommandKind::SetStage ? number <= 255 :
                    w.kind == net::QuestCommandKind::CompleteQuest || number <= net::kMaxObjectiveIndex;
                if (!fits)
                    LOG_WARN("Quests: %s %08X %u from script %08X: out of range, not sent", w.name, quest->refID,
                        number, scriptObj->refID);
                else if (g_questCommands.size() < kMaxPendingQuestCommands &&
                    g_sentQuestCommands.ShouldSend(static_cast<uint8_t>(c.kind), c.quest, c.number, c.on,
                        GetTickCount()))
                {
                    g_questCommands.push_back(c);
                    LOG_INFO("Quests: %s %08X %u %u from script %08X (ours) sent to the host, not done here", w.name,
                        quest->refID, c.number, c.on, scriptObj->refID);
                }
                break;
            }
            }
            *result = 0;
            return true;
        }
        template <int... I>
        constexpr std::array<Cmd_Execute, sizeof...(I)> QuestWrappers(std::integer_sequence<int, I...>)
        {
            return {&QuestCommandWatched<I>...};
        }

        bool ActivateWatched(COMMAND_ARGS)
        {
            if (g_holdPlayerEffects && thisObj && !(scriptObj && g_ourScripts.count(scriptObj)) &&
                (!thisObj->baseForm || thisObj->baseForm->typeID != kFormType_TESObjectACTI))
            {
                LOG_INFO("Activations: %08X's Activate not done (another player's use, replayed here; not an activator)",
                    thisObj->refID);
                *result = 0;
                return true;
            }
            ++g_scriptActivating;
            bool done = g_activateOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                opcodeOffsetPtr);
            --g_scriptActivating;
            return done;
        }

        bool MoveToWatched(COMMAND_ARGS)
        {
            if (g_holdPlayerEffects && thisObj && thisObj == Player())
            {
                LOG_INFO("Activations: MoveTo on our player not done (another player's use, replayed here)");
                *result = 0;
                return true;
            }
            return g_moveToOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList, result,
                opcodeOffsetPtr);
        }

        Cmd_Execute g_startConversationOriginal = nullptr;
        bool StartConversationWatched(COMMAND_ARGS)
        {
            if (thisObj && !g_sentByReplay.empty())
            {
                uint32_t now = GetTickCount();
                for (auto it = g_sentByReplay.begin(); it != g_sentByReplay.end();)
                    it = coop::IsDue(now, it->second) ? g_sentByReplay.erase(it) : std::next(it);
                auto it = g_sentByReplay.find(thisObj);
                if (it != g_sentByReplay.end())
                {
                    g_sentByReplay.erase(it);
                    LOG_INFO("Activations: %08X's StartConversation not done (it follows another player's use, "
                        "replayed here: their conversation)", thisObj->refID);
                    *result = 0;
                    return true;
                }
            }
            return g_startConversationOriginal(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList,
                result, opcodeOffsetPtr);
        }

        struct MenuCommand
        {
            const char* name = nullptr;
            Cmd_Execute original = nullptr;
        };
        constexpr int kMenuCommands = static_cast<int>(std::size(opcodes::kOpensScreen) +
            std::size(opcodes::kOpensScreenToo));
        MenuCommand g_menuCommands[kMenuCommands];

        template <int I>
        bool MenuCommandWatched(COMMAND_ARGS)
        {
            if (g_holdPlayerEffects)
            {
                LOG_INFO("Activations: %s not done (another player's use, replayed here: their screen)",
                    g_menuCommands[I].name);
                *result = 0;
                return true;
            }
            return g_menuCommands[I].original(paramInfo, scriptData, thisObj, containingObj, scriptObj, eventList,
                result, opcodeOffsetPtr);
        }
        template <int... I>
        constexpr std::array<Cmd_Execute, sizeof...(I)> MenuWrappers(std::integer_sequence<int, I...>)
        {
            return {&MenuCommandWatched<I>...};
        }
    }

    bool WatchActivations()
    {
        static bool s_done = false;
        if (s_done)
            return true;
        if (!g_cmdTable || !g_events)
            return false;
        auto* activate = const_cast<CommandInfo*>(g_cmdTable->GetByName("Activate"));
        auto* moveTo = const_cast<CommandInfo*>(g_cmdTable->GetByName("MoveTo"));
        if (!activate || !activate->execute || !moveTo || !moveTo->execute ||
            !g_events->SetNativeEventHandler("OnActivate", OnActivate))
        {
            LOG_WARN("Activations: no Activate or MoveTo command, or no OnActivate event");
            return false;
        }
        g_activateOriginal = activate->execute;
        activate->execute = &ActivateWatched;
        g_moveToOriginal = moveTo->execute;
        moveTo->execute = &MoveToWatched;
        if (auto* talk = const_cast<CommandInfo*>(g_cmdTable->GetByName("StartConversation")); talk && talk->execute)
        {
            g_startConversationOriginal = talk->execute;
            talk->execute = &StartConversationWatched;
        }
        else
            LOG_WARN("Activations: no StartConversation command; a replayed use's conversation may start here");
        static constexpr auto kQuestWrappers = QuestWrappers(std::make_integer_sequence<int, kQuestWatched>{});
        for (int i = 0; i < kQuestWatched; ++i)
        {
            auto* c = const_cast<CommandInfo*>(g_cmdTable->GetByName(g_questWatched[i].name));
            if (c && c->execute)
            {
                g_questWatched[i].original = c->execute;
                c->execute = kQuestWrappers[i];
                LOG_DEBUG("Quests: watching %s (%u parameters) for a friend's object scripts", g_questWatched[i].name,
                    c->numParams);
            }
            else
                LOG_WARN("Quests: no %s command; a friend's object scripts' %s stays in their game",
                    g_questWatched[i].name, g_questWatched[i].name);
        }
        static constexpr auto kMenuWrappers = MenuWrappers(std::make_integer_sequence<int, kMenuCommands>{});
        int menus = 0;
        for (int i = 0; i < kMenuCommands; ++i)
        {
            size_t first = std::size(opcodes::kOpensScreen);
            uint16_t opcode = i < static_cast<int>(first) ? opcodes::kOpensScreen[i] : opcodes::kOpensScreenToo[i - first];
            auto* c = opcode == opcodes::kStartConversation ? nullptr :
                const_cast<CommandInfo*>(g_cmdTable->GetByOpcode(opcode));
            if (c && c->execute)
            {
                g_menuCommands[i].name = c->longName;
                g_menuCommands[i].original = c->execute;
                c->execute = kMenuWrappers[i];
                ++menus;
            }
            else if (opcode != opcodes::kStartConversation)
                LOG_DEBUG("Activations: no command %04X", opcode);
        }
        s_done = true;
        LOG_INFO("Activations: watching activators used (%d of %d menu commands)", menus, kMenuCommands);
        return true;
    }

    void KeepActivations(bool on)
    {
        g_keepActivations = on;
        if (!on)
        {
            g_activations.clear();
            g_ownUseRef = nullptr;
            g_ownUseLock.Clear();
            g_sentByReplay.clear();
        }
    }

    bool UsedLikeActivator(const TESObjectREFR* ref)
    {
        return (ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectACTI) ||
            ScriptedContainerOrDoor(ref);
    }

    void SendQuestCommandsOf(std::function<bool(uint32_t)> hostRuns)
    {
        g_hostRunsQuest = std::move(hostRuns);
        g_questCommands.clear();
        g_sentQuestCommands.Clear();
    }

    std::vector<net::QuestCommand> TakeQuestCommands()
    {
        return std::move(g_questCommands);
    }

    bool RunQuestCommand(const net::QuestCommand& c, const char** skipped)
    {
        *skipped = nullptr;
        TESForm* quest = LookupForm(c.quest);
        QuestInfo info;
        if (!quest || quest->typeID != kFormType_TESQuest || !Player() || !ReadQuest(c.quest, info))
        {
            *skipped = "not a quest here";
            return false;
        }
        switch (c.kind)
        {
        case net::QuestCommandKind::SetStage:
            if (std::find(info.done.begin(), info.done.end(), static_cast<uint8_t>(c.number)) != info.done.end())
            {
                *skipped = "that stage is done here";
                return false;
            }
            if (!g_setStageScript)
                g_setStageScript = Compile("set stage",
                    "ref rQuest\n"
                    "int iStage\n"
                    "Begin Function {rQuest, iStage}\n"
                    "  SetStage rQuest iStage\n"
                    "End\n");
            return g_setStageScript && g_scripts->CallFunctionAlt(g_setStageScript, Player(), 2, quest, c.number);
        case net::QuestCommandKind::ShowObjective:
            return ShowObjective(c.quest, c.number, c.on != 0);
        case net::QuestCommandKind::CompleteObjective:
            return CompleteObjective(c.quest, c.number, c.on != 0);
        case net::QuestCommandKind::CompleteQuest:
            if (info.flags & coop::kQuestCompleted)
            {
                *skipped = "completed here";
                return false;
            }
            return CompleteQuest(c.quest);
        }
        return false;
    }

    std::vector<uint32_t> TakeActivations()
    {
        g_ownUseRef = nullptr;
        return std::move(g_activations);
    }

    void UpdateOwnUseLock(uint32_t now, bool inConversation)
    {
        switch (g_ownUseLock.Update(now, PlayerDisabledControls() != 0, inConversation))
        {
        case coop::OwnUseLock::Step::Over:
            LOG_INFO("Activations: our own use's controls lock is over (given back by its own scripts)");
            break;
        case coop::OwnUseLock::Step::GiveBack:
            GiveBackOwnUseControls("nothing gave them back for 60 s");
            break;
        default:
            break;
        }
    }

    bool OwnUseHoldsControls() { return g_ownUseLock.On(); }

    void GiveBackOwnUseControls(const char* why)
    {
        if (!g_ownUseLock.On())
            return;
        uint8_t s = g_ownUseLock.Switches();
        g_ownUseLock.Clear();
        TESObjectREFR* player = Player();
        if (!g_pcEnable || !player)
            return;
        g_scripts->CallFunctionAlt(g_pcEnable, player, 7, s & 1u, (s >> 1) & 1u, (s >> 2) & 1u, (s >> 3) & 1u,
            (s >> 4) & 1u, (s >> 5) & 1u, (s >> 6) & 1u);
        LOG_WARN("Activations: our own use's controls lock given back (%s): switches %02X", why, s);
    }

    void HoldPlayerEffects(bool on)
    {
        g_holdPlayerEffects += on ? 1 : -1;
        if (g_holdPlayerEffects < 0)
            g_holdPlayerEffects = 0;
    }

    void ActivateAsPlayer(TESObjectREFR* ref)
    {
        if (!g_activateAs && ref)
            g_activateAs = Compile("activate as player", "ref rWho\nBegin Function {rWho}\n  Activate rWho 1\nEnd\n");
        if (g_activateAs && ref && Player())
            g_scripts->CallFunctionAlt(g_activateAs, ref, 1, Player());
    }

    bool LabActivate(TESObjectREFR* ref)
    {
        if (!ref || !Player())
            return false;
        using ActivateFn = bool(__thiscall*)(TESObjectREFR*, TESObjectREFR*, UInt32, UInt32, UInt32);
        return reinterpret_cast<ActivateFn>(addr::kRefActivate)(ref, Player(), 0, 0, 1);
    }

    void HoldRewardsFrom(const TESObjectREFR* ref, uint32_t ms)
    {
        if (ref && g_heldRewards.size() < 256)
            g_heldRewards[ref] = GetTickCount() + ms;
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
            if (g_watched[i].kind == Kind::RemoveAllItems || g_watched[i].kind == Kind::RemoveAllTypedItems)
                LOG_INFO("Gear: %s has %u parameters%s", g_watched[i].name, info->numParams,
                    info->numParams <= 8 ? "" : " (more than we read: not followed)");
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

    void ReplayingOthersLine(bool on) { g_replayingLine = on; }

    std::vector<net::PlayerCommand> TakePlayerCommands() { return std::move(g_playerCommands); }

    int GiveBackAfterReplay()
    {
        std::vector<net::PlayerCommand> gives = std::move(g_replayGiveBacks);
        g_replayGiveBacks.clear();
        int done = 0;
        for (const net::PlayerCommand& c : gives)
            if (ApplyPlayerCommand(c))
            {
                g_gearLockers.GivenBack(c.ref);
                ++done;
            }
        return done;
    }

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
        case Kind::RemoveAllTypedItems:
        case Kind::GiveAllItems:
        case Kind::GiveAllTypedItems:
        {
            if (!container)
                return false;
            bool typed = c.kind == Kind::RemoveAllTypedItems || c.kind == Kind::GiveAllTypedItems;
            bool take = net::IsTakeKind(c.kind);
            int args = (c.flags & net::kCommandArgsMask) >> net::kCommandArgsShift;
            if (args < 1 || args > kMaxWholeArgs[typed])
                args = kMaxWholeArgs[typed];
            Script* script = take ? g_pcTake[typed][args] : g_pcGive[typed][args];
            if (!script)
                return false;
            TESForm* keep = nullptr;
            if (typed && args >= 5)
            {
                uint32_t list = coop::HoldoutListFor(c.form, Number(g_sneak, player));
                keep = list ? LookupForm(list) : nullptr;
                if (list && (!keep || keep->typeID != kFormType_BGSListForm))
                    return false;
                if (list != c.form)
                    LOG_INFO("Gear: our own Sneak keeps %08X (the host's kept %08X)", list, c.form);
            }
            if (take)
            {
                g_gearLockers.Add(c.ref, false);
                std::vector<Item> held;
                if ((c.flags & net::kCommandOnlyIfEmpty) && (!Contents(ref, held) || !held.empty()))
                {
                    LOG_INFO("Gear: locker %08X already holds %zu kinds of ours: the host's take isn't done again",
                        c.ref, held.size());
                    return true;
                }
            }
            std::vector<Item> inside;
            if (Contents(ref, inside) && !take && inside.empty())
                return true;
            if (take)
                g_scripts->CallFunctionAlt(script, player, static_cast<UInt8>(args), ref, n(0), n(1), n(2), keep);
            else
                g_scripts->CallFunctionAlt(script, ref, static_cast<UInt8>(args - 1), n(0), n(1), n(2), keep);
            size_t was = inside.size();
            Contents(ref, inside);
            LOG_INFO("Gear: %s locker %08X (%s, %d arguments, keeps %08X): it holds %zu kinds of items (%zu before)",
                take ? "our gear into" : "our gear back out of", c.ref, typed ? "typed" : "all", args,
                keep ? keep->refID : 0u, inside.size(), was);
            return true;
        }
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
}
