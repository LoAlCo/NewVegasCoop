// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <set>
#include <vector>

namespace game
{
    class Npcs;

    class Containers
    {
    public:
        void Update(coop::Coop& coop, uint32_t now, const Npcs& npcs);

    private:
        void HostApply(coop::Coop& coop);
        void FriendLook(coop::Coop& coop, const Npcs& npcs, uint32_t now);
        void GiveBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now);
        std::vector<uint32_t> m_bodyRefs;
        std::vector<std::pair<float, uint32_t>> m_giveOrder;
        uint32_t m_bodiesGiven = 0;
        uint32_t m_boxesPutBack = 0;
        void ShareBoxes(coop::Coop& coop, uint32_t now);
        bool ReadBox(coop::Coop& coop, uint32_t id);
        std::vector<uint32_t> m_boxes;
        std::set<uint32_t> m_boxesRead;
        uint32_t m_nextBoxList = 0, m_boxTurn = 0;
        double m_boxListMs = 0;
        uint32_t m_boxLists = 0;
        size_t m_farBoxes = 0;
        uint32_t m_nextLook = 0;
        uint32_t m_closedHeld = 0;
        uint32_t m_openRef = 0;
        bool m_waitedForPrompt = false;
        std::vector<coop::Coop::ItemChangeFrom> m_hostChanges;
        uint32_t m_closedToMatch = 0, m_closedUntil = 0;
        void Watch(coop::Coop& coop, const Npcs& npcs, uint32_t now);
        std::vector<uint32_t> m_watchList;
        uint32_t m_nextWatchList = 0;
        double m_readMs = 0;
        uint32_t m_reads = 0, m_changes = 0, m_refunds = 0, m_fixes = 0, m_nextLog = 0;
    };
}
