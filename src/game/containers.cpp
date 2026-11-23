// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/containers.h"

#include "coop/clock.h"
#include "game/far_interior.h"
#include "game/game.h"
#include "game/npcs.h"
#include "log.h"

#include <windows.h>

#include <algorithm>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 250;
        constexpr uint32_t kMatchAfterCloseMs = 5000;
        constexpr int kBodiesALook = 3;
        constexpr int kBodyReadsALook = 8;
        constexpr int kBoxChecksALook = 2;
        constexpr uint32_t kBoxListMs = 2000;
        constexpr int kBoxReadsALook = 6;
        constexpr size_t kMaxSharedBoxes = 800;
        constexpr float kWatchRange = 800.0f;
        constexpr uint32_t kWatchListMs = 1000;
        constexpr size_t kMaxWatched = 8;

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
            for (const net::ItemCount& r : coop.TakeItemRefunds())
            {
                ++m_refunds;
                if (InventoryBusy(Player()))
                    CloseScreenForChange(nullptr);
                WhenInventoryFree(Player(), [r](TESObjectREFR* player) {
                    ChangeItemNow(player, r);
                    LOG_INFO("Containers: %d of %08X (condition %d, mods %d) %s our inventory (someone got there "
                        "first)", r.count < 0 ? -r.count : r.count, r.item, r.health, r.mods,
                        r.count < 0 ? "taken back out of" : "given back to");
                    Notify(r.count < 0 ? "Someone else took that first" : "That couldn't go in there: given back");
                    RefreshItemList();
                });
            }

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
            TESObjectREFR* open = OpenContainer();
            if (!open)
                open = CrosshairRef();
            if (IsLootable(open))
            {
                std::vector<net::ItemCount> items;
                if (Read(open, items, m_readMs))
                {
                    ++m_reads;
                    if (IsActor(open))
                        coop.SetBodyContents(RefId(open), items);
                    else
                        coop.SetContainerContents(RefId(open), items);
                }
            }
            ShareBoxes(coop, now);
        }
        else
        {
            Watch(coop, npcs, now);
            FriendLook(coop, npcs, now);
            GiveBodies(coop, npcs, now);
        }

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_reads || m_changes || m_refunds || m_fixes || m_bodiesGiven || m_boxesPutBack))
                LOG_INFO("Containers: read %u times, %.3f ms on average; %u friends' changes done, %u refunded; "
                    "%u times matched to the host on aiming; %u bodies and boxes given the host's contents, %u boxes "
                    "put back to them", m_reads, m_reads ? m_readMs / m_reads : 0.0, m_changes, m_refunds, m_fixes,
                    m_bodiesGiven, m_boxesPutBack);
            if (m_nextLog && m_boxLists)
                LOG_INFO("Containers: sharing %zu boxes near us (%zu of them in interiors run for a friend; %zu read so "
                    "far); listing them took %.3f ms each time on average (%u times)", m_boxes.size(), m_farBoxes,
                    m_boxesRead.size(), m_boxListMs / m_boxLists,
                    m_boxLists);
            m_nextLog = now + 60000;
            m_readMs = m_boxListMs = 0;
            m_reads = m_changes = m_refunds = m_fixes = m_bodiesGiven = m_boxesPutBack = m_boxLists = 0;
        }
    }

    void Containers::HostApply(coop::Coop& coop)
    {
        for (coop::Coop::ItemChangeFrom& c : coop.TakeContainerChanges())
            m_hostChanges.push_back(std::move(c));
        if (m_hostChanges.empty())
            return;
        for (const coop::Coop::ItemChangeFrom& c : m_hostChanges)
            if (InventoryBusy(LookupRef(c.change.ref)))
            {
                CloseScreenForChange("Another player changed what's in here: open it again");
                return;
            }
        std::vector<coop::Coop::ItemChangeFrom> changes;
        changes.swap(m_hostChanges);
        std::vector<uint32_t> bodies;
        for (const coop::Coop::ItemChangeFrom& c : changes)
        {
            const net::ItemCount& want = c.change.change;
            TESObjectREFR* ref = LookupRef(c.change.ref);
            bool locker = IsGearLocker(c.change.ref);
            bool ok = IsLootable(ref) && IsItem(want.item) && !locker;
            if (ok && std::find(bodies.begin(), bodies.end(), c.change.ref) == bodies.end())
                bodies.push_back(c.change.ref);
            if (locker)
                LOG_INFO("Containers: %s's change to gear locker %08X refused (each game's copy holds its own "
                    "player's gear)", coop.NameOf(c.playerId).c_str(), c.change.ref);
            net::ItemCount refund = want;
            refund.count = 0;
            if (want.count < 0 && locker)
                refund.count = want.count;
            else if (want.count < 0)
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
                    ChangeItemNow(ref, taken);
                }
                refund.count = -(-want.count - take);
            }
            else if (ok)
                ChangeItemNow(ref, want);
            else
                refund.count = want.count;
            ++m_changes;
            if (refund.count)
                ++m_refunds;
            LOG_DEBUG("Containers: %s %s %d of %08X %s %08X%s", coop.NameOf(c.playerId).c_str(),
                want.count < 0 ? "took" : "put", want.count < 0 ? -want.count : want.count, want.item,
                want.count < 0 ? "from" : "into", c.change.ref, refund.count ? " (some given back)" : "");
            coop.ContainerChangeDone(c.playerId, c.change.ref, c.change.seq, refund);
        }
        for (uint32_t id : bodies)
        {
            std::vector<net::ItemCount> items;
            if (TESObjectREFR* body = LookupRef(id); IsLootable(body) && Read(body, items, m_readMs))
            {
                ++m_reads;
                if (IsActor(body))
                    coop.SetBodyContents(id, items);
                else
                    coop.SetContainerContents(id, items);
            }
        }
    }

    bool Containers::ReadBox(coop::Coop& coop, uint32_t id)
    {
        std::vector<net::ItemCount> items;
        TESObjectREFR* box = LookupRef(id);
        if (!IsLootable(box) || IsActor(box) || !Read(box, items, m_readMs))
            return false;
        ++m_reads;
        coop.SetBodyContents(id, items);
        return true;
    }

    void Containers::ShareBoxes(coop::Coop& coop, uint32_t now)
    {
        if (coop::IsDue(now, m_nextBoxList))
        {
            m_nextBoxList = now + kBoxListMs;
            LARGE_INTEGER start, end, freq;
            QueryPerformanceCounter(&start);
            std::vector<TESObjectREFR*> nearby;
            ContainersNear(nearby, LoadedReach(), true);
            coop::Pose me, it;
            GetPose(Player(), me);
            std::vector<std::pair<float, uint32_t>> byDistance;
            m_farBoxes = 0;
            for (TESObjectREFR* box : nearby)
            {
                uint32_t id = RefId(box);
                if (net::IsMadeWhilePlaying(id) || IsGearLocker(id))
                    continue;
                bool away = GetPose(box, it) && it.space != me.space && IsFarInterior(it.space);
                m_farBoxes += away;
                byDistance.push_back({away ? 0.0f : DistanceToPlayer(box), id});
            }
            std::sort(byDistance.begin(), byDistance.end());
            if (byDistance.size() > kMaxSharedBoxes)
                byDistance.resize(kMaxSharedBoxes);
            m_boxes.clear();
            std::set<uint32_t> read;
            for (const auto& [distance, id] : byDistance)
            {
                m_boxes.push_back(id);
                if (m_boxesRead.count(id))
                    read.insert(id);
            }
            m_boxesRead.swap(read);
            coop.SetSharedBoxes(m_boxes);
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&freq);
            m_boxListMs += 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
            ++m_boxLists;
        }
        int reads = 0;
        if (m_boxesRead.size() < m_boxes.size())
            for (uint32_t id : m_boxes)
            {
                if (reads >= kBoxReadsALook)
                    break;
                if (m_boxesRead.insert(id).second)
                {
                    ReadBox(coop, id);
                    ++reads;
                }
            }
        for (; reads < kBoxReadsALook && !m_boxes.empty(); ++reads)
            ReadBox(coop, m_boxes[m_boxTurn++ % m_boxes.size()]);
    }

    void Containers::GiveBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        m_bodyRefs.clear();
        coop.BodiesWaiting(m_bodyRefs);
        m_giveOrder.clear();
        for (uint32_t hostRef : m_bodyRefs)
        {
            TESObjectREFR* ours = npcs.OurRefOf(hostRef);
            bool box = ours && !IsActor(ours);
            if ((box && !IsLootable(ours)) || IsGearLocker(hostRef))
                coop.ForgetBody(hostRef);
            else if (!box)
                m_giveOrder.push_back({-1.0f, hostRef});
            else if (Is3DLoaded(ours))
                m_giveOrder.push_back({DistanceToPlayer(ours), hostRef});
        }
        std::sort(m_giveOrder.begin(), m_giveOrder.end());
        int given = 0, reads = 0;
        for (const auto& [distance, hostRef] : m_giveOrder)
        {
            if (reads >= kBodyReadsALook || given >= kBodiesALook)
                break;
            bool box = distance >= 0;
            TESObjectREFR* body = box ? npcs.OurRefOf(hostRef) : npcs.BodyOf(hostRef, now);
            if (!body || InventoryBusy(body))
                continue;
            std::vector<net::ItemCount> local, fixes;
            ++reads;
            if (!Read(body, local, m_readMs))
                continue;
            ++m_reads;
            if (!coop.BodyFixes(hostRef, local, fixes))
                continue;
            for (const net::ItemCount& f : fixes)
                if (IsItem(f.item))
                    ChangeItemNow(body, f);
            coop.BodyDone(hostRef);
            if (!fixes.empty() && Read(body, local, m_readMs))
                coop.WatchedSettled(hostRef, local);
            if (fixes.empty())
                continue;
            ++given;
            ++m_bodiesGiven;
            LOG_DEBUG("Containers: %s %08X (the host's %08X) given what the host's holds (%zu kinds of items "
                "changed)", box ? "box" : "body", RefId(body), hostRef, fixes.size());
        }
        for (int i = 0; i < kBoxChecksALook; ++i)
        {
            uint32_t hostRef = 0;
            if (!coop.NextBodyToCheck(hostRef))
                break;
            TESObjectREFR* box = npcs.OurRefOf(hostRef);
            if (!box || IsActor(box) || !IsLootable(box) || !Is3DLoaded(box) || IsGearLocker(hostRef) ||
                InventoryBusy(box))
                continue;
            std::vector<net::ItemCount> local, fixes;
            if (!Read(box, local, m_readMs))
                continue;
            ++m_reads;
            if (!coop.BodyFixes(hostRef, local, fixes) || fixes.empty())
                continue;
            for (const net::ItemCount& f : fixes)
                if (IsItem(f.item))
                    ChangeItemNow(box, f);
            if (Read(box, local, m_readMs))
                coop.WatchedSettled(hostRef, local);
            ++m_boxesPutBack;
            LOG_DEBUG("Containers: box %08X put back to what the host's holds (%zu kinds of items: our own game had "
                "changed it)", hostRef, fixes.size());
        }
    }

    void Containers::Watch(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        if (coop::IsDue(now, m_nextWatchList))
        {
            m_nextWatchList = now + kWatchListMs;
            std::vector<TESObjectREFR*> nearby, actors;
            ContainersNear(nearby, kWatchRange);
            NearbyActors(actors);
            for (TESObjectREFR* a : actors)
                if (a != Player() && IsLootable(a))
                    nearby.push_back(a);
            std::vector<std::pair<float, uint32_t>> byDistance;
            for (TESObjectREFR* ref : nearby)
                if (float d = DistanceToPlayer(ref); d <= kWatchRange)
                    byDistance.push_back({d, RefId(ref)});
            std::sort(byDistance.begin(), byDistance.end());
            m_watchList.clear();
            for (const auto& [distance, id] : byDistance)
                if (m_watchList.size() < kMaxWatched)
                    m_watchList.push_back(id);
        }
        std::vector<uint32_t> ids = m_watchList;
        if (TESObjectREFR* aimed = CrosshairRef(); IsLootable(aimed) &&
            std::find(ids.begin(), ids.end(), RefId(aimed)) == ids.end())
            ids.insert(ids.begin(), RefId(aimed));
        TESObjectREFR* open = OpenContainer();
        std::vector<coop::BoxContents> boxes;
        for (uint32_t id : ids)
        {
            TESObjectREFR* ref = LookupRef(id);
            uint32_t hostRef = IsLootable(ref) && ref != open ? npcs.HostRefOf(id) : 0;
            if (!hostRef || IsGearLocker(hostRef))
                continue;
            coop::BoxContents box{hostRef, {}};
            if (!Read(ref, box.second, m_readMs))
                continue;
            ++m_reads;
            boxes.push_back(std::move(box));
        }
        std::vector<net::ItemCount> mine;
        if (boxes.empty() || !Read(Player(), mine, m_readMs))
            boxes.clear();
        coop.WatchContainers(boxes, mine);
    }

    void Containers::FriendLook(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        TESObjectREFR* open = OpenContainer();
        bool isOpen = open != nullptr;
        if (!open && m_closedToMatch)
        {
            TESObjectREFR* aimed = CrosshairRef();
            TESObjectREFR* closed = coop::IsDue(now, m_closedUntil) ? nullptr : LookupRef(m_closedToMatch);
            if (closed && !(aimed && aimed != closed && IsLootable(aimed)))
                open = closed;
            else
                m_closedToMatch = 0;
        }
        if (!open)
            open = MerchantContainerOf(DialoguePartner());
        if (!open)
            open = CrosshairRef();
        uint32_t hostRef = open && IsLootable(open) ? npcs.HostRefOf(RefId(open)) : 0;
        if (hostRef && IsGearLocker(hostRef))
            hostRef = 0;
        coop.ViewContainer(hostRef, isOpen);
        if (!hostRef)
            return;
        std::vector<net::ItemCount> local;
        if (!Read(open, local, m_readMs))
            return;
        ++m_reads;
        std::vector<net::ItemCount> fixes = coop.LookAtContainer(local, !isOpen);
        if (fixes.empty())
            return;
        if (isOpen || InventoryBusy(open))
        {
            CloseScreenForChange("The host's game holds something else in here: open it again");
            m_closedUntil = now + kMatchAfterCloseMs;
            if (m_closedToMatch != RefId(open))
            {
                m_closedToMatch = RefId(open);
                LOG_INFO("Containers: %08X's screen closed: it holds something else in the host's game (%zu kinds "
                    "of items differ); matched once it has gone", RefId(open), fixes.size());
            }
            return;
        }
        m_closedToMatch = 0;
        for (const net::ItemCount& f : fixes)
            if (IsItem(f.item))
                ChangeItemNow(open, f);
        ++m_fixes;
        coop.BodyDone(hostRef);
        if (Read(open, local, m_readMs))
        {
            coop.ContainerSettled(local);
            coop.WatchedSettled(hostRef, local);
        }
        LOG_DEBUG("Containers: %08X matched to the host's (%zu kinds of items changed)", RefId(open), fixes.size());
    }
}
