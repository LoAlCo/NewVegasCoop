// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"
#include "game/game.h"
#include "game/npcs.h"

#include <cstdint>
#include <set>
#include <vector>

namespace game
{
    class StandIns;

    // On a visitor: tells the host about the companions of our save. On the host: shows copies of visitors' ones.
    class VisitorCompanions
    {
    public:
        void Update(coop::Coop& coop, const StandIns& standIns, Npcs& npcs, const coop::Pose& me, uint32_t now,
            bool bothKeep);
        // True when the hit was about a companion and is fully dealt with here.
        bool TookHit(const Hit& hit, coop::Coop& coop, const StandIns& standIns, Npcs& npcs);
        std::vector<uint32_t> MadeRefIds() const { return m_copies.MadeRefIds(); }
        void Forget();

    private:
        void UpdateVisitor(coop::Coop& coop, const StandIns& standIns, Npcs& npcs, const coop::Pose& me, uint32_t now);
        void UpdateHost(coop::Coop& coop, const StandIns& standIns, const coop::Pose& me, uint32_t now);
        void Stop(Npcs& npcs);

        Npcs m_mine;
        Npcs m_copies;
        std::set<uint32_t> m_own;
        std::vector<uint32_t> m_watching;
        uint32_t m_nextLook = 0;
        bool m_running = false;
    };
}
