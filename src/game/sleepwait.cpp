// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/sleepwait.h"

#include "coop/clock.h"
#include "game/addresses.h"
#include "game/game.h"
#include "log.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "nvse/GameForms.h"

namespace game
{
    namespace
    {
        constexpr uint32_t kWaitButton = 4;
        constexpr uintptr_t kMenuSlider = 0x2C;
        constexpr uintptr_t kMenuStarted = 0x44;
        constexpr uintptr_t kMenuSleep = 0x45;
        constexpr float kClockOffDays = 0.002f;
        constexpr uint32_t kClockEveryMs = 1000;

        SleepWait* g_self = nullptr;
        uintptr_t g_original = 0;
        bool g_letThrough = false;
        constexpr int kWaitQuestion = 1;

        void __fastcall ClickHook(void* menu, void*, uint32_t tile, void* target)
        {
            if (g_self && !g_self->Clicked(menu, tile))
                return;
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, void*)>(g_original)(menu, tile, target);
        }

        uint8_t* Menu() { return *reinterpret_cast<uint8_t**>(addr::kSleepWaitMenu); }

        void* Slider(uint8_t* menu) { return menu ? *reinterpret_cast<void**>(menu + kMenuSlider) : nullptr; }

        float SliderHours(uint8_t* menu)
        {
            void* tile = Slider(menu);
            uint32_t value = *reinterpret_cast<uint32_t*>(addr::kSleepWaitHoursValue);
            return tile ? reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(addr::kTileGetValueFloat)(tile, value)
                        : 0.0f;
        }

        void SetSliderHours(uint8_t* menu, float hours)
        {
            if (void* tile = Slider(menu))
                reinterpret_cast<void(__thiscall*)(void*, uint32_t, float, bool)>(addr::kTileSetValueFloat)(tile,
                    *reinterpret_cast<uint32_t*>(addr::kSleepWaitHoursValue), hours, true);
        }

        TESGlobal* Global(uint32_t id)
        {
            TESForm* form = LookupForm(id);
            return form && form->typeID == kFormType_TESGlobal ? static_cast<TESGlobal*>(form) : nullptr;
        }
    }

    void SleepWait::Install(coop::Coop& coop)
    {
        m_coop = &coop;
        g_self = this;
        if (!g_original)
        {
            auto* slot = reinterpret_cast<uintptr_t*>(addr::kSleepWaitClickSlot);
            DWORD old;
            if (VirtualProtect(slot, 4, PAGE_READWRITE, &old))
            {
                g_original = *slot;
                *slot = reinterpret_cast<uintptr_t>(&ClickHook);
                VirtualProtect(slot, 4, old, &old);
            }
            if (g_original && g_original != addr::kSleepWaitClick)
                LOG_INFO("Sleep/wait: another plugin handles the menu's buttons too (%08X); ours passes on to it",
                    static_cast<unsigned>(g_original));
        }
        LOG_INFO("Sleep/wait: menu buttons %s", g_original ? "hooked" : "NOT hooked");
    }

    bool SleepWait::Clicked(void* menu, uint32_t tile)
    {
        if (tile != kWaitButton)
            return true;
        if (g_letThrough)
        {
            g_letThrough = false;
            return true;
        }
        if (!m_coop || !m_coop->InGame())
            return true;
        if (m_asking)
            return false;
        auto* m = static_cast<uint8_t*>(menu);
        float slider = SliderHours(m);
        int hours = static_cast<int>(slider) + 1;
        hours = hours < 1 ? 1 : hours > net::kMaxWaitHours ? net::kMaxWaitHours : hours;
        bool sleep = m[kMenuSleep] != 0;
        m_asking = true;
        m_coop->AskToWait(static_cast<uint8_t>(hours), sleep);
        LOG_INFO("Sleep/wait: we asked to %s %d hours (slider %.2f)", sleep ? "sleep" : "wait", hours, slider);
        char text[96];
        std::snprintf(text, sizeof(text), "Asking the others to %s %d hours...", sleep ? "sleep" : "wait", hours);
        Notify(text);
        return false;
    }

    void SleepWait::StartWait(uint8_t hours, bool sleep, bool mine)
    {
        uint8_t* menu = Menu();
        if (!mine || !menu || menu[kMenuStarted])
        {
            if (menu)
                CloseMenu();
            reinterpret_cast<void(__cdecl*)(bool)>(addr::kShowSleepWaitMenu)(sleep);
            menu = Menu();
            if (!menu)
            {
                LOG_WARN("Sleep/wait: couldn't open the menu to wait with the others");
                return;
            }
        }
        SetSliderHours(menu, hours - 1.0f);
        g_letThrough = true;
        reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t, void*)>(&ClickHook)(menu, nullptr, kWaitButton,
            nullptr);
        g_letThrough = false;
        LOG_INFO("Sleep/wait: %s %u hours with everyone (slider now %.2f, started %d)", sleep ? "sleeping" : "waiting",
            hours, SliderHours(menu), Menu() ? Menu()[kMenuStarted] : -1);

    }

    void SleepWait::Ask(const coop::Coop::WaitEvent& e)
    {
        char text[160];
        std::snprintf(text, sizeof(text), "%s wants to %s %u hours. Is that OK?", m_coop->NameOf(e.requester).c_str(),
            e.sleep ? "sleep" : "wait", e.hours);
        m_popupOpen = AskYesNo(text, kWaitQuestion);
        if (!m_popupOpen)
        {
            LOG_WARN("Sleep/wait: couldn't show the question; answering no");
            m_coop->AnswerWait(false);
        }
    }

    void SleepWait::LabPress(uint8_t hours, bool sleep)
    {
        if (!Menu())
            reinterpret_cast<void(__cdecl*)(bool)>(addr::kShowSleepWaitMenu)(sleep);
        uint8_t* menu = Menu();
        if (!menu)
        {
            LOG_INFO("Lab: the sleep/wait menu didn't open");
            return;
        }
        SetSliderHours(menu, hours - 1.0f);
        LOG_INFO("Lab: pressing Wait (slider %.2f, sleeping %d)", SliderHours(menu), menu[kMenuSleep]);
        ClickHook(menu, nullptr, kWaitButton, nullptr);
    }

    void SleepWait::FollowHostClock()
    {
        net::GameTime t;
        if (!m_coop->TakeGameTime(t))
            return;
        uint8_t* menu = Menu();
        if (menu && menu[kMenuStarted])
            return;
        TESGlobal* days = Global(ids::kGameDaysPassed);
        TESGlobal* hour = Global(ids::kGameHour);
        TESGlobal* day = Global(ids::kGameDay);
        TESGlobal* month = Global(ids::kGameMonth);
        TESGlobal* year = Global(ids::kGameYear);
        if (!days || !hour || !day || !month || !year || std::fabs(days->data - t.daysPassed) < kClockOffDays)
            return;
        LOG_INFO("Clock: set to the host's (day %.3f, %.2f h; ours was day %.3f, %.2f h)", t.daysPassed, t.hour,
            days->data, hour->data);
        days->data = t.daysPassed;
        hour->data = t.hour;
        day->data = t.day;
        month->data = t.month;
        year->data = t.year;
        *reinterpret_cast<uint8_t*>(addr::kGameTimeGlobals + addr::kGameTimeReload) = 1;
        auto* food = reinterpret_cast<float*>(addr::kHardcoreFoodMark);
        LOG_INFO("Clock: hardcore needs counted afresh from now (marks were %.1f %.1f %.1f)", *food,
            *reinterpret_cast<const float*>(addr::kHardcoreWaterMark),
            *reinterpret_cast<const float*>(addr::kHardcoreSleepMark));
        *food = 0.0f;
    }

    void SleepWait::Update(uint32_t now)
    {
        if (!m_coop || !m_coop->InGame())
        {
            m_asking = m_popupOpen = false;
            return;
        }
        if (m_coop->GetRole() == coop::Role::Host && coop::IsDue(now, m_nextClock))
        {
            m_nextClock = now + kClockEveryMs;
            TESGlobal* days = Global(ids::kGameDaysPassed);
            TESGlobal* hour = Global(ids::kGameHour);
            TESGlobal* day = Global(ids::kGameDay);
            TESGlobal* month = Global(ids::kGameMonth);
            TESGlobal* year = Global(ids::kGameYear);
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG_INFO("Clock: %s (day %.3f, %.2f h, %d/%d/%d)", days && hour && day && month && year ? "found" :
                    "NOT found", days ? days->data : 0.f, hour ? hour->data : 0.f, day ? int(day->data) : 0,
                    month ? int(month->data) + 1 : 0, year ? int(year->data) : 0);
            }
            if (days && hour && day && month && year)
            {
                net::GameTime t;
                t.daysPassed = days->data;
                t.hour = hour->data < 0 ? 0 : hour->data > 24 ? 24 : hour->data;
                t.day = static_cast<uint8_t>(day->data < 1 ? 1 : day->data > 31 ? 31 : day->data);
                t.month = static_cast<uint8_t>(month->data < 0 ? 0 : month->data > 11 ? 11 : month->data);
                t.year = static_cast<uint16_t>(year->data < 0 ? 0 : year->data > 65535 ? 65535 : year->data);
                m_coop->SetMyGameTime(t);
            }
        }
        else if (m_coop->GetRole() == coop::Role::Friend)
            FollowHostClock();

        if (m_asking && !Menu())
        {
            m_asking = false;
            m_coop->AskToWait(0, false);
            LOG_INFO("Sleep/wait: we closed the menu; called it off");
        }
        int answer = m_popupOpen ? TakeAnswer(kWaitQuestion) : -1;
        if (answer >= 0)
        {
            m_popupOpen = false;
            m_coop->AnswerWait(answer == 1);
            LOG_INFO("Sleep/wait: we answered %s", answer == 1 ? "yes" : "no");
        }

        for (const coop::Coop::WaitEvent& e : m_coop->TakeWaitEvents())
        {
            bool mine = e.requester == m_coop->MyPlayerId();
            switch (e.kind)
            {
            case coop::Coop::WaitEvent::Kind::Asked:
                Ask(e);
                break;
            case coop::Coop::WaitEvent::Kind::Start:
                m_asking = false;
                StartWait(e.hours, e.sleep, mine);
                break;
            case coop::Coop::WaitEvent::Kind::Refused:
            {
                if (mine)
                    m_asking = false;
                std::string why = e.refusedBy != net::kNobody ? m_coop->NameOf(e.refusedBy) + " said no" :
                    std::string("called off, or not everyone answered");
                std::string text = "No waiting: " + why;
                Notify(text.c_str());
                LOG_INFO("Sleep/wait: %s", text.c_str());
                break;
            }
            }
        }
    }
}
