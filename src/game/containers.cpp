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

        int32_t CountOf(const std::vector<net::ItemCount>& items, const net::ItemCount& kind)
        {
            for (const net::ItemCount& i : items)
                if (i.SameKind(kind))
                    return i.count;
            return 0;
        }
    }

    void Containers::Update(coop::Coop& coop, uint32_t now, const Npcs& npcs)
    {
        if (!coop.InGame())
            return;
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
                int32_t have = 0;
                std::vector<net::ItemCount> items;
                if (ok && Read(ref, items, m_readMs))
                    have = CountOf(items, want);
                int32_t take = -want.count < have ? -want.count : have;
                if (take > 0)
                {
                    net::ItemCount taken = want;
                    taken.count = -take;
                    ChangeItem(ref, taken);
                }
                refund.count = -(-want.count - take);
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
        bool isOpen = open != nullptr;
        if (!open)
            open = CrosshairRef();
        uint32_t hostRef = open && IsLootable(open) ? npcs.HostRefOf(RefId(open)) : 0;
        coop.ViewContainer(hostRef, isOpen);
        if (!hostRef)
            return;
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
