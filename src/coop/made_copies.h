// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coop
{
    // A load doesn't free temporary copies of forms (harmless weapons, the teleporter...): the game gives a new ID to any
    // with changes on record that the loaded save doesn't have, and leaves the rest alone. A copy can then share its ID
    // with what the save brings back, so we make new ones after every load instead of trusting the old. This remembers
    // what we made, so the log can tell how many moved. stillAlive is the only thing that reads through the old pointers.
    class MadeCopies
    {
    public:
        struct Moved
        {
            size_t moved = 0;
            size_t alive = 0;
            size_t forgotten = 0;
        };

        void Note(uint32_t id, const void* copy) { m_live.push_back({id, copy}); }

        void Forget()
        {
            m_forgotten.insert(m_forgotten.end(), m_live.begin(), m_live.end());
            m_live.clear();
        }

        template <typename Lookup, typename StillAlive>
        Moved TakeMoved(Lookup&& lookup, StillAlive&& stillAlive)
        {
            Moved out;
            out.forgotten = m_forgotten.size();
            for (const Copy& copy : m_forgotten)
                if (static_cast<const void*>(lookup(copy.id)) != copy.at)
                {
                    ++out.moved;
                    out.alive += stillAlive(copy.at) ? 1 : 0;
                }
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
