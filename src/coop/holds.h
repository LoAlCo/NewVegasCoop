// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace coop
{
    struct LocalActor
    {
        uint32_t ref = 0;
        bool playerOrStandIn = false;
        bool ourCopy = false;
        bool sharedByHost = false;
        bool enabled = true;
        bool alive = true;
        bool loaded = true;
        bool samePlace = true;
    };

    inline bool ShouldHold(const LocalActor& a, bool hostListFull)
    {
        return !hostListFull && a.ref != 0 && !a.playerOrStandIn && !a.ourCopy && !a.sharedByHost && a.enabled &&
            a.alive && a.loaded && a.samePlace;
    }

    class Holds
    {
    public:
        static constexpr size_t kMaxHolds = 64;

        struct Released
        {
            uint32_t ref = 0;
            bool wasGhost = false;
            bool operator==(const Released& o) const { return ref == o.ref && wasGhost == o.wasGhost; }
        };
        struct Changes
        {
            std::vector<uint32_t> start;
            std::vector<Released> release;
        };

        Changes Look(const std::vector<LocalActor>& nearby, bool hostListFull)
        {
            Changes c;
            std::set<uint32_t> want;
            for (const LocalActor& a : nearby)
                if (ShouldHold(a, hostListFull))
                    want.insert(a.ref);
            for (auto it = m_held.begin(); it != m_held.end();)
                if (!want.count(it->first))
                {
                    c.release.push_back({it->first, it->second});
                    it = m_held.erase(it);
                }
                else
                    ++it;
            for (uint32_t ref : want)
                if (!m_held.count(ref) && m_held.size() < kMaxHolds)
                {
                    m_held[ref] = false;
                    c.start.push_back(ref);
                }
            return c;
        }
        std::vector<Released> ReleaseAll()
        {
            std::vector<Released> all;
            for (const auto& [ref, ghost] : m_held)
                all.push_back({ref, ghost});
            m_held.clear();
            return all;
        }
        void NoteGhost(uint32_t ref, bool wasGhost)
        {
            if (auto it = m_held.find(ref); it != m_held.end())
                it->second = wasGhost;
        }
        bool IsHeld(uint32_t ref) const { return m_held.count(ref) != 0; }
        size_t Count() const { return m_held.size(); }

    private:
        std::map<uint32_t, bool> m_held;
    };
}
