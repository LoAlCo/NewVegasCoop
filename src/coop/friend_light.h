// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"
#include "coop/smoothing.h"

#include <cstdint>

namespace coop
{
    // Keeps a friend's Pip-Boy light on their character in step with what they send: turns it on or off when that
    // changes, and while it's on, asks now and then for a look that the game hasn't dropped it.
    class FriendLight
    {
    public:
        static constexpr uint32_t kCheckMs = 2000;
        enum class Step : uint8_t { Nothing, TurnOn, TurnOff, Check };

        static bool Wanted(uint8_t poseFlags, bool setting, bool hidden)
        {
            return setting && !hidden && (poseFlags & Pose::kLightOn) != 0;
        }

        Step Next(bool want, uint32_t now)
        {
            if (want != m_on)
            {
                m_on = want;
                m_nextCheck = Later(now);
                return want ? Step::TurnOn : Step::TurnOff;
            }
            if (m_on && IsDue(now, m_nextCheck))
            {
                m_nextCheck = Later(now);
                return Step::Check;
            }
            return Step::Nothing;
        }

        bool On() const { return m_on; }
        void Forget() { m_on = false; m_nextCheck = 0; }

    private:
        static uint32_t Later(uint32_t now)
        {
            uint32_t at = now + kCheckMs;
            return at ? at : 1;
        }

        bool m_on = false;
        uint32_t m_nextCheck = 0;
    };
}
