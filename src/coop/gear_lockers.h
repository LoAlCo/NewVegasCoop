// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace coop
{
    struct GearLockerId
    {
        const char* file;
        uint32_t id;
        bool line;
        const char* where;
    };
    constexpr GearLockerId kGearLockers[] = {
        {"FalloutNV.esm", 0x127D84, true, "the Tops"},
        {"FalloutNV.esm", 0x16569C, true, "the Ultra-Luxe"},
        {"FalloutNV.esm", 0x12795C, true, "Gomorrah"},
        {"FalloutNV.esm", 0x1715B3, true, "the Silver Rush"},
        {"FalloutNV.esm", 0x172434, true, "McCarran's interrogation room"},
        {"FalloutNV.esm", 0x139B6B, true, "the Fort"},
        {"FalloutNV.esm", 0x0E3669, true, "Hidden Valley"},
        {"FalloutNV.esm", 0x136A26, false, "the Fort's arena"},
        {"DeadMoney.esm", 0x00EFCC, true, "the Sierra Madre"},
    };
    constexpr uint32_t kHoldoutWeapons = 0x000F0F05, kImprovedHoldoutWeapons = 0x00129876;
    constexpr float kImprovedHoldoutSneak = 50;

    inline uint32_t HoldoutListFor(uint32_t hostList, float ourSneak)
    {
        if (hostList != kHoldoutWeapons && hostList != kImprovedHoldoutWeapons)
            return hostList;
        return ourSneak >= kImprovedHoldoutSneak ? kImprovedHoldoutWeapons : kHoldoutWeapons;
    }

    struct RewardItem
    {
        uint32_t id;
        float health;
    };
    struct RewardBox
    {
        const char* file;
        uint32_t box, quest;
        uint8_t stage;
        const char* flag;
        RewardItem items[9];
    };
    constexpr RewardBox kHonestHeartsReward = {"HonestHearts.esm", 0x011602, 0x010C25, 10, "_NVCoopHHReward",
        {{0x00A38D, 0.85f},
         {0x00B435, 0.75f},
         {0x009DBE, 0.85f},
         {0x00A7EC, 0.75f},
         {0x00A3DF, 0.75f},
         {0x00A3E0, 0.75f},
         {0x009FF1, 0.75f},
         {0x00D103, 0.75f},
         {0x010729, 0.85f}}};

    template <class HasFn>
    std::vector<RewardItem> RewardToAdd(const std::vector<RewardItem>& items, HasFn has)
    {
        std::vector<RewardItem> out;
        for (const RewardItem& i : items)
            if (has(i.id) <= 0)
                out.push_back(i);
        return out;
    }

    class GearLockers
    {
    public:
        static constexpr size_t kMaxLockers = 64, kMaxTakesPerLocker = 4;

        void Clear() { *this = GearLockers(); }
        void Add(uint32_t ref, bool line)
        {
            if (!ref || m_rewards.count(ref))
                return;
            auto it = m_lockers.find(ref);
            if (it != m_lockers.end())
                it->second |= line;
            else if (m_lockers.size() < kMaxLockers)
                m_lockers[ref] = line;
        }
        void AddReward(uint32_t ref)
        {
            if (ref && m_rewards.size() < kMaxLockers && !m_lockers.count(ref))
                m_rewards.insert(ref);
        }
        bool IsReward(uint32_t ref) const { return m_rewards.count(ref) != 0; }
        bool IsPrivate(uint32_t ref) const { return Is(ref) || IsReward(ref); }
        bool Is(uint32_t ref) const { return ref && m_lockers.count(ref) != 0; }
        bool TakenInLine(uint32_t ref) const
        {
            auto it = m_lockers.find(ref);
            return it != m_lockers.end() && it->second;
        }
        std::vector<uint32_t> All() const
        {
            std::vector<uint32_t> out;
            for (const auto& [ref, _] : m_lockers)
                out.push_back(ref);
            return out;
        }

        void Taken(const net::PlayerCommand& c)
        {
            if (!net::IsTakeKind(c.kind) || !Is(c.ref))
                return;
            std::vector<net::PlayerCommand>& takes = m_active[c.ref];
            for (const net::PlayerCommand& t : takes)
                if (Same(t, c))
                    return;
            if (takes.size() < kMaxTakesPerLocker)
                takes.push_back(c);
        }
        void GivenBack(uint32_t ref) { m_active.erase(ref); }
        bool Active(uint32_t ref) const { return m_active.count(ref) != 0; }

        enum class InReplay
        {
            NotDone,
            OursAfter,
        };
        InReplay ForReplayedUse(const net::PlayerCommand& c) const
        {
            return net::IsWholeInventoryKind(c.kind) && !net::IsTakeKind(c.kind) && Is(c.ref) ? InReplay::OursAfter
                                                                                              : InReplay::NotDone;
        }

        template <class HoldsFn>
        std::vector<net::PlayerCommand> ForJoiner(HoldsFn holds) const
        {
            std::vector<net::PlayerCommand> out;
            for (const auto& [ref, _] : m_lockers)
            {
                bool full = holds(ref);
                auto it = m_active.find(ref);
                if (it != m_active.end() && full)
                    for (net::PlayerCommand t : it->second)
                    {
                        t.flags |= net::kCommandOnlyIfEmpty;
                        out.push_back(t);
                    }
                else if (!full)
                {
                    net::PlayerCommand give;
                    give.kind = net::PlayerCommandKind::GiveAllItems;
                    give.ref = ref;
                    give.flags = 1 << net::kCommandArgsShift;
                    out.push_back(give);
                }
            }
            return out;
        }

    private:
        static bool Same(const net::PlayerCommand& a, const net::PlayerCommand& b)
        {
            return a.kind == b.kind && a.ref == b.ref && a.form == b.form && a.flags == b.flags &&
                std::memcmp(a.n, b.n, sizeof(a.n)) == 0;
        }
        std::map<uint32_t, bool> m_lockers;
        std::set<uint32_t> m_rewards;
        std::map<uint32_t, std::vector<net::PlayerCommand>> m_active;
    };
}
