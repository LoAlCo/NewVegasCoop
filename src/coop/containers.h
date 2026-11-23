// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace coop
{
    std::vector<net::ItemCount> ItemDiff(const std::vector<net::ItemCount>& from,
        const std::vector<net::ItemCount>& to);

    std::vector<net::ItemCount> MovesWithPlayer(const std::vector<net::ItemCount>& boxBefore,
        const std::vector<net::ItemCount>& boxNow, std::vector<net::ItemCount>& player);
    using BoxContents = std::pair<uint32_t, std::vector<net::ItemCount>>;

    class FriendContainer
    {
    public:
        bool View(uint32_t ref, bool open = true);
        std::vector<net::ItemCount> Look(const std::vector<net::ItemCount>& local,
            std::vector<net::ContainerChange>& changes, bool mayChange = true);
        void Closing(const std::vector<net::ItemCount>& local, std::vector<net::ContainerChange>& changes);
        void Settle(const std::vector<net::ItemCount>& local) { m_snapshot = local; }
        void Heard(const net::ContainerContents& m);
        void Watch(const std::vector<BoxContents>& boxes, const std::vector<net::ItemCount>& player,
            std::vector<net::ContainerChange>& changes);
        void SettleWatched(uint32_t ref, const std::vector<net::ItemCount>& local);
        void Reset() { *this = FriendContainer(); }

    private:
        uint32_t m_ref = 0;
        bool m_open = false;
        uint32_t m_seq = 0;
        bool m_hasSnapshot = false;
        std::vector<net::ItemCount> m_snapshot;
        std::vector<BoxContents> m_watched;
        bool m_hasPlayer = false;
        std::vector<net::ItemCount> m_player;
        bool m_hasHost = false;
        net::ContainerContents m_host;
    };
}
