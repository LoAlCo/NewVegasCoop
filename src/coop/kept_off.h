// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace coop
{
    constexpr uint32_t kKeepOffRetryMs = 1000;
    constexpr uint32_t kKeepOffFreezeMs = 3000;

    enum class KeepOff : uint8_t
    {
        Nothing,    // it's off, or we just asked
        SwitchOff,  // it's back on: ask again
        Freeze,     // it won't stay off: freeze it in place (combat off, can't be hit) instead
    };

    // On putting an NPC back: only switch it on if its enable parent (when it has one) is on too.
    inline bool RestoreOn(bool hasParent, bool parentOn) { return !hasParent || parentOn; }

    // Our own originals of NPCs that rolled differently here than in the host's game: we switch them off and show a
    // copy of the host's instead. Something (an enable parent, a script) can switch one back on, and then it fights
    // as a phantom the host can't see. This decides when to switch it off again and when to give up and freeze it.
    class KeptOff
    {
    public:
        KeepOff Look(uint32_t ref, uint32_t now, bool enabled)
        {
            State& s = m_states[ref];
            if (!enabled)
            {
                s.onSince = 0;
                return KeepOff::Nothing;
            }
            if (s.frozen)
                return KeepOff::Nothing;
            if (!s.onSince)
            {
                s.onSince = now ? now : 1;
                s.nextTry = now + kKeepOffRetryMs;
                return KeepOff::Nothing;
            }
            if (now - s.onSince >= kKeepOffFreezeMs)
            {
                s.frozen = true;
                return KeepOff::Freeze;
            }
            if (!IsDue(now, s.nextTry))
                return KeepOff::Nothing;
            s.nextTry = now + kKeepOffRetryMs;
            return KeepOff::SwitchOff;
        }

        void NoteGhost(uint32_t ref, bool wasGhost) { m_states[ref].wasGhost = wasGhost; }

        struct Gone
        {
            bool frozen = false;
            bool wasGhost = false;
        };
        Gone Forget(uint32_t ref)
        {
            Gone g;
            auto it = m_states.find(ref);
            if (it == m_states.end())
                return g;
            g.frozen = it->second.frozen;
            g.wasGhost = it->second.wasGhost;
            m_states.erase(it);
            return g;
        }

        bool IsFrozen(uint32_t ref) const
        {
            auto it = m_states.find(ref);
            return it != m_states.end() && it->second.frozen;
        }
        size_t FrozenCount() const
        {
            size_t n = 0;
            for (const auto& [ref, s] : m_states)
                n += s.frozen ? 1 : 0;
            return n;
        }
        void Clear() { m_states.clear(); }

    private:
        struct State
        {
            uint32_t onSince = 0;
            uint32_t nextTry = 0;
            bool frozen = false;
            bool wasGhost = false;
        };
        std::map<uint32_t, State> m_states;
    };
}
