// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>

namespace coop
{
    constexpr uint8_t kSkyExterior = 3;

    // What a game's sky shows this frame, as form ids.
    struct WeatherSky
    {
        uint32_t weather = 0;
        uint32_t previous = 0;
        uint32_t climate = 0;
        uint32_t rootSpace = 0;
        float percent = 1;
        uint8_t mode = 0;
    };

    // The host's side: says when its weather changed. Only a real outdoor sky counts, so a host who walks indoors
    // keeps offering the last outdoor weather and friends outside hold steady.
    class WeatherChanged
    {
    public:
        bool See(const WeatherSky& sky)
        {
            if (sky.mode != kSkyExterior || !sky.weather)
                return false;
            bool newWeather = !m_seen || sky.weather != m_state.weather;
            bool changed = newWeather || sky.climate != m_state.climate || sky.rootSpace != m_state.rootSpace;
            if (newWeather)
                m_state.flags = !m_seen || !sky.previous ? net::kWeatherPopped : 0;
            else if (changed)
                m_state.flags = 0;
            m_state.weather = sky.weather;
            m_state.climate = sky.climate;
            m_state.rootSpace = sky.rootSpace;
            m_state.percent = sky.percent < 0 ? 0 : sky.percent > 1 ? 1 : sky.percent;
            m_seen = true;
            return changed;
        }
        bool Seen() const { return m_seen; }
        const net::WeatherState& State() const { return m_state; }
        void Reset() { *this = WeatherChanged{}; }

    private:
        net::WeatherState m_state;
        bool m_seen = false;
    };

    enum class WeatherAction { None, Fade, Pop, Release };

    // A friend's side of the decision.
    struct WeatherLocal
    {
        bool allowed = false;   // setting on, in co-op, save hooks in place
        bool following = false; // the override slot holds the host's weather
        uint8_t mode = 0;
        uint32_t climate = 0;
        uint32_t rootSpace = 0;
        uint32_t slot = 0;      // what the override slot holds now
    };

    inline bool WeatherShared(const net::WeatherState& host, const WeatherLocal& me)
    {
        return me.mode == kSkyExterior && host.climate && host.climate == me.climate && host.rootSpace &&
            host.rootSpace == me.rootSpace;
    }

    // host is null until the host has sent anything. An empty slot, or a follow that just began, snaps straight to the
    // host's sky; a change while following fades unless the host's own change was instant.
    inline WeatherAction WeatherFollow(const net::WeatherState* host, const WeatherLocal& me)
    {
        if (!me.allowed || !host || !WeatherShared(*host, me))
            return me.following ? WeatherAction::Release : WeatherAction::None;
        if (!me.following || !me.slot)
            return WeatherAction::Pop;
        if (me.slot == host->weather)
            return WeatherAction::None;
        return host->flags & net::kWeatherPopped ? WeatherAction::Pop : WeatherAction::Fade;
    }
}
