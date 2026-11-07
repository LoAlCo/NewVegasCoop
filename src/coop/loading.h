// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <vector>

namespace coop
{
    class LoadGate
    {
    public:
        static constexpr uint32_t kMaxWaitMs = 60000;

        void Expect(const std::vector<uint8_t>& players, uint32_t now)
        {
            m_waiting = players;
            m_since = now;
        }
        void Arrived(uint8_t player) { Forget(player); }
        void Left(uint8_t player) { Forget(player); }

        bool Waiting(uint32_t now, std::vector<uint8_t>* timedOut = nullptr)
        {
            if (!m_waiting.empty() && now - m_since >= kMaxWaitMs)
            {
                if (timedOut)
                    *timedOut = m_waiting;
                m_waiting.clear();
            }
            return !m_waiting.empty();
        }
        bool Busy() const { return !m_waiting.empty(); }
        uint8_t WaitingFor() const { return m_waiting.empty() ? 0 : m_waiting.front(); }

    private:
        void Forget(uint8_t player)
        {
            for (size_t i = 0; i < m_waiting.size(); ++i)
                if (m_waiting[i] == player)
                {
                    m_waiting.erase(m_waiting.begin() + i);
                    return;
                }
        }

        std::vector<uint8_t> m_waiting;
        uint32_t m_since = 0;
    };
}
