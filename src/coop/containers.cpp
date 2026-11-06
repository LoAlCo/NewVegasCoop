// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/containers.h"

#include "log.h"

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

    int32_t TotalOf(const std::vector<net::ItemCount>& items, uint32_t item)
    {
        int64_t total = 0;
        for (const net::ItemCount& i : items)
            if (i.item == item)
                total += i.count;
        return static_cast<int32_t>(total);
    }

    ContainerTake PlanContainerTake(const std::vector<net::ItemCount>& have, const net::ItemCount& want)
    {
        ContainerTake plan;
        int32_t left = want.count < 0 ? -want.count : 0;
        auto take = [&](const net::ItemCount& stack) {
            int32_t n = stack.count < left ? stack.count : left;
            if (n <= 0)
                return;
            net::ItemCount remove = stack;
            remove.count = -n;
            plan.removes.push_back(remove);
            plan.taken += n;
            left -= n;
        };
        for (const net::ItemCount& i : have)
            if (i.SameKind(want))
                take(i);
        for (const net::ItemCount& i : have)
            if (i.item == want.item && !i.SameKind(want))
                take(i);
        plan.missing = left;
        return plan;
    }

    bool FriendContainer::View(uint32_t ref, bool open)
    {
        bool nowOpen = ref && open;
        if (ref == m_ref && m_open && !nowOpen)
            m_closedSinceLook = true;
        m_open = nowOpen;
        if (ref == m_ref)
            return false;
        m_ref = ref;
        m_closedSinceLook = false;
        m_hasSnapshot = m_hasHost = false;
        m_snapshot.clear();
        return true;
    }

    std::vector<net::ItemCount> FriendContainer::Look(const std::vector<net::ItemCount>& local,
        std::vector<net::ContainerChange>& changes, const std::vector<net::ItemCount>& playerChange)
    {
        if (!m_ref)
            return {};
        // Take All, and closing right after taking something, shut the menu between two looks: what changed since the
        // last look is still the player's doing, and has to reach the host.
        bool theirDoing = m_open || m_closedSinceLook;
        m_closedSinceLook = false;
        if (m_hasSnapshot)
        {
            std::vector<net::ItemCount> diff = ItemDiff(m_snapshot, local);
            if (theirDoing)
                for (const net::ItemCount& d : diff)
                    changes.push_back({m_ref, ++m_seq, d});
            else if (!diff.empty())
            {
                std::map<uint32_t, int64_t> player;
                for (const net::ItemCount& p : playerChange)
                    player[p.item] += p.count;
                size_t ignored = 0;
                for (net::ItemCount d : diff)
                {
                    int64_t& moved = player[d.item];
                    int64_t wanted = d.count < 0 ? -static_cast<int64_t>(d.count) : d.count;
                    int64_t matched = 0;
                    if (d.count < 0 && moved > 0)
                        matched = wanted < moved ? wanted : moved;
                    else if (d.count > 0 && moved < 0)
                        matched = wanted < -moved ? wanted : -moved;
                    if (matched < wanted)
                        ++ignored;
                    if (!matched)
                        continue;
                    moved += d.count < 0 ? -matched : matched;
                    d.count = static_cast<int32_t>(d.count < 0 ? -matched : matched);
                    changes.push_back({m_ref, ++m_seq, d});
                    LOG_DEBUG("Containers: %08X and our inventory traded %d of %08X with no menu seen open: the "
                        "player's doing", m_ref, d.count, d.item);
                }
                if (ignored)
                    LOG_DEBUG("Containers: %08X changed by %zu kinds of items that our inventory didn't: not told to "
                        "the host", m_ref, ignored);
            }
        }
        m_snapshot = local;
        m_hasSnapshot = true;
        if (m_hasHost && m_host.ref == m_ref && static_cast<int32_t>(m_host.seen - m_seq) >= 0)
        {
            m_hasHost = false;
            return ItemDiff(local, m_host.items);
        }
        return {};
    }

    void FriendContainer::Heard(const net::ContainerContents& m)
    {
        if (m.ref != m_ref || !m_ref)
            return;
        m_host = m;
        m_hasHost = true;
    }
}
