// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <utility>

namespace coop
{
    class InventoryGate
    {
    public:
        static constexpr size_t kMaxListed = 3;
        static constexpr size_t kTooManyKept = 64;
        static constexpr size_t kMaxKept = 1024;

        void Frame(bool screenOpen, std::initializer_list<uint32_t> listed)
        {
            m_count = 0;
            if (screenOpen)
                for (uint32_t ref : listed)
                    if (ref && m_count < kMaxListed)
                        m_listed[m_count++] = ref;
        }
        bool ScreenOpen() const { return m_count != 0; }
        bool Busy(uint32_t ref) const
        {
            for (size_t i = 0; i < m_count; ++i)
                if (m_listed[i] == ref)
                    return true;
            return false;
        }
        bool Do(uint32_t ref, std::function<void()> change)
        {
            if (!Busy(ref) && !HasKept(ref))
            {
                change();
                return true;
            }
            if (m_kept.size() >= kMaxKept)
            {
                ++m_dropped;
                return false;
            }
            m_kept.push_back({ref, std::move(change)});
            ++m_keptEver;
            return false;
        }
        size_t Flush()
        {
            size_t made = 0;
            std::deque<Kept> still;
            while (!m_kept.empty())
            {
                Kept k = std::move(m_kept.front());
                m_kept.pop_front();
                if (Busy(k.ref))
                    still.push_back(std::move(k));
                else
                {
                    k.change();
                    ++made;
                }
            }
            m_kept = std::move(still);
            return made;
        }
        size_t Waiting() const { return m_kept.size(); }
        bool TooMany() const { return m_kept.size() > kTooManyKept; }
        void Clear() { m_kept.clear(); }
        std::pair<uint32_t, uint32_t> TakeCounts()
        {
            std::pair<uint32_t, uint32_t> counts{m_keptEver, m_dropped};
            m_keptEver = m_dropped = 0;
            return counts;
        }

    private:
        struct Kept
        {
            uint32_t ref;
            std::function<void()> change;
        };
        bool HasKept(uint32_t ref) const
        {
            for (const Kept& k : m_kept)
                if (k.ref == ref)
                    return true;
            return false;
        }

        uint32_t m_listed[kMaxListed] = {};
        size_t m_count = 0;
        std::deque<Kept> m_kept;
        uint32_t m_keptEver = 0, m_dropped = 0;
    };
}
