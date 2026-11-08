// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    enum PartyPerk : uint8_t
    {
        kScrounger = 1,
        kFortuneFinder = 2,
        kThemsGoodEatin = 4,
        kCertifiedTech = 8,
    };
    constexpr uint8_t kPartyPerksAll = kScrounger | kFortuneFinder | kThemsGoodEatin | kCertifiedTech;

    struct PartyPerkInfo
    {
        uint8_t bit;
        const char* file;
        uint32_t localId;
        const char* name;
    };
    constexpr PartyPerkInfo kPartyPerks[] = {
        {kScrounger, "FalloutNV.esm", 0x031DAA, "Scrounger"},
        {kFortuneFinder, "FalloutNV.esm", 0x031DE3, "Fortune Finder"},
        {kThemsGoodEatin, "OldWorldBlues.esm", 0x0098C9, "Them's Good Eatin'"},
        {kCertifiedTech, "LonesomeRoad.esm", 0x00A6DE, "Certified Tech"},
    };

    inline uint8_t KillLoot(uint8_t party, uint8_t hostOwn, bool hostCredited, bool robot)
    {
        uint8_t lists = robot ? (party & kCertifiedTech) : (party & kThemsGoodEatin);
        if (hostCredited)
            lists &= static_cast<uint8_t>(~hostOwn);
        return lists;
    }

    inline float OddsTarget(bool ownHas, bool partyHas, float current, float perkValue, float fileValue)
    {
        if (ownHas)
            return -1;
        if (partyHas)
            return current == perkValue ? -1 : perkValue;
        return current == perkValue ? fileValue : -1;
    }

    class KillCredits
    {
    public:
        static constexpr uint32_t kCreditMs = 5000;
        static constexpr size_t kMax = 128;

        void Hit(uint32_t ref, uint8_t player, uint32_t now)
        {
            if (!ref)
                return;
            if (!m_hits.count(ref) && m_hits.size() >= kMax)
            {
                auto oldest = m_hits.begin();
                for (auto it = m_hits.begin(); it != m_hits.end(); ++it)
                    if (static_cast<int32_t>(it->second.at - oldest->second.at) < 0)
                        oldest = it;
                m_hits.erase(oldest);
            }
            m_hits[ref] = {player, now};
        }

        std::vector<uint32_t> Recent(uint32_t now)
        {
            std::vector<uint32_t> out;
            for (auto it = m_hits.begin(); it != m_hits.end();)
                if (now - it->second.at > kCreditMs)
                    it = m_hits.erase(it);
                else
                    out.push_back((it++)->first);
            return out;
        }

        int Died(uint32_t ref, uint32_t now)
        {
            auto it = m_hits.find(ref);
            if (it == m_hits.end())
                return -1;
            int player = now - it->second.at <= kCreditMs ? it->second.player : -1;
            m_hits.erase(it);
            return player;
        }

        void Clear() { m_hits.clear(); }
        size_t Size() const { return m_hits.size(); }

    private:
        struct Last { uint8_t player; uint32_t at; };
        std::map<uint32_t, Last> m_hits;
    };
}
