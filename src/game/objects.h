// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"
#include "coop/objects.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace game
{
    class Objects
    {
    public:
        void Update(coop::Coop& coop, uint32_t now);
        void Forget();

    private:
        void Host(coop::Coop& coop, uint32_t now);
        void Friend(coop::Coop& coop, uint32_t now);
        bool Ours(coop::Coop& coop, uint32_t ref);
        void ListDestructibles(coop::Coop& coop, uint32_t now);
        std::vector<uint32_t> m_destructibles;
        const void* m_cell = nullptr;
        uint32_t m_nextList = 0, m_nextHealthLook = 0;
        coop::HostObjects m_host;
        coop::FriendObjects m_friend;
        std::vector<std::string> m_loadOrder;
        bool m_watching = false;
        uint32_t m_nextLook = 0, m_nextSend = 0, m_nextLog = 0;
        double m_lookMs = 0;
        uint32_t m_looks = 0, m_noted = 0, m_sent = 0, m_destroyedSet = 0, m_played = 0, m_full = 0, m_damaged = 0,
            m_hitsSent = 0, m_hitsApplied = 0;
    };
}
