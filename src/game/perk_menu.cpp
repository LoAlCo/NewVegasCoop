// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/perk_menu.h"

#include "game/addresses.h"
#include "game/game.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace game
{
    namespace
    {
        constexpr float kGreyedAlpha = 128;
        constexpr uint32_t kTraitBrightness = 0xFF5, kTraitRed = 0xFB2, kTraitGreen = 0xFB3, kTraitBlue = 0xFB4;

        using AlphaFn = void(__cdecl*)(void* tile, void* perk);
        using DescribeFn = void(__thiscall*)(void* perk, char* out, uint32_t size);
        using SetFloatFn = void(__thiscall*)(void* tile, uint32_t trait, float value, bool propagate);

        AlphaFn g_alphaBefore = nullptr;
        DescribeFn g_describeBefore = nullptr;
        std::vector<uint32_t> g_taken;
        uint32_t g_greyedLogged = 0;
        bool g_hoveredTaken = false;
        void* g_noteMenu = nullptr;
        bool g_noteShown = false;
        int g_noteSettle = 0;

        void* Child(void* tile, const char* name)
        {
            for (auto* node = *reinterpret_cast<void**>(static_cast<char*>(tile) + 4); node;
                 node = *reinterpret_cast<void**>(node))
            {
                void* child = reinterpret_cast<void**>(node)[2];
                const char* childName =
                    child ? *reinterpret_cast<const char**>(static_cast<char*>(child) + 0x20) : nullptr;
                if (childName && _stricmp(childName, name) == 0)
                    return child;
            }
            return nullptr;
        }

        bool Taken(const void* perk)
        {
            if (!perk || g_taken.empty())
                return false;
            uint32_t id = *reinterpret_cast<const uint32_t*>(static_cast<const char*>(perk) + 0x0C);
            return std::find(g_taken.begin(), g_taken.end(), id) != g_taken.end();
        }

        void __cdecl PerkAlpha(void* tile, void* perk)
        {
            g_alphaBefore(tile, perk);
            if (!tile || !Taken(perk))
                return;
            uint32_t trait = *reinterpret_cast<const uint32_t*>(addr::kLevelUpTextAlphaTrait);
            auto set = reinterpret_cast<SetFloatFn>(addr::kTileSetValueFloat);
            set(tile, trait, kGreyedAlpha, true);
            if (void* text = Child(tile, "ListItemText"))
            {
                set(text, kTraitBrightness, -1, true);
                set(text, kTraitRed, kPartyPerkRed[0], true);
                set(text, kTraitGreen, kPartyPerkRed[1], true);
                set(text, kTraitBlue, kPartyPerkRed[2], true);
            }
            if (g_greyedLogged < 32)
            {
                ++g_greyedLogged;
                LOG_INFO("Level up: perk %08X greyed out (someone else in the party has it)",
                    *reinterpret_cast<const uint32_t*>(static_cast<const char*>(perk) + 0x0C));
            }
        }

        void __fastcall PerkDescribe(void* perk, void*, char* out, uint32_t size)
        {
            g_describeBefore(perk, out, size);
            g_hoveredTaken = Taken(perk);
        }

        bool Patch(uintptr_t at, uint8_t opcode, uintptr_t& before, uintptr_t ours, bool relative)
        {
            auto* p = reinterpret_cast<uint8_t*>(at);
            if (*p != opcode)
            {
                LOG_WARN("Level up: %08X isn't the expected instruction (%02X); party perks aren't greyed out", at, *p);
                return false;
            }
            int32_t value = *reinterpret_cast<int32_t*>(p + 1);
            before = relative ? at + 5 + value : static_cast<uintptr_t>(value);
            int32_t now = relative ? static_cast<int32_t>(ours - (at + 5)) : static_cast<int32_t>(ours);
            DWORD old;
            VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
            *reinterpret_cast<int32_t*>(p + 1) = now;
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            return true;
        }
    }

    void TakeOverLevelUpPerks()
    {
        uintptr_t alpha = 0, describe = 0;
        if (!Patch(addr::kLevelUpPerkAlphaPush, 0x68, alpha, reinterpret_cast<uintptr_t>(&PerkAlpha), false))
            return;
        g_alphaBefore = reinterpret_cast<AlphaFn>(alpha);
        if (Patch(addr::kLevelUpPerkDescribeCall, 0xE8, describe, reinterpret_cast<uintptr_t>(&PerkDescribe), true))
            g_describeBefore = reinterpret_cast<DescribeFn>(describe);
        LOG_INFO("Level up: party perks greyed out through us (%s first; description: %s)",
            alpha == addr::kLevelUpPerkAlpha ? "the game's" : "another plugin's",
            !g_describeBefore ? "not taken over" : describe == addr::kPerkDescribe ? "the game's" : "another plugin's");
    }

    void SetPerksTakenByOthers(const std::vector<uint32_t>& perks)
    {
        g_taken = perks;
    }

    void UpdateLevelUpNote()
    {
        void* menu = *reinterpret_cast<void**>(addr::kLevelUpMenu);
        if (!menu)
        {
            g_noteMenu = nullptr;
            g_noteShown = false;
            g_hoveredTaken = false;
            return;
        }
        bool perkPage = *reinterpret_cast<uint32_t*>(static_cast<char*>(menu) + addr::kLevelUpMenuPage) == 1;
        bool want = perkPage && g_hoveredTaken && !g_taken.empty();
        if (want && menu != g_noteMenu)
        {
            LevelUpNote(0);
            g_noteMenu = menu;
            g_noteShown = false;
            LOG_INFO("Level up: the party perk note put into the menu");
        }
        if (menu != g_noteMenu)
            return;
        if (want != g_noteShown)
        {
            LevelUpNote(want ? 1 : 2);
            g_noteShown = want;
            g_noteSettle = want ? 2 : 0;
        }
        else if (g_noteShown && g_noteSettle > 0)
        {
            LevelUpNote(1);
            --g_noteSettle;
        }
    }
}
