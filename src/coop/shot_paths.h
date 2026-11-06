// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"
#include "net/protocol.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace coop
{
    constexpr uint32_t kShotPathWaitMs = 500;
    // A fast gun's paths aren't sent reliably, so now and then one is lost. Its shot shouldn't look much later than the rest.
    constexpr uint32_t kFastShotPathWaitMs = 200;
    constexpr size_t kMaxShotsWaiting = 32;

    // A friend's shot arrives as two messages: the attack, seen when their animation starts, and the path, seen
    // when the projectile leaves the gun. Either can come first. This pairs them up, and says when an attack's path
    // never came, so its shot can be fired from the character's own aim instead of going missing.
    class ShotPaths
    {
    public:
        void Attacked(uint32_t shots, uint32_t now, uint32_t waitMs = kShotPathWaitMs)
        {
            DropExpired(m_ahead, now);
            for (; shots > 0 && !m_ahead.empty(); --shots)
                m_ahead.pop_front();
            for (; shots > 0 && m_waiting.size() < kMaxShotsWaiting; --shots)
                m_waiting.push_back(now + waitMs);
        }

        // In a fast gun's stream, a path says the shots before it needn't be made up for: one lost tracer among many isn't
        // seen, but a handful fired late when the stream stops would be.
        void PathCame(uint32_t now, bool stream = false)
        {
            DropExpired(m_ahead, now);
            if (stream && !m_waiting.empty())
                m_waiting.clear();
            else if (!m_waiting.empty())
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

    // Projectiles a trigger pull made leave from one spot: that's what tells pellets from separate shots. They become
    // one path, from their middle, saying how far they spread.
    inline std::vector<net::ShotPath> GroupPellets(const std::vector<net::ShotPath>& shots)
    {
        constexpr float kFullCircle = 6.2831853f;
        std::vector<net::ShotPath> paths;
        for (size_t i = 0; i < shots.size();)
        {
            const net::ShotPath& first = shots[i];
            size_t end = i + 1;
            while (end < shots.size() && end - i < net::kMaxShotPellets && shots[end].projectile == first.projectile &&
                shots[end].x == first.x && shots[end].y == first.y && shots[end].z == first.z)
                ++end;
            float sumX = 0, sumZ = 0;
            for (size_t k = i; k < end; ++k)
            {
                sumX += std::remainder(shots[k].rotX - first.rotX, kFullCircle);
                sumZ += std::remainder(shots[k].rotZ - first.rotZ, kFullCircle);
            }
            float count = static_cast<float>(end - i);
            float middleX = sumX / count, middleZ = sumZ / count;
            float spread = 0;
            for (size_t k = i; k < end; ++k)
            {
                float dx = std::remainder(shots[k].rotX - first.rotX, kFullCircle) - middleX;
                float dz = std::remainder(shots[k].rotZ - first.rotZ, kFullCircle) - middleZ;
                float off = std::sqrt(dx * dx + dz * dz);
                spread = off > spread ? off : spread;
            }
            net::ShotPath path = first;
            path.rotX = first.rotX + middleX;
            path.rotZ = first.rotZ + middleZ;
            path.pellets = static_cast<uint8_t>(end - i);
            path.spread = spread;
            paths.push_back(path);
            i = end;
        }
        return paths;
    }

    // Shots can come dozens a second, so only slow ones get a log line each.
    class ShotLogPace
    {
    public:
        // How many shots this line stands for, or 0 for no line now.
        uint32_t Look(bool everyTime, uint32_t now)
        {
            ++m_unsaid;
            if (!everyTime && m_nextAt != 0 && !IsDue(now, m_nextAt))
                return 0;
            m_nextAt = now + 1000;
            uint32_t said = m_unsaid;
            m_unsaid = 0;
            return said;
        }

    private:
        uint32_t m_nextAt = 0, m_unsaid = 0;
    };
}
