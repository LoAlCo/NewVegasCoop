// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <map>
#include <set>

class TESForm;

namespace game
{
    class StandIns;

    class Companions
    {
    public:
        void Update(coop::Coop& coop, const StandIns& standIns, uint32_t now);
        void FriendLineRan(uint8_t playerId, uint32_t now);
        void ReleaseAll();
        // Our follow packages are temporary copies, and a load can delete those.
        void ForgetPackages() { m_packages.clear(); }

    private:
        void Follow(uint32_t ref, uint8_t playerId, coop::Coop& coop, const StandIns& standIns);
        void Release(uint32_t ref, const char* why);
        std::map<uint32_t, uint8_t> m_assigned;
        struct Package { TESForm* form = nullptr; uint32_t target = 0; };
        std::map<uint8_t, Package> m_packages;
        std::set<uint32_t> m_known;
        uint8_t m_lineFrom = 0;
        uint32_t m_lineAt = 0, m_nextLook = 0;
    };
}
