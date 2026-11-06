// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace coop
{
    constexpr uint32_t kShotPathWaitMs = 500;
    constexpr size_t kMaxShotsWaiting = 32;

    // A friend's slow shot arrives as two messages: the attack, seen when their animation starts, and the path, seen
    // when the projectile leaves the gun. Either can come first. This pairs them up, and says when an attack's path
    // never came, so its shot can be fired from the character's own aim instead of going missing.
    class ShotPaths
    {
    public:
        void Attacked(uint32_t shots, uint32_t now)
        {
            DropExpired(m_ahead, now);
            for (; shots > 0 && !m_ahead.empty(); --shots)
                m_ahead.pop_front();
            for (; shots > 0 && m_waiting.size() < kMaxShotsWaiting; --shots)
                m_waiting.push_back(now + kShotPathWaitMs);
        }

        void PathCame(uint32_t now)
        {
            DropExpired(m_ahead, now);
            if (!m_waiting.empty())
                m_waiting.pop_front();
            else if (m_ahead.size() < kMaxShotsWaiting)
                m_ahead.push_back(now + kShotPathWaitMs);
        }

        // How many attacks gave up waiting for their path just now.
        uint32_t TakeOverdue(uint32_t now)
        {
            DropExpired(m_ahead, now);
            return DropExpired(m_waiting, now);
        }

    private:
        static uint32_t DropExpired(std::deque<uint32_t>& list, uint32_t now)
        {
            uint32_t dropped = 0;
            for (; !list.empty() && IsDue(now, list.front()); ++dropped)
                list.pop_front();
            return dropped;
        }

        std::deque<uint32_t> m_waiting;
        std::deque<uint32_t> m_ahead;
    };
}
