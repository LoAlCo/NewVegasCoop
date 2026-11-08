// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace coop
{
    class GivenRounds
    {
    public:
        static constexpr size_t kMaxKinds = 16;

        void Add(uint32_t ammo, int32_t count)
        {
            if (!ammo || count <= 0)
                return;
            for (auto& [kind, n] : m_given)
                if (kind == ammo)
                {
                    n = n > INT32_MAX - count ? INT32_MAX : n + count;
                    return;
                }
            if (m_given.size() < kMaxKinds)
                m_given.push_back({ammo, count});
        }

        template <class HasFn>
        std::vector<std::pair<uint32_t, int32_t>> TakeBack(HasFn has)
        {
            std::vector<std::pair<uint32_t, int32_t>> out;
            for (const auto& [ammo, given] : m_given)
            {
                int32_t now = has(ammo);
                int32_t take = now < given ? now : given;
                if (take > 0)
                    out.push_back({ammo, take});
            }
            m_given.clear();
            return out;
        }

        bool Empty() const { return m_given.empty(); }

    private:
        std::vector<std::pair<uint32_t, int32_t>> m_given;
    };
}
