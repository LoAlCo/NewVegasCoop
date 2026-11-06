// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cmath>
#include <cstdint>

namespace coop
{
    // Each game runs its own ragdoll, so a body lands somewhere a bit different on the host and on a visitor. A body
    // is only moved once it has come to rest in both games and is clearly in the wrong place: moving it for small
    // differences or mid-fall would show as jitter.
    constexpr uint32_t kBodyLookEveryMs = 500;
    constexpr uint32_t kBodySettleMs = 3000;
    constexpr uint32_t kBodyRetryMs = 3000;
    constexpr float kBodyStillRange = 24;
    constexpr float kBodyFarOff = 150;
    constexpr uint32_t kMaxBodyMoves = 3;

    struct BodySpot
    {
        float x = 0, y = 0, z = 0;
        float DistanceTo(const BodySpot& o) const
        {
            float dx = x - o.x, dy = y - o.y, dz = z - o.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    };

    class BodyPlacer
    {
    public:
        // Should our copy of the body go to the host's spot now? Call it now and then; call Moved() on a move.
        bool Look(uint32_t now, const BodySpot& host, const BodySpot& ours)
        {
            if (!m_started)
            {
                m_started = true;
                m_host = host;
                m_ours = ours;
                m_hostStill = m_oursStill = now;
            }
            if (host.DistanceTo(m_host) > kBodyStillRange)
            {
                // A new resting place on the host (shoved, dragged, blown up): that's worth new tries.
                m_host = host;
                m_hostStill = now;
                m_moves = 0;
                m_gaveUp = false;
            }
            if (ours.DistanceTo(m_ours) > kBodyStillRange)
            {
                m_ours = ours;
                m_oursStill = now;
            }
            if (now - m_hostStill < kBodySettleMs || now - m_oursStill < kBodySettleMs ||
                host.DistanceTo(ours) <= kBodyFarOff)
                return false;
            if (m_moves >= kMaxBodyMoves)
            {
                m_gaveUp = true;
                return false;
            }
            return !m_moves || now - m_movedAt >= kBodyRetryMs;
        }

        void Moved(uint32_t now)
        {
            ++m_moves;
            m_movedAt = now;
        }

        uint32_t Moves() const { return m_moves; }
        bool GaveUp() const { return m_gaveUp; }

    private:
        bool m_started = false;
        bool m_gaveUp = false;
        BodySpot m_host, m_ours;
        uint32_t m_hostStill = 0, m_oursStill = 0, m_movedAt = 0;
        uint32_t m_moves = 0;
    };
}
