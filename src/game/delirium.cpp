// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/delirium.h"

#include "log.h"

#include <windows.h>

namespace game::delirium
{
    namespace
    {
        constexpr size_t kMaxEvents = 128;

        const DeliriumApi* g_api = nullptr;
        bool g_on = false;
        bool (*g_isCopy)(uint32_t) = nullptr;
        std::vector<DeliriumEvent> g_events;
        bool g_saidFull = false;

        bool IsCopy(uint32_t refId)
        {
            return g_on && g_isCopy && g_isCopy(refId);
        }

        void Heard(const DeliriumEvent* ev)
        {
            if (!g_on || !ev)
                return;
            if (g_events.size() >= kMaxEvents)
            {
                if (!g_saidFull)
                    LOG_WARN("Delirium: too many reactions in one frame; dropped some");
                g_saidFull = true;
                return;
            }
            g_events.push_back(*ev);
        }

        bool Usable() { return g_api && g_on; }
    }

    bool Load(bool (*isCopy)(uint32_t localRef))
    {
        HMODULE dll = GetModuleHandleA("FNVDelirium.dll");
        if (!dll)
        {
            LOG_INFO("Delirium: FNV Delirium isn't installed (its reactions aren't synced)");
            return false;
        }
        using GetApi = const DeliriumApi* (*)();
        auto get = reinterpret_cast<GetApi>(GetProcAddress(dll, "Delirium_GetApi"));
        const DeliriumApi* api = get ? get() : nullptr;
        if (!api || api->version < 1 || api->size < sizeof(DeliriumApi) || !api->SetListener || !api->SetCopyCheck ||
            !api->ReportHit || !api->StartReaction || !api->GetRoot || !api->SteerRoot || !api->ForceOutcome ||
            !api->IsReacting || !api->GetLastHit)
        {
            LOG_WARN("Delirium: FNVDelirium.dll is loaded but has no interface we know (too old? version %u); its "
                "reactions aren't synced", api ? api->version : 0);
            return false;
        }
        g_api = api;
        g_isCopy = isCopy;
        g_api->SetCopyCheck(&IsCopy);
        g_api->SetListener(&Heard);
        LOG_INFO("Delirium: found FNV Delirium (interface version %u); its reactions are synced when everyone has it",
            api->version);
        return true;
    }

    bool Present() { return g_api != nullptr; }

    void SetOn(bool on)
    {
        if (on == g_on)
            return;
        g_on = on;
        g_events.clear();
        g_saidFull = false;
        if (g_api)
            LOG_INFO("Delirium: syncing %s", on ? "on" : "off");
    }

    bool On() { return Usable(); }

    bool IsReacting(uint32_t localRef) { return Usable() && localRef && g_api->IsReacting(localRef); }

    std::vector<DeliriumEvent> TakeEvents()
    {
        g_saidFull = false;
        return std::move(g_events);
    }

    bool GetRoot(uint32_t localRef, float out[7]) { return Usable() && g_api->GetRoot(localRef, out); }

    bool StartReaction(const DeliriumReaction& reaction) { return Usable() && g_api->StartReaction(&reaction); }

    void SteerRoot(uint32_t localRef, const float root[7])
    {
        if (Usable())
            g_api->SteerRoot(localRef, root);
    }

    void ForceOutcome(uint32_t localRef, uint32_t outcome, const float pos[3], float heading)
    {
        if (Usable())
            g_api->ForceOutcome(localRef, outcome, pos, heading);
    }

    void ReportHit(const DeliriumHit& hit)
    {
        if (Usable())
            g_api->ReportHit(&hit);
    }

    bool GetLastHit(uint32_t localRef, DeliriumHit& out) { return Usable() && g_api->GetLastHit(localRef, &out); }
}
