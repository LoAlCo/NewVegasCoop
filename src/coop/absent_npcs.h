// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr uint32_t kAbsentMs = 6000;
    constexpr uint32_t kMaxHides = 3;
    constexpr uint32_t kNpcListFreshMs = 2500;

    // The host's NPC list says who it lacks only if it's recent, about the place we're both in, and not cut short.
    inline bool CanTellWhoHostLacks(uint32_t now, uint32_t listAt, uint32_t listSpace, uint32_t hostSpace,
        uint32_t mySpace, size_t sharedCount, size_t maxShared)
    {
        return listAt != 0 && now - listAt <= kNpcListFreshMs && mySpace != 0 && listSpace == mySpace &&
            hostSpace == mySpace && sharedCount < maxShared;
    }

    class AbsentNpcs
    {
    public:
        struct Result
        {
            std::vector<uint32_t> hide;
            std::vector<uint32_t> gaveUp;
        };

        // missing: alive, switched-on actors of ours near the host that the host doesn't list right now.
        Result Look(uint32_t now, bool canTell, uint32_t space, const std::vector<uint32_t>& missing)
        {
            Result out;
            if (!canTell || space != m_space)
                m_since.clear();
            m_space = space;
            if (!canTell)
                return out;
            for (auto it = m_since.begin(); it != m_since.end();)
                it = std::find(missing.begin(), missing.end(), it->first) == missing.end() ? m_since.erase(it)
                                                                                            : std::next(it);
            for (uint32_t ref : missing)
            {
                uint32_t since = m_since.emplace(ref, now).first->second;
                if (now - since < kAbsentMs)
                    continue;
                m_since.erase(ref);
                Hidden& h = m_hidden[ref];
                if (h.gaveUp)
                    continue;
                if (h.times < kMaxHides)
                {
                    ++h.times;
                    out.hide.push_back(ref);
                }
                else
                {
                    h.gaveUp = true;
                    out.gaveUp.push_back(ref);
                }
            }
            return out;
        }

        // The host lists this one again: returns whether we had it hidden.
        bool Reshown(uint32_t ref) { return m_hidden.erase(ref) != 0; }

        // The host's "it's switched on" says nothing about it being near the host, so it doesn't undo a hide.
        bool KeepsOff(uint32_t ref) const
        {
            auto it = m_hidden.find(ref);
            return it != m_hidden.end() && !it->second.gaveUp;
        }

        // We can't hide this one: stop trying until the host lists it again.
        void GiveUp(uint32_t ref) { m_hidden[ref].gaveUp = true; }

        std::vector<uint32_t> TakeHidden()
        {
            std::vector<uint32_t> refs;
            for (const auto& [ref, h] : m_hidden)
                if (!h.gaveUp)
                    refs.push_back(ref);
            Clear();
            return refs;
        }

        size_t HiddenCount() const
        {
            size_t count = 0;
            for (const auto& [ref, h] : m_hidden)
                count += h.gaveUp ? 0 : 1;
            return count;
        }

        void Clear()
        {
            m_since.clear();
            m_hidden.clear();
            m_space = 0;
        }

    private:
        struct Hidden
        {
            uint32_t times = 0;
            bool gaveUp = false;
        };
        std::map<uint32_t, uint32_t> m_since;
        std::map<uint32_t, Hidden> m_hidden;
        uint32_t m_space = 0;
    };
}
