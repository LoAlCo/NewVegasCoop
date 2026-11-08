// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>

namespace coop
{
    class CopyKill
    {
    public:
        static constexpr uint32_t kAgainMs = 3000;
        static constexpr int kMaxTries = 5;
        enum class Do
        {
            Nothing,
            Unprotect,
            Kill,
            GiveUp,
        };
        Do Step(uint32_t now, bool deadHere)
        {
            if (deadHere)
            {
                m_unprotected = false;
                return Do::Nothing;
            }
            if (m_unprotected)
            {
                m_unprotected = false;
                return Do::Kill;
            }
            if (m_tries && now - m_at < kAgainMs)
                return Do::Nothing;
            if (m_tries >= kMaxTries)
            {
                if (m_gaveUp)
                    return Do::Nothing;
                m_gaveUp = true;
                return Do::GiveUp;
            }
            ++m_tries;
            m_at = now;
            m_unprotected = true;
            return Do::Unprotect;
        }
        int Tries() const { return m_tries; }

    private:
        uint32_t m_at = 0;
        int m_tries = 0;
        bool m_unprotected = false;
        bool m_gaveUp = false;
    };
}
