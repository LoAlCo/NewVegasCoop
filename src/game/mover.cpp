// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/mover.h"

#include "game/addresses.h"
#include "game/game.h"
#include "log.h"

#include "nvse/GameObjects.h"

#include <atomic>

namespace game
{
    namespace
    {
        bool g_mapOk = false;

        using SetFlagsFn = void(__thiscall*)(void* mover, UInt32 flags);
        using ReleaseFn = void(__thiscall*)(void* mover);
        using ResetFn = void(__thiscall*)(void* mover, UInt32 arg);

        template <typename T>
        T Field(const void* object, uint32_t offset)
        {
            return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
        }

        uintptr_t Table(const void* object) { return *reinterpret_cast<const uintptr_t*>(object); }
        uintptr_t Slot(uintptr_t table, int slot) { return reinterpret_cast<const uintptr_t*>(table)[slot]; }

        void* MoverOf(TESObjectREFR* ref, MoverKind& kind)
        {
            kind = MoverKind::None;
            if (!IsActor(ref))
                return nullptr;
            void* mover = static_cast<Actor*>(ref)->actorMover;
            if (!mover)
                return nullptr;
            uintptr_t table = Table(mover);
            kind = table == mover::kActorMoverTable ? MoverKind::Npc :
                table == mover::kPlayerMoverTable ? MoverKind::Player : MoverKind::Unknown;
            if (kind != MoverKind::Unknown && Field<const void*>(mover, mover::kActor) != ref)
                kind = MoverKind::Unknown;
            return mover;
        }

        bool SlotIs(uintptr_t table, int slot, uintptr_t expected, const char* what)
        {
            uintptr_t found = Slot(table, slot);
            if (found == expected)
                return true;
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(found), &module) && module != GetModuleHandleA(nullptr))
            {
                GetModuleFileNameA(module, name, MAX_PATH);
                const char* file = strrchr(name, '\\');
                LOG_INFO("Memory map: %s is hooked by %s (fine: it passes on to the game's)", what, file ? file + 1 : name);
                return true;
            }
            LOG_ERROR("Memory map: %s is %08X, expected %08X", what, found, expected);
            return false;
        }

        struct Hold
        {
            std::atomic<void*> mover{nullptr};
            std::atomic<const void*> actor{nullptr};
            std::atomic<uint32_t> flags{0};
            std::atomic<uint32_t> until{0};
        };
        constexpr size_t kMaxHolds = 64;
        Hold g_holds[kMaxHolds];
        bool g_hooked = false;
        bool g_warnedFull = false;
        ReleaseFn g_nextRelease = nullptr;
        ResetFn g_nextReset = nullptr;
        SetFlagsFn g_setFlags = nullptr;

        bool Lapsed(const Hold& h, uint32_t now) { return static_cast<int32_t>(h.until.load() - now) <= 0; }

        void KeepHold(void* m)
        {
            uint32_t now = GetTickCount();
            for (Hold& h : g_holds)
                if (h.mover.load() == m)
                {
                    if (!Lapsed(h, now) && Field<const void*>(m, mover::kActor) == h.actor.load())
                        g_setFlags(m, h.flags.load());
                    return;
                }
        }

        void NoteAiFlag(const char* where)
        {
            static std::atomic<int> s_said{0};
            int bit = AiThreadsRunning() ? 1 : 2;
            if (!(s_said.fetch_or(bit) & bit))
                LOG_INFO("Frame: the AI threads' flag read %d in %s (thread %lu)", bit == 1, where, GetCurrentThreadId());
        }

        void __fastcall OnRelease(void* m)
        {
            NoteAiFlag("an actor's movement release");
            g_nextRelease(m);
            KeepHold(m);
        }

        void __fastcall OnReset(void* m, void*, UInt32 arg)
        {
            g_nextReset(m, arg);
            KeepHold(m);
        }

        bool Chain(int slot, void* ours, uintptr_t& next)
        {
            auto* entry = reinterpret_cast<uintptr_t*>(mover::kActorMoverTable) + slot;
            DWORD old;
            if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
                return false;
            next = *entry;
            InterlockedExchange(reinterpret_cast<volatile LONG*>(entry), static_cast<LONG>(reinterpret_cast<uintptr_t>(ours)));
            VirtualProtect(entry, sizeof(*entry), old, &old);
            return true;
        }

        void HookHolds()
        {
            using namespace mover;
            if (g_hooked)
                return;
            uintptr_t release = 0, reset = 0;
            g_setFlags = reinterpret_cast<SetFlagsFn>(Slot(kActorMoverTable, kSetFlagsSlot));
            if (!Chain(kReleaseFlagsSlot, reinterpret_cast<void*>(&OnRelease), release))
            {
                LOG_ERROR("Couldn't chain onto NPC release-movement: puppets' legs may be taken back by their AI");
                return;
            }
            g_nextRelease = reinterpret_cast<ReleaseFn>(release);
            if (Chain(kResetSlot, reinterpret_cast<void*>(&OnReset), reset))
                g_nextReset = reinterpret_cast<ResetFn>(reset);
            else
                LOG_ERROR("Couldn't chain onto NPC reset-movement");
            g_hooked = true;
            LOG_INFO("Holding puppets' legs (chained onto NPC release- and reset-movement)");
        }
    }

    const char* MoverKindName(MoverKind kind)
    {
        switch (kind)
        {
        case MoverKind::Npc: return "NPC";
        case MoverKind::Player: return "player";
        case MoverKind::Unknown: return "unknown";
        default: return "none";
        }
    }

    bool CheckMoverMap()
    {
        using namespace mover;
        bool ok = SlotIs(kActorMoverTable, kSetFlagsSlot, kActorSetFlags, "NPC set-movement") &
            (g_hooked || SlotIs(kActorMoverTable, kReleaseFlagsSlot, kReleaseFlags, "NPC release-movement")) &
            SlotIs(kActorMoverTable, kGetFlagsSlot, kActorGetFlags, "NPC get-movement") &
            (g_hooked || SlotIs(kActorMoverTable, kResetSlot, kActorReset, "NPC reset-movement")) &
            SlotIs(kPlayerMoverTable, kSetFlagsSlot, kPlayerSetFlags, "player set-movement") &
            SlotIs(kPlayerMoverTable, kReleaseFlagsSlot, kReleaseFlags, "player release-movement") &
            SlotIs(kPlayerMoverTable, kGetFlagsSlot, kPlayerGetFlags, "player get-movement");
        MoverKind kind;
        MoverOf(Player(), kind);
        if (kind != MoverKind::Player)
        {
            LOG_ERROR("Memory map: the player's mover is %s, expected a player mover", MoverKindName(kind));
            ok = false;
        }
        g_mapOk = ok;
        if (ok)
        {
            LOG_INFO("Memory map: movers check out (function tables and the player's mover)");
            HookHolds();
        }
        else
            LOG_ERROR("Memory map: movers don't match, so the mod won't touch anyone's movement");
        return ok;
    }

    MoverState ReadMover(TESObjectREFR* ref)
    {
        MoverState s;
        void* m = MoverOf(ref, s.kind);
        if (s.kind == MoverKind::Npc)
        {
            s.flags = Field<uint32_t>(m, mover::kFlags);
            s.asked = Field<uint32_t>(m, mover::kAskedFlags);
            s.askedOn = Field<uint8_t>(m, mover::kAskedOn) != 0;
        }
        else if (s.kind == MoverKind::Player)
            s.flags = Field<uint32_t>(m, mover::kPlayerFlags);
        return s;
    }

    bool AskMovementFlags(TESObjectREFR* ref, uint32_t flags)
    {
        MoverKind kind;
        void* m = MoverOf(ref, kind);
        if (!g_mapOk || (kind != MoverKind::Npc && kind != MoverKind::Player))
            return false;
        reinterpret_cast<SetFlagsFn>(Slot(Table(m), mover::kSetFlagsSlot))(m, flags);
        return true;
    }

    bool HoldMovementFlags(TESObjectREFR* ref, uint32_t flags)
    {
        MoverKind kind;
        void* m = MoverOf(ref, kind);
        if (!g_mapOk || kind != MoverKind::Npc)
            return false;
        uint32_t now = GetTickCount();
        Hold* mine = nullptr;
        Hold* spare = nullptr;
        for (Hold& h : g_holds)
        {
            if (h.mover.load() == m)
            {
                mine = &h;
                break;
            }
            if (!spare && (!h.mover.load() || Lapsed(h, now)))
                spare = &h;
        }
        if (!mine && spare)
        {
            mine = spare;
            mine->mover.store(nullptr);
            mine->actor.store(ref);
            mine->flags.store(flags);
            mine->until.store(now + kHoldMs);
            mine->mover.store(m);
        }
        else if (mine)
        {
            mine->actor.store(ref);
            mine->flags.store(flags);
            mine->until.store(now + kHoldMs);
        }
        else if (!g_warnedFull)
        {
            g_warnedFull = true;
            LOG_WARN("More than %zu puppets: the rest can have their legs taken back by their AI", kMaxHolds);
        }
        reinterpret_cast<SetFlagsFn>(Slot(Table(m), mover::kSetFlagsSlot))(m, flags);
        return true;
    }

    void ForgetHolds()
    {
        for (Hold& h : g_holds)
        {
            h.until.store(0);
            h.mover.store(nullptr);
        }
    }

    bool ReleaseMovementFlags(TESObjectREFR* ref)
    {
        MoverKind kind;
        void* m = MoverOf(ref, kind);
        if (!g_mapOk || kind != MoverKind::Npc)
            return false;
        for (Hold& h : g_holds)
            if (h.mover.load() == m)
            {
                h.until.store(0);
                h.mover.store(nullptr);
            }
        reinterpret_cast<ReleaseFn>(Slot(Table(m), mover::kReleaseFlagsSlot))(m);
        return true;
    }
}
