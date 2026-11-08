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
        bool g_holdKillCam = false;

        uintptr_t g_pickCameraPath = 0;
        bool g_ownViewPlayback = true;

        void* __fastcall NoVatsCameraPath(void* paths, void*, TESObjectREFR* attacker, TESObjectREFR* target)
        {
            if (!g_ownViewPlayback && g_pickCameraPath)
            {
                using Fn = void*(__thiscall*)(void*, TESObjectREFR*, TESObjectREFR*);
                void* path = reinterpret_cast<Fn>(g_pickCameraPath)(paths, attacker, target);
                LOG_INFO("VATS: the game's playback camera path %08X on %08X (lab)",
                    path ? static_cast<TESForm*>(path)->refID : 0, target ? target->refID : 0);
                return path;
            }
            static uint32_t s_lastTarget = 0;
            uint32_t id = target ? target->refID : 0;
            if (id != s_lastTarget)
            {
                s_lastTarget = id;
                LOG_INFO("VATS: playback in the player's own view (no camera path) on %08X", id);
            }
            return nullptr;
        }
    }

    namespace
    {
        uintptr_t g_shotStage = 0;

        int __fastcall MeleeShotStage(void* shot)
        {
            if (!shot || !g_shotStage)
            {
                static bool s_logged = false;
                if (!s_logged)
                    LOG_INFO("VATS: a melee action without a camera shot: its check passed (own-view playback)");
                s_logged = true;
                return 0x7FFFFFFF;
            }
            using Fn = int(__fastcall*)(void*);
            return reinterpret_cast<Fn>(g_shotStage)(shot);
        }
    }

    bool PlayVatsInOwnView()
    {
        static int s_done = -1;
        if (s_done >= 0)
            return s_done != 0;
        g_shotStage = HookCall(addr::kVatsMeleeShotStageCall, reinterpret_cast<const void*>(&MeleeShotStage));
        if (!g_shotStage)
        {
            LOG_ERROR("VATS: the melee check's call isn't there (%08X); the game's playback camera stays",
                static_cast<unsigned>(addr::kVatsMeleeShotStageCall));
            s_done = 0;
            return false;
        }
        if (g_shotStage != addr::kVatsShotStage)
            LOG_INFO("VATS: another plugin is at %08X too (%08X); co-op calls it when there's a shot",
                static_cast<unsigned>(addr::kVatsMeleeShotStageCall), static_cast<unsigned>(g_shotStage));
        uintptr_t was = HookCall(addr::kVatsPickCameraPathCall, reinterpret_cast<const void*>(&NoVatsCameraPath));
        s_done = was != 0;
        g_pickCameraPath = was;
        if (was && was != addr::kVatsPickCameraPath)
            LOG_INFO("VATS: another plugin picks the playback camera at %08X too (%08X); co-op skips it",
                static_cast<unsigned>(addr::kVatsPickCameraPathCall), static_cast<unsigned>(was));
        return s_done != 0;
    }

    void SetVatsOwnViewPlayback(bool on)
    {
        g_ownViewPlayback = on;
    }

    namespace
    {
        uintptr_t g_deathPlayback = 0;
        bool g_noDownedPlayback = true;

        void __fastcall DeathPlaybackUnlessDowned(void* vats, void*)
        {
            TESObjectREFR* player = Player();
            if (g_noDownedPlayback && !coop::vats::DeathPlaybackAllowed(LifeState(player)))
            {
                static uint32_t s_skipped = 0;
                if (s_skipped++ % 1000 == 0)
                    LOG_INFO("Downed: no death playback for our player (knocked out, not dead; %u so far)",
                        s_skipped);
                return;
            }
            using Fn = void(__thiscall*)(void*);
            reinterpret_cast<Fn>(g_deathPlayback)(vats);
        }
    }

    bool NoDeathPlaybackWhenDowned()
    {
        static int s_done = -1;
        if (s_done >= 0)
            return s_done != 0;
        g_deathPlayback =
            HookCall(addr::kPlayerUpdateDeathPlaybackCall, reinterpret_cast<const void*>(&DeathPlaybackUnlessDowned));
        s_done = g_deathPlayback != 0;
        if (g_deathPlayback && g_deathPlayback != addr::kVatsPlayerDeathPlayback)
            LOG_INFO("Downed: another plugin starts the player's death playback at %08X too (%08X); co-op passes on "
                "to it", static_cast<unsigned>(addr::kPlayerUpdateDeathPlaybackCall),
                static_cast<unsigned>(g_deathPlayback));
        return s_done != 0;
    }

    void SetDownedDeathPlayback(bool on)
    {
        g_noDownedPlayback = !on;
    }

    uint32_t VatsMode()
    {
        return *reinterpret_cast<const uint32_t*>(addr::kVatsCameraData + addr::kVatsCameraMode);
    }

    void SetKillCamMode(uint32_t mode, bool hold)
    {
        TESObjectREFR* player = Player();
        if (player)
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(player) + layout::kPlayerKillCamMode) = mode;
        g_holdKillCam = hold;
    }

    void KeepVatsFeel()
    {
        float* floor = reinterpret_cast<float*>(addr::kVatsTargetFovMin + addr::kSettingValue);
        float own = coop::vats::FovFloor(*reinterpret_cast<const float*>(addr::kDefaultWorldFov + addr::kSettingValue));
        if (own > 0.0f && *floor != own)
        {
            LOG_INFO("VATS: aiming keeps the player's own field of view, %g (its floor was %g)", own, *floor);
            *floor = own;
        }
        TESObjectREFR* player = Player();
        if (!player || g_holdKillCam)
            return;
        uint32_t* killCam = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(player) + layout::kPlayerKillCamMode);
        if (*killCam != 0)
        {
            LOG_INFO("VATS: kill camera off in co-op (its mode was %u)", *killCam);
            *killCam = 0;
        }
    }
}
