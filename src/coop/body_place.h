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

    // A body someone moves after it came to rest (dragged, shoved, blown up) is followed: the host shares it a few
    // times a second, and a visitor steers its copy's pelvis at a point a little in the past, always between two
    // updates. Its own physics keeps running, so the limbs trail and it stays out of walls.
    constexpr uint32_t kBodyShareEveryMs = 200;
    constexpr uint32_t kBodyBlendMs = 250;
    constexpr uint32_t kBodyFollowEndMs = 1000;
    constexpr float kBodyChaseSeconds = 0.2f;
    constexpr float kBodyMaxSpeed = 600;
    constexpr float kBodyCloseEnough = 8;
    constexpr uint32_t kMaxBodiesFollowed = 4;

    struct BodySpot
    {
        float x = 0, y = 0, z = 0;
        float DistanceTo(const BodySpot& o) const
        {
            float dx = x - o.x, dy = y - o.y, dz = z - o.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    };

    // The speed that takes a body from where it is toward the target; false when it's close enough to leave be.
    inline bool BodyPull(const BodySpot& target, const BodySpot& ours, BodySpot& speed)
    {
        float off = target.DistanceTo(ours);
        if (off <= kBodyCloseEnough)
            return false;
        float scale = 1 / kBodyChaseSeconds;
        if (off * scale > kBodyMaxSpeed)
            scale = kBodyMaxSpeed / off;
        speed = {(target.x - ours.x) * scale, (target.y - ours.y) * scale, (target.z - ours.z) * scale};
        return true;
    }

    class BodyFollower
    {
    public:
        // Is the host's body being moved after having come to rest? Bodies still falling from their death aren't
        // followed: each game runs that fall, and BodyPlacer sorts out where they ended up.
        bool Look(uint32_t now, const BodySpot& host)
        {
            if (!m_started)
            {
                m_started = true;
                m_rest = host;
                m_restSince = now;
                return false;
            }
            if (host.DistanceTo(m_rest) > kBodyStillRange)
            {
                m_rest = host;
                m_restSince = now;
                m_following = m_rested;
            }
            else if (now - m_restSince >= kBodySettleMs)
                m_rested = true;
            if (m_following && now - m_restSince >= kBodyFollowEndMs)
                m_following = false;
            return m_following;
        }

    private:
        bool m_started = false, m_rested = false, m_following = false;
        BodySpot m_rest;
        uint32_t m_restSince = 0;
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
