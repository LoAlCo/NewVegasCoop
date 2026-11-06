// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/visitor_companions.h"

#include "coop/coop.h"

#include <cmath>
#include <utility>

namespace coop
{
    bool ShareCompanionWithHost(bool bothKeep, bool inHostsParty, bool hostSharesIt)
    {
        return bothKeep || (!inHostsParty && !hostSharesIt);
    }

    bool CopyHealthDrifted(float here, float host)
    {
        return std::fabs(here - host) >= 1.0f;
    }

    CompanionHit RouteCompanionHit(const CompanionHitFacts& f)
    {
        if (f.host)
        {
            if (f.targetIsCopy)
                return f.attackerIsFriendly ? CompanionHit::Undo : CompanionHit::UndoAndTellOwner;
            return f.attackerIsCopy ? CompanionHit::Undo : CompanionHit::NotOurs;
        }
        if (f.targetIsOurCompanion)
            return f.attackerIsCopyOrStandIn ? CompanionHit::Undo : CompanionHit::Done;
        if (f.attackerIsOurCompanion)
            return f.targetIsStandIn ? CompanionHit::NotOurs : CompanionHit::Report;
        return CompanionHit::NotOurs;
    }

    std::vector<net::NpcState> NpcsForPlayer(const std::vector<net::NpcState>& npcs,
        const std::map<uint32_t, uint8_t>& copyOwners, uint8_t player)
    {
        std::vector<net::NpcState> out;
        out.reserve(npcs.size());
        for (const net::NpcState& n : npcs)
            if (auto it = copyOwners.find(n.ref); it == copyOwners.end() || it->second != player)
                out.push_back(n);
        return out;
    }

    uint32_t CompanionCopies::KeyOf(uint8_t owner, uint32_t ref) const
    {
        auto it = m_keys.find({owner, ref});
        return it == m_keys.end() ? 0 : it->second;
    }

    uint32_t CompanionCopies::RealRefOf(uint32_t key) const
    {
        for (const auto& [who, k] : m_keys)
            if (k == key)
                return who.second;
        return 0;
    }

    uint32_t CompanionCopies::MakeKey(uint8_t owner, uint32_t ref)
    {
        size_t theirs = 0;
        bool used[256] = {};
        for (const auto& [who, k] : m_keys)
            if (who.first == owner)
            {
                ++theirs;
                used[k & 0xFF] = true;
            }
        if (theirs >= kMaxCompanionCopiesPerPlayer)
            return 0;
        uint8_t slot = 0;
        while (used[slot])
            ++slot;
        uint32_t key = CompanionCopyKey(owner, slot);
        m_keys[{owner, ref}] = key;
        return key;
    }

    void CompanionCopies::Heard(uint8_t owner, const net::NpcStates& m, uint32_t now)
    {
        for (const net::NpcState& n : m.npcs)
        {
            uint32_t key = KeyOf(owner, n.ref);
            if (!key)
            {
                if (!n.base || net::IsMadeWhilePlaying(n.base) || !(key = MakeKey(owner, n.ref)))
                    continue;
            }
            HearNpc(m_npcs[key], m.timeMs, m.space, n, now);
        }
    }

    void CompanionCopies::HeardGear(uint8_t owner, net::NpcGear m)
    {
        auto it = m_npcs.find(KeyOf(owner, m.ref));
        if (it != m_npcs.end() && (it->second.gear != m.items || it->second.gearChanges == 0))
        {
            it->second.gear = std::move(m.items);
            ++it->second.gearChanges;
        }
    }

    void CompanionCopies::Forget(uint8_t owner)
    {
        for (auto it = m_keys.begin(); it != m_keys.end();)
        {
            if (it->first.first != owner)
            {
                ++it;
                continue;
            }
            m_npcs.erase(it->second);
            it = m_keys.erase(it);
        }
    }

    void CompanionCopies::Expire(uint32_t now)
    {
        for (auto it = m_keys.begin(); it != m_keys.end();)
        {
            auto npc = m_npcs.find(it->second);
            if (npc != m_npcs.end() && now - npc->second.heardAt <= kNpcTimeoutMs)
            {
                ++it;
                continue;
            }
            if (npc != m_npcs.end())
                m_npcs.erase(npc);
            it = m_keys.erase(it);
        }
    }

    void CompanionCopies::Clear()
    {
        m_keys.clear();
        m_npcs.clear();
    }
}
