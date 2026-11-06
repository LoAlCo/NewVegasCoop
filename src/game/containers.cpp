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
        constexpr uint32_t kHoldViewMs = 30000;

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

        coop::FormCounts CountOnPlayer(const std::vector<uint32_t>& forms, double& ms)
        {
            LARGE_INTEGER start, end, freq;
            QueryPerformanceCounter(&start);
            coop::FormCounts counts;
            for (uint32_t form : forms)
                if (TESForm* item = LookupForm(form))
                    counts[form] = ItemCount(Player(), item);
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&freq);
            ms += 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
            return counts;
        }
    }

    void Containers::Update(coop::Coop& coop, uint32_t now, const Npcs& npcs)
    {
        if (!coop.InGame())
        {
            m_heldRef = 0;
            m_playerKnown = false;
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

            TESObjectREFR* open = OpenContainer();
            uint32_t openRef = open ? RefId(open) : 0;
            if (openRef && openRef != m_openRef && npcs.IsHeld(openRef))
            {
                if (m_closedHeld != openRef)
                {
                    CloseMenu();
                    LOG_INFO("Containers: %s (%08X) is held here (not in the host's game here yet); its screen "
                        "closed", Name(open), openRef);
                    Notify("Not here yet: the host's game hasn't loaded them");
                    m_closedHeld = openRef;
                }
                openRef = 0;
            }
            else if (!openRef)
                m_closedHeld = 0;
            if (openRef != m_openRef)
            {
                if (m_openRef)
                    LOG_DEBUG("Containers: menu on %08X shut", m_openRef);
                if (openRef)
                    LOG_DEBUG("Containers: menu open on %08X (%s, the host's %08X)", openRef,
                        IsLootable(open) ? "lootable" : "not lootable", npcs.HostRefOf(openRef));
                if (TESObjectREFR* was = m_openRef ? LookupRef(m_openRef) : nullptr; was && IsLootable(was))
                {
                    std::vector<net::ItemCount> items;
                    if (Read(was, items, m_readMs))
                    {
                        ++m_reads;
                        coop.ClosedContainer(items);
                    }
                }
                m_openRef = openRef;
                m_nextLook = now;
            }
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
            FriendLook(coop, npcs, now);

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
            bool locker = IsGearLocker(c.change.ref);
            bool ok = IsLootable(ref) && IsItem(want.item) && !locker;
            if (locker)
                LOG_INFO("Containers: %s's change to gear locker %08X refused (each game's copy holds its own "
                    "player's gear)", coop.NameOf(c.playerId).c_str(), c.change.ref);
            net::ItemCount refund = want;
            refund.count = 0;
            if (want.count < 0 && locker)
                refund.count = want.count;
            else if (want.count < 0)
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
        RefreshLootWindows();
    }

    void Containers::FriendLook(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        TESObjectREFR* open = OpenContainer();
        TESObjectREFR* target = open ? open : CrosshairRef();
        uint32_t hostRef = target && IsLootable(target) ? npcs.HostRefOf(RefId(target)) : 0;
        if (hostRef && IsGearLocker(hostRef))
            hostRef = 0;
        // The crosshair drops off a body now and then (and while a menu is up), and what we know of the body would go
        // with it. So the last one stays watched for a moment, and gets a last look before we move on to another.
        TESObjectREFR* held = m_heldRef ? LookupRef(m_heldRef) : nullptr;
        uint32_t heldHostRef = !open && held && IsLootable(held) ? npcs.HostRefOf(m_heldRef) : 0;
        if (hostRef)
        {
            if (heldHostRef && m_heldRef != RefId(target))
                FriendRead(coop, held);
            m_heldRef = RefId(target);
            m_heldUntil = now + kHoldViewMs;
        }
        else if (heldHostRef && !coop::IsDue(now, m_heldUntil))
        {
            target = held;
            hostRef = heldHostRef;
        }
        else
            m_heldRef = 0;
        coop.ViewContainer(hostRef, open && target == open);
        if (hostRef)
            FriendRead(coop, target);
        else
            m_playerKnown = false;
    }

    void Containers::FriendRead(coop::Coop& coop, TESObjectREFR* open)
    {
        std::vector<net::ItemCount> local;
        if (!Read(open, local, m_readMs))
            return;
        ++m_reads;
        coop::FormCounts mine = CountOnPlayer(coop::FormsToCount(m_playerBefore, local), m_readMs);
        std::vector<net::ItemCount> playerChange;
        if (m_playerKnown)
            playerChange = coop::CountChanges(m_playerBefore, mine);
        m_playerKnown = true;
        m_playerBefore = coop::CountsToKeep(mine, local);
        std::vector<net::ItemCount> fixes = coop.LookAtContainer(local, playerChange);
        if (fixes.empty())
            return;
        for (const net::ItemCount& f : fixes)
            if (IsItem(f.item))
                ChangeItem(open, f);
        RefreshItemList();
        RefreshLootWindows();
        ++m_fixes;
        if (Read(open, local, m_readMs))
        {
            coop.ContainerSettled(local);
            m_playerBefore = coop::CountsToKeep(CountOnPlayer(coop::FormsToCount({}, local), m_readMs), local);
        }
        LOG_DEBUG("Containers: %08X matched to the host's (%zu kinds of items changed)", RefId(open), fixes.size());
    }
}
