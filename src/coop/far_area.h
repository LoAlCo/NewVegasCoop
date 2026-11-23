// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace coop
{
    constexpr int kNoGrid = -2;
    inline bool NearGrid(int x, int y, int cx, int cy, int half)
    {
        if (half < 0)
            return false;
        return std::abs(x - cx) <= half + 1 && std::abs(y - cy) <= half + 1;
    }

    using Cell = std::pair<int, int>;

    struct FriendCell
    {
        uint8_t id = 0;
        int x = 0, y = 0;
    };

    struct FarPlan
    {
        std::vector<Cell> attach;
        std::vector<Cell> detach;
        bool memoryFull = false;
    };

    constexpr int kFarAreaRadius = 1;
    constexpr int kFarKeepRadius = 2;
    constexpr int kFarAreaRadiusIndoors = 2;
    constexpr size_t kMaxFarFriends = 3;
    constexpr uint32_t kFarMemoryLimitMb = 3300;

    inline FarPlan PlanFarCells(const std::set<Cell>& ours, std::vector<FriendCell> friends, int cx, int cy, int half,
        uint32_t usedMb)
    {
        std::sort(friends.begin(), friends.end(), [](const FriendCell& a, const FriendCell& b) { return a.id < b.id; });
        if (friends.size() > kMaxFarFriends)
            friends.resize(kMaxFarFriends);
        auto within = [&](const Cell& c, int r) {
            for (const FriendCell& f : friends)
                if (std::abs(c.first - f.x) <= r && std::abs(c.second - f.y) <= r)
                    return true;
            return false;
        };
        const int area = half == kNoGrid ? kFarAreaRadiusIndoors : kFarAreaRadius;
        const int keep = half == kNoGrid ? kFarAreaRadiusIndoors + 1 : kFarKeepRadius;
        FarPlan plan;
        for (const Cell& c : ours)
            if (NearGrid(c.first, c.second, cx, cy, half) || !within(c, keep))
                plan.detach.push_back(c);
        for (int ring = 0; ring <= area && plan.attach.empty(); ++ring)
            for (const FriendCell& f : friends)
                for (int dx = -ring; dx <= ring && plan.attach.empty(); ++dx)
                    for (int dy = -ring; dy <= ring && plan.attach.empty(); ++dy)
                    {
                        if (std::abs(dx) != ring && std::abs(dy) != ring)
                            continue;
                        Cell c{f.x + dx, f.y + dy};
                        if (ours.count(c) || NearGrid(c.first, c.second, cx, cy, half))
                            continue;
                        if (usedMb > kFarMemoryLimitMb)
                            plan.memoryFull = true;
                        else
                            plan.attach.push_back(c);
                    }
        return plan;
    }

    constexpr size_t kMaxFarInteriors = 3;
    constexpr uint32_t kFarInteriorKeepMs = 10000;

    struct FriendPlace
    {
        uint8_t id = 0;
        uint32_t cell = 0;
    };

    struct InteriorPlan
    {
        std::vector<uint32_t> attach;
        std::vector<uint32_t> detach;
        bool memoryFull = false;
    };

    inline InteriorPlan PlanFarInteriors(std::map<uint32_t, uint32_t>& ours, std::vector<FriendPlace> friends,
        uint32_t hostCell, uint32_t usedMb, uint32_t now)
    {
        std::sort(friends.begin(), friends.end(), [](const FriendPlace& a, const FriendPlace& b) { return a.id < b.id; });
        InteriorPlan plan;
        std::vector<uint32_t> wanted;
        for (const FriendPlace& f : friends)
            if (f.cell && f.cell != hostCell && std::find(wanted.begin(), wanted.end(), f.cell) == wanted.end())
                wanted.push_back(f.cell);
        for (uint32_t c : wanted)
            if (auto it = ours.find(c); it != ours.end())
                it->second = now;
        size_t kept = 0;
        for (const auto& [cell, last] : ours)
            if (cell == hostCell || static_cast<int32_t>(now - last) >= static_cast<int32_t>(kFarInteriorKeepMs))
                plan.detach.push_back(cell);
            else
                ++kept;
        for (uint32_t c : wanted)
        {
            if (ours.count(c) || kept >= kMaxFarInteriors)
                continue;
            if (usedMb > kFarMemoryLimitMb)
                plan.memoryFull = true;
            else
                plan.attach.push_back(c);
            break;
        }
        return plan;
    }

    template <typename T>
    bool TakeOutOfList(T* list, unsigned size, T item)
    {
        if (!list || !item)
            return false;
        for (unsigned i = 0; i < size; ++i)
            if (list[i] == item)
            {
                for (unsigned j = i; j + 1 < size; ++j)
                    list[j] = list[j + 1];
                list[size - 1] = T{};
                return true;
            }
        return false;
    }
}
