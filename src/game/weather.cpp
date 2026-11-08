// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/weather.h"

#include "game/addresses.h"
#include "game/game.h"
#include "log.h"

#include <cstdio>
#include <set>

#include "nvse/CommandTable.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameScript.h"

namespace game
{
    namespace
    {
        static_assert(kFormType_TESWeather == 0x35, "the sky read in a running game says weathers are type 0x35");

        using ForceFn = void(__thiscall*)(void*, TESForm*, bool);
        using HurryFn = void(__thiscall*)(void*, bool);

        bool g_saveHooked = false;
        bool g_following = false;
        TESForm* g_before = nullptr;
        bool g_badFormLogged = false;
        uintptr_t g_writeSky = 0;
        uintptr_t g_writeSkyChunk = 0;
        Cmd_Execute g_forceWeather = nullptr;
        Cmd_Execute g_setWeather = nullptr;
        Cmd_Execute g_releaseOverride = nullptr;

        uint8_t* Sky() { return *reinterpret_cast<uint8_t**>(addr::kSkyInstance); }

        TESForm*& SkySlot(uint8_t* sky, uint32_t offset) { return *reinterpret_cast<TESForm**>(sky + offset); }

        uint32_t IdOf(const TESForm* form) { return form ? form->refID : 0; }

        uint32_t RootSpaceOfPlayer()
        {
            TESObjectREFR* me = Player();
            TESObjectCELL* cell = me ? me->parentCell : nullptr;
            TESWorldSpace* world = cell ? cell->worldSpace : nullptr;
            for (int depth = 0; world && world->parent && depth < 16; ++depth)
                world = world->parent;
            return world ? world->refID : 0;
        }

        coop::WeatherSky ReadSky(uint8_t* sky)
        {
            coop::WeatherSky s;
            s.climate = IdOf(SkySlot(sky, layout::kSkyClimate));
            s.weather = IdOf(SkySlot(sky, layout::kSkyCurrent));
            s.previous = IdOf(SkySlot(sky, layout::kSkyPrevious));
            s.percent = *reinterpret_cast<float*>(sky + layout::kSkyPercent);
            s.mode = static_cast<uint8_t>(*reinterpret_cast<uint32_t*>(sky + layout::kSkyMode));
            s.rootSpace = RootSpaceOfPlayer();
            return s;
        }

        void Force(uint8_t* sky, TESForm* weather)
        {
            reinterpret_cast<ForceFn>(addr::kSkyForceWeather)(sky, weather, true);
        }

        void Release(uint8_t* sky)
        {
            if (sky)
                SkySlot(sky, layout::kSkyOverride) = g_before;
            g_following = false;
            g_before = nullptr;
        }

        // While a save is written, the slot holds what the game had before we followed the host.
        class LocalOverride
        {
        public:
            explicit LocalOverride(void* sky) : m_sky(g_following ? static_cast<uint8_t*>(sky) : nullptr)
            {
                if (!m_sky)
                    return;
                m_ours = SkySlot(m_sky, layout::kSkyOverride);
                SkySlot(m_sky, layout::kSkyOverride) = g_before;
            }
            ~LocalOverride()
            {
                if (m_sky)
                    SkySlot(m_sky, layout::kSkyOverride) = m_ours;
            }

        private:
            uint8_t* m_sky;
            TESForm* m_ours = nullptr;
        };

        void __fastcall WriteSkyHook(void* sky, void*, void* buffer)
        {
            LocalOverride local(sky);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(g_writeSky)(sky, buffer);
        }

        void __fastcall WriteSkyChunkHook(void* sky, void*)
        {
            LocalOverride local(sky);
            reinterpret_cast<void(__thiscall*)(void*)>(g_writeSkyChunk)(sky);
        }

        // A friend's own scripts don't get to change the sky while it's the host's.
        bool Swallow(const char* name, Script* script, double* result)
        {
            static std::set<const Script*> s_logged;
            if (s_logged.size() < 256 && s_logged.insert(script).second)
                LOG_INFO("Weather: %s from script %08X not done here (following the host's weather)", name,
                    script ? script->refID : 0);
            *result = 0;
            return true;
        }

        bool ForceWeatherHook(COMMAND_ARGS)
        {
            return g_following ? Swallow("ForceWeather", scriptObj, result) : g_forceWeather(PASS_COMMAND_ARGS);
        }

        bool SetWeatherHook(COMMAND_ARGS)
        {
            return g_following ? Swallow("SetWeather", scriptObj, result) : g_setWeather(PASS_COMMAND_ARGS);
        }

        bool ReleaseOverrideHook(COMMAND_ARGS)
        {
            return g_following ? Swallow("ReleaseWeatherOverride", scriptObj, result) :
                g_releaseOverride(PASS_COMMAND_ARGS);
        }

        uintptr_t HookWriter(uintptr_t site, uintptr_t expected, const void* to)
        {
            uintptr_t was = HookCallSite(site, to);
            if (!was)
                LOG_WARN("Weather: couldn't hook the save at %08X (it isn't a call)", static_cast<unsigned>(site));
            else if (was != expected)
                LOG_INFO("Weather: another plugin hooks the save at %08X too (%08X); ours passes on to it",
                    static_cast<unsigned>(site), static_cast<unsigned>(was));
            return was;
        }

        Cmd_Execute WrapCommand(const char* name, Cmd_Execute wrapper)
        {
            auto was = reinterpret_cast<Cmd_Execute>(WrapScriptCommand(name, reinterpret_cast<void*>(wrapper)));
            if (!was)
                LOG_WARN("Weather: no %s command to watch; a friend's script could change the sky", name);
            return was;
        }
    }

    void Weather::Install(coop::Coop& coop)
    {
        m_coop = &coop;
        if (g_writeSky || g_writeSkyChunk)
            return;
        g_writeSky = HookWriter(addr::kSkySaveCall, addr::kSkySave, reinterpret_cast<const void*>(&WriteSkyHook));
        g_writeSkyChunk = HookWriter(addr::kSkySaveChunkCall, addr::kSkySaveChunk,
            reinterpret_cast<const void*>(&WriteSkyChunkHook));
        g_saveHooked = g_writeSky && g_writeSkyChunk;
        g_forceWeather = WrapCommand("ForceWeather", &ForceWeatherHook);
        g_setWeather = WrapCommand("SetWeather", &SetWeatherHook);
        g_releaseOverride = WrapCommand("ReleaseWeatherOverride", &ReleaseOverrideHook);
        if (g_saveHooked)
            LOG_INFO("Weather: ready");
        else
            LOG_WARN("Weather: friends here won't follow the host's weather, as it could get stuck in their saves");
    }

    void Weather::Host(const coop::WeatherSky& sky)
    {
        bool changed = m_changed.See(sky);
        if (changed)
            LOG_INFO("Weather: ours is now %08X (climate %08X, world %08X, %s)", sky.weather, sky.climate,
                sky.rootSpace, m_changed.State().flags & net::kWeatherPopped ? "instant" : "fading");
        if (m_changed.Seen())
            m_coop->SetMyWeather(m_changed.State(), changed);
    }

    void Weather::Friend(uint32_t now, bool syncOn, const coop::WeatherSky& sky)
    {
        uint8_t* skyAt = Sky();
        net::WeatherState host;
        bool hasHost = m_coop->HostWeather(host);
        TESForm* form = hasHost ? LookupForm(host.weather) : nullptr;
        bool usable = form && form->typeID == kFormType_TESWeather && !net::IsMadeWhilePlaying(host.weather);
        if (hasHost && !usable && !g_badFormLogged)
        {
            g_badFormLogged = true;
            LOG_WARN("Weather: the host's weather %08X isn't one we can show", host.weather);
        }

        coop::WeatherLocal me;
        me.allowed = syncOn && g_saveHooked && usable;
        me.following = g_following;
        me.mode = sky.mode;
        me.climate = sky.climate;
        me.rootSpace = sky.rootSpace;
        me.slot = IdOf(SkySlot(skyAt, layout::kSkyOverride));

        m_why = !syncOn ? "setting off, or still loading" : !g_saveHooked ? "save hooks missing" : !hasHost ? "nothing from the host yet" :
            !usable ? "unusable weather form" : sky.mode != coop::kSkyExterior ? "not outdoors" :
            sky.climate != host.climate ? "different climate" : sky.rootSpace != host.rootSpace ? "different world" :
            "following";

        switch (coop::WeatherFollow(hasHost ? &host : nullptr, me))
        {
        case coop::WeatherAction::None:
            break;
        case coop::WeatherAction::Release:
            Release(skyAt);
            LOG_INFO("Weather: [%u ms] back to our own (%s)", now, m_why);
            break;
        case coop::WeatherAction::Fade:
            SkySlot(skyAt, layout::kSkyOverride) = form;
            if (SkySlot(skyAt, layout::kSkyCurrent))
                reinterpret_cast<HurryFn>(addr::kSkyHurryFade)(skyAt, true);
            else
                Force(skyAt, form);
            LOG_INFO("Weather: [%u ms] fading to the host's %08X (it's at %.2f)", now, host.weather, host.percent);
            break;
        case coop::WeatherAction::Pop:
            if (!g_following)
                g_before = SkySlot(skyAt, layout::kSkyOverride);
            g_following = true;
            Force(skyAt, form);
            LOG_INFO("Weather: [%u ms] snapped to the host's %08X (ours was %08X)", now, host.weather, sky.weather);
            break;
        }
    }

    void Weather::Update(uint32_t now, bool settingOn, bool worldReady)
    {
        if (!m_coop)
            return;
        uint8_t* sky = Sky();
        bool inGame = m_coop->InGame();
        if (!inGame || m_coop->GetRole() != coop::Role::Friend)
        {
            if (g_following)
            {
                Release(sky);
                LOG_INFO("Weather: [%u ms] back to our own (co-op ended)", now);
            }
            m_why = "not a friend in a game";
        }
        if (!inGame)
        {
            m_changed.Reset();
            return;
        }
        if (!sky || !Player() || InLoadingScreen())
            return;
        bool syncOn = settingOn && worldReady;
        if (m_coop->GetRole() == coop::Role::Host)
        {
            if (!settingOn)
            {
                m_coop->StopMyWeather();
                m_changed.Reset();
                m_why = "setting off, so not sending";
            }
            else if (worldReady)
                Host(ReadSky(sky));
        }
        else if (m_coop->GetRole() == coop::Role::Friend)
        {
            if (syncOn || g_following)
                Friend(now, syncOn, ReadSky(sky));
            else
                m_why = "setting off, or still loading";
        }
    }

    void Weather::Forget()
    {
        // Whatever we'd written is about to be replaced by a save, or the session is ending.
        if (g_following)
            Release(Sky());
        m_changed.Reset();
        g_badFormLogged = false;
    }

    std::string Weather::Report() const
    {
        uint8_t* sky = Sky();
        if (!sky || !m_coop)
            return "Weather: no sky yet";
        coop::WeatherSky s = ReadSky(sky);
        net::WeatherState host;
        bool hasHost = m_coop->HostWeather(host);
        const net::WeatherState mine = m_changed.State();
        char text[320];
        std::snprintf(text, sizeof(text),
            "Weather: here %08X (from %08X, %.2f), climate %08X, world %08X, sky mode %u, slot %08X\n"
            "Host %s %08X (%.2f), climate %08X, world %08X; sending %08X\n"
            "%s (%s); save hooks %s",
            s.weather, s.previous, s.percent, s.climate, s.rootSpace, s.mode, IdOf(SkySlot(sky, layout::kSkyOverride)),
            hasHost ? "says" : "hasn't said", hasHost ? host.weather : 0, hasHost ? host.percent : 0.f,
            hasHost ? host.climate : 0, hasHost ? host.rootSpace : 0, mine.weather,
            g_following ? "Following" : "Not following", m_why, g_saveHooked ? "in" : "MISSING");
        return text;
    }
}
