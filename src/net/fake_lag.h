// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <map>
#include <random>

namespace net
{
    struct FakeLag
    {
        uint32_t pingMs = 0;
        uint32_t jitterMs = 0;
        uint32_t lossPercent = 0;
        bool On() const { return pingMs || jitterMs || lossPercent; }
    };

    inline bool ParseFakeLag(const char* text, FakeLag& out)
    {
        out = FakeLag{};
        uint32_t values[3] = {};
        int count = 0;
        const char* p = text;
        while (p && *p && count < 3)
        {
            if (*p < '0' || *p > '9')
                return false;
            uint64_t v = 0;
            while (*p >= '0' && *p <= '9')
                if ((v = v * 10 + static_cast<uint64_t>(*p++ - '0')) > 10000)
                    return false;
            values[count++] = static_cast<uint32_t>(v);
            if (*p == ',' && p[1])
                ++p;
            else if (*p)
                return false;
        }
        if (!count || (p && *p) || values[2] > 100)
            return false;
        out = {values[0], values[1], values[2]};
        return true;
    }

    template <class T>
    class LagQueue
    {
    public:
        enum class Kind { Reliable, Unreliable, Barrier };

        explicit LagQueue(FakeLag lag, uint32_t seed = 1) : m_lag(lag), m_rng(seed)
        {
            if (m_lag.lossPercent > 100)
                m_lag.lossPercent = 100;
        }

        void Push(uint64_t now, Kind kind, T item)
        {
            uint64_t due = now + m_lag.pingMs / 2 + Random(m_lag.jitterMs);
            if (kind != Kind::Barrier && Random(99) < m_lag.lossPercent)
            {
                if (kind == Kind::Unreliable)
                    return;
                due += m_lag.pingMs + Random(m_lag.jitterMs);
            }
            if (kind != Kind::Unreliable && due < m_lastReliable)
                due = m_lastReliable;
            if (kind != Kind::Reliable && due < m_lastUnreliable)
                due = m_lastUnreliable;
            if (kind != Kind::Unreliable)
                m_lastReliable = due;
            if (kind != Kind::Reliable)
                m_lastUnreliable = due;
            m_items.emplace(due, std::move(item));
        }

        bool Pop(uint64_t now, T& out)
        {
            if (m_items.empty() || m_items.begin()->first > now)
                return false;
            out = std::move(m_items.begin()->second);
            m_items.erase(m_items.begin());
            return true;
        }

        size_t Size() const { return m_items.size(); }

    private:
        uint32_t Random(uint32_t max)
        {
            return max ? std::uniform_int_distribution<uint32_t>(0, max)(m_rng) : 0;
        }

        FakeLag m_lag;
        std::mt19937 m_rng;
        std::multimap<uint64_t, T> m_items;
        uint64_t m_lastReliable = 0, m_lastUnreliable = 0;
    };
}
