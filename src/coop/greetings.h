// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"

#include <cstdint>
#include <iterator>
#include <map>
#include <vector>

namespace coop
{
    struct PlayerAt
    {
        uint8_t id = 0;
        Pose pose;
    };

    inline bool ClosestPlayer(const Pose& npc, const std::vector<PlayerAt>& players, uint8_t& closest)
    {
        bool found = false;
        float best = 0;
        for (const PlayerAt& p : players)
        {
            if (p.pose.space != npc.space)
                continue;
            float dx = p.pose.x - npc.x, dy = p.pose.y - npc.y, dz = p.pose.z - npc.z;
            float d = dx * dx + dy * dy + dz * dz;
            if (!found || d < best || (d == best && p.id < closest))
                found = true, best = d, closest = p.id;
        }
        return found;
    }

    class Greetings
    {
    public:
        static constexpr uint32_t kHoldMs = 10000;

        uint8_t HostGreeted(uint32_t npc, uint8_t me, uint8_t closest, uint32_t now)
        {
            if (Held(npc, now))
                return kNobody;
            if (closest != me)
                m_handed[npc] = now + kHoldMs;
            return closest;
        }

        bool FriendAsks(uint32_t npc, uint8_t asker, uint8_t closest, bool someoneTalking, uint32_t now)
        {
            if (closest != asker || someoneTalking || Held(npc, now))
                return false;
            m_handed[npc] = now + kHoldMs;
            return true;
        }

        void Clear() { m_handed.clear(); }
        static constexpr uint8_t kNobody = 0xFF;

    private:
        bool Held(uint32_t npc, uint32_t now)
        {
            for (auto it = m_handed.begin(); it != m_handed.end();)
                it = static_cast<int32_t>(now - it->second) >= 0 ? m_handed.erase(it) : std::next(it);
            return m_handed.count(npc) != 0;
        }
        std::map<uint32_t, uint32_t> m_handed;
    };

    class GreetAsks
    {
    public:
        static constexpr uint32_t kEveryMs = 5000;
        bool MayAsk(uint32_t npc, uint32_t now)
        {
            auto it = m_last.find(npc);
            if (it != m_last.end() && now - it->second < kEveryMs)
                return false;
            if (m_last.size() >= 64)
                m_last.clear();
            m_last[npc] = now;
            return true;
        }
        void Clear() { m_last.clear(); }

    private:
        std::map<uint32_t, uint32_t> m_last;
    };
}
