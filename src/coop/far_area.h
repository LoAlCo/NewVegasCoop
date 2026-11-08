// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

namespace coop
{
    inline bool NearGrid(int x, int y, int cx, int cy, int half)
    {
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
        FarPlan plan;
        for (const Cell& c : ours)
            if (NearGrid(c.first, c.second, cx, cy, half) || !within(c, kFarKeepRadius))
                plan.detach.push_back(c);
        for (const FriendCell& f : friends)
            for (int dx = -kFarAreaRadius; dx <= kFarAreaRadius; ++dx)
                for (int dy = -kFarAreaRadius; dy <= kFarAreaRadius; ++dy)
                {
                    Cell c{f.x + dx, f.y + dy};
                    if (ours.count(c) || NearGrid(c.first, c.second, cx, cy, half) || !plan.attach.empty())
                        continue;
                    if (usedMb > kFarMemoryLimitMb)
                        plan.memoryFull = true;
                    else
                        plan.attach.push_back(c);
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
