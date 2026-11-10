// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/trade_window.h"

#include "game/addresses.h"
#include "game/game.h"
#include "game/internal.h"
#include "log.h"

#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"

#include <cmath>
#include <cstring>
#include <windows.h>

namespace game
{
    namespace
    {
        // The barter screen (behaviour read from the game, addresses only).
        constexpr uintptr_t kShowBarter = 0x00704F80;      // cdecl (ref, barter modifier)
        constexpr uintptr_t kBarterPrice = 0x0072ED00;     // cdecl float (tile, stack)
        constexpr uintptr_t kBarterAccept = 0x0072FD10;    // thiscall on the menu
        constexpr uintptr_t kBarterRebuild = 0x00730690;   // cdecl (bool now)
        constexpr uintptr_t kListHasTile = 0x007242F0;     // thiscall (list, tile)
        constexpr uintptr_t kStackCondition = 0x004BCDB0;  // thiscall float (stack, as percent)
        constexpr uintptr_t kPendingTrait = 0x011D8FB4;    // the trait id marking a picked line
        constexpr uint32_t kMerchantTiles = 0xD8, kBuyPicks = 0x10C, kSellPicks = 0x114;
        constexpr uint8_t kExtraWeaponModFlags = 0x8D;
        constexpr uint32_t kChestBase = 0x0009874E;        // Footlocker01Empty: no items, no script, no respawn
        constexpr uintptr_t kScanFrom = 0x00720000, kScanTo = 0x00740000;

        bool g_ready = false;
        TESForm* g_chestBase = nullptr;
        TESObjectREFR* g_chest = nullptr;
        std::vector<StallEntry> g_stall;

        struct RawPick
        {
            uint32_t form = 0;
            uint8_t type = 0;
            int32_t count = 0;
            float condition = -1;
            uint8_t mods = 0;
            bool fromPartner = false;
        };
        std::vector<RawPick> g_picks;
        bool g_accepted = false;
        Script* g_nameOf = nullptr;
        bool g_nameTried = false;

        void* BarterMenu() { return *reinterpret_cast<void* const*>(addr::kBarterMenu); }

        bool OursOpen()
        {
            void* menu = BarterMenu();
            return g_chest && menu &&
                *reinterpret_cast<TESObjectREFR* const*>(static_cast<uint8_t*>(menu) + layout::kBarterMenuMerchant) == g_chest;
        }

        RawPick ReadStack(const void* stack)
        {
            RawPick p;
            auto* bytes = static_cast<const uint8_t*>(stack);
            auto* form = *reinterpret_cast<TESForm* const*>(bytes + 8);
            if (!form)
                return p;
            p.form = form->refID;
            p.type = form->typeID;
            p.count = *reinterpret_cast<const int32_t*>(bytes + 4);
            using Condition = float(__thiscall*)(const void*, bool);
            float c = reinterpret_cast<Condition>(kStackCondition)(stack, true);
            if (c >= 0)
                p.condition = c <= 1.0001f ? c * 100 : c;
            const void* const* extras = *reinterpret_cast<const void* const* const*>(bytes);
            const void* list = extras ? *extras : nullptr;
            if (list)
            {
                using GetByType = const uint8_t*(__thiscall*)(const void*, uint8_t);
                const uint8_t* flags = reinterpret_cast<GetByType>(addr::kExtraListGetByType)(list, kExtraWeaponModFlags);
                if (flags)
                    p.mods = flags[0xC] & 7;
            }
            return p;
        }

        float Distance(const RawPick& p, const net::ItemCount& kind)
        {
            float health = kind.health == net::kNoCondition ? 100.0f : kind.health;
            float condition = p.condition < 0 ? 100.0f : p.condition;
            return std::fabs(health - condition) + (kind.mods == p.mods ? 0.0f : 1000.0f);
        }

        // The closest of `kinds` to the picked stack (same item, nearest condition and mods), or -1.
        template <typename Get>
        int Closest(const RawPick& p, size_t n, Get get)
        {
            int best = -1;
            float bestScore = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const net::ItemCount& kind = get(i);
                if (kind.item != p.form)
                    continue;
                float score = Distance(p, kind);
                if (best < 0 || score < bestScore)
                {
                    best = static_cast<int>(i);
                    bestScore = score;
                }
            }
            return best;
        }

        float __cdecl TradePrice(void* tile, void* stack)
        {
            using Original = float(__cdecl*)(void*, void*);
            if (!OursOpen() || !tile || !stack)
                return reinterpret_cast<Original>(kBarterPrice)(tile, stack);
            auto* menu = static_cast<uint8_t*>(BarterMenu());
            using HasTile = bool(__thiscall*)(void*, void*);
            bool merchantLine = reinterpret_cast<HasTile>(kListHasTile)(menu + kMerchantTiles, tile);
            using GetFloat = float(__thiscall*)(void*, uint32_t);
            bool picked = reinterpret_cast<GetFloat>(addr::kTileGetValueFloat)(tile,
                *reinterpret_cast<const uint32_t*>(kPendingTrait)) != 0;
            if (merchantLine == picked)
                return 0;
            RawPick p = ReadStack(stack);
            int i = Closest(p, g_stall.size(), [](size_t k) -> const net::ItemCount& { return g_stall[k].item; });
            return i < 0 ? 0.0f : static_cast<float>(g_stall[i].price);
        }

        void ReadPicks(const uint8_t* head, bool fromPartner)
        {
            // An inline list head: {stack, next}.
            for (const void* const* node = reinterpret_cast<const void* const*>(head); node && g_picks.size() < 200;
                node = static_cast<const void* const*>(node[1]))
            {
                if (!node[0])
                    continue;
                RawPick p = ReadStack(node[0]);
                p.fromPartner = fromPartner;
                if (p.form && p.count > 0)
                    g_picks.push_back(p);
            }
        }

        void __fastcall TradeAccept(void* menu, void*)
        {
            using Original = void(__thiscall*)(void*);
            if (!OursOpen() || menu != BarterMenu())
            {
                reinterpret_cast<Original>(kBarterAccept)(menu);
                return;
            }
            g_picks.clear();
            ReadPicks(static_cast<uint8_t*>(menu) + kBuyPicks, true);
            ReadPicks(static_cast<uint8_t*>(menu) + kSellPicks, false);
            g_accepted = true;
            LOG_INFO("Trade: Accept pressed with %zu picks; nothing moved by the screen", g_picks.size());
        }

        int RedirectCalls(uintptr_t target, const void* to)
        {
            int n = 0;
            for (uintptr_t at = kScanFrom; at + 5 <= kScanTo; ++at)
            {
                auto* p = reinterpret_cast<const uint8_t*>(at);
                if (p[0] != 0xE8)
                    continue;
                int32_t rel;
                std::memcpy(&rel, p + 1, 4);
                if (at + 5 + rel != target)
                    continue;
                if (HookCall(at, to) == target)
                {
                    LOG_DEBUG("Trade: call at %08X now goes through the mod", static_cast<unsigned>(at));
                    ++n;
                }
            }
            return n;
        }

        bool PicksMarked()
        {
            auto* menu = static_cast<const uint8_t*>(BarterMenu());
            return menu && (*reinterpret_cast<void* const*>(menu + kBuyPicks) ||
                *reinterpret_cast<void* const*>(menu + kSellPicks));
        }

        void Fill(const std::vector<StallEntry>& stall)
        {
            g_stall = stall;
            for (const StallEntry& e : stall)
                ChangeItem(g_chest, e.item);
        }
    }

    bool InstallTradeWindow()
    {
        if (g_ready)
            return true;
        int prices = RedirectCalls(kBarterPrice, reinterpret_cast<const void*>(&TradePrice));
        int accepts = RedirectCalls(kBarterAccept, reinterpret_cast<const void*>(&TradeAccept));
        g_ready = prices > 0 && accepts > 0;
        if (g_ready)
            LOG_INFO("Trade: the barter screen's prices (%d calls) and Accept (%d) go through the mod", prices, accepts);
        else
            LOG_WARN("Trade: couldn't find the barter screen's price (%d) or Accept (%d) calls; trading is off", prices,
                accepts);
        return g_ready;
    }

    bool TradeWindowReady() { return g_ready; }

    bool OpenTradeWindow(const std::string& title, const std::vector<StallEntry>& stall)
    {
        if (!g_ready || !g_compiled || !Player() || BarterMenu() || g_chest)
            return false;
        if (!g_chestBase)
        {
            TESForm* original = LookupForm(kChestBase);
            NVSEArrayVarInterface::Element result;
            if (!original || original->typeID != kFormType_TESObjectCONT ||
                !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, original))
                return false;
            g_chestBase = result.GetTESForm();
            if (!g_chestBase || g_chestBase->typeID != kFormType_TESObjectCONT)
            {
                g_chestBase = nullptr;
                return false;
            }
        }
        auto* base = static_cast<TESObjectCONT*>(g_chestBase);
        SetName(base->name.name, title.c_str());
        base->container.formCountList.m_listHead.data = nullptr;
        base->container.formCountList.m_listHead.next = nullptr;
        NVSEArrayVarInterface::Element result;
        if (!g_scripts->CallFunction(g_spawn, Player(), nullptr, &result, 1, g_chestBase))
            return false;
        TESForm* placed = result.GetTESForm();
        if (!placed || !placed->GetIsReference())
            return false;
        g_chest = static_cast<TESObjectREFR*>(placed);
        SetEnabled(g_chest, false);
        RemoveAllItems(g_chest);
        Fill(stall);
        g_picks.clear();
        g_accepted = false;
        reinterpret_cast<void(__cdecl*)(TESObjectREFR*, int)>(kShowBarter)(g_chest, 0);
        LOG_INFO("Trade: window opened (%s, %zu listings, chest %08X)", title.c_str(), stall.size(), g_chest->refID);
        return true;
    }

    bool TradeWindowShown() { return OursOpen(); }

    bool TradeWindowExists() { return g_chest != nullptr; }

    bool RefreshTradeWindow(const std::vector<StallEntry>& stall)
    {
        if (!OursOpen() || PicksMarked())
            return false;
        RemoveAllItems(g_chest);
        Fill(stall);
        reinterpret_cast<void(__cdecl*)(bool)>(kBarterRebuild)(true);
        return true;
    }

    void CloseTradeWindow()
    {
        if (OursOpen())
            CloseMenu();
    }

    void TidyTradeWindow()
    {
        if (!g_chest || OursOpen())
            return;
        RemoveAllItems(g_chest);
        if (g_remove)
            g_scripts->CallFunctionAlt(g_remove, g_chest, 0);
        LOG_INFO("Trade: window closed; chest %08X removed", g_chest->refID);
        g_chest = nullptr;
        g_stall.clear();
    }

    std::string ItemName(uint32_t formId)
    {
        TESForm* form = LookupForm(formId);
        if (!form || !g_compiled || !Player())
            return "item";
        if (!g_nameTried)
        {
            g_nameTried = true;
            g_nameOf = Compile("item name",
                "ref rItem\n"
                "Begin Function {rItem}\n"
                "  SetFunctionValue $rItem\n"
                "End\n");
        }
        NVSEArrayVarInterface::Element result;
        if (!g_nameOf || !g_scripts->CallFunction(g_nameOf, Player(), nullptr, &result, 1, form))
            return "item";
        const char* name = result.GetString();
        return name && *name ? name : "item";
    }

    const char* WhyNotTradeable(const net::ItemCount& kind)
    {
        TESForm* form = LookupForm(kind.item);
        if (!form || !IsItem(kind.item))
            return "it isn't an item";
        if (net::IsMadeWhilePlaying(kind.item))
            return "it was made during play, so the other game doesn't have it";
        if (form->IsQuestItem())
            return "it's a quest item";
        if (form->typeID == kFormType_TESKey || form->typeID == kFormType_BGSNote)
            return form->typeID == kFormType_TESKey ? "keys stay with their owner" : "notes stay with their owner";
        if (form == TeleporterItem())
            return "it's the Co-op Teleporter";
        for (const net::WornItem& w : WornGear(Player()))
            if (w.item == kind.item && (w.health == kind.health || kind.health == net::kNoCondition))
                return "you're wearing it (take it off first)";
        return nullptr;
    }

    void ForgetTradeWindow()
    {
        g_chest = nullptr;
        g_stall.clear();
        g_picks.clear();
        g_accepted = false;
    }

    bool TakeTradeAccept(std::vector<TradePick>& picks)
    {
        picks.clear();
        if (!g_accepted)
            return false;
        g_accepted = false;
        std::vector<Item> mine;
        bool haveMine = Contents(Player(), mine);
        for (const RawPick& p : g_picks)
        {
            TradePick pick;
            pick.fromPartner = p.fromPartner;
            pick.itemType = p.type;
            int i;
            if (p.fromPartner)
            {
                i = Closest(p, g_stall.size(), [](size_t k) -> const net::ItemCount& { return g_stall[k].item; });
                if (i < 0)
                    continue;
                pick.item = g_stall[i].item;
            }
            else
            {
                if (!haveMine)
                    continue;
                std::vector<net::ItemCount> kinds;
                for (const Item& m : mine)
                    kinds.push_back({m.id, m.count, m.health, m.mods});
                i = Closest(p, kinds.size(), [&](size_t k) -> const net::ItemCount& { return kinds[k]; });
                if (i < 0)
                    continue;
                pick.item = kinds[i];
            }
            pick.item.count = p.count < pick.item.count ? p.count : pick.item.count;
            picks.push_back(pick);
        }
        g_picks.clear();
        return true;
    }
}
