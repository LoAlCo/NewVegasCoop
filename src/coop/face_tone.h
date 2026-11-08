// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"

#include <cstdint>

namespace coop
{
    class FaceTone
    {
    public:
        static constexpr uint32_t kLookMs = 250;
        static constexpr uint32_t kEveryMs = 2000;
        static constexpr uint32_t kSettleMs = 1000;

        bool TimeToLook(uint32_t now)
        {
            if (!IsDue(now, m_nextLook))
                return false;
            m_nextLook = Set(now + kLookMs);
            return true;
        }
        bool Check(uintptr_t head, bool looksKnown, uint32_t now)
        {
            if (!head || !looksKnown)
                return false;
            if (m_settleUntil && !IsDue(now, m_settleUntil))
            {
                m_head = head;
                return false;
            }
            m_settleUntil = 0;
            return head != m_head && IsDue(now, m_next);
        }
        void Made(uintptr_t head, uint32_t now)
        {
            m_head = head;
            m_next = Set(now + kEveryMs);
            m_settleUntil = Set(now + kSettleMs);
        }

    private:
        static uint32_t Set(uint32_t at) { return at ? at : 1; }
        uintptr_t m_head = 0;
        uint32_t m_next = 0;
        uint32_t m_nextLook = 0;
        uint32_t m_settleUntil = 0;
    };
}
