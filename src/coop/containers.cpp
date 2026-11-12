// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/containers.h"

#include "log.h"

#include <algorithm>
#include <iterator>
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

    std::vector<uint32_t> FormsToCount(const FormCounts& before, const std::vector<net::ItemCount>& local)
    {
        FormCounts forms = before;
        for (const net::ItemCount& i : local)
            forms[i.item];
        std::vector<uint32_t> out;
        out.reserve(forms.size());
        for (const auto& [form, count] : forms)
            out.push_back(form);
        return out;
    }

    std::vector<net::ItemCount> CountChanges(const FormCounts& before, const FormCounts& now)
    {
        std::vector<net::ItemCount> out;
        for (const auto& [form, was] : before)
        {
            auto is = now.find(form);
            int32_t count = is == now.end() ? 0 : is->second;
            if (count != was)
            {
                net::ItemCount change;
                change.item = form;
                change.count = count - was;
                out.push_back(change);
            }
        }
        return out;
    }

    FormCounts CountsToKeep(const FormCounts& now, const std::vector<net::ItemCount>& local)
    {
        FormCounts keep;
        for (const net::ItemCount& i : local)
        {
            auto is = now.find(i.item);
            keep[i.item] = is == now.end() ? 0 : is->second;
        }
        return keep;
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

    bool BodyLootWatch::Look(uint32_t ref, const std::vector<net::ItemCount>& items, bool settling)
    {
        auto seen = m_seen.find(ref);
        if (seen == m_seen.end())
        {
            m_seen[ref] = {items, items};
            return true;
        }
        Seen& s = seen->second;
        std::vector<net::ItemCount> diff = ItemDiff(s.items, items);
        s.items = items;
        bool share = false;
        if (settling)
            share = std::any_of(diff.begin(), diff.end(), [](const net::ItemCount& d) { return d.count < 0; });
        else
            share = !ItemDiff(s.shared, items).empty();
        if (share)
            s.shared = items;
        return share;
    }

    void BodyLootWatch::KeepOnly(const std::vector<uint32_t>& refs)
    {
        for (auto it = m_seen.begin(); it != m_seen.end();)
            it = std::find(refs.begin(), refs.end(), it->first) != refs.end() ? std::next(it) : m_seen.erase(it);
    }

    std::vector<uint32_t> ContainerShareWatch::ToRead(const std::vector<uint32_t>& inReach, size_t maxNew)
    {
        m_near = inReach;
        std::sort(m_near.begin(), m_near.end());
        m_near.erase(std::unique(m_near.begin(), m_near.end()), m_near.end());
        for (auto it = m_seen.begin(); it != m_seen.end();)
            it = std::binary_search(m_near.begin(), m_near.end(), it->first) ? std::next(it) : m_seen.erase(it);
        std::vector<uint32_t> out;
        bool anySeen = false;
        for (uint32_t ref : m_near)
            if (!m_seen.count(ref))
            {
                if (out.size() < maxNew)
                    out.push_back(ref);
            }
            else
                anySeen = true;
        if (anySeen)
        {
            auto next = std::upper_bound(m_near.begin(), m_near.end(), m_last);
            for (size_t i = 0; i < m_near.size(); ++i, ++next)
            {
                if (next == m_near.end())
                    next = m_near.begin();
                if (m_seen.count(*next))
                {
                    m_last = *next;
                    out.push_back(*next);
                    break;
                }
            }
        }
        return out;
    }

    bool ContainerShareWatch::Read(uint32_t ref, const std::vector<net::ItemCount>& items)
    {
        if (!std::binary_search(m_near.begin(), m_near.end(), ref))
            return false;
        auto seen = m_seen.find(ref);
        if (seen == m_seen.end())
        {
            m_seen[ref] = items;
            return true;
        }
        if (ItemDiff(seen->second, items).empty())
            return false;
        seen->second = items;
        return true;
    }

    bool UnseenContainers::Offered(uint32_t ref, uint8_t playerId, uint32_t seq,
        const std::vector<net::ItemCount>& items)
    {
        if (!ref || m_records.count(ref))
            return false;
        m_records[ref] = {items, playerId, seq};
        return true;
    }

    const std::vector<net::ItemCount>* UnseenContainers::Find(uint32_t ref) const
    {
        auto it = m_records.find(ref);
        return it == m_records.end() ? nullptr : &it->second.items;
    }

    bool UnseenContainers::Change(uint32_t ref, uint8_t playerId, uint32_t seq, const net::ItemCount& want,
        net::ItemCount& refund)
    {
        refund = want;
        refund.count = 0;
        auto it = m_records.find(ref);
        if (it == m_records.end())
            return false;
        Record& r = it->second;
        if (playerId == r.by && static_cast<int32_t>(seq - r.seq) <= 0)
            return true;
        auto add = [&](const net::ItemCount& c) {
            auto same = std::find_if(r.items.begin(), r.items.end(),
                [&](const net::ItemCount& i) { return i.SameKind(c); });
            if (same == r.items.end())
            {
                if (c.count > 0)
                    r.items.push_back(c);
                return;
            }
            same->count += c.count;
            if (same->count <= 0)
                r.items.erase(same);
        };
        if (want.count < 0)
        {
            ContainerTake plan = PlanContainerTake(r.items, want);
            for (const net::ItemCount& remove : plan.removes)
                add(remove);
            refund.count = -plan.missing;
        }
        else
            add(want);
        return true;
    }

    bool UnseenContainers::Release(uint32_t ref, std::vector<net::ItemCount>& out)
    {
        auto it = m_records.find(ref);
        if (it == m_records.end())
            return false;
        out = std::move(it->second.items);
        m_records.erase(it);
        return true;
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
        std::vector<net::ContainerChange>& changes, const std::vector<net::ItemCount>& playerChange, bool fix)
    {
        if (!m_ref)
            return {};
        if (m_hasSnapshot)
        {
            std::vector<net::ItemCount> diff = ItemDiff(m_snapshot, local);
            if (m_open)
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
        if (fix && m_hasHost && m_host.ref == m_ref && static_cast<int32_t>(m_host.seen - m_seq) >= 0)
        {
            m_hasHost = false;
            return ItemDiff(local, m_host.items);
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

    void FriendContainer::Heard(const net::ContainerContents& m)
    {
        if (m.ref != m_ref || !m_ref)
            return;
        m_host = m;
        m_hasHost = true;
    }
}
