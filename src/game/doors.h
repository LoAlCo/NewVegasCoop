// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <vector>

namespace game
{
    class Doors
    {
    public:
        void Update(coop::Coop& coop, uint32_t now);
        void Forget();

    private:
        std::vector<uint32_t> m_refs;
        const void* m_cell = nullptr;
        uint32_t m_farSeen = 0;
        uint32_t m_nextList = 0, m_nextLook = 0;
        double m_lookMs = 0, m_listMs = 0;
        uint32_t m_looks = 0, m_lists = 0, m_applied = 0, m_nextLog = 0;
    };
}
