// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coop
{
    // Temporary copies of forms (harmless weapons, the teleporter...) last only until the game loads a save or goes back
    // to the main menu, so nothing may keep pointing at them past that. This remembers what we made, without ever
    // reading through those pointers again, to tell after a load how many really went: the log's proof of it.
    class MadeCopies
    {
    public:
        struct Gone
        {
            size_t gone = 0;
            size_t forgotten = 0;
        };

        void Note(uint32_t id, const void* copy) { m_live.push_back({id, copy}); }

        void Forget()
        {
            m_forgotten.insert(m_forgotten.end(), m_live.begin(), m_live.end());
            m_live.clear();
        }

        template <typename Lookup>
        Gone TakeGone(Lookup&& lookup)
        {
            Gone out;
            out.forgotten = m_forgotten.size();
            for (const Copy& copy : m_forgotten)
                out.gone += static_cast<const void*>(lookup(copy.id)) != copy.at;
            m_forgotten.clear();
            return out;
        }

    private:
        struct Copy
        {
            uint32_t id;
            const void* at;
        };
        std::vector<Copy> m_live;
        std::vector<Copy> m_forgotten;
    };
}
