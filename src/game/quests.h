// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace game
{
    class Quests
    {
    public:
        void Update(coop::Coop& coop, uint32_t now);
        void Stop();
        void Forget() { m_switched.clear(); }

    private:
        void Host(coop::Coop& coop, uint32_t now);
        void Friend(coop::Coop& coop, uint32_t now);
        std::vector<std::string> m_loadOrder;
        const std::vector<std::string>* m_questMods = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> m_prints;
        uint32_t m_joins = 0;
        size_t m_resendAt = 0;
        uint32_t m_nextLook = 0;
        void Enables(coop::Coop& coop, uint32_t now, bool host);
        std::map<uint32_t, bool> m_switched;
        uint32_t m_nextSwitchedResend = 0;
        uint32_t m_enablesSent = 0, m_enablesSet = 0;
        std::vector<std::pair<uint32_t, void*>> m_detached;
        bool m_detachedAll = false;
        uint32_t m_quietUntil = 0;
        double m_lookMs = 0;
        uint32_t m_looks = 0, m_sent = 0, m_set = 0, m_nextLog = 0;
    };
}
