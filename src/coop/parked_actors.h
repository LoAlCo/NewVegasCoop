// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace coop
{
    class ParkedActors
    {
    public:
        static constexpr size_t kWarnAt = 128;

        void Park(uint32_t id, uint32_t key, bool reusable)
        {
            for (Entry& e : m_entries)
                if (e.id == id)
                {
                    e = {id, key, reusable};
                    return;
                }
            m_entries.push_back({id, key, reusable});
        }
        uint32_t Take(uint32_t key, const std::function<bool(uint32_t)>& usable)
        {
            if (!key)
                return 0;
            for (size_t i = m_entries.size(); i-- > 0;)
            {
                const Entry& e = m_entries[i];
                if (e.key != key || !e.reusable || !usable(e.id))
                    continue;
                uint32_t id = e.id;
                m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
                return id;
            }
            return 0;
        }
        void Prune(const std::function<bool(uint32_t)>& exists)
        {
            for (size_t i = 0; i < m_entries.size();)
                if (exists(m_entries[i].id))
                    ++i;
                else
                    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
        }
        std::vector<uint32_t> Ids() const
        {
            std::vector<uint32_t> ids;
            ids.reserve(m_entries.size());
            for (const Entry& e : m_entries)
                ids.push_back(e.id);
            return ids;
        }
        size_t Size() const { return m_entries.size(); }
        void Clear() { m_entries.clear(); }

    private:
        struct Entry
        {
            uint32_t id;
            uint32_t key;
            bool reusable;
        };
        std::vector<Entry> m_entries;
    };
}
