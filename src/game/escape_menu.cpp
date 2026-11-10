// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/escape_menu.h"

#include "game/addresses.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include <utility>

namespace game
{
    namespace
    {
        using MenuModeFn = bool(__cdecl*)();
        constexpr int kCalls = static_cast<int>(sizeof(addr::kMenuModeCalls) / sizeof(addr::kMenuModeCalls[0]));
        MenuModeFn g_before[kCalls] = {};
        bool g_live = false;
        bool g_boxesLive = false;

        template <int I>
        bool __cdecl InMenuMode()
        {
            bool paused = g_before[I]();
            if (paused && g_boxesLive)
                return false;
            if (paused && g_live && reinterpret_cast<MenuModeFn>(addr::kPauseMenuUp)())
                return false;
            return paused;
        }

        template <int... I>
        constexpr MenuModeFn HookFor(int i, std::integer_sequence<int, I...>)
        {
            constexpr MenuModeFn hooks[] = {&InMenuMode<I>...};
            return hooks[i];
        }
    }

    void KeepWorldRunningInEscapeMenu()
    {
        int taken = 0, ours = 0;
        for (int i = 0; i < kCalls; ++i)
        {
            auto* at = reinterpret_cast<uint8_t*>(addr::kMenuModeCalls[i]);
            if (*at != 0xE8)
            {
                LOG_WARN("Escape menu: %08X isn't a call (%02X); left alone", addr::kMenuModeCalls[i], *at);
                continue;
            }
            uintptr_t target = addr::kMenuModeCalls[i] + 5 + *reinterpret_cast<int32_t*>(at + 1);
            g_before[i] = reinterpret_cast<MenuModeFn>(target);
            ours += target != addr::kInMenuMode;
            MenuModeFn hook = HookFor(i, std::make_integer_sequence<int, kCalls>{});
            DWORD old;
            VirtualProtect(at + 1, 4, PAGE_EXECUTE_READWRITE, &old);
            *reinterpret_cast<int32_t*>(at + 1) =
                static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - (addr::kMenuModeCalls[i] + 5));
            VirtualProtect(at + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, 5);
            ++taken;
        }
        LOG_INFO("Escape menu: %d of %d checks taken over (%d already Real Time Menus'), so co-op keeps the world "
            "running there", taken, kCalls, ours);
    }

    void CoopBoxesLive(bool live)
    {
        if (live != g_boxesLive)
            LOG_DEBUG("Menus: %s", live ? "a co-op box is up; the world keeps running" : "no co-op box up");
        g_boxesLive = live;
    }

    void EscapeMenuLive(bool live)
    {
        if (live != g_live)
            LOG_INFO("Escape menu: %s", live ? "the world keeps running in it (co-op)" : "pauses the game again");
        g_live = live;
    }
}
