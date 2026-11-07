// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"
#include "net/protocol.h"

#include <cstdint>
#include <cstdlib>

namespace coop
{
    // Optional limb syncing. Each game runs its own ragdoll, and the torso alone is kept together by body_place.h. With
    // this on, the host also shares where the limbs are relative to the torso, and a visitor steers its copy's limbs
    // there for a short while after each change, then lets physics rest so a body doesn't jitter.
    constexpr uint32_t kBodyPoseEveryMs = 200;
    constexpr uint32_t kBodyPoseStillMs = 1000;
    constexpr uint32_t kBodyPoseRepeatMs = 5000;
    constexpr uint32_t kBodyPoseSteerMs = 1500;
    constexpr int kBodyPoseMoved = 6;
    constexpr uint32_t kMaxBodiesPosed = 4;

    inline bool BodyPoseDiffers(const net::BodyPose& a, const net::BodyPose& b)
    {
        if (a.bones != b.bones)
            return true;
        for (size_t bone = 0; bone < net::kBodyPoseBones; ++bone)
            if (a.bones & (1u << bone))
                for (int axis = 0; axis < 3; ++axis)
                    if (std::abs(int(a.offsets[bone][axis]) - int(b.offsets[bone][axis])) > kBodyPoseMoved)
                        return true;
        return false;
    }

    // On the host: when a body's pose is worth sending. The rest pose goes out reliably so a visitor ends up matching,
    // then repeats now and then for anyone who joined or arrived later.
    class BodyPoseSharer
    {
    public:
        enum class Send { None, Moving, Rest };

        Send Look(uint32_t now, const net::BodyPose& pose)
        {
            if (!m_have || BodyPoseDiffers(pose, m_sent))
            {
                m_have = true;
                m_sent = pose;
                m_movedAt = now;
                m_atRest = false;
                return Send::Moving;
            }
            if (!m_atRest)
            {
                if (now - m_movedAt < kBodyPoseStillMs)
                    return Send::None;
                m_atRest = true;
                m_sent = pose;
                m_nextRepeat = now + kBodyPoseRepeatMs;
                return Send::Rest;
            }
            if (!IsDue(now, m_nextRepeat))
                return Send::None;
            m_nextRepeat = now + kBodyPoseRepeatMs;
            return Send::Moving;
        }

    private:
        net::BodyPose m_sent;
        bool m_have = false, m_atRest = false;
        uint32_t m_movedAt = 0, m_nextRepeat = 0;
    };

    // On a visitor: which pose to steer to and for how long. A repeat of the same pose doesn't start steering again, so
    // a limb that can't reach its spot (a wall in the way) is left alone instead of being pulled every few seconds.
    class BodyPoseFollower
    {
    public:
        bool Heard(uint32_t now, const net::BodyPose& pose)
        {
            if (m_have && !BodyPoseDiffers(pose, m_target))
                return false;
            m_have = true;
            m_target = pose;
            m_steering = true;
            m_until = now + kBodyPoseSteerMs;
            return true;
        }

        bool Steering(uint32_t now)
        {
            if (m_steering && IsDue(now, m_until))
                m_steering = false;
            return m_steering;
        }

        void Finish() { m_steering = false; }
        const net::BodyPose& Target() const { return m_target; }

    private:
        net::BodyPose m_target;
        bool m_have = false, m_steering = false;
        uint32_t m_until = 0;
    };
}
