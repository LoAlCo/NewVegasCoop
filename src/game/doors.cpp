// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/doors.h"

#include "coop/clock.h"
#include "game/game.h"
#include "log.h"

#include "nvse/GameObjects.h"

#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 250;
        constexpr uint32_t kListMs = 2000;
        constexpr float kMyReach = 400;

        double MsSince(const LARGE_INTEGER& start)
        {
            LARGE_INTEGER end, freq;
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&freq);
            return 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
        }

        bool IsOpen(int openState)
        {
            return openState == 1 || openState == 2;
        }

        bool Apply(const net::DoorState& want)
        {
            TESObjectREFR* ref = LookupRef(want.ref);
            DoorInfo now = ReadDoor(ref);
            if (now.open == 0)
                return false;
            bool open = (want.flags & net::kDoorOpen) != 0, locked = (want.flags & net::kDoorLocked) != 0;
            if (!now.leadsAway && IsOpen(now.open) != open)
                SetDoorOpen(ref, open);
            if (now.locked != locked)
                SetDoorLocked(ref, locked, want.lockLevel);
            LOG_DEBUG("Door %08X set %s, %s", want.ref, open ? "open" : "closed", locked ? "locked" : "unlocked");
            return true;
        }
    }

    void Doors::Forget()
    {
        m_refs.clear();
        m_cell = nullptr;
        m_nextList = 0;
    }

    void Doors::Update(coop::Coop& coop, uint32_t now)
    {
        if (!coop.InGame())
        {
            Forget();
            return;
        }
        bool host = coop.GetRole() == coop::Role::Host;
        if (host)
            for (const net::DoorState& use : coop.TakeDoorUses())
                if (Apply(use))
                    ++m_applied;
                else
                    LOG_DEBUG("A friend used door %08X, which isn't a door here", use.ref);

        if (!coop::IsDue(now, m_nextLook))
            return;
        m_nextLook = now + kLookMs;
        TESObjectREFR* player = Player();
        if (!player || !player->parentCell)
            return;

        if (player->parentCell != m_cell || coop::IsDue(now, m_nextList))
        {
            LARGE_INTEGER start;
            QueryPerformanceCounter(&start);
            std::vector<TESObjectREFR*> doors, boxes;
            DoorsNear(doors, LoadedReach());
            ContainersNear(boxes, LoadedReach());
            m_refs.clear();
            for (TESObjectREFR* d : doors)
                m_refs.push_back(RefId(d));
            for (TESObjectREFR* b : boxes)
                if (ReadDoor(b).open)
                    m_refs.push_back(RefId(b));
            m_cell = player->parentCell;
            m_nextList = now + kListMs;
            m_listMs += MsSince(start);
            ++m_lists;
        }

        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        std::vector<coop::LocalDoor> local;
        local.reserve(m_refs.size());
        for (uint32_t id : m_refs)
        {
            TESObjectREFR* ref = LookupRef(id);
            DoorInfo info = ReadDoor(ref);
            if (info.open == 0 || !net::IsDoorRef(id))
                continue;
            coop::LocalDoor d;
            d.state.ref = id;
            d.state.flags = coop::SharedDoorFlags(IsOpen(info.open), info.leadsAway, info.locked);
            d.state.lockLevel = static_cast<uint8_t>(info.lockLevel);
            float dx = ref->posX - player->posX, dy = ref->posY - player->posY, dz = ref->posZ - player->posZ;
            d.nearMe = dx * dx + dy * dy + dz * dz < kMyReach * kMyReach;
            local.push_back(d);
        }
        if (host)
        {
            std::vector<net::DoorState> states;
            states.reserve(local.size());
            for (const coop::LocalDoor& d : local)
                states.push_back(d.state);
            coop.SetDoors(std::move(states));
        }
        else
            for (const net::DoorState& fix : coop.ObserveDoors(now, local))
                if (Apply(fix))
                    ++m_applied;
        m_lookMs += MsSince(start);
        ++m_looks;

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && m_looks)
                LOG_INFO("Doors: %u near; reading them took %.3f ms on average (%u times), listing %.3f ms (%u "
                    "times); %u changed to match %s", static_cast<unsigned>(local.size()), m_lookMs / m_looks,
                    m_looks, m_lists ? m_listMs / m_lists : 0.0, m_lists, m_applied,
                    host ? "friends' uses" : "the host");
            m_nextLog = now + 60000;
            m_lookMs = m_listMs = 0;
            m_looks = m_lists = m_applied = 0;
        }
    }
}
