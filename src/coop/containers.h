// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <vector>

namespace coop
{
    std::vector<net::ItemCount> ItemDiff(const std::vector<net::ItemCount>& from,
        const std::vector<net::ItemCount>& to);

    int32_t TotalOf(const std::vector<net::ItemCount>& items, uint32_t item);

    struct ContainerTake
    {
        std::vector<net::ItemCount> removes;
        int32_t taken = 0;
        int32_t missing = 0;
    };
    // A player's copy of a stack can read differently from the host's (its condition, say), so the host takes the
    // kind they asked for first and then others of the same item. Only what truly isn't there is given back.
    ContainerTake PlanContainerTake(const std::vector<net::ItemCount>& have, const net::ItemCount& want);

    class FriendContainer
    {
    public:
        bool View(uint32_t ref, bool open = true);
        uint32_t Viewing() const { return m_ref; }
        std::vector<net::ItemCount> Look(const std::vector<net::ItemCount>& local,
            std::vector<net::ContainerChange>& changes);
        void Settle(const std::vector<net::ItemCount>& local) { m_snapshot = local; }
        void Heard(const net::ContainerContents& m);
        void Reset() { *this = FriendContainer(); }

    private:
        uint32_t m_ref = 0;
        bool m_open = false;
        bool m_closedSinceLook = false;
        uint32_t m_seq = 0;
        bool m_hasSnapshot = false;
        std::vector<net::ItemCount> m_snapshot;
        bool m_hasHost = false;
        net::ContainerContents m_host;
    };
}
