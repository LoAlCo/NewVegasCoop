// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <map>
#include <set>

namespace game
{
    class WorldItems
    {
    public:
        void Update(coop::Coop& coop, uint32_t now);
        void Forget();

    private:
        void HostApply(coop::Coop& coop, uint32_t now);
        struct Glide
        {
            coop::ItemPose from, to;
            uint32_t startedAt = 0;
            bool held = false;
        };
        void GlideTo(uint32_t ref, const coop::ItemPose& to, bool held, uint32_t now, const char* whose);
        void Glides(uint32_t now);
        bool Following(uint32_t ref, uint32_t now) const;
        std::map<uint32_t, Glide> m_glides;
        std::map<uint32_t, uint32_t> m_settling;
        std::set<uint32_t> m_goneNoted;
        uint32_t m_grabbed = 0;

        uint32_t m_nextLook = 0;
        const void* m_area = nullptr;
        double m_lookMs = 0;
        uint32_t m_looks = 0, m_near = 0, m_takes = 0, m_drops = 0, m_refunds = 0, m_made = 0, m_removed = 0,
            m_moved = 0, m_sent = 0, m_nextLog = 0;
    };
}
