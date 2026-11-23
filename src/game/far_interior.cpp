// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/far_interior.h"

#include "game/addresses.h"
#include "game/game.h"
#include "game/internal.h"
#include "log.h"

#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

namespace game
{
    namespace
    {
        template <typename T>
        T& Field(void* object, uint32_t offset)
        {
            return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
        }

        void* Tes() { return *reinterpret_cast<void**>(layout::kTesSingleton); }
        int StateOf(void* cell) { return reinterpret_cast<int(__thiscall*)(void*)>(layout::kCellState)(cell); }
        void* NodeOf(void* cell) { return reinterpret_cast<void*(__thiscall*)(void*)>(layout::kCellNode)(cell); }

        std::set<void*> g_ours;
        uint32_t g_oursChanged = 0;

        void* InteriorCell(uint32_t id)
        {
            TESForm* form = LookupForm(id);
            if (!form || form->typeID != kFormType_TESObjectCELL)
                return nullptr;
            return (Field<uint8_t>(form, layout::kCellFlags) & 1) ? form : nullptr;
        }

        void SetHidden(void* cell, bool hidden)
        {
            if (void* node = NodeOf(cell))
            {
                uint32_t& flags = Field<uint32_t>(node, layout::kNiFlags);
                flags = hidden ? (flags | 1u) : (flags & ~1u);
            }
        }
    }

    bool AttachFarInterior(uint32_t cellId, std::string& why)
    {
        void* cell = InteriorCell(cellId);
        if (!cell)
            return why = "not an interior cell", false;
        TESObjectREFR* player = Player();
        if (!Tes() || !player)
            return why = "no game yet", false;
        if (player->parentCell == cell)
            return why = "our player is in it", false;
        int state = StateOf(cell), before = state;
        if (state == 5 || state == 6)
            return why = "attached already", false;
        void* root = Field<void*>(Tes(), layout::kTesSceneRoot);
        if (!root)
            return why = "no scene root", false;

        bool loaded = reinterpret_cast<bool(__thiscall*)(void*)>(layout::kCellLoadRefs)(cell);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kCellAttachStart)(cell, root);
        int afterStart = StateOf(cell);
        void* havok = reinterpret_cast<void*(__thiscall*)(void*)>(layout::kCellHavokWorld)(cell);
        void* outdoor = reinterpret_cast<void*(__cdecl*)()>(layout::kOutdoorHavokWorld)();
        if (havok)
            reinterpret_cast<void(__thiscall*)(void*)>(layout::kHavokBeforeAttach)(havok);
        if (afterStart == 5)
            reinterpret_cast<void(__thiscall*)(void*, bool)>(layout::kCellAttachFinish)(cell, true);
        if (havok)
            reinterpret_cast<void(__thiscall*)(void*, int)>(layout::kHavokAfterAttach)(havok, 0);
        if (void* node = NodeOf(cell))
        {
            reinterpret_cast<void(__thiscall*)(void*)>(layout::kNodeAfterAttach)(node);
            if (!reinterpret_cast<int(__thiscall*)(void*)>(layout::kNodeIsUpdated)(node))
            {
                alignas(16) uint8_t update[16] = {};
                reinterpret_cast<void(__thiscall*)(void*, float, int, int)>(layout::kUpdateDataMake)(update, 0.0f, 0, 0);
                reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::kNiAVObjectUpdate)(node, update);
            }
        }
        SetHidden(cell, true);
        state = StateOf(cell);
        LOG_INFO("Far interior %08X: attached (state %d, was %d; refs loaded %d, %d after the first half; its havok world "
            "%s, %s the outdoor one; scene node %s)", cellId, state, before, loaded, afterStart, havok ? "found" : "none",
            havok == outdoor ? "is" : "isn't", NodeOf(cell) ? "hidden" : "none");
        if (state != 6)
            return why = "its state is " + std::to_string(state) + " after attaching", false;
        g_ours.insert(cell);
        ++g_oursChanged;
        return true;
    }

    bool DetachFarInterior(uint32_t cellId, std::string& why)
    {
        void* cell = InteriorCell(cellId);
        if (!cell || !g_ours.count(cell))
            return why = "not one of ours", false;
        SetHidden(cell, false);
        reinterpret_cast<void(__thiscall*)(void*, bool)>(layout::kCellDetach)(cell, true);
        g_ours.erase(cell);
        ++g_oursChanged;
        LOG_INFO("Far interior %08X: detached (state %d)", cellId, StateOf(cell));
        return true;
    }

    int FarInteriorState(uint32_t cellId, bool& hidden, std::vector<uint32_t>& actors)
    {
        hidden = false;
        actors.clear();
        void* cell = InteriorCell(cellId);
        if (!cell)
            return -1;
        if (void* node = NodeOf(cell))
            hidden = (Field<uint32_t>(node, layout::kNiFlags) & 1) != 0;
        for (auto* node = &static_cast<TESObjectCELL*>(cell)->objectList.m_listHead; node; node = node->next)
            if (TESObjectREFR* ref = node->data; ref && IsActor(ref))
                actors.push_back(RefId(ref));
        return StateOf(cell);
    }

    bool ShowFarInterior(uint32_t cellId, bool show)
    {
        void* cell = InteriorCell(cellId);
        if (!cell || !g_ours.count(cell))
            return false;
        SetHidden(cell, !show);
        LOG_INFO("Far interior %08X: %s (lab)", cellId, show ? "shown" : "hidden");
        return true;
    }

    void LetGoOfFarInteriors()
    {
        for (void* cell : std::set<void*>(g_ours))
        {
            std::string why;
            DetachFarInterior(static_cast<TESForm*>(cell)->refID, why);
        }
    }

    std::vector<const TESObjectCELL*> FarInteriorCells()
    {
        std::vector<const TESObjectCELL*> out;
        for (void* cell : g_ours)
            if (StateOf(cell) == 6)
                out.push_back(static_cast<const TESObjectCELL*>(cell));
        return out;
    }

    uint32_t FarInteriorsChanged() { return g_oursChanged; }

    bool IsInteriorPlace(uint32_t space) { return space && InteriorCell(space) != nullptr; }

    bool IsFarInterior(uint32_t space)
    {
        void* cell = space ? InteriorCell(space) : nullptr;
        return cell && g_ours.count(cell) && StateOf(cell) == 6;
    }

    namespace
    {
        bool g_guarded = false;
        std::map<uint32_t, uint32_t> g_planned;
        uint32_t g_nextPlanLook = 0, g_nextMemoryNote = 0;
    }

    void UpdateFarInteriors(bool hosting, const std::vector<coop::FriendPlace>& friends, uint32_t usedMb, uint32_t now)
    {
        if (static_cast<int32_t>(now - g_nextPlanLook) < 0)
            return;
        g_nextPlanLook = now + 500;
        hosting = hosting && g_guarded;
        for (auto it = g_planned.begin(); it != g_planned.end();)
        {
            void* cell = InteriorCell(it->first);
            it = cell && g_ours.count(cell) ? std::next(it) : g_planned.erase(it);
        }
        if (!hosting)
        {
            if (!g_planned.empty())
                LOG_INFO("Far interiors: let go of the %zu friends were in (not hosting)", g_planned.size());
            for (const auto& [id, last] : std::map<uint32_t, uint32_t>(g_planned))
            {
                std::string why;
                DetachFarInterior(id, why);
            }
            g_planned.clear();
            return;
        }
        TESObjectREFR* player = Player();
        TESObjectCELL* here = player ? player->parentCell : nullptr;
        uint32_t hostCell = here && !here->worldSpace ? here->refID : 0;
        std::vector<coop::FriendPlace> inside;
        for (const coop::FriendPlace& f : friends)
            if (InteriorCell(f.cell))
                inside.push_back(f);
        coop::InteriorPlan plan = coop::PlanFarInteriors(g_planned, inside, hostCell, usedMb, now);
        for (uint32_t id : plan.detach)
        {
            std::string why;
            if (InteriorCell(id) && g_ours.count(InteriorCell(id)))
                DetachFarInterior(id, why);
            g_planned.erase(id);
            LOG_INFO("Far interiors: let go of %08X (no friend in it for %u s, or we're in it)", id,
                coop::kFarInteriorKeepMs / 1000);
        }
        for (uint32_t id : plan.attach)
        {
            std::string why;
            if (AttachFarInterior(id, why))
            {
                g_planned[id] = now;
                LOG_INFO("Far interiors: %08X attached for a friend in it (%zu now)", id, g_planned.size());
            }
            else
            {
                LOG_WARN("Far interiors: %08X, where a friend is, not attached: %s", id, why.c_str());
                g_nextPlanLook = now + 5000;
            }
        }
        if (plan.memoryFull && static_cast<int32_t>(now - g_nextMemoryNote) >= 0)
        {
            g_nextMemoryNote = now + 60000;
            LOG_WARN("Far interiors: our game uses %u MB (over %u): no more interiors run for friends", usedMb,
                coop::kFarMemoryLimitMb);
        }
    }

    namespace
    {
        using EnterFn = void(__thiscall*)(void* tes, void* cell, void* position);
        constexpr const uintptr_t (&kEnterCalls)[3] = layout::kTesEnterInteriorCalls;
        EnterFn g_enterNext[3] = {};

        template <int I>
        void __fastcall EnterHook(void* tes, void*, void* cell, void* position)
        {
            if (cell && g_ours.count(cell))
            {
                uint32_t id = static_cast<TESForm*>(cell)->refID;
                std::string why;
                DetachFarInterior(id, why);
                g_planned.erase(id);
                LOG_INFO("Far interiors: we're going into %08X: ours detached first (the game attaches it)", id);
            }
            g_enterNext[I](tes, cell, position);
        }
        constexpr void(__fastcall* kEnterHooks[])(void*, void*, void*, void*) = {EnterHook<0>, EnterHook<1>,
            EnterHook<2>};
    }

    namespace
    {
        uintptr_t g_footstepNext = 0;
        std::atomic<uint32_t> g_footstepsSkipped{0}, g_footstepCalls{0};

        void __cdecl FootstepHook(TESObjectREFR* actor, int event)
        {
            ++g_footstepCalls;
            TESObjectCELL* cell = actor ? actor->parentCell : nullptr;
            TESObjectREFR* player = Player();
            TESObjectCELL* ours = player ? player->parentCell : nullptr;
            if (cell && (Field<uint8_t>(cell, layout::kCellFlags) & 1) && cell != ours)
            {
                uint32_t n = ++g_footstepsSkipped;
                if (n == 1 || n % 1000 == 0)
                    LOG_DEBUG("Far interiors: footstep effects skipped for actors in an interior we run (%u so far)", n);
                return;
            }
            reinterpret_cast<void(__cdecl*)(TESObjectREFR*, int)>(g_footstepNext)(actor, event);
        }
    }

    std::string FootstepStats()
    {
        return "footstep effects " + std::to_string(g_footstepCalls.load()) + ", skipped " +
            std::to_string(g_footstepsSkipped.load()) + (g_footstepNext ? "" : " (not hooked)");
    }

    bool InstallFarInteriorGuard()
    {
        if (!g_footstepNext)
        {
            g_footstepNext = HookCall(addr::kFootstepEffectCall, reinterpret_cast<const void*>(&FootstepHook));
            if (!g_footstepNext)
            {
                LOG_ERROR("Far interiors: the game's footstep call isn't as expected; no interiors run for friends");
                return false;
            }
            if (g_footstepNext != addr::kFootstepEffect)
                LOG_INFO("Far interiors: another plugin hooks the footstep call too; ours goes first and passes on");
        }
        g_guarded = InstallEnterGuard();
        return g_guarded;
    }

    bool InstallEnterGuard()
    {
        if (g_enterNext[0])
            return true;
        for (uintptr_t site : kEnterCalls)
            if (reinterpret_cast<const uint8_t*>(site)[0] != 0xE8)
            {
                LOG_ERROR("Far interiors: the game's call at %08X isn't as expected; no interiors run for friends",
                    static_cast<uint32_t>(site));
                return false;
            }
        for (int i = 0; i < 3; ++i)
        {
            auto* p = reinterpret_cast<uint8_t*>(kEnterCalls[i]);
            int32_t rel;
            std::memcpy(&rel, p + 1, 4);
            uintptr_t target = kEnterCalls[i] + 5 + rel;
            DWORD old;
            if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
                return false;
            g_enterNext[i] = reinterpret_cast<EnterFn>(target);
            int32_t ours = static_cast<int32_t>(reinterpret_cast<uintptr_t>(kEnterHooks[i]) - (kEnterCalls[i] + 5));
            std::memcpy(p + 1, &ours, 4);
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            if (target != layout::kTesEnterInterior)
                LOG_INFO("Far interiors: another plugin hooks the game's call at %08X too; ours goes first and passes on",
                    static_cast<uint32_t>(kEnterCalls[i]));
        }
        return true;
    }
}
