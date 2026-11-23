// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/containers.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace coop
{
    std::vector<net::ItemCount> ItemDiff(const std::vector<net::ItemCount>& from,
        const std::vector<net::ItemCount>& to)
    {
        std::map<std::tuple<uint32_t, uint8_t, uint8_t>, int64_t> change;
        for (const net::ItemCount& i : to)
            change[{i.item, i.health, i.mods}] += i.count;
        for (const net::ItemCount& i : from)
            change[{i.item, i.health, i.mods}] -= i.count;
        std::vector<net::ItemCount> out;
        for (const auto& [kind, count] : change)
            if (count != 0)
                out.push_back({std::get<0>(kind), static_cast<int32_t>(count), std::get<1>(kind), std::get<2>(kind)});
        return out;
    }

    std::vector<net::ItemCount> MovesWithPlayer(const std::vector<net::ItemCount>& boxBefore,
        const std::vector<net::ItemCount>& boxNow, std::vector<net::ItemCount>& player)
    {
        std::vector<net::ItemCount> out;
        for (const net::ItemCount& b : ItemDiff(boxBefore, boxNow))
            for (net::ItemCount& p : player)
            {
                if (!b.SameKind(p) || !p.count || (b.count < 0) == (p.count < 0))
                    continue;
                int32_t want = b.count < 0 ? -b.count : b.count, have = p.count < 0 ? -p.count : p.count;
                int32_t moved = want < have ? want : have;
                net::ItemCount m = b;
                m.count = b.count < 0 ? -moved : moved;
                out.push_back(m);
                p.count += b.count < 0 ? -moved : moved;
                break;
            }
        return out;
    }

    bool FriendContainer::View(uint32_t ref, bool open)
    {
        m_open = ref && open;
        if (ref == m_ref)
            return false;
        m_ref = ref;
        m_hasSnapshot = m_hasHost = false;
        m_snapshot.clear();
        return true;
    }

    std::vector<net::ItemCount> FriendContainer::Look(const std::vector<net::ItemCount>& local,
        std::vector<net::ContainerChange>& changes, bool mayChange)
    {
        if (!m_ref)
            return {};
        if (m_hasSnapshot && m_open)
            for (const net::ItemCount& d : ItemDiff(m_snapshot, local))
                changes.push_back({m_ref, ++m_seq, d});
        m_snapshot = local;
        m_hasSnapshot = true;
        if (m_hasHost && m_host.ref == m_ref && static_cast<int32_t>(m_host.seen - m_seq) >= 0)
        {
            std::vector<net::ItemCount> fixes = ItemDiff(local, m_host.items);
            if (mayChange || fixes.empty())
                m_hasHost = false;
            return fixes;
        }
        return {};
    }

    void FriendContainer::Closing(const std::vector<net::ItemCount>& local,
        std::vector<net::ContainerChange>& changes)
    {
        if (!m_ref || !m_open)
            return;
        if (m_hasSnapshot)
            for (const net::ItemCount& d : ItemDiff(m_snapshot, local))
                changes.push_back({m_ref, ++m_seq, d});
        m_snapshot = local;
        m_hasSnapshot = true;
        m_open = false;
    }

    void FriendContainer::Watch(const std::vector<BoxContents>& boxes, const std::vector<net::ItemCount>& player,
        std::vector<net::ContainerChange>& changes)
    {
        if (m_hasPlayer)
        {
            std::vector<net::ItemCount> gained = ItemDiff(m_player, player);
            for (const BoxContents& now : boxes)
            {
                if (gained.empty())
                    break;
                auto was = std::find_if(m_watched.begin(), m_watched.end(),
                    [&](const BoxContents& w) { return w.first == now.first; });
                if (was != m_watched.end() && now.first)
                    for (const net::ItemCount& d : MovesWithPlayer(was->second, now.second, gained))
                        changes.push_back({now.first, ++m_seq, d});
            }
        }
        m_watched = boxes;
        m_player = player;
        m_hasPlayer = true;
    }

    void FriendContainer::SettleWatched(uint32_t ref, const std::vector<net::ItemCount>& local)
    {
        for (BoxContents& w : m_watched)
            if (w.first == ref)
                w.second = local;
    }

    void FriendContainer::Heard(const net::ContainerContents& m)
    {
        if (m.ref != m_ref || !m_ref)
            return;
        m_host = m;
        m_hasHost = true;
    }
}
