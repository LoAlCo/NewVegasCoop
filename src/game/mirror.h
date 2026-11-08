// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"
#include "game/puppet.h"

#include <cstdint>

class TESObjectREFR;

namespace game
{
    class Mirror
    {
    public:
        void Start(TESObjectREFR* actor);
        void Stop();
        uint32_t Copier() const { return m_refId; }
        void Update(uint32_t now);
        void Attack(uint8_t shots, bool release = false, bool reload = false);

    private:
        uint32_t m_refId = 0;
        coop::Smoother m_smoother;
        Puppet m_puppet;
        uint32_t m_nextFeed = 0, m_nextGear = 0;
        bool m_madeCompanion = false;
    };
}
