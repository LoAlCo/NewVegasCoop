// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/addresses.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstring>

namespace game
{
    namespace
    {
        // XInput's own layout
        struct PadButtons { uint16_t buttons; uint8_t leftTrigger, rightTrigger; int16_t lx, ly, rx, ry; };
        struct PadRead { uint32_t packet; PadButtons pad; };
        using GetStateFn = uint32_t(__stdcall*)(uint32_t, PadRead*);

        GetStateFn g_getState = nullptr;   // XInput's own, which chat reads
        GetStateFn g_gameNext = nullptr;   // what the game's import held before chat's filter went in
        bool g_tried = false;
        bool g_filterTried = false;
        bool g_filtered = false;
        std::atomic<bool> g_hide{false};

        // What the game gets in place of XInputGetState: the real thing, or, while chat has the controller, a controller
        // with nothing pressed. Its packet number changes so the game takes the empty one in, and changes again when the
        // real one comes back.
        uint32_t __stdcall GameGetState(uint32_t user, PadRead* out)
        {
            uint32_t result = g_gameNext(user, out);
            if (result == 0 && out && g_hide.load(std::memory_order_relaxed))
            {
                out->packet += 0x40000000;
                std::memset(&out->pad, 0, sizeof(out->pad));
            }
            return result;
        }

        bool FindXInput()
        {
            if (g_tried)
                return g_getState != nullptr;
            g_tried = true;
            for (const char* dll : {"xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll"})
            {
                HMODULE m = GetModuleHandleA(dll);
                if (!m)
                    m = LoadLibraryA(dll);
                if (m && (g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(m, "XInputGetState"))))
                {
                    LOG_INFO("Chat: controller read through %s", dll);
                    return true;
                }
            }
            LOG_WARN("Chat: no XInput found; chat can't be used with a controller");
            return false;
        }

        // Puts GameGetState in the game's import. Whatever was there already (XInput, or another mod that hooked it
        // first) is what gets called, so that mod keeps working.
        bool FilterGame()
        {
            if (g_filterTried)
                return g_filtered;
            g_filterTried = true;
            auto* slot = reinterpret_cast<GetStateFn*>(addr::kXInputGetStateImport);
            MEMORY_BASIC_INFORMATION info{};
            HMODULE owner = nullptr;
            if (!VirtualQuery(slot, &info, sizeof(info)) || info.State != MEM_COMMIT || !*slot ||
                !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<const char*>(*slot), &owner))
            {
                LOG_WARN("Chat: the game's XInput import at %08X isn't there; while chat is open the game still sees the "
                    "controller", static_cast<unsigned>(addr::kXInputGetStateImport));
                return false;
            }
            GetStateFn was = *slot;
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
                return false;
            g_gameNext = was;
            *slot = &GameGetState;
            VirtualProtect(slot, sizeof(*slot), old, &old);
            g_filtered = true;
            char name[MAX_PATH] = {};
            GetModuleFileNameA(owner, name, MAX_PATH);
            const char* file = std::strrchr(name, '\\');
            LOG_INFO("Chat: the game's controller goes through chat (to %s)", file ? file + 1 : name);
            return true;
        }
    }

    bool ReadChatPad(coop::ChatPadInput& out)
    {
        out = coop::ChatPadInput{};
        if (!FindXInput())
            return false;
        for (uint32_t user = 0; user < 4; ++user)
        {
            PadRead r{};
            if (g_getState(user, &r) != 0)
                continue;
            out.connected = true;
            out.buttons = r.pad.buttons;
            out.ry = r.pad.ry / (r.pad.ry < 0 ? 32768.0f : 32767.0f);
            return true;
        }
        return false;
    }

    void HidePadFromGame(bool hide)
    {
        if (hide && !FilterGame())
            return;
        if (g_hide.exchange(hide) != hide)
            LOG_DEBUG("Chat: the game %s the controller", hide ? "no longer sees" : "sees");
    }
}
