// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/far_cells.h"

#include "coop/far_area.h"

#include "game/addresses.h"
#include "game/game.h"
#include "log.h"

#include <windows.h>

#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace game
{
    namespace
    {
        template <typename T>
        T Field(const void* object, uint32_t offset)
        {
            return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
        }

        void* Tes() { return *reinterpret_cast<void**>(layout::kTesSingleton); }

        void* WorldSpace()
        {
            TESObjectREFR* player = Player();
            if (!player || !player->parentCell)
                return nullptr;
            return Field<void*>(player->parentCell, layout::kCellWorldSpace);
        }

        void* CellAt(int x, int y, bool create)
        {
            void* world = WorldSpace();
            if (!world)
                return nullptr;
            using CellAtFn = void*(__thiscall*)(void*, int, int);
            void* cell = reinterpret_cast<CellAtFn>(layout::kWorldSpaceCellAt)(world, x, y);
            if (cell || !create)
                return cell;
            void* data = *reinterpret_cast<void**>(addr::kDataHandler);
            using GetOrCreateFn = void*(__thiscall*)(void*, int, int, void*, bool);
            cell = reinterpret_cast<GetOrCreateFn>(layout::kDataHandlerGetOrCreateCell)(data, x, y, world, true);
            if (!cell)
                return nullptr;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kWorldSpaceAddCellRefs)(world, cell);
            reinterpret_cast<void(__thiscall*)(void*, bool)>(layout::kCellSetFlag10)(cell, true);
            LOG_INFO("Far cells: cell %d, %d loaded", x, y);
            return cell;
        }

        int StateOf(void* cell) { return reinterpret_cast<int(__thiscall*)(void*)>(layout::kCellState)(cell); }

        bool InGrid(void* cell)
        {
            void* grid = Field<void*>(Tes(), layout::kTesGridCells);
            if (!grid || !cell)
                return false;
            uint32_t size = Field<uint32_t>(grid, layout::kGridSize);
            void* const* cells = Field<void* const*>(grid, layout::kGridCellArray);
            if (!cells || size == 0 || size > 31)
                return false;
            for (uint32_t i = 0; i < size * size; ++i)
                if (cells[i] == cell)
                    return true;
            return false;
        }

        struct Ours
        {
            void* cell = nullptr;
            void* world = nullptr;
        };
        std::map<std::pair<int, int>, Ours> g_ours;
        using FillSlotFn = void(__thiscall*)(void* tes, uint32_t gridX, uint32_t gridY, int cellX, int cellY);
        FillSlotFn g_fillSlot = nullptr;
        uint32_t g_nextLook = 0;

        bool TakeOutOfBuffer(void* cell)
        {
            void** buffer = Tes() ? Field<void**>(Tes(), layout::kTesExteriorBuffer) : nullptr;
            unsigned size = *reinterpret_cast<const unsigned*>(layout::kExteriorBufferSetting + 4);
            return size <= 1024 && coop::TakeOutOfList(buffer, size, cell);
        }

        void LetGo(const std::pair<int, int>& at, const Ours& o)
        {
            int state = StateOf(o.cell);
            if (state == 6 || state == 5)
                reinterpret_cast<void(__thiscall*)(void*, bool)>(layout::kCellDetach)(o.cell, true);
            if (o.world == WorldSpace())
                reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kTesAddCellToBuffer)(Tes(), o.cell);
            else
                reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kDataHandlerDeleteCell)(
                    *reinterpret_cast<void**>(addr::kDataHandler), o.cell);
            LOG_INFO("Far cells: cell %d, %d let go (state %d)", at.first, at.second, StateOf(o.cell));
            g_ours.erase(at);
        }

        bool GridAround(int& cx, int& cy, int& half)
        {
            void* tes = Tes();
            void* grid = tes ? Field<void*>(tes, layout::kTesGridCells) : nullptr;
            if (!grid || !WorldSpace())
                return false;
            cx = Field<int>(tes, layout::kTesGridCentre);
            cy = Field<int>(tes, layout::kTesGridCentre + 4);
            half = static_cast<int>(Field<uint32_t>(grid, layout::kGridSize) / 2);
            return true;
        }
        bool NearGrid(int x, int y)
        {
            int cx, cy, half;
            if (!GridAround(cx, cy, half))
                return true;
            return coop::NearGrid(x, y, cx, cy, half);
        }

        struct HavokHold
        {
            void* world = reinterpret_cast<void*(__cdecl*)()>(layout::kOutdoorHavokWorld)();
            HavokHold()
            {
                if (world)
                    reinterpret_cast<void(__thiscall*)(void*)>(layout::kHavokBeforeAttach)(world);
            }
            ~HavokHold()
            {
                if (world)
                    reinterpret_cast<void(__thiscall*)(void*, int)>(layout::kHavokAfterAttach)(world, 0);
            }
        };
    }

    int FarCellState(int x, int y)
    {
        void* cell = CellAt(x, y, false);
        return cell ? StateOf(cell) : -1;
    }

    bool IsGridCell(int x, int y) { return InGrid(CellAt(x, y, false)); }

    bool AttachFarCell(int x, int y, std::string& why)
    {
        if (!g_fillSlot)
            return why = "the grid's guard isn't in (InstallFarCellGuard)", false;
        if (!Tes() || !WorldSpace())
            return why = "not outdoors", false;
        if (NearGrid(x, y))
            return why = "in or next to the player's grid (the grid's to attach)", false;
        void* cell = CellAt(x, y, true);
        if (!cell)
            return why = "no such cell", false;
        int state = StateOf(cell), before = state;
        if (state == 6 || state == 5)
            return why = InGrid(cell) ? "it's one of the player's own grid cells" : "attached already", false;
        void* root = Field<void*>(Tes(), layout::kTesSceneRoot);
        if (!root)
            return why = "no scene root", false;
        {
            HavokHold hold;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kCellAttachStart)(cell, root);
            if (StateOf(cell) == 5)
            {
                reinterpret_cast<void(__thiscall*)(void*, bool)>(layout::kCellAttachFinish)(cell, true);
                if (void* node = reinterpret_cast<void*(__thiscall*)(void*)>(layout::kCellNode)(cell))
                {
                    reinterpret_cast<void(__thiscall*)(void*)>(layout::kNodeAfterAttach)(node);
                    if (!reinterpret_cast<int(__thiscall*)(void*)>(layout::kNodeIsUpdated)(node))
                    {
                        alignas(16) uint8_t update[16] = {};
                        reinterpret_cast<void(__thiscall*)(void*, float, int, int)>(layout::kUpdateDataMake)(update, 0.0f,
                            0, 0);
                        reinterpret_cast<void(__thiscall*)(void*, void*)>(layout::kNodeUpdate)(node, update);
                    }
                }
            }
        }
        void* list = reinterpret_cast<uint8_t*>(Tes()) + layout::kTesAttachedList;
        reinterpret_cast<void(__thiscall*)(void*, void**)>(layout::kListPushFront)(list, &cell);
        state = StateOf(cell);
        LOG_INFO("Far cells: cell %d, %d attached (state %d, was %d)", x, y, state, before);
        if (state != 6)
            return why = "its state is " + std::to_string(state) + " after attaching", false;
        g_ours[{x, y}] = {cell, WorldSpace()};
        if (TakeOutOfBuffer(cell))
            LOG_DEBUG("Far cells: cell %d, %d taken out of the cell buffer", x, y);
        return true;
    }

    bool DetachFarCell(int x, int y, std::string& why)
    {
        auto it = g_ours.find({x, y});
        if (it == g_ours.end())
            return why = "not one of ours", false;
        LetGo(it->first, Ours(it->second));
        return true;
    }

    void LetGoOfFarCells()
    {
        while (!g_ours.empty())
            LetGo(g_ours.begin()->first, Ours(g_ours.begin()->second));
    }

    int FarCellCount() { return static_cast<int>(g_ours.size()); }

    void UpdateFarCells(uint32_t now)
    {
        if (g_ours.empty() || static_cast<int32_t>(now - g_nextLook) < 0)
            return;
        g_nextLook = now + 1000;
        for (auto it = g_ours.begin(); it != g_ours.end();)
        {
            auto [at, o] = *it;
            ++it;
            int state = StateOf(o.cell);
            if (o.world != WorldSpace())
            {
                LOG_INFO("Far cells: cell %d, %d is in a worldspace we left", at.first, at.second);
                LetGo(at, o);
            }
            else if (state != 6)
            {
                LOG_WARN("Far cells: cell %d, %d isn't attached any more (state %d); forgotten", at.first, at.second,
                    state);
                g_ours.erase(at);
            }
            else if (NearGrid(at.first, at.second))
            {
                LOG_INFO("Far cells: cell %d, %d is next to the player's grid now: the grid's to attach", at.first,
                    at.second);
                LetGo(at, o);
            }
            else if (TakeOutOfBuffer(o.cell))
                LOG_WARN("Far cells: cell %d, %d was in the cell buffer (put there by the game); taken out", at.first,
                    at.second);
        }

        static uint32_t s_nextSummary = 0, s_looks = 0, s_beyond = 0;
        void** buffer = Field<void**>(Tes(), layout::kTesExteriorBuffer);
        unsigned size = *reinterpret_cast<const unsigned*>(layout::kExteriorBufferSetting + 4);
        int cx, cy, half;
        if (buffer && size <= 1024 && GridAround(cx, cy, half))
        {
            unsigned front = static_cast<unsigned>((2 * half + 1) * (2 * half + 1));
            ++s_looks;
            for (unsigned i = front; i < size; ++i)
                if (buffer[i] && StateOf(buffer[i]) == 6)
                {
                    ++s_beyond;
                    LOG_WARN("Far cells: an attached cell sits at %u in the cell buffer (only the first %u should hold "
                        "attached ones)", i, front);
                    break;
                }
        }
        if (static_cast<int32_t>(now - s_nextSummary) >= 0)
        {
            if (s_nextSummary)
                LOG_INFO("Far cells: %zu attached by us; %u looks at the cell buffer (%u slots), %u with an attached "
                    "cell past the grid's slots (30 s)", g_ours.size(), s_looks, size, s_beyond);
            s_nextSummary = now + 30000;
            s_looks = s_beyond = 0;
        }
    }

    namespace
    {
        std::set<std::pair<int, int>> g_area;
        uint32_t g_nextAreaLook = 0, g_nextMemoryNote = 0;
        int CellOf(float v) { return static_cast<int>(std::floor(v / 4096.0f)); }
    }

    bool InFarCell(float x, float y) { return g_ours.count({CellOf(x), CellOf(y)}) != 0; }

    void UpdateFarArea(bool hosting, const std::vector<coop::FriendCell>& friends, uint32_t usedMb, uint32_t now)
    {
        if (static_cast<int32_t>(now - g_nextAreaLook) < 0)
            return;
        g_nextAreaLook = now + 500;
        for (auto it = g_area.begin(); it != g_area.end();)
            it = g_ours.count(*it) ? std::next(it) : g_area.erase(it);

        int cx, cy, half;
        if (!hosting || !GridAround(cx, cy, half))
        {
            for (auto [x, y] : std::set<std::pair<int, int>>(g_area))
            {
                std::string why;
                DetachFarCell(x, y, why);
            }
            if (!g_area.empty())
                LOG_INFO("Far cells: let go of the %zu around friends (not hosting outdoors)", g_area.size());
            g_area.clear();
            return;
        }
        coop::FarPlan plan = coop::PlanFarCells(g_area, friends, cx, cy, half, usedMb);
        for (auto [x, y] : plan.detach)
        {
            std::string why;
            DetachFarCell(x, y, why);
            g_area.erase({x, y});
            LOG_DEBUG("Far cells: let go of cell %d, %d (no friend near it, or next to our grid)", x, y);
        }
        for (auto [x, y] : plan.attach)
        {
            std::string why;
            if (AttachFarCell(x, y, why))
            {
                g_area.insert({x, y});
                LOG_INFO("Far cells: cell %d, %d attached around a friend (%zu now)", x, y, g_area.size());
            }
            else
            {
                LOG_WARN("Far cells: cell %d, %d around a friend not attached: %s", x, y, why.c_str());
                g_nextAreaLook = now + 5000;
            }
        }
        if (plan.memoryFull && static_cast<int32_t>(now - g_nextMemoryNote) >= 0)
        {
            g_nextMemoryNote = now + 60000;
            LOG_WARN("Far cells: our game uses %u MB (over %u): no more cells attached around friends", usedMb,
                coop::kFarMemoryLimitMb);
        }
    }

    namespace
    {
        void __fastcall FillSlotHook(void* tes, void*, uint32_t gridX, uint32_t gridY, int cellX, int cellY)
        {
            if (auto it = g_ours.find({cellX, cellY}); it != g_ours.end())
            {
                LetGo(it->first, Ours(it->second));
                g_area.erase({cellX, cellY});
                LOG_INFO("Far cells: cell %d, %d goes into the player's grid: ours detached first", cellX, cellY);
            }
            g_fillSlot(tes, gridX, gridY, cellX, cellY);
        }
    }

    bool InstallFarCellGuard()
    {
        if (g_fillSlot)
            return true;
        auto* p = reinterpret_cast<uint8_t*>(layout::kTesFillGridSlotCall);
        if (p[0] != 0xE8)
        {
            LOG_ERROR("Far cells: the grid's slot call at %08X isn't as expected (%02X); no far cells",
                static_cast<uint32_t>(layout::kTesFillGridSlotCall), p[0]);
            return false;
        }
        int32_t rel;
        std::memcpy(&rel, p + 1, 4);
        FillSlotFn original = reinterpret_cast<FillSlotFn>(layout::kTesFillGridSlotCall + 5 + rel);
        DWORD old;
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
            return false;
        g_fillSlot = original;
        int32_t ours = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FillSlotHook) - (layout::kTesFillGridSlotCall + 5));
        std::memcpy(p + 1, &ours, 4);
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        if (reinterpret_cast<uintptr_t>(original) != layout::kTesFillGridSlot)
            LOG_INFO("Far cells: another plugin hooks the grid's slot call too; ours goes first and passes on");
        return true;
    }
}
