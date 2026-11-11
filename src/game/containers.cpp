// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/containers.h"

#include "coop/clock.h"
#include "game/game.h"
#include "game/npcs.h"
#include "log.h"

#include <algorithm>
#include <utility>
#include <vector>
#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 250;
        constexpr uint32_t kHoldViewMs = 30000;
        // Containers this near a friend get the host's loot sent ahead of time: a few seconds' walk, well short of
        // what the game keeps loaded, so few are watched.
        constexpr float kShareReach = 1500;
        constexpr uint32_t kNearListMs = 1000;
        constexpr size_t kMaxNewReads = 4;
        // What the host sent for a container our game hasn't loaded yet is tried again for this long (one far from
        // the host is shared with everyone as soon as a friend sets it, and may be a while's walk away).
        constexpr uint32_t kKeepUnmatchedMs = 600000;
        // A friend's change to a container the host can't see waits this long for their offer of its contents.
        constexpr uint32_t kWaitForOfferMs = 5000;
        // A body that just died is tried for this long (our copy may die a moment after the host says so), and its
        // loose items are kept hidden this long at most while the host's list of them comes.
        constexpr uint32_t kHideTriesMs = 2000;
        constexpr uint32_t kHideForMs = 3000;
        constexpr size_t kMaxUnmatched = 256;

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
            m_shared = coop::ContainerShareWatch();
            m_nearFriends.clear();
            m_unmatched.clear();
            m_waiting.clear();
            m_aliveNpcs.clear();
            m_toHide.clear();
            m_hidden.clear();
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
            HostApply(coop, now);
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
                    HostRelease(coop, ref, items);
                    coop.SetContainerContents(refId, items);
                }
                else
                    coop.HostCantSee(refId);
            }
            HostWatchBodies(coop, npcs, now);
            HostShareNearby(coop, now);
        }
        else
        {
            FriendLook(coop, npcs, now);
            FriendMatchBodies(coop, npcs, now);
        }

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

    bool Containers::HostRelease(coop::Coop& coop, TESObjectREFR* ref, std::vector<net::ItemCount>& items)
    {
        std::vector<net::ItemCount> record;
        if (!coop.Unseen().Size() || !coop.Unseen().Release(RefId(ref), record))
            return false;
        size_t changed = 0;
        for (const net::ItemCount& f : coop::ItemDiff(items, record))
            if (IsItem(f.item))
            {
                ChangeItem(ref, f);
                ++changed;
            }
        Read(ref, items, m_readMs);
        RefreshLootWindows();
        LOG_INFO("Containers: %08X is loaded here now; set to what friends had (%zu kinds of items changed)", RefId(ref),
            changed);
        return true;
    }

    void Containers::HostApply(coop::Coop& coop, uint32_t now)
    {
        std::vector<Waiting> changes = std::move(m_waiting);
        m_waiting.clear();
        for (coop::Coop::ItemChangeFrom& c : coop.TakeContainerChanges())
            changes.push_back({std::move(c), now});
        if (changes.empty())
            return;
        TESObjectREFR* mine = OpenContainer();
        bool changedMine = false;
        for (const Waiting& w : changes)
        {
            const coop::Coop::ItemChangeFrom& c = w.change;
            const net::ItemCount& want = c.change.change;
            TESObjectREFR* ref = LookupRef(c.change.ref);
            bool locker = IsGearLocker(c.change.ref);
            bool ok = IsLootable(ref) && IsItem(want.item) && !locker;
            if (locker)
                LOG_INFO("Containers: %s's change to gear locker %08X refused (each game's copy holds its own "
                    "player's gear)", coop.NameOf(c.playerId).c_str(), c.change.ref);
            if (!ok && !locker && IsItem(want.item))
            {
                // Our game can't see it: it's checked against the record of the friend who set it instead.
                net::ItemCount back;
                if (coop.Unseen().Change(c.change.ref, c.playerId, c.change.seq, want, back))
                {
                    ++m_changes;
                    if (back.count)
                        ++m_refunds;
                    LOG_DEBUG("Containers: %s %s %d of %08X %s %08X (not loaded here: its record)%s",
                        coop.NameOf(c.playerId).c_str(), want.count < 0 ? "took" : "put",
                        want.count < 0 ? -want.count : want.count, want.item, want.count < 0 ? "from" : "into",
                        c.change.ref, back.count ? " (some given back)" : "");
                    coop.ContainerChangeDone(c.playerId, c.change.seq, back);
                    if (const std::vector<net::ItemCount>* record = coop.Unseen().Find(c.change.ref))
                        coop.ShareBodyLoot(c.change.ref, *record);
                    continue;
                }
                if (now - w.since < kWaitForOfferMs)
                {
                    m_waiting.push_back(w);
                    continue;
                }
                LOG_INFO("Containers: %08X isn't loaded here and nobody's told us what's in it; %s's change given "
                    "back", c.change.ref, coop.NameOf(c.playerId).c_str());
            }
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

    void Containers::HostWatchBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        std::vector<std::pair<uint32_t, bool>> bodies = npcs.DeadBodies(now);
        std::vector<uint32_t> refs;
        refs.reserve(bodies.size());
        for (const auto& [ref, settling] : bodies)
            refs.push_back(ref);
        m_bodyLoot.KeepOnly(refs);
        if (bodies.empty())
            return;
        auto look = [&](uint32_t ref, bool settling) {
            TESObjectREFR* body = LookupRef(ref);
            std::vector<net::ItemCount> items;
            if (!IsLootable(body) || !Read(body, items, m_readMs))
                return;
            ++m_reads;
            if (m_bodyLoot.Look(ref, items, settling))
            {
                LOG_DEBUG("Containers: body %08X changed; telling friends (%zu kinds of items in it)", ref, items.size());
                coop.ShareBodyLoot(ref, items);
            }
        };
        // The one our player has open or is looking at right away, and the others (a friend or a script may have
        // changed them) one at a time, so this costs the same however many bodies lie around.
        // New ones first, a few a look: a friend may loot one a second after the kill.
        size_t fresh = 0;
        std::vector<uint32_t> looked;
        for (const auto& [ref, settling] : bodies)
            if (!m_bodyLoot.Knows(ref) && fresh < kMaxNewReads)
            {
                look(ref, settling);
                looked.push_back(ref);
                ++fresh;
            }
        auto lookedAt = [&](uint32_t ref) { return std::find(looked.begin(), looked.end(), ref) != looked.end(); };
        TESObjectREFR* mine = OpenContainer();
        if (!mine)
            mine = CrosshairRef();
        uint32_t mineRef = mine ? RefId(mine) : 0;
        for (const auto& [ref, settling] : bodies)
            if (ref == mineRef && !lookedAt(ref))
                look(ref, settling);
        auto next = std::find_if(bodies.begin(), bodies.end(),
            [&](const auto& b) { return b.first > m_lastBodyLook; });
        if (next == bodies.end())
            next = bodies.begin();
        m_lastBodyLook = next->first;
        if (next->first != mineRef && !lookedAt(next->first))
            look(next->first, next->second);
    }

    void Containers::HostShareNearby(coop::Coop& coop, uint32_t now)
    {
        if (coop::IsDue(now, m_nextNearList))
        {
            m_nextNearList = now + kNearListMs;
            m_nearFriends.clear();
            coop::Pose me;
            std::vector<coop::Pose> friends;
            if (GetPose(Player(), me))
                for (const auto& [id, remote] : coop.Remotes())
                    if (coop::Pose p; remote.motion.Newest(p) && p.space == me.space)
                        friends.push_back(p);
            if (!friends.empty() || coop.Unseen().Size())
            {
                std::vector<TESObjectREFR*> boxes;
                ContainersNear(boxes, LoadedReach());
                for (TESObjectREFR* box : boxes)
                {
                    uint32_t id = RefId(box);
                    // One a friend set while our game couldn't see it, here now: it's set to theirs.
                    if (std::vector<net::ItemCount> items; coop.Unseen().Find(id) && Read(box, items, m_readMs))
                    {
                        ++m_reads;
                        HostRelease(coop, box, items);
                    }
                    coop::Pose at;
                    if (net::IsMadeWhilePlaying(id) || IsGearLocker(id) || !GetPose(box, at))
                        continue;
                    for (const coop::Pose& f : friends)
                    {
                        float dx = at.x - f.x, dy = at.y - f.y, dz = at.z - f.z;
                        if (dx * dx + dy * dy + dz * dz < kShareReach * kShareReach)
                        {
                            m_nearFriends.push_back(id);
                            break;
                        }
                    }
                }
            }
        }
        for (uint32_t id : m_shared.ToRead(m_nearFriends, kMaxNewReads))
        {
            TESObjectREFR* box = LookupRef(id);
            std::vector<net::ItemCount> items;
            if (!IsLootable(box) || !Read(box, items, m_readMs))
                continue;
            ++m_reads;
            if (m_shared.Read(id, items))
            {
                LOG_DEBUG("Containers: telling friends what's in %08X near them (%zu kinds of items)", id, items.size());
                coop.ShareBodyLoot(id, items);
            }
        }
    }

    void Containers::FriendHideFreshBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        std::set<uint32_t> alive;
        for (const auto& [hostRef, npc] : coop.Npcs())
        {
            if (!npc.dead)
                alive.insert(hostRef);
            else if (m_aliveNpcs.count(hostRef))
                m_toHide[hostRef] = now + kHideTriesMs;
        }
        m_aliveNpcs.swap(alive);

        for (auto it = m_toHide.begin(); it != m_toHide.end();)
        {
            uint32_t hostRef = it->first;
            auto npc = coop.Npcs().find(hostRef);
            // The host's list already came: it's matched below.
            if (npc == coop.Npcs().end() || m_unmatched.count(hostRef))
            {
                it = m_toHide.erase(it);
                continue;
            }
            TESObjectREFR* body = npcs.OurRefOf(hostRef);
            std::vector<net::ItemCount> local;
            if (!IsLootable(body) || !Read(body, local, m_readMs))
            {
                it = coop::IsDue(now, it->second) ? m_toHide.erase(it) : std::next(it);
                continue;
            }
            it = m_toHide.erase(it);
            std::set<uint32_t> keep{npc->second.weapon};
            for (const net::WornItem& w : npc->second.gear)
                keep.insert(w.item);
            Hidden hidden;
            for (const net::ItemCount& i : local)
                if (!keep.count(i.item) && IsItem(i.item))
                {
                    hidden.items.push_back(i);
                    net::ItemCount out = i;
                    out.count = -i.count;
                    ChangeItem(body, out);
                }
            if (hidden.items.empty())
                continue;
            hidden.until = now + kHideForMs;
            m_hidden[hostRef] = std::move(hidden);
            // Not the player's doing, so not a change to tell the host.
            if (hostRef == coop.ViewingContainer() && Read(body, local, m_readMs))
                coop.ContainerSettled(local);
            RefreshItemList();
            RefreshLootWindows();
            LOG_DEBUG("Containers: body %08X died; %zu kinds of loose items hidden until the host says what it carried",
                RefId(body), m_hidden[hostRef].items.size());
        }

        for (auto it = m_hidden.begin(); it != m_hidden.end();)
        {
            if (!coop::IsDue(now, it->second.until))
            {
                ++it;
                continue;
            }
            TESObjectREFR* body = npcs.OurRefOf(it->first);
            if (IsLootable(body))
            {
                for (const net::ItemCount& i : it->second.items)
                    ChangeItem(body, i);
                if (it->first == coop.ViewingContainer())
                    if (std::vector<net::ItemCount> local; Read(body, local, m_readMs))
                        coop.ContainerSettled(local);
                RefreshItemList();
                RefreshLootWindows();
                LOG_INFO("Containers: the host didn't say what body %08X carried; our own roll given back", RefId(body));
            }
            it = m_hidden.erase(it);
        }
    }

    void Containers::FriendBodyMatched(coop::Coop& coop, uint32_t hostRef)
    {
        (void)coop;
        m_hidden.erase(hostRef);
        m_toHide.erase(hostRef);
    }

    void Containers::FriendMatchBodies(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        for (auto& [hostRef, items] : coop.TakeBodyLoots())
            if (m_unmatched.size() < kMaxUnmatched || m_unmatched.count(hostRef))
                m_unmatched[hostRef] = {std::move(items), now + kKeepUnmatchedMs};
        FriendHideFreshBodies(coop, npcs, now);
        for (auto it = m_unmatched.begin(); it != m_unmatched.end();)
        {
            uint32_t hostRef = it->first;
            // The one being watched is matched by FriendRead, which knows what our player just took from it. One with
            // its loose items hidden has nothing of them to take, so it's matched here.
            bool watched = hostRef == coop.ViewingContainer();
            if (watched && !m_hidden.count(hostRef))
            {
                it = m_unmatched.erase(it);
                continue;
            }
            TESObjectREFR* body = npcs.OurRefOf(hostRef);
            std::vector<net::ItemCount> local;
            if (!body || !IsLootable(body) || !Read(body, local, m_readMs))
            {
                // Our game may not have loaded it yet: the host shares what's near us a moment before we get there.
                it = coop::IsDue(now, it->second.until) ? m_unmatched.erase(it) : std::next(it);
                continue;
            }
            std::vector<net::ItemCount> items = std::move(it->second.items);
            it = m_unmatched.erase(it);
            FriendBodyMatched(coop, hostRef);
            size_t changed = 0;
            for (const net::ItemCount& f : coop::ItemDiff(local, items))
                if (IsItem(f.item))
                {
                    ChangeItem(body, f);
                    ++changed;
                }
            if (!changed)
                continue;
            if (watched)
                if (std::vector<net::ItemCount> after; Read(body, after, m_readMs))
                    coop.ContainerSettled(after);
            RefreshItemList();
            RefreshLootWindows();
            ++m_fixes;
            LOG_DEBUG("Containers: %08X matched to the host's without looking at it (%zu kinds of items changed)",
                RefId(body), changed);
        }
    }

    void Containers::FriendLook(coop::Coop& coop, const Npcs& npcs, uint32_t now)
    {
        for (uint32_t hostRef : coop.TakeContainerAsks())
        {
            // The host's game can't see it: ours becomes everyone's. Only a container; a body is the host's to say.
            TESObjectREFR* ours = npcs.OurRefOf(hostRef);
            std::vector<net::ItemCount> items;
            if (IsContainer(ours) && Read(ours, items, m_readMs))
                coop.OfferContainer(hostRef, items);
        }
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
        FriendBodyMatched(coop, coop.ViewingContainer());
        if (Read(open, local, m_readMs))
        {
            coop.ContainerSettled(local);
            m_playerBefore = coop::CountsToKeep(CountOnPlayer(coop::FormsToCount({}, local), m_readMs), local);
        }
        LOG_DEBUG("Containers: %08X matched to the host's (%zu kinds of items changed)", RefId(open), fixes.size());
    }
}
