// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"
#include "coop/weather.h"

#include <cstdint>
#include <string>

namespace game
{
    // Everyone outdoors in the same place sees the host's weather. A friend's copy puts it in the game's own override
    // slot, and takes it back out while a save is written and when co-op ends, so a friend's save keeps its own weather.
    class Weather
    {
    public:
        void Install(coop::Coop& coop);
        void Update(uint32_t now, bool settingOn, bool worldReady);
        void Forget();
        std::string Report() const;

    private:
        void Host(const coop::WeatherSky& sky);
        void Friend(uint32_t now, bool syncOn, const coop::WeatherSky& sky);
        coop::Coop* m_coop = nullptr;
        coop::WeatherChanged m_changed;
        const char* m_why = "not started";
    };
}
