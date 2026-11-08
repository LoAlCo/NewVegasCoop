// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/world_items.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace coop
{
    namespace
    {
        float Turn(float a, float b)
        {
            return std::fabs(std::remainder(a - b, 6.2831853f));
        }

        ItemPose PoseOf(const LocalItem& l) { return {l.x, l.y, l.z, l.rx, l.ry, l.rz}; }
        ItemPose PoseOf(const net::ItemState& s) { return {s.x, s.y, s.z, s.rx, s.ry, s.rz}; }
    }

    bool ItemPose::Differs(const ItemPose& o, float distance, float angle) const
    {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return dx * dx + dy * dy + dz * dz > distance * distance || Turn(rx, o.rx) > angle || Turn(ry, o.ry) > angle ||
            Turn(rz, o.rz) > angle;
    }

    void FriendItems::Heard(const net::ItemState& host, uint32_t now)
    {
        if (m_host.size() >= kMaxKnownItems && m_host.find(host.ref) == m_host.end())
        {
            m_host.clear();
            m_heardAt.clear();
        }
        m_host[host.ref] = host;
        m_heardAt[host.ref] = now;
    }

    void FriendItems::Made(uint32_t hostRef, uint32_t ourRef)
    {
        auto it = m_copies.find(hostRef);
        if (it == m_copies.end())
            return;
        it->second = ourRef;
        if (ourRef)
        {
            const net::ItemState& host = m_host[hostRef];
            m_seen[ourRef] = {true, host.count, host.base, false, PoseOf(host), false, host.health, host.mods};
        }
    }

    void FriendItems::Taken(uint32_t ourRef, uint32_t hostRef)
    {
        m_taken.insert(ourRef);
        if (hostRef)
            m_copies[hostRef] = 0;
    }

    void FriendItems::Clear()
    {
        m_host.clear();
        m_heardAt.clear();
        m_copies.clear();
        m_seen.clear();
        m_dropped.clear();
        m_removed.clear();
        m_taken.clear();
        m_switchedOn.clear();
        m_goneHere.clear();
        m_parentGaps.clear();
        m_own.clear();
        m_holding.clear();
        m_followed.clear();
        m_started = false;
    }

    uint32_t FriendItems::OurRefOf(uint32_t hostRef) const
    {
        auto it = m_copies.find(hostRef);
        if (it != m_copies.end())
            return it->second;
        return net::IsMadeWhilePlaying(hostRef) ? 0 : hostRef;
    }

    uint32_t FriendItems::HostRefOf(uint32_t ourRef) const
    {
        if (!ourRef)
            return 0;
        for (const auto& [hostRef, ours] : m_copies)
            if (ours == ourRef)
                return hostRef;
        return net::IsMadeWhilePlaying(ourRef) ? 0 : ourRef;
    }

    ItemActions FriendItems::Look(const std::vector<LocalItem>& local, bool sameArea, uint32_t now)
    {
        ItemActions out;
        std::map<uint32_t, uint32_t> hostOf;
        for (const auto& [hostRef, ours] : m_copies)
            if (ours)
                hostOf[ours] = hostRef;

        std::map<uint32_t, Seen> looked;
        for (const LocalItem& l : local)
        {
            if (l.present && (m_removed.erase(l.ref) + m_taken.erase(l.ref)) > 0)
            {
                m_seen.erase(l.ref);
                m_dropped.erase(l.ref);
                m_own.erase(l.ref);
                m_holding.erase(l.ref);
                m_followed.erase(l.ref);
                if (auto c = hostOf.find(l.ref); c != hostOf.end())
                {
                    m_copies[c->second] = 0;
                    hostOf.erase(c);
                }
            }
            auto was = m_seen.find(l.ref);
            bool wasPresent = was == m_seen.end() ? l.present : was->second.present;
            bool afresh = l.present || was == m_seen.end();
            looked[l.ref] = {l.present, afresh ? l.count : was->second.count, l.base, l.nearMe, PoseOf(l), l.movable,
                afresh ? l.health : was->second.health, afresh ? l.mods : was->second.mods};
            bool made = net::IsMadeWhilePlaying(l.ref);
            auto copy = hostOf.find(l.ref);
            uint32_t hostRef = !made ? l.ref : copy != hostOf.end() ? copy->second : 0;

            auto parentWord = l.parent && !made ? m_host.find(hostRef) : m_host.end();
            if (parentWord != m_host.end() && parentWord->second.parent == l.parent &&
                now - m_heardAt[hostRef] <= kHostItemStaleMs)
            {
                uint32_t word = m_heardAt[hostRef];
                if (parentWord->second.parentOn == l.parentOn)
                    m_parentGaps.erase(l.parent);
                else
                {
                    if (m_parentGaps.size() > 256)
                        m_parentGaps.clear();
                    ParentGap& gap = m_parentGaps.emplace(l.parent, ParentGap{word, false}).first->second;
                    if (!gap.asked && word != gap.firstWord)
                    {
                        gap.asked = true;
                        out.parents.push_back({l.parent, parentWord->second.parentOn});
                    }
                }
            }

            if (wasPresent && !l.present && l.nearMe && hostRef && !l.movable && !m_removed.count(l.ref))
            {
                out.takes.push_back({hostRef, l.base, was->second.count, was->second.health, was->second.mods});
                Taken(l.ref, copy != hostOf.end() ? hostRef : 0);
                continue;
            }
            if (!l.present)
            {
                auto host = hostRef ? m_host.find(hostRef) : m_host.end();
                if (host != m_host.end() && host->second.present && now - m_heardAt[hostRef] <= kHostItemStaleMs &&
                    !m_taken.count(l.ref))
                {
                    uint32_t word = m_heardAt[hostRef];
                    bool settled = m_goneHere.emplace(l.ref, word).first->second != word;
                    bool copied = false;
                    if (settled && !made && !l.deleted && m_switchedOn.insert(l.ref).second)
                        out.enable.push_back(l.ref);
                    else if (settled && !made && l.deleted)
                    {
                        auto copy = m_copies.emplace(hostRef, 0);
                        if (copy.second)
                            out.make.push_back(host->second);
                        copied = copy.first->second != 0;
                    }
                    if (!copied)
                        out.differ.push_back({DesyncKind::Item, hostRef, DesyncAspect::There,
                            "gone here, there in the host's game"});
                }
                else
                    m_goneHere.erase(l.ref);
                continue;
            }
            m_switchedOn.erase(l.ref);
            m_goneHere.erase(l.ref);
            if (made && !hostRef)
            {
                if (m_started && l.nearMe && !net::IsMadeWhilePlaying(l.base) && m_dropped.insert(l.ref).second)
                    out.drops.push_back({l.base, l.count, l.x, l.y, l.z, l.health, l.mods});
                out.remove.push_back(l.ref);
                continue;
            }
            auto host = m_host.find(hostRef);
            if (host == m_host.end())
                continue;
            const net::ItemState& h = host->second;
            if (!h.present)
            {
                out.remove.push_back(l.ref);
                if (made && !net::IsMadeWhilePlaying(hostRef))
                    m_copies.erase(hostRef);
                out.differ.push_back({DesyncKind::Item, hostRef, DesyncAspect::There, "here, gone in the host's game"});
                continue;
            }
            const ItemPose mine = PoseOf(l);
            uint32_t word = m_heardAt[hostRef];
            if (now - word > kHostItemStaleMs)
            {
                m_own.erase(l.ref);
                m_holding.erase(l.ref);
                continue;
            }

            auto own = m_own.find(l.ref);
            bool fresh = own == m_own.end() && (l.held || (was != m_seen.end() && was->second.present && m_started &&
                sameArea && !l.following && (!l.movable || l.nearMe || was->second.nearMe) &&
                mine.Differs(was->second.pose, kItemMoveDistance, kItemTurnAngle)));
            if (fresh)
            {
                own = m_own.emplace(l.ref, Own{}).first;
                m_followed.erase(l.ref);
            }
            if (own != m_own.end())
            {
                Own& o = own->second;
                bool changed = fresh || mine.Differs(o.sent, kItemNudge, kItemNudgeAngle);
                if (changed || l.held)
                    o.movedAt = now;
                if (changed || l.held != o.held || (l.held && now - o.sentAt >= kHeldResendMs))
                {
                    out.moved.push_back({hostRef, mine.x, mine.y, mine.z, mine.rx, mine.ry, mine.rz, l.held});
                    o.sent = mine;
                    o.held = l.held;
                    o.sentAt = now;
                }
                if (!l.held && now - o.movedAt >= kOwnItemMs)
                    m_own.erase(own);
                m_holding.erase(l.ref);
                continue;
            }

            auto followed = m_followed.find(l.ref);
            bool newWord = followed == m_followed.end() || followed->second != word;
            float dx = l.x - h.x, dy = l.y - h.y, dz = l.z - h.z;
            float off = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!l.following && !l.movable && off > 2 * kItemMoveDistance)
                out.differ.push_back({DesyncKind::Item, hostRef, DesyncAspect::Place,
                    std::to_string(static_cast<int>(off)) + " units from the host's"});
            if (newWord && (h.held || m_holding.count(l.ref) ||
                mine.Differs(PoseOf(h), kItemMoveDistance, kItemTurnAngle)))
            {
                out.move.push_back({l.ref, h.x, h.y, h.z, h.rx, h.ry, h.rz, h.held});
                m_followed[l.ref] = word;
            }
            if (h.held)
                m_holding.insert(l.ref);
            else
                m_holding.erase(l.ref);
        }

        for (auto it = m_host.begin(); it != m_host.end();)
        {
            const net::ItemState& h = it->second;
            if (!net::IsMadeWhilePlaying(h.ref))
            {
                ++it;
                continue;
            }
            auto copy = m_copies.find(h.ref);
            if (h.present && copy == m_copies.end())
            {
                m_copies[h.ref] = 0;
                out.make.push_back(h);
            }
            else if (!h.present)
            {
                if (copy != m_copies.end())
                {
                    if (copy->second && looked.find(copy->second) == looked.end())
                        out.remove.push_back(copy->second);
                    m_copies.erase(copy);
                }
                m_heardAt.erase(it->first);
                it = m_host.erase(it);
                continue;
            }
            ++it;
        }
        for (const auto& [ref, seen] : m_seen)
        {
            if (looked.find(ref) != looked.end())
                continue;
            uint32_t hostRef = 0;
            if (!net::IsMadeWhilePlaying(ref))
                hostRef = ref;
            else if (auto copy = hostOf.find(ref); copy != hostOf.end())
                hostRef = copy->second;
            if (sameArea && seen.present && seen.nearMe && hostRef && !seen.movable && !m_removed.count(ref))
            {
                out.takes.push_back({hostRef, seen.base, seen.count, seen.health, seen.mods});
                Taken(ref, net::IsMadeWhilePlaying(ref) ? hostRef : 0);
            }
            else if (hostOf.find(ref) != hostOf.end())
                looked[ref] = seen;
        }
        for (auto it = m_dropped.begin(); it != m_dropped.end();)
            it = looked.find(*it) != looked.end() ? std::next(it) : m_dropped.erase(it);
        for (uint32_t ref : out.remove)
            m_removed.insert(ref);
        if (m_taken.size() > kMaxKnownItems)
            m_taken.clear();
        for (auto it = m_removed.begin(); it != m_removed.end();)
            it = looked.find(*it) != looked.end() ||
                std::find(out.remove.begin(), out.remove.end(), *it) != out.remove.end() ? std::next(it) :
                m_removed.erase(it);
        for (auto it = m_own.begin(); it != m_own.end();)
            it = looked.find(it->first) != looked.end() ? std::next(it) : m_own.erase(it);
        for (auto it = m_holding.begin(); it != m_holding.end();)
            it = looked.find(*it) != looked.end() ? std::next(it) : m_holding.erase(it);
        for (auto it = m_goneHere.begin(); it != m_goneHere.end();)
            it = looked.find(it->first) != looked.end() ? std::next(it) : m_goneHere.erase(it);
        for (auto it = m_followed.begin(); it != m_followed.end();)
            it = looked.find(it->first) != looked.end() ? std::next(it) : m_followed.erase(it);
        m_seen = std::move(looked);
        m_started = true;
        return out;
    }
}
