// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    struct NpcCandidate
    {
        uint32_t place = 0;
        bool dead = false;
        float distance = 0;
    };

    inline std::vector<size_t> PickNpcs(const std::vector<NpcCandidate>& all, size_t cap)
    {
        std::vector<size_t> order(all.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (all[a].place != all[b].place)
                return all[a].place < all[b].place;
            if (all[a].dead != all[b].dead)
                return !all[a].dead;
            return all[a].distance != all[b].distance ? all[a].distance < all[b].distance : a < b;
        });
        std::vector<size_t> rank(all.size());
        std::map<std::pair<uint32_t, bool>, size_t> next;
        for (size_t i : order)
            rank[i] = next[{all[i].place, all[i].dead}]++;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (all[a].dead != all[b].dead)
                return !all[a].dead;
            if (rank[a] != rank[b])
                return rank[a] < rank[b];
            return all[a].distance != all[b].distance ? all[a].distance < all[b].distance : a < b;
        });
        if (order.size() > cap)
            order.resize(cap);
        return order;
    }
}
