// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"
#include "net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace coop
{
    constexpr uint32_t kAskAfterHeldMs = 1000;
    constexpr uint32_t kAskAgainMs = 5000;

    // The host's answer about an NPC of its own a visitor holds, from what its game has.
    inline net::NpcAbsence WhyNotShared(bool exists, bool dead, bool enabled, bool samePlace, float dx, float dy,
        float reach)
    {
        if (!exists)
            return net::NpcAbsence::Missing;
        if (dead)
            return net::NpcAbsence::Dead;
        if (!enabled)
            return net::NpcAbsence::Off;
        if (!samePlace || dx * dx + dy * dy > reach * reach)
            return net::NpcAbsence::Elsewhere;
        return net::NpcAbsence::Running;
    }

    enum class AbsentAction : uint8_t
    {
        Keep, // the host runs it: keep holding it until it's shared
        Kill, // dead in the host's game
        Hide, // switched off, somewhere else or not there at all in the host's game
    };

    inline AbsentAction ActOn(net::NpcAbsence why)
    {
        switch (why)
        {
        case net::NpcAbsence::Running:
            return AbsentAction::Keep;
        case net::NpcAbsence::Dead:
            return AbsentAction::Kill;
        default:
            return AbsentAction::Hide;
        }
    }

    // Which held NPCs to ask the host about: held a moment (most are shared within a second or two of arriving),
    // then at most once every few seconds each, a message's worth at a time.
    class NpcAsks
    {
    public:
        std::vector<uint32_t> Due(const std::vector<uint32_t>& held, uint32_t now)
        {
            std::set<uint32_t> still(held.begin(), held.end());
            for (auto it = m_heldSince.begin(); it != m_heldSince.end();)
                it = still.count(it->first) ? std::next(it) : m_heldSince.erase(it);
            for (auto it = m_nextAsk.begin(); it != m_nextAsk.end();)
                it = still.count(it->first) ? std::next(it) : m_nextAsk.erase(it);
            std::vector<uint32_t> ask;
            for (uint32_t ref : held)
            {
                if (net::IsMadeWhilePlaying(ref))
                    continue;
                uint32_t since = m_heldSince.emplace(ref, now).first->second;
                if (now - since < kAskAfterHeldMs)
                    continue;
                auto next = m_nextAsk.find(ref);
                if (next != m_nextAsk.end() && !IsDue(now, next->second))
                    continue;
                if (ask.size() >= net::kMaxNpcAsks)
                    break;
                m_nextAsk[ref] = now + kAskAgainMs;
                ask.push_back(ref);
            }
            return ask;
        }
        void Clear()
        {
            m_heldSince.clear();
            m_nextAsk.clear();
        }

    private:
        std::map<uint32_t, uint32_t> m_heldSince;
        std::map<uint32_t, uint32_t> m_nextAsk;
    };
}
