// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/containers.h"

#include "coop/clock.h"
#include "game/game.h"
#include "game/npcs.h"
#include "log.h"

#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 250;

        bool Read(TESObjectREFR* ref, std::vector<net::ItemCount>& out, double& ms)
        {
            LARGE_INTEGER start, end, freq;
            QueryPerformanceCounter(&start);
            std::vector<Item> items;
            bool ok = Contents(ref, items);
            out.clear();
            for (const Item& i : items)
                out.push_back({i.id, i.count, i.health, i.mods});
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&freq);
            ms += 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
            return ok;
        }
    }

    void Containers::Update(coop::Coop& coop, uint32_t now, const Npcs& npcs)
    {
        if (!coop.InGame())
        {
            m_openRef = 0;
            return;
        }
        if (QuantityPromptOpen())
        {
            if (!m_waitedForPrompt)
                LOG_DEBUG("Containers: waiting while a \"how many?\" prompt is open");
            m_waitedForPrompt = true;
            return;
        }
        m_waitedForPrompt = false;
        bool host = coop.GetRole() == coop::Role::Host;
        if (host)
            HostApply(coop);
        else
        {
            std::vector<net::ItemCount> refunds = coop.TakeItemRefunds();
            for (const net::ItemCount& r : refunds)
            {
                ChangeItem(Player(), r);
                ++m_refunds;
                LOG_INFO("Containers: %d of %08X (condition %d, mods %d) %s our inventory (someone got there first)",
                    r.count < 0 ? -r.count : r.count, r.item, r.health, r.mods,
                    r.count < 0 ? "taken back out of" : "given back to");
                Notify(r.count < 0 ? "Someone else took that first" : "That couldn't go in there: given back");
            }
            if (!refunds.empty())
                RefreshItemList();
            if (TESObjectREFR* open = OpenContainer())
                m_openRef = RefId(open);
        }

        if (!coop::IsDue(now, m_nextLook))
            return;
        m_nextLook = now + kLookMs;
        if (host)
        {
            for (uint32_t refId : coop.ViewedContainers())
            {
                TESObjectREFR* ref = LookupRef(refId);
                std::vector<net::ItemCount> items;
                if (IsLootable(ref) && Read(ref, items, m_readMs))
                {
                    ++m_reads;
                    coop.SetContainerContents(refId, items);
                }
            }
        }
        else
            FriendLook(coop, npcs);

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_reads || m_changes || m_refunds || m_fixes))
                LOG_INFO("Containers: read %u times, %.3f ms on average; %u friends' changes done, %u refunded; "
                    "%u times matched to the host", m_reads, m_reads ? m_readMs / m_reads : 0.0, m_changes,
                    m_refunds, m_fixes);
            m_nextLog = now + 60000;
            m_readMs = 0;
            m_reads = m_changes = m_refunds = m_fixes = 0;
        }
    }

    void Containers::HostApply(coop::Coop& coop)
    {
        std::vector<coop::Coop::ItemChangeFrom> changes = coop.TakeContainerChanges();
        if (changes.empty())
            return;
        TESObjectREFR* mine = OpenContainer();
        bool changedMine = false;
        for (const coop::Coop::ItemChangeFrom& c : changes)
        {
            const net::ItemCount& want = c.change.change;
            TESObjectREFR* ref = LookupRef(c.change.ref);
            bool ok = IsLootable(ref) && IsItem(want.item);
            net::ItemCount refund = want;
            refund.count = 0;
            if (want.count < 0)
            {
                coop::ContainerTake plan;
                std::vector<net::ItemCount> items;
                if (ok && Read(ref, items, m_readMs))
                    plan = coop::PlanContainerTake(items, want);
                else
                    plan.missing = -want.count;
                for (const net::ItemCount& remove : plan.removes)
                    ChangeItem(ref, remove);
                std::vector<net::ItemCount> after;
                if (plan.taken > 0 && Read(ref, after, m_readMs))
                {
                    int32_t stuck = coop::TotalOf(after, want.item) - (coop::TotalOf(items, want.item) - plan.taken);
                    if (stuck > 0)
                    {
                        LOG_WARN("Containers: %d of %08X stayed in %08X when taken by kind; taking them by item",
                            stuck, want.item, c.change.ref);
                        ChangeItem(ref, want.item, -stuck);
                    }
                }
                refund.count = -plan.missing;
            }
            else if (ok)
                ChangeItem(ref, want);
            else
                refund.count = want.count;
            ++m_changes;
            if (refund.count)
                ++m_refunds;
            LOG_DEBUG("Containers: %s %s %d of %08X %s %08X%s", coop.NameOf(c.playerId).c_str(),
                want.count < 0 ? "took" : "put", want.count < 0 ? -want.count : want.count, want.item,
                want.count < 0 ? "from" : "into", c.change.ref, refund.count ? " (some given back)" : "");
            coop.ContainerChangeDone(c.playerId, c.change.seq, refund);
            changedMine |= ok && ref == mine;
        }
        if (changedMine)
            RefreshItemList();
    }

    void Containers::FriendLook(coop::Coop& coop, const Npcs& npcs)
    {
        TESObjectREFR* open = OpenContainer();
        uint32_t openRef = open ? RefId(open) : 0;
        if (m_openRef && m_openRef != openRef)
        {
            // The menu shut since the last look, and the crosshair may be off it by now: one more look at what was
            // open, or what was taken in those last moments (all of it, with Take All) never reaches the host. It
            // may have opened and shut between two looks, so it's told it was open first.
            TESObjectREFR* closed = LookupRef(m_openRef);
            uint32_t closedHostRef = closed && IsLootable(closed) ? npcs.HostRefOf(m_openRef) : 0;
            m_openRef = openRef;
            if (closedHostRef)
            {
                coop.ViewContainer(closedHostRef, true);
                coop.ViewContainer(closedHostRef, false);
                FriendRead(coop, closed);
                if (!open)
                    return;
            }
        }
        m_openRef = openRef;
        TESObjectREFR* target = open ? open : CrosshairRef();
        uint32_t hostRef = target && IsLootable(target) ? npcs.HostRefOf(RefId(target)) : 0;
        coop.ViewContainer(hostRef, open != nullptr);
        if (hostRef)
            FriendRead(coop, target);
    }

    void Containers::FriendRead(coop::Coop& coop, TESObjectREFR* open)
    {
        std::vector<net::ItemCount> local;
        if (!Read(open, local, m_readMs))
            return;
        ++m_reads;
        std::vector<net::ItemCount> fixes = coop.LookAtContainer(local);
        if (fixes.empty())
            return;
        for (const net::ItemCount& f : fixes)
            if (IsItem(f.item))
                ChangeItem(open, f);
        RefreshItemList();
        ++m_fixes;
        if (Read(open, local, m_readMs))
            coop.ContainerSettled(local);
        LOG_DEBUG("Containers: %08X matched to the host's (%zu kinds of items changed)", RefId(open), fixes.size());
    }
}
