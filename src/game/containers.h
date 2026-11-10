// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/containers.h"
#include "coop/coop.h"

#include <cstdint>
#include <map>
#include <vector>

class TESObjectREFR;

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
        void FriendRead(coop::Coop& coop, TESObjectREFR* ref);
        void HostWatchBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now);
        void HostShareNearby(coop::Coop& coop, uint32_t now);
        void FriendMatchBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now);
        coop::BodyLootWatch m_bodyLoot;
        coop::ContainerShareWatch m_shared;
        std::vector<uint32_t> m_nearFriends;
        uint32_t m_nextNearList = 0;
        struct Unmatched
        {
            std::vector<net::ItemCount> items;
            uint32_t until = 0;
        };
        std::map<uint32_t, Unmatched> m_unmatched;
        uint32_t m_lastBodyLook = 0;
        uint32_t m_nextLook = 0;
        uint32_t m_closedHeld = 0;
        uint32_t m_openRef = 0;
        uint32_t m_heldRef = 0;
        uint32_t m_heldUntil = 0;
        bool m_playerKnown = false;
        coop::FormCounts m_playerBefore;
        bool m_waitedForPrompt = false;
        double m_readMs = 0;
        uint32_t m_reads = 0, m_changes = 0, m_refunds = 0, m_fixes = 0, m_nextLog = 0;
    };
}
