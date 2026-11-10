// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/npcs.h"

#include "game/delirium.h"
#include "coop/delirium_sync.h"

#include "coop/copy_kill.h"
#include "coop/levelled_roll.h"
#include "coop/visitor_companions.h"

#include "coop/clock.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/standins.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kStatsEveryMs = 30000;
        constexpr uint32_t kHealthCheckEveryMs = 2000;
        constexpr uint32_t kPuppetStatsEveryMs = 30000;
        float ShowDistance() { return LoadedReach() - 192; }
        constexpr float kShowMargin = 800;
        constexpr uint32_t kBringEveryMs = 1000;
        constexpr uint32_t kGearEveryMs = 2000;
        constexpr uint32_t kDropsSettleMs = 15000;
        constexpr uint32_t kMaxDressings = 3;
        constexpr uint32_t kGunCheckEveryMs = 1000;
        constexpr uint32_t kMakeEveryMs = 250;
        constexpr uint32_t kSettleMs = 1500;
        constexpr uint32_t kWakeAfterMs = 2000;
        constexpr uint32_t kWakeEveryMs = 3000;
        constexpr uint32_t kWakeSoonMs = 300;
        constexpr uint32_t kKnockEveryMs = 3000;
        constexpr uint32_t kMaxKnockTries = 3;
        constexpr uint32_t kWeaponCheckEveryMs = 2000;
        constexpr uint32_t kMaxWeaponGifts = 2;
        constexpr uint32_t kMaxGiftsPerCopy = 10;
        constexpr size_t kMaxTakeBacks = 64;
        constexpr uint32_t kSitCheckEveryMs = 250;
        constexpr uint32_t kStandAfterMs = 1000;
        constexpr uint32_t kSwitchOnAfterMs = 3000;
        constexpr uint32_t kSwitchOnHeardMs = 1000;
        constexpr uint32_t kSwitchOnEveryMs = 3000;
        constexpr uint32_t kMaxSwitchOns = 5;
        constexpr uint32_t kHoldLookEveryMs = 250;

        double MsSince(const LARGE_INTEGER& start)
        {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            return 1000.0 * static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
        }

        float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        bool SafeToRevive() { return !InLoadingScreen() && !InMenuMode(); }

        uint32_t RolledAs(TESObjectREFR* actor)
        {
            BaseInfo b = DescribeBase(actor);
            bool actorBase = b.leveledActorType == layout::kFormTypeNpc || b.leveledActorType == layout::kFormTypeCreature;
            return actorBase && !net::IsMadeWhilePlaying(b.leveledActor) ? b.leveledActor : 0;
        }
    }

    void Npcs::Share(coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        if (coop.Remotes().empty())
        {
            m_watched.clear();
            return;
        }
        if (!coop::IsDue(now, m_nextShare))
            return;
        m_nextShare = now + coop::kPoseIntervalMs;
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        for (const net::NpcPile& pile : coop.TakePiles())
        {
            TESObjectREFR* actor = LookupRef(pile.ref);
            Watched& w = m_watched[pile.ref];
            if (!actor || !IsActor(actor) || !IsDead(actor) || w.pile)
                continue;
            MakePile(actor, pile.kind);
            w.pile = pile.kind;
            if (!w.diedAt)
                w.diedAt = now;
            LOG_DEBUG("NPCs: %s (%08X) turned to %s in a friend's game: here too", Name(actor), pile.ref,
                pile.kind == net::kPileGoo ? "goo" : "ash");
        }

        if (m_shareOnly)
        {
            m_nearby.clear();
            for (uint32_t id : m_shareList)
                if (TESObjectREFR* a = LookupRef(id))
                    m_nearby.push_back(a);
        }
        else
            NearbyActors(m_nearby);
        std::vector<coop::Pose> players{myPose};
        for (const auto& [id, remote] : coop.Remotes())
            if (coop::Pose p; remote.motion.Sample(now, p) && p.space == myPose.space)
                players.push_back(p);
        auto nearestPlayer = [&players](const coop::Pose& at) {
            float nearest = std::hypot(at.x - players[0].x, at.y - players[0].y);
            for (const coop::Pose& p : players)
                nearest = (std::min)(nearest, std::hypot(at.x - p.x, at.y - p.y));
            return nearest;
        };
        std::vector<std::pair<std::pair<bool, float>, net::NpcState>> found;
        std::map<uint32_t, Watched> watched;
        uint32_t notShared = 0, elsewhere = 0;
        LARGE_INTEGER actorStart = start;
        TESObjectREFR *timed = nullptr, *slowest = nullptr;
        double slowestMs = 0;
        auto timeNext = [&](TESObjectREFR* next) {
            if (timed && MsSince(actorStart) > slowestMs)
            {
                slowestMs = MsSince(actorStart);
                slowest = timed;
            }
            QueryPerformanceCounter(&actorStart);
            timed = next;
        };
        const uint32_t grabbed = GrabbedItem();
        for (TESObjectREFR* actor : m_nearby)
        {
            timeNext(actor);
            uint32_t ref = RefId(actor);
            coop::Pose at;
            uint32_t base = 0;
            const bool needsBase = net::IsMadeWhilePlaying(ref) || m_shareOnly;
            if (needsBase)
            {
                BaseInfo b = DescribeBase(actor);
                base = b.leveledBase ? b.leveledBase : b.base;
            }
            if (actor == Player() || standIns.IsStandIn(actor) || !net::IsSharedRef(ref) || !IsEnabled(actor) ||
                (needsBase && (!base || net::IsMadeWhilePlaying(base))))
            {
                ++notShared;
                continue;
            }
            if (!GetPose(actor, at) || at.space != myPose.space)
            {
                ++elsewhere;
                continue;
            }
            bool dead = IsDead(actor), down = !dead && IsKnockedOut(actor);
            Watched w;
            if (auto it = m_watched.find(ref); it != m_watched.end())
                w = it->second;
            uint8_t flags = net::kNpcDead;
            if (dead && grabbed == ref)
                flags |= net::kNpcHeld;
            if (!dead)
            {
                int sitting = 0;
                Stance(actor, flags, sitting);
                if (down)
                    flags |= net::kNpcDown;
                if (sitting)
                    flags |= net::kNpcSitting;
            }
            bool weaponOut = !dead && !down && (flags & coop::Pose::kWeaponOut);
            int shots = -1;
            if (!dead && !down && (weaponOut || coop::IsDue(now, w.nextGunCheck)))
            {
                if (weaponOut && !w.weaponWasOut)
                    w.gun.rounds = -1;
                shots = CountShots(actor, w.gun);
                w.isGun = shots >= 0;
                w.nextGunCheck = now + kGunCheckEveryMs;
            }
            else if (!dead && !down)
                shots = w.isGun ? 0 : -1;
            w.weaponWasOut = weaponOut;
            if (dead)
            {
                if (coop::IsDue(now, w.nextBodyLook))
                {
                    w.nextBodyLook = now + coop::kBodyShareEveryMs;
                    w.bodyX = at.x, w.bodyY = at.y, w.bodyZ = at.z, w.bodyHeading = at.heading;
                    BodyPosition(actor, w.bodyX, w.bodyY, w.bodyZ);
                }
                at.x = w.bodyX, at.y = w.bodyY, at.z = w.bodyZ, at.heading = w.bodyHeading;
            }
            else
                w.nextBodyLook = 0;
            float maxHealth = MaxHealth(actor);
            net::NpcState n{ref, at.x, at.y, at.z, at.heading, flags, dead ? uint16_t(0) : MovementFlags(actor),
                dead ? 0.0f : Clamp(std::round(Health(actor)), -net::kMaxHealth, net::kMaxHealth),
                maxHealth > 0 ? Clamp(maxHealth, 1, net::kMaxHealth) : 1, dead ? 0 : PlayedIdle(actor),
                dead ? 0 : w.gun.weapon};
            if ((n.idle >> 24) == 0xFF)
                n.idle = 0;
            if ((n.weapon >> 24) == 0xFF)
                n.weapon = 0;
            n.base = base;
            n.rolled = RolledAs(actor);
            if (dead)
            {
                if (coop::IsDue(now, w.nextLimbCheck))
                {
                    w.limbs = LimbsGone(actor);
                    w.nextLimbCheck = now + 1000;
                }
                n.limbs = w.limbs;
                if (!w.diedAt)
                    w.diedAt = now;
                if (!w.pile && now - w.diedAt < 4000)
                    w.pile = PileKind(actor);
                n.pile = w.pile;
                if (now - w.diedAt < kDropsSettleMs && MoveDroppedIntoBody(actor))
                {
                    if (OpenContainer() == actor)
                        RefreshItemList();
                    RefreshLootWindows();
                }
            }
            else
                w.diedAt = 0, w.pile = 0;
            found.push_back({{dead, nearestPlayer(at)}, n});
            if (dead)
            {
                watched[ref] = w;
                continue;
            }
            if (coop::IsDue(now, w.nextGearAt))
            {
                w.nextGearAt = now + kGearEveryMs;
                coop.SetNpcGear(ref, WornGear(actor));
            }

            int action = AnimAction(actor);
            bool started = IsAttackAction(action) && w.action != action;
            bool reloads = IsReloadAction(action) && w.action != action;
            bool released = IsThrowRelease(action) && w.action != action;
            uint32_t gunShots = shots >= 0 ? w.shotCounter.Look(started, IsAttackPhase(action),
                static_cast<uint32_t>(shots), reloads) : 0;
            uint8_t count = shots < 0 ? (started ? 1 : 0) : static_cast<uint8_t>(gunShots < 255 ? gunShots : 255);
            w.action = action;
            watched[ref] = w;
            if (count || reloads || released)
            {
                coop.NpcAttacked(ref, count, reloads, released);
                m_attacks += count;
                m_reloads += reloads ? 1 : 0;
            }
        }
        timeNext(nullptr);
        m_watched.swap(watched);
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<net::NpcState> npcs;
        for (size_t i = 0; i < found.size() && i < coop::kMaxNpcs; ++i)
            npcs.push_back(found[i].second);
        if (found.empty() != m_foundNone && !m_shareOnly)
        {
            m_foundNone = found.empty();
            LOG_INFO("NPCs: %s near us (%zu actors: %u players, switched off or ones a friend can't make, %u in "
                "another place; we're in %08X)", m_foundNone ? "no one to share" : "sharing again", m_nearby.size(),
                notShared, elsewhere, myPose.space);
        }
        coop.SetNpcs(myPose.space, std::move(npcs));

        double ms = MsSince(start);
        if (ms > 20 && slowest)
            LOG_INFO("NPCs: a look took %.1f ms; the slowest was %08X (%s), %.1f ms", ms, RefId(slowest), Name(slowest),
                slowestMs);
        m_shareMs += ms;
        ++m_shares;
        LogStats(now, found.size());
    }

    void Npcs::CheckDesyncs(coop::Coop& coop, uint32_t now, const coop::Pose& myPose)
    {
        if (coop.GetRole() != coop::Role::Friend || !coop.InGame() || !coop::IsDue(now, m_nextDesyncCheck))
            return;
        m_nextDesyncCheck = now + 1000;
        if (coop::IsDue(now, m_nextDesyncLog))
        {
            if (m_nextDesyncLog)
                LOG_DEBUG("Desync checks: %u looks at NPCs, %u disagreeing (the last 30 s)", m_desyncLooks, m_desyncSeen);
            m_nextDesyncLog = now + 30000;
            m_desyncLooks = m_desyncSeen = 0;
        }
        auto saw = [&](coop::Disagreement d) {
            ++m_desyncSeen;
            coop.SawDesync(d);
        };
        const float checkDistance = ShowDistance() - 1000;
        constexpr float kPlaceOff = 400;
        for (const auto& [ref, npc] : coop.Npcs())
        {
            coop::Pose host;
            if (now - npc.heardAt > 2000 || !npc.motion.Sample(now, host) || host.space != myPose.space ||
                std::hypot(host.x - myPose.x, host.y - myPose.y) > checkDistance)
                continue;
            ++m_desyncLooks;
            TESObjectREFR* ours = Find(ref);
            if (!ours)
            {
                if (!npc.dead)
                    saw({coop::DesyncKind::Npc, ref, coop::DesyncAspect::There, "not here"});
                continue;
            }
            if (!IsEnabled(ours))
            {
                saw({coop::DesyncKind::Npc, ref, coop::DesyncAspect::There, "switched off here"});
                continue;
            }
            bool dead = IsDead(ours);
            if (dead != npc.dead)
            {
                saw({coop::DesyncKind::Npc, ref, coop::DesyncAspect::Life,
                    dead ? "dead here, alive in the host's game" : "alive here, dead in the host's game"});
                continue;
            }
            if (dead)
                continue;
            coop::Pose at;
            if (GetPose(ours, at))
            {
                float off = std::hypot(at.x - host.x, at.y - host.y);
                if (off > kPlaceOff)
                    saw({coop::DesyncKind::Npc, ref, coop::DesyncAspect::Place,
                        std::to_string(static_cast<int>(off)) + " units from the host's"});
            }
            float health = Health(ours);
            float allowed = npc.maxHealth * 0.25f > 10 ? npc.maxHealth * 0.25f : 10;
            if (npc.maxHealth > 0 && std::fabs(health - npc.health) > allowed)
                saw({coop::DesyncKind::Npc, ref, coop::DesyncAspect::Health, "health " +
                    std::to_string(static_cast<int>(health)) + " here, " + std::to_string(static_cast<int>(npc.health)) +
                    " in the host's game"});
        }
    }

    void Npcs::Update(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        UpdateCopies(coop, now, myPose, standIns);
        HoldUnshared(coop, now, myPose, standIns);
        m_updateMs += MsSince(start);
        ++m_updates;
        LogStats(now, coop.Npcs().size());
    }

    void Npcs::HoldUnshared(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        if (coop.GetRole() != coop::Role::Friend || !coop.InGame())
        {
            if (m_holds.Count())
                ReleaseHolds();
            return;
        }
        if (coop::IsDue(now, m_nextHoldLook))
        {
            m_nextHoldLook = now + kHoldLookEveryMs;
            NearbyActors(m_nearby);
            // Our own companions are never held, whatever the host's Visitors' Companions setting: they follow us
            // through loads and doors, and holding one in place while that happens crashes the game.
            std::vector<uint32_t> team = Teammates();
            std::vector<coop::LocalActor> nearby;
            nearby.reserve(m_nearby.size());
            for (TESObjectREFR* actor : m_nearby)
            {
                coop::LocalActor a;
                a.ref = RefId(actor);
                a.playerOrStandIn = actor == Player() || standIns.IsStandIn(actor);
                a.ourCopy = m_entries.count(a.ref) || (net::IsMadeWhilePlaying(a.ref) && HostRefOf(a.ref) != 0);
                a.sharedByHost = coop.Npcs().count(a.ref) != 0;
                a.ourCompanion = std::find(team.begin(), team.end(), a.ref) != team.end();
                a.enabled = IsEnabled(actor);
                a.alive = !IsDead(actor);
                a.loaded = Is3DLoaded(actor);
                coop::Pose at;
                a.samePlace = GetPose(actor, at) && at.space == myPose.space;
                nearby.push_back(a);
            }
            coop::Holds::Changes changes = m_holds.Look(nearby, coop.Npcs().size() >= coop::kMaxNpcs);
            for (const coop::Holds::Released& r : changes.release)
                LetGoOfHold(r, coop.Npcs().count(r.ref) || m_entries.count(r.ref));
            for (uint32_t ref : changes.start)
            {
                TESObjectREFR* actor = LookupRef(ref);
                coop::Pose spot;
                if (!actor || !GetPose(actor, spot))
                    continue;
                spot.flags = 0;
                spot.moves = 0;
                Held& h = m_heldActors[ref];
                h.spot = spot;
                char who[96];
                snprintf(who, sizeof(who), "%s (%08X, held)", Name(actor), ref);
                h.puppet.Start(who, kPuppetStatsEveryMs);
                SetCombatOff(actor, true);
                m_holds.NoteGhost(ref, IsGhost(actor));
                SetGhost(actor, true);
                ++m_holdsStarted;
                LOG_INFO("NPCs: %s (%08X) is near us but the host's game doesn't share it (not running it there); "
                    "held still", Name(actor), ref);
            }
        }
        for (auto& [ref, h] : m_heldActors)
        {
            TESObjectREFR* actor = LookupRef(ref);
            coop::Pose at;
            if (coop.Npcs().count(ref) || m_entries.count(ref))
                continue;
            if (actor && IsActor(actor) && Is3DLoaded(actor) && GetPose(actor, at) && at.space == h.spot.space)
                h.puppet.Update(actor, h.spot, now);
        }
    }

    void Npcs::LetGoOfHold(const coop::Holds::Released& held, bool nowOurCopy)
    {
        uint32_t ref = held.ref;
        auto it = m_heldActors.find(ref);
        if (it == m_heldActors.end())
            return;
        TESObjectREFR* actor = LookupRef(ref);
        if (actor && !IsActor(actor))
            actor = nullptr;
        if (actor)
            SetGhost(actor, held.wasGhost);
        if (nowOurCopy)
            LOG_INFO("NPCs: %08X, held, is shared by the host now", ref);
        else if (actor)
        {
            it->second.puppet.Release(actor);
            SetCombatOff(actor, false);
            LOG_DEBUG("NPCs: %08X, held, is let go (gone from near us)", ref);
        }
        m_heldActors.erase(it);
    }

    void Npcs::ReleaseHolds()
    {
        for (const coop::Holds::Released& held : m_holds.ReleaseAll())
            LetGoOfHold(held, false);
        m_heldActors.clear();
    }

    void Npcs::UpdateCopies(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        const auto& shared = m_copyFrom ? *m_copyFrom : coop.Npcs();
        TakeBackWhenClosed();
        StepRevives(coop, now);
        m_bodiesPulled = 0;

        for (auto it = m_made.begin(); it != m_made.end();)
        {
            uint32_t ref = it->first;
            ++it;
            if (!shared.count(ref) && !(Find(ref) && Find(ref) == OpenContainer()))
                Unmake(ref);
        }
        for (auto it = m_rerolled.begin(); it != m_rerolled.end();)
        {
            if (shared.count(*it) || m_made.count(*it))
            {
                ++it;
                continue;
            }
            if (TESObjectREFR* own = LookupRef(*it))
                SetEnabled(own, true);
            it = m_rerolled.erase(it);
        }
        auto stillHere = [&](uint32_t ref) {
            TESObjectREFR* a = Find(ref);
            coop::Pose at;
            return a && IsActor(a) && Is3DLoaded(a) && GetPose(a, at) && at.space == myPose.space;
        };
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            uint32_t ref = it->first;
            ++it;
            if (!shared.count(ref))
            {
                Release(ref);
                m_nextHoldLook = 0;
            }
        }
        for (auto it = m_killed.begin(); it != m_killed.end();)
            it = shared.count(it->first) ? std::next(it) : m_killed.erase(it);
        for (auto it = m_nextRevive.begin(); it != m_nextRevive.end();)
            it = shared.count(it->first) ? std::next(it) : m_nextRevive.erase(it);
        for (auto it = m_bodies.begin(); it != m_bodies.end();)
            it = shared.count(it->first) ? std::next(it) : m_bodies.erase(it);
        for (auto it = m_switchedOff.begin(); it != m_switchedOff.end();)
            it = shared.count(it->first) ? std::next(it) : m_switchedOff.erase(it);
        for (auto it = m_putDown.begin(); it != m_putDown.end();)
            it = shared.count(it->first) ? std::next(it) : m_putDown.erase(it);

        const float showDistance = ShowDistance();
        for (auto& [ref, npc] : shared)
        {
            coop::Pose pose;
            float reach = m_entries.count(ref) || m_made.count(ref) ? showDistance + kShowMargin : showDistance;
            bool here = npc.motion.Sample(now, pose) && pose.space == myPose.space &&
                std::hypot(pose.x - myPose.x, pose.y - myPose.y) <= reach;
            bool made = OursIsMade(ref);
            TESObjectREFR* actor = here ? Find(ref) : nullptr;
            if (actor && (!IsActor(actor) || actor == Player() || standIns.IsStandIn(actor)))
                actor = nullptr;
            if (actor && !made && coop::CompareRolls(npc.rolled, RolledAs(actor)) == coop::RollMatch::Different)
            {
                LOG_INFO("NPCs: %s (%08X) is %08X here but %08X in the host's game (each game rolls its own); "
                    "switched ours off, making a copy of the host's", Name(actor), ref, RolledAs(actor), npc.rolled);
                Release(ref);
                m_revive.Drop(ref);
                SetEnabled(actor, false);
                m_rerolled.insert(ref);
                m_killed.erase(ref);
                m_nextRevive.erase(ref);
                m_switchedOff.erase(ref);
                m_localDeaths.erase(ref);
                m_piled.erase(ref);
                made = true;
                actor = nullptr;
            }
            if (made && !here)
            {
                if (m_made.count(ref))
                    Unmake(ref);
                continue;
            }
            if (made && !actor && !npc.dead)
                actor = Make(ref, npc, pose, now);
            auto it = m_entries.find(ref);
            if (!actor)
            {
                if (it != m_entries.end() && !stillHere(ref))
                    Release(ref);
                continue;
            }

            if (!IsEnabled(actor))
            {
                if (m_revive.Busy(ref))
                    continue;
                SwitchedOff& off = m_switchedOff[ref];
                if (!off.since)
                    off.since = now;
                else if (now - off.since >= kSwitchOnAfterMs && now - npc.heardAt <= kSwitchOnHeardMs &&
                    off.tries < kMaxSwitchOns && coop::IsDue(now, off.nextTry))
                {
                    off.nextTry = now + kSwitchOnEveryMs;
                    ++off.tries;
                    SetEnabled(actor, true);
                    LOG_INFO("NPCs: %s (%08X) was switched off here, not in the host's game; switched it on%s",
                        Name(actor), ref, off.tries == kMaxSwitchOns ? " (the last try: something keeps switching it "
                        "off here)" : "");
                }
                continue;
            }
            if (auto off = m_switchedOff.find(ref); off != m_switchedOff.end())
                off->second.since = 0;

            if (IsDead(actor))
            {
                uint32_t& diedAt = m_localDeaths[ref];
                if (!diedAt)
                    diedAt = now;
                if (!m_piled.count(ref) && now - diedAt < 4000)
                    if (uint8_t kind = PileKind(actor))
                    {
                        m_piled.insert(ref);
                        if (npc.pile != kind)
                            coop.ReportPile(ref, kind);
                        LOG_DEBUG("NPCs: our copy of %08X turned to %s here", ref, kind == net::kPileGoo ? "goo" : "ash");
                    }
            }
            else if (m_localDeaths.erase(ref))
                m_piled.erase(ref);

            if (npc.dead)
            {
                coop::CopyKill& kill = m_killed[ref];
                int lifeBefore = LifeState(actor);
                bool aiRunning = AiThreadsRunning();
                static bool s_saidFlag = false;
                if (!s_saidFlag)
                {
                    s_saidFlag = true;
                    LOG_INFO("Frame: the AI threads' flag read %d in our frame code (thread %lu)", aiRunning,
                        GetCurrentThreadId());
                }
                switch (aiRunning ? coop::CopyKill::Do::Nothing : kill.Step(now, IsDead(actor)))
                {
                case coop::CopyKill::Do::Unprotect:
                    Unprotect(actor);
                    if (kill.Tries() > 1)
                        LOG_INFO("NPCs: our copy of %s (%08X) didn't stay dead (life state %d); killing it again (try %d "
                            "of %d)", Name(actor), ref, lifeBefore, kill.Tries(), coop::CopyKill::kMaxTries);
                    break;
                case coop::CopyKill::Do::Kill:
                    KillPlain(actor);
                    ++m_kills;
                    LOG_DEBUG("NPCs: %s (%08X) died on the host; killed our copy (life state %d -> %d; limbs gone there "
                        "%04X, not cut here)", Name(actor), ref, lifeBefore, LifeState(actor), npc.limbs);
                    break;
                case coop::CopyKill::Do::GiveUp:
                    LOG_WARN("NPCs: our copy of %s (%08X) doesn't stay dead here (life state %d after %d kills); left "
                        "alone", Name(actor), ref, lifeBefore, coop::CopyKill::kMaxTries);
                    break;
                default:
                    if (aiRunning && !m_saidAiRunning)
                    {
                        m_saidAiRunning = true;
                        LOG_WARN("NPCs: the game's AI threads were running when we came to kill %08X; waiting", ref);
                    }
                    break;
                }
                if (it != m_entries.end())
                    Release(ref);
                if (npc.pile && IsDead(actor) && !m_piled.count(ref))
                {
                    MakePile(actor, npc.pile);
                    m_piled.insert(ref);
                    LOG_DEBUG("NPCs: %s (%08X) turned to %s on the host: ours too", Name(actor), ref,
                        npc.pile == net::kPileGoo ? "goo" : "ash");
                }
                PlaceBody(ref, actor, npc, pose, now);
                continue;
            }
            m_killed.erase(ref);
            m_bodies.erase(ref);
            if (!IsDead(actor) && !IsKnockedOut(actor))
                m_nextRevive.erase(ref);
            // Never while we hold it still as one the host doesn't share, or while its body is open on our screen.
            if (!m_revive.Busy(ref) && !m_holds.IsHeld(ref))
                ApplyRevive(ref, actor, m_revive.Look(ref, now, IsDead(actor), true, Is3DLoaded(actor),
                    SafeToRevive() && actor != OpenContainer()), npc);
            if (IsDead(actor) || m_revive.Busy(ref))
                continue;
            // FNV Delirium runs the host's hit reaction on our copy: its body is Delirium's until that ends.
            bool reacting = delirium::IsReacting(RefId(actor));
            if (npc.down)
            {
                // Knocked out on the host (an essential NPC at 0 health, or a script): ours goes down too, once; the
                // wake-up below brings it back when the host's gets up.
                // Tried again if ours gets up on its own while the host's is still down (a few times).
                PutDown& down = m_putDown[ref];
                if (!reacting && !IsKnockedOut(actor) && down.tries < kMaxKnockTries && coop::IsDue(now, down.nextTry))
                {
                    if (it != m_entries.end())
                    {
                        it->second.puppet.Release(actor);
                        it->second.letGo = true;
                    }
                    KnockedHow how = KnockDown(actor);
                    ++down.tries;
                    down.nextTry = now + kKnockEveryMs;
                    ++m_knockedDown;
                    LOG_INFO("NPCs: %s (%08X) is knocked out on the host; knocked ours down (%s, try %u, life state %d)",
                        Name(actor), ref, how == KnockedHow::Essential ? "essential: health below 0" :
                        how == KnockedHow::Unconscious ? "unconscious, pushed over" : "couldn't", down.tries,
                        LifeState(actor));
                }
                // Health is left alone while ours is down (raising it would stand an essential one up early).
                if (it != m_entries.end() && !IsKnockedOut(actor))
                    MatchHealth(actor, npc, it->second, now, true);
                continue;
            }
            bool wePutItDown = m_putDown.erase(ref) != 0;
            if (wePutItDown && IsKnockedOut(actor))
                m_nextRevive[ref] = now + kWakeSoonMs;
            if (IsKnockedOut(actor) && reacting)
                continue;
            if (IsKnockedOut(actor))
            {
                auto due = m_nextRevive.find(ref);
                if (due == m_nextRevive.end())
                    m_nextRevive[ref] = now + kWakeAfterMs;
                else if (coop::IsDue(now, due->second))
                {
                    LOG_INFO("NPCs: %s (%08X) is knocked out here but up on the host; waking it", Name(actor), ref);
                    m_nextRevive[ref] = now + kWakeEveryMs;
                    ++m_deadHere;
                    // An essential one went down with health below 0: up again with the host's health, or it falls
                    // straight back down.
                    if (Health(actor) < 1)
                        ChangeHealth(actor, (npc.health > 1 ? npc.health : 1) - Health(actor));
                    WakeUp(actor);
                    if (it != m_entries.end())
                        it->second.hostHealth = -1;
                }
                continue;
            }

            if (it == m_entries.end())
            {
                if (m_entries.size() >= coop::kMaxNpcs)
                {
                    if (!m_warnedFull)
                        LOG_WARN("NPCs: copying %zu already; the rest are left to their own AI", coop::kMaxNpcs);
                    m_warnedFull = true;
                    continue;
                }
                it = m_entries.emplace(ref, Entry{}).first;
                char who[96];
                snprintf(who, sizeof(who), "%s (%08X)", Name(actor), ref);
                it->second.puppet.Start(who, kPuppetStatsEveryMs);
                if (s_labSteps & 8)
                    SetCombatOff(actor, true);
                LOG_DEBUG("NPCs: copying %s", who);
            }
            Entry& e = it->second;

            coop::Pose current;
            if (!GetPose(actor, current) || current.space != pose.space ||
                std::hypot(current.x - pose.x, current.y - pose.y) > coop::kTeleportDistance)
            {
                if (!reacting && coop::IsDue(now, e.nextBringAt))
                {
                    e.nextBringAt = now + kBringEveryMs;
                    BringToPlayer(actor);
                    Move(actor, pose);
                    ++m_brought;
                }
                continue;
            }
            if (!Is3DLoaded(actor))
                continue;
            if (!reacting && coop::IsDue(now, e.nextSitCheck))
            {
                e.nextSitCheck = now + kSitCheckEveryMs;
                uint8_t flags;
                int sitting;
                Stance(actor, flags, sitting);
                bool sittingDown = sitting == 1 || sitting == 2;
                if (npc.sitting || sitting < 1 || sitting > 4)
                    e.sittingSince = 0;
                else if (!e.sittingSince && !sittingDown)
                    e.sittingSince = now;
                else if (sittingDown || now - e.sittingSince >= kStandAfterMs)
                {
                    LOG_DEBUG("NPCs: %08X %s here but not on the host; moving it out of its seat", ref,
                        sittingDown ? "sits down" : "sits");
                    e.sittingSince = 0;
                    BringToPlayer(actor);
                    Move(actor, pose);
                    ++m_stoodUp;
                    continue;
                }
            }
            if (reacting)
            {
                if (!e.letGo)
                {
                    e.letGo = true;
                    e.puppet.Release(actor);
                    LOG_DEBUG("NPCs: our copy of %08X reacts to a hit (FNV Delirium); not steered until it ends", ref);
                }
            }
            else
            {
                if (e.letGo)
                {
                    e.letGo = false;
                    e.puppet.BlendUntil(now + coop::kBlendAfterReactionMs);
                    coop::Pose here;
                    float off = GetPose(actor, here) ? std::hypot(here.x - pose.x, here.y - pose.y) : -1;
                    LOG_DEBUG("NPCs: our copy of %08X is done reacting, %.0f units from the host's; steered again (slid "
                        "over, not snapped)", ref, off);
                }
                e.puppet.Update(actor, pose, now);
            }

            MatchHealth(actor, npc, e, now, false);
            bool settled = true;
            if (made)
            {
                auto m = m_made.find(ref);
                if (m != m_made.end())
                {
                    if (!m->second.loadedAt)
                        m->second.loadedAt = now;
                    settled = now - m->second.loadedAt >= kSettleMs;
                }
            }
            if (settled && npc.gearChanges && coop::IsDue(now, e.nextGearCheck))
            {
                e.nextGearCheck = now + kGearEveryMs;
                if (e.gearChanges != npc.gearChanges)
                {
                    e.gearChanges = npc.gearChanges;
                    e.dressings = 0;
                }
                std::vector<net::WornItem> worn = WornGear(actor);
                if (worn != npc.gear && e.dressings < kMaxDressings)
                {
                    ++e.dressings;
                    size_t takenOut = 0, putOn = 0;
                    if (s_labSteps & 2)
                        for (const net::WornItem& h : npc.gear)
                            if (std::find(worn.begin(), worn.end(), h) == worn.end() && Wear(actor, h))
                                ++putOn;
                    if (s_labSteps & 1)
                        for (const net::WornItem& w : worn)
                            if (std::none_of(npc.gear.begin(), npc.gear.end(),
                                [&](const net::WornItem& h) { return h.item == w.item; }))
                            {
                                RemoveItem(actor, w.item);
                                ++takenOut;
                            }
                    ++m_dressed;
                    LOG_DEBUG("NPCs: dressed our copy of %08X like the host's (%zu items; put on %zu, took out %zu)",
                        ref, npc.gear.size(), putOn, takenOut);
                }
                else if (worn != npc.gear && e.dressings == kMaxDressings)
                {
                    ++e.dressings;
                    LOG_INFO("NPCs: our copy of %08X still wears something else than the host's after %u tries", ref,
                        kMaxDressings);
                }
            }
            if (npc.weapon != e.weapon)
                e.weaponGifts = 0;
            if (settled && (npc.weapon != e.weapon || coop::IsDue(now, e.nextWeaponCheck)))
            {
                e.weapon = npc.weapon;
                e.nextWeaponCheck = now + kWeaponCheckEveryMs;
                if (npc.weapon && e.weaponGifts < kMaxWeaponGifts && e.allGifts < kMaxGiftsPerCopy &&
                    (s_labSteps & 4) && !(HeldWeaponId(actor) == npc.weapon && HoldsHarmless(actor)) &&
                    AddAndEquip(actor, npc.weapon, &e.rounds))
                {
                    ++e.weaponGifts;
                    if (++e.allGifts == kMaxGiftsPerCopy)
                        LOG_WARN("NPCs: gave %08X %u weapons; no more (the host keeps changing them?)", ref,
                            kMaxGiftsPerCopy);
                    if (e.gaveWeapon && e.gaveWeapon != npc.weapon)
                        RemoveItem(actor, e.gaveWeapon);
                    e.gaveWeapon = npc.weapon;
                    ++m_weapons;
                    LOG_DEBUG("NPCs: %08X now holds %08X", ref, npc.weapon);
                }
            }
            if (npc.idle != e.idle && !reacting)
            {
                e.idle = npc.idle;
                if (npc.idle && !(pose.flags & coop::Pose::kWeaponOut) && PlayIdle(actor, npc.idle))
                {
                    ++m_idles;
                    LOG_DEBUG("NPCs: %08X plays idle %08X", ref, npc.idle);
                }
            }
        }
    }

    void Npcs::StepRevives(const coop::Coop& coop, uint32_t now)
    {
        const auto& shared = coop.Npcs();
        for (uint32_t ref : m_revive.Bodies())
        {
            auto npc = shared.find(ref);
            TESObjectREFR* actor = Find(ref);
            bool isActor = actor && IsActor(actor);
            if (npc == shared.end() || npc->second.dead || !isActor)
            {
                if (m_revive.Drop(ref) && isActor)
                {
                    SwitchBody(actor, true);
                    LOG_INFO("NPCs: %s (%08X) was on its way back, but the host no longer has it alive; switched it "
                        "back on as it was", Name(actor), ref);
                }
                continue;
            }
            if (m_revive.Busy(ref))
                ApplyRevive(ref, actor, m_revive.Look(ref, now, IsDead(actor), IsEnabled(actor), Is3DLoaded(actor),
                    SafeToRevive() && actor != OpenContainer()), npc->second);
        }
    }

    void Npcs::ApplyRevive(uint32_t ref, TESObjectREFR* actor, const coop::ReviveSteps::Step& step,
        const coop::RemoteNpc& npc)
    {
        using Steps = coop::ReviveSteps;
        switch (step.news)
        {
        case Steps::News::None:
            break;
        case Steps::News::Waiting:
            LOG_INFO("NPCs: %s (%08X) is dead here but alive on the host; waiting for the menu or loading screen to "
                "end before bringing it back", Name(actor), ref);
            break;
        case Steps::News::Started:
            LOG_INFO("NPCs: %s (%08X) is dead here but alive on the host (health %.0f / %.0f there); switching its "
                "body off to bring it back (try %u)", Name(actor), ref, npc.health, npc.maxHealth, step.tries);
            break;
        case Steps::News::Revived:
            ++m_deadHere;
            LOG_INFO("NPCs: %s (%08X) is alive again; switching it back on (try %u)", Name(actor), ref, step.tries);
            break;
        case Steps::News::BodyStayed:
            LOG_WARN("NPCs: %s (%08X): its body wouldn't unload (%s), so it wasn't brought back; left as it was, trying "
                "again later (try %u of %u)", Name(actor), ref, IsEnabled(actor) ? "it never switched off" :
                "switched off, 3D still there", step.tries, coop::kMaxReviveTries);
            break;
        case Steps::News::StillDead:
            LOG_WARN("NPCs: %s (%08X) stayed dead after being brought back; trying again later (try %u of %u)",
                Name(actor), ref, step.tries, coop::kMaxReviveTries);
            break;
        case Steps::News::GaveUp:
            LOG_WARN("NPCs: gave up bringing back %s (%08X) after %u tries; it stays dead here while alive on the host",
                Name(actor), ref, step.tries);
            break;
        }
        switch (step.action)
        {
        case Steps::Do::Nothing:
            break;
        case Steps::Do::SwitchOff:
            SwitchBody(actor, false);
            break;
        case Steps::Do::Resurrect:
            if (Resurrect(actor))
                if (auto entry = m_entries.find(ref); entry != m_entries.end())
                    entry->second.hostHealth = -1;
            break;
        case Steps::Do::SwitchOn:
            SwitchBody(actor, true);
            break;
        }
    }

    void Npcs::FollowBody(uint32_t ref, TESObjectREFR* actor, const coop::RemoteNpc& npc, Body& b, uint32_t now)
    {
        if (!s_smoothBodies)
            return;
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        coop::Pose blended, at;
        if (!npc.motion.Sample(now - coop::kBodyBlendMs, blended))
            return;
        coop::BodySpot target{blended.x, blended.y, blended.z}, ours;
        bool following = b.follower.Look(now, target, npc.held);
        // The host's copy moving right after we told it where the body is, is only it doing as we said. Taken for the
        // host moving it, it would stop our telling mid-flight and pull ours back to where theirs had got to.
        if (b.reporting || !coop::IsDue(now, b.echoUntil))
            following = false;
        if (following != b.following)
        {
            b.following = following;
            if (following)
            {
                b.followFrames = 0;
                b.followOffSum = b.followOffMost = 0;
                b.followMs = 0;
                LOG_INFO("NPCs: the host moves the body of %s (%08X); following it", Name(actor), ref);
            }
            else
                LOG_INFO("NPCs: stopped following the body of %s (%08X): %u frames, off by %.0f on average (most %.0f) "
                    "units, %.3f ms a frame", Name(actor), ref, b.followFrames,
                    b.followFrames ? b.followOffSum / b.followFrames : 0, b.followOffMost,
                    b.followFrames ? b.followMs / b.followFrames : 0.0);
        }
        if (following)
            b.easer.Finish();
        if (coop::IsDue(now, b.nextReport))
        {
            b.nextReport = now + coop::kBodyShareEveryMs;
            coop::BodySpot mine;
            coop::Pose where;
            if (!InMenu() && GetPose(actor, where) && where.space == blended.space &&
                BodyPosition(actor, mine.x, mine.y, mine.z))
            {
                bool held = GrabbedItem() == RefId(actor);
                bool reporting = b.reporter.Look(now, mine, held, following || b.easing || b.judgeAt != 0);
                if (reporting != b.reporting)
                {
                    b.reporting = reporting;
                    LOG_INFO("NPCs: %s the host about the body of %s (%08X), which %s", reporting ? "telling" : "done telling",
                        Name(actor), ref, reporting ? (held ? "our player carries" : "moved here") : "stopped");
                }
                if (reporting)
                {
                    b.echoUntil = now + coop::kBodyEchoMs;
                    m_bodyReports.push_back({ref, mine.x, mine.y, mine.z});
                }
                if (coop::IsDue(now, b.nextLimbLook))
                {
                    // A blast of ours can tear it apart in our game alone.
                    b.nextLimbLook = now + 1000;
                    uint16_t ours = LimbsGone(actor) & ~npc.limbs & ~b.toldLimbs;
                    if (ours)
                    {
                        b.toldLimbs |= ours;
                        m_bodyLimbs.push_back({ref, ours});
                        LOG_INFO("NPCs: the body of %s (%08X) lost limbs %04X here; telling the host", Name(actor), ref, ours);
                    }
                }
            }
        }
        if ((!following && b.easer.Done() && !b.easing) || !coop::IsDue(now, b.nextPullTry) ||
            m_bodiesPulled >= coop::kMaxBodiesFollowed || InMenu() || !GetPose(actor, at) ||
            at.space != blended.space || !BodyPosition(actor, ours.x, ours.y, ours.z) ||
            GrabbedItem() == RefId(actor))
            return;
        float off = ours.DistanceTo(target);
        bool easing = b.easer.Look(now, target, ours);
        if (easing != b.easing)
        {
            b.easing = easing;
            if (easing)
                LOG_INFO("NPCs: easing the body of %s (%08X) %.0f units to the host's spot", Name(actor), ref, off);
            else
                LOG_INFO("NPCs: finished easing the body of %s (%08X): %.0f units from the host's", Name(actor), ref, off);
        }
        if (!following && !easing)
            return;
        ++m_bodiesPulled;
        ++b.followFrames;
        b.followOffSum += off;
        b.followOffMost = (std::max)(b.followOffMost, off);
        coop::BodySpot speed;
        if (coop::BodyPull(target, ours, speed))
        {
            std::string detail;
            if (PullBody(actor, speed.x, speed.y, speed.z, detail))
            {
                if (!b.saidPull)
                    LOG_INFO("NPCs: steering the body of %s (%08X) by its %s", Name(actor), ref, detail.c_str());
                b.saidPull = true;
                b.nextPullTry = 0;
            }
            else
            {
                if (!b.saidNoPull)
                    LOG_INFO("NPCs: can't steer the body of %s (%08X): %s; it moves when it stops instead", Name(actor),
                        ref, detail.c_str());
                b.saidNoPull = true;
                b.nextPullTry = now + 5000;
            }
        }
        b.followMs += MsSince(start);
    }

    void Npcs::FollowFriendsBodies(coop::Coop& coop, uint32_t now)
    {
        for (const net::BodyLimbs& m : coop.TakeBodyLimbs())
        {
            TESObjectREFR* actor = LookupRef(m.ref);
            if (!actor || !IsActor(actor) || !IsDead(actor) || !Is3DLoaded(actor))
                continue;
            uint16_t left = m.limbs & ~LimbsGone(actor);
            if (!left)
                continue;
            Kill(actor, left);
            LOG_INFO("NPCs: the body of %s (%08X) lost limbs %04X in a friend's game; taking them off here", Name(actor),
                m.ref, left);
        }
        std::vector<net::BodyMove> moves = coop.TakeBodyMoves();
        if (!s_smoothBodies)
            return;
        for (const net::BodyMove& m : moves)
        {
            if (!m_friendsBodies.count(m.ref))
                if (TESObjectREFR* actor = LookupRef(m.ref))
                    LOG_INFO("NPCs: a friend moves the body of %s (%08X); following it", Name(actor), m.ref);
            m_friendsBodies[m.ref].Heard(now, {m.x, m.y, m.z});
        }
        uint32_t pulled = 0;
        for (auto it = m_friendsBodies.begin(); it != m_friendsBodies.end();)
        {
            uint32_t ref = it->first;
            TESObjectREFR* actor = LookupRef(ref);
            if (it->second.Done(now) || !actor || !IsActor(actor) || !IsDead(actor))
            {
                if (actor)
                    LOG_INFO("NPCs: a friend stopped moving the body of %s (%08X)", Name(actor), ref);
                it = m_friendsBodies.erase(it);
                continue;
            }
            coop::BodySpot target = it->second.Target(now), ours, speed;
            ++it;
            // If our own player has hold of it, ours wins, and the friend's copy comes back to it.
            if (pulled >= coop::kMaxBodiesFollowed || InMenu() || !Is3DLoaded(actor) || GrabbedItem() == ref ||
                !BodyPosition(actor, ours.x, ours.y, ours.z) || !coop::BodyPull(target, ours, speed))
                continue;
            ++pulled;
            std::string detail;
            if (!PullBody(actor, speed.x, speed.y, speed.z, detail))
                LOG_DEBUG("NPCs: can't steer the body of %s (%08X) for a friend: %s", Name(actor), ref, detail.c_str());
        }
    }

    void Npcs::PlaceBody(uint32_t ref, TESObjectREFR* actor, const coop::RemoteNpc& npc, const coop::Pose& host,
        uint32_t now)
    {
        // Ash and goo piles are their own objects where the body was: moving the actor wouldn't move them.
        if (!IsDead(actor) || npc.pile || m_piled.count(ref) || now - npc.heardAt > 2000)
            return;
        Body& b = m_bodies[ref];
        FollowBody(ref, actor, npc, b, now);
        if (!coop::IsDue(now, b.nextLook))
            return;
        b.nextLook = now + coop::kBodyLookEveryMs;
        coop::BodySpot ours, theirs{host.x, host.y, host.z};
        coop::Pose at;
        if (!BodyPosition(actor, ours.x, ours.y, ours.z) || !GetPose(actor, at) || at.space != host.space)
            return;
        float off = ours.DistanceTo(theirs);
        if (b.judgeAt && coop::IsDue(now, b.judgeAt))
        {
            b.judgeAt = 0;
            LOG_INFO("NPCs: the body of %s (%08X) is now %.0f units from the host's (was %.0f; ours at %.0f %.0f %.0f, "
                "its reference at %.0f %.0f %.0f)", Name(actor), ref, off, b.offBefore, ours.x, ours.y, ours.z, at.x,
                at.y, at.z);
        }
        if (!b.placer.Look(now, theirs, ours))
        {
            if (b.placer.GaveUp() && !b.saidGaveUp)
            {
                b.saidGaveUp = true;
                LOG_INFO("NPCs: left the body of %s (%08X) %.0f units from the host's after %u moves", Name(actor), ref,
                    off, b.placer.Moves());
            }
            else if (!b.placer.GaveUp())
                b.saidGaveUp = false;
            return;
        }
        if (InMenu() || GrabbedItem() == RefId(actor))
            return;
        // The first try just moves it. If the bones stayed behind (a live ragdoll can pull them back), load its 3D again.
        bool reload = b.placer.Moves() > 0;
        LOG_INFO("NPCs: moving the body of %s (%08X) %.0f units to the host's spot%s", Name(actor), ref, off,
            reload ? ", loading its 3D again" : "");
        MoveBody(actor, theirs.x - ours.x, theirs.y - ours.y, theirs.z - ours.z, reload);
        b.placer.Moved(now);
        b.offBefore = off;
        b.judgeAt = now + 2000;
        ++m_bodiesMoved;
    }

    void Npcs::MatchHealth(TESObjectREFR* actor, const coop::RemoteNpc& npc, Entry& e, uint32_t now, bool down)
    {
        // Down on the host: never below 1 here, so a copy that isn't essential here isn't killed by it.
        float want = down && npc.health < 1 ? 1 : npc.health;
        bool changed = npc.health != e.hostHealth;
        bool drifted = false;
        if (coop::IsDue(now, e.nextHealthCheck))
        {
            e.nextHealthCheck = now + kHealthCheckEveryMs;
            drifted = coop::CopyHealthDrifted(Health(actor), want);
        }
        if (!changed && !drifted)
            return;
        float max = MaxHealth(actor);
        if (std::fabs(max - npc.maxHealth) > 0.5f)
        {
            if (e.ownMaxHealth == 0)
                e.ownMaxHealth = max;
            SetMaxHealth(actor, npc.maxHealth);
        }
        float change = want - Health(actor);
        if (std::fabs(change) >= 0.5f)
        {
            ChangeHealth(actor, change);
            ++m_healthSets;
            if (!changed)
                ++m_healthFixes;
        }
        e.hostHealth = npc.health;
    }

    void Npcs::Attack(uint32_t ref, uint8_t shots, bool reload, bool release)
    {
        TESObjectREFR* actor = m_entries.count(ref) ? Find(ref) : nullptr;
        if (OursIsMade(ref))
        {
            auto m = m_made.find(ref);
            if (m == m_made.end() || !m->second.loadedAt || GetTickCount() - m->second.loadedAt < kSettleMs)
                actor = nullptr;
        }
        if (!actor || !IsActor(actor) || IsDead(actor) || !Is3DLoaded(actor))
        {
            ++m_attacksNotShown;
            return;
        }
        if (!(StanceFlags(actor) & coop::Pose::kWeaponOut))
        {
            ++m_attacksNotShown;
            if (InScriptAnimation(actor))
                StopIdle(actor);
            SetAlert(actor, true);
            SetWeaponOut(actor, true);
            LOG_DEBUG("NPCs: %08X didn't attack (%u shots): its weapon isn't out; drawing it", ref, shots);
            return;
        }
        if (InScriptAnimation(actor))
            StopIdle(actor);
        if (shots)
        {
            PlayNpcAttack(actor, shots);
            ++m_attacksShown;
            LOG_DEBUG("NPCs: %08X attacks (%u shots)", ref, shots);
        }
        if (reload)
        {
            if (Reload(actor, 1))
                ++m_reloadsShown;
            LOG_DEBUG("NPCs: %08X reloads", ref);
        }
        if (release)
        {
            ReleaseNpcThrow(actor);
            LOG_DEBUG("NPCs: %08X's throw left its hand", ref);
        }
    }

    void Npcs::Release(uint32_t ref)
    {
        auto it = m_entries.find(ref);
        if (it == m_entries.end())
            return;
        if (TESObjectREFR* actor = Find(ref); actor && IsActor(actor))
        {
            it->second.puppet.Release(actor);
            SetAlert(actor, false);
            SetCombatOff(actor, false);
            if (it->second.ownMaxHealth > 0)
                SetMaxHealth(actor, it->second.ownMaxHealth);
            TakeBack gifts{RefId(actor), it->second.gaveWeapon, std::move(it->second.rounds)};
            if (gifts.weapon || !gifts.rounds.Empty())
            {
                if (actor != OpenContainer())
                    TakeBackGifts(actor, gifts);
                else if (m_takeBacks.size() < kMaxTakeBacks)
                {
                    m_takeBacks.push_back(std::move(gifts));
                    LOG_DEBUG("NPCs: %08X's body is open on our screen: what we gave it is taken back once it closes",
                        ref);
                }
            }
        }
        LOG_DEBUG("NPCs: %08X is back to its own AI", ref);
        m_entries.erase(it);
    }

    bool Npcs::OursIsMade(uint32_t ref) const
    {
        return net::IsMadeWhilePlaying(ref) || m_rerolled.count(ref) || KeptApart(ref);
    }

    TESObjectREFR* Npcs::Find(uint32_t ref) const
    {
        if (!OursIsMade(ref))
            return LookupRef(ref);
        auto it = m_made.find(ref);
        return it == m_made.end() ? nullptr : LookupRef(it->second.localId);
    }

    TESObjectREFR* Npcs::Make(uint32_t ref, const coop::RemoteNpc& npc, const coop::Pose& pose, uint32_t now)
    {
        if (m_made.count(ref))
        {
            LOG_INFO("NPCs: our copy of %08X is gone; making another", ref);
            m_made.erase(ref);
            m_entries.erase(ref);
        }
        if (m_made.size() >= coop::kMaxNpcs || !coop::IsDue(now, m_nextMakeAt))
            return nullptr;
        m_nextMakeAt = now + kMakeEveryMs;
        uint32_t from = coop::MakeCopyFrom(npc.base, npc.rolled);
        // A companion of our own save: the host doesn't say what its one of the same ref is made from.
        if (!from)
            if (TESObjectREFR* real = LookupRef(ref))
            {
                BaseInfo b = DescribeBase(real);
                from = b.leveledBase ? b.leveledBase : b.base;
            }
        TESObjectREFR* actor = from ? PlaceActor(from) : nullptr;
        if (!actor)
        {
            LOG_WARN("NPCs: couldn't make a copy of the host's %08X from %08X (not an NPC or creature here: mods "
                "differ?)", ref, from);
            m_nextMakeAt = now + 5000;
            return nullptr;
        }
        Move(actor, pose);
        if (m_copyFrom)
            SideWithPlayer(actor);
        Made m;
        m.localId = RefId(actor);
        m_made[ref] = m;
        ++m_madeCount;
        LOG_DEBUG("NPCs: made %08X %s, our copy of the host's %08X (from %08X)", RefId(actor), Name(actor), ref,
            from);
        return actor;
    }

    void Npcs::TakeBackGifts(TESObjectREFR* actor, TakeBack& gifts)
    {
        if (gifts.weapon)
            RemoveItem(actor, gifts.weapon);
        int32_t rounds = 0;
        for (const auto& [ammo, count] : gifts.rounds.TakeBack(
                 [&](uint32_t id) { TESForm* f = LookupForm(id); return f ? ItemCount(actor, f) : 0; }))
        {
            ChangeItem(actor, ammo, -count);
            rounds += count;
        }
        if (rounds)
            LOG_DEBUG("NPCs: took back the %d rounds we gave %08X", rounds, RefId(actor));
    }

    void Npcs::TakeBackWhenClosed()
    {
        if (m_takeBacks.empty())
            return;
        TESObjectREFR* open = OpenContainer();
        for (auto it = m_takeBacks.begin(); it != m_takeBacks.end();)
        {
            TESObjectREFR* actor = LookupRef(it->ourRef);
            if (actor && actor == open)
            {
                ++it;
                continue;
            }
            if (actor && IsActor(actor))
                TakeBackGifts(actor, *it);
            it = m_takeBacks.erase(it);
        }
    }

    void Npcs::Unmake(uint32_t ref)
    {
        TESObjectREFR* actor = Find(ref);
        Release(ref);
        if (actor)
            Remove(actor);
        m_made.erase(ref);
        m_killed.erase(ref);
        m_nextRevive.erase(ref);
        m_bodies.erase(ref);
        m_localDeaths.erase(ref);
        m_piled.erase(ref);
        ++m_unmadeCount;
        LOG_DEBUG("NPCs: deleted our copy of %08X", ref);
    }

    std::vector<std::pair<uint32_t, bool>> Npcs::DeadBodies(uint32_t now) const
    {
        std::vector<std::pair<uint32_t, bool>> bodies;
        for (const auto& [ref, w] : m_watched)
            if (w.diedAt)
                bodies.emplace_back(ref, now - w.diedAt < kDropsSettleMs);
        return bodies;
    }

    uint32_t Npcs::HostRefOf(uint32_t ourRef) const
    {
        if (!net::IsMadeWhilePlaying(ourRef))
            return ourRef;
        for (auto& [hostRef, m] : m_made)
            if (m.localId == ourRef)
                return hostRef;
        return 0;
    }

    std::vector<uint32_t> Npcs::MadeRefIds() const
    {
        std::vector<uint32_t> ids;
        for (auto& [_, m] : m_made)
            ids.push_back(m.localId);
        return ids;
    }

    void Npcs::ShareOnly(std::vector<uint32_t> refs)
    {
        m_shareOnly = true;
        m_shareList = std::move(refs);
    }

    void Npcs::UpdateFrom(const std::map<uint32_t, coop::RemoteNpc>& list, const coop::Coop& coop, uint32_t now,
        const coop::Pose& myPose, const StandIns& standIns)
    {
        m_copyFrom = &list;
        UpdateCopies(coop, now, myPose, standIns);
    }

    void Npcs::SetKeptApart(const std::set<uint32_t>& refs)
    {
        if (refs == m_keptApart)
            return;
        for (uint32_t ref : m_keptApart)
            if (!refs.count(ref) && m_made.count(ref))
                Unmake(ref);
        for (uint32_t ref : refs)
            if (!m_keptApart.count(ref))
            {
                // Until now our real one may have been the host's copy.
                Release(ref);
                m_killed.erase(ref);
                m_nextRevive.erase(ref);
                m_switchedOff.erase(ref);
            }
        m_keptApart = refs;
    }

    bool Npcs::IsCopy(uint32_t ourRef) const
    {
        if (net::IsMadeWhilePlaying(ourRef))
        {
            for (auto& [hostRef, m] : m_made)
                if (m.localId == ourRef)
                    return true;
            return false;
        }
        return !KeptApart(ourRef) && m_entries.count(ourRef) != 0;
    }

    void Npcs::ReleaseAll()
    {
        ReleaseHolds();
        for (uint32_t ref : m_revive.TakeSwitchedOff())
            if (TESObjectREFR* actor = Find(ref); actor && IsActor(actor))
            {
                SwitchBody(actor, true);
                LOG_DEBUG("NPCs: co-op's over with %08X on its way back; switched it back on as it was", ref);
            }
        while (!m_made.empty())
            Unmake(m_made.begin()->first);
        while (!m_entries.empty())
            Release(m_entries.begin()->first);
        for (uint32_t ref : m_rerolled)
            if (TESObjectREFR* own = LookupRef(ref))
                SetEnabled(own, true);
        m_rerolled.clear();
        m_killed.clear();
        m_nextRevive.clear();
        m_bodies.clear();
        m_switchedOff.clear();
    }

    void Npcs::Forget()
    {
        m_holds.ReleaseAll();
        m_heldActors.clear();
        m_entries.clear();
        m_made.clear();
        m_rerolled.clear();
        m_keptApart.clear();
        m_killed.clear();
        m_nextRevive.clear();
        m_revive.Clear();
        m_bodies.clear();
        m_switchedOff.clear();
        for (auto it = m_localDeaths.begin(); it != m_localDeaths.end();)
            it = net::IsMadeWhilePlaying(it->first) ? m_localDeaths.erase(it) : std::next(it);
        for (auto it = m_piled.begin(); it != m_piled.end();)
            it = net::IsMadeWhilePlaying(*it) ? m_piled.erase(it) : std::next(it);
        m_watched.clear();
    }

    void Npcs::LogStats(uint32_t now, size_t shared)
    {
        if (m_shareOnly || m_copyFrom || !coop::IsDue(now, m_nextStats))
            return;
        bool first = m_nextStats == 0;
        m_nextStats = now + kStatsEveryMs;
        if (!first && m_shares)
            LOG_INFO("NPCs: %zu near us; looking for them took %.2f ms each time on average (%u times); shared %u "
                "attacks (shots and swings) and %u reloads", shared, m_shareMs / m_shares, m_shares, m_attacks,
                m_reloads);
        else if (!first)
            LOG_INFO("NPCs: copying %zu of the %zu the host shares; brought %u over from elsewhere; showed %u attacks "
                "(%u not shown) and %u reloads; killed %u; matched health %u times; played %u idles; gave %u "
                "weapons; stood up %u; moved %u bodies; brought back %u that died or fell here but not on the host (in "
                "30 s); took %.2f ms a frame on average", m_entries.size(), shared, m_brought, m_attacksShown,
                m_attacksNotShown, m_reloadsShown, m_kills, m_healthSets, m_idles, m_weapons, m_stoodUp, m_bodiesMoved,
                m_deadHere,
                m_updates ? m_updateMs / m_updates : 0.0);
        if (!first && (m_madeCount || m_unmadeCount || !m_made.empty()))
            LOG_INFO("NPCs: %zu copies we made of ones the host's game made while playing or rolled differently "
                "(%zu of ours switched off for that); made %u, deleted %u, dressed %u times (in 30 s)", m_made.size(),
                m_rerolled.size(), m_madeCount, m_unmadeCount, m_dressed);
        if (!first && (m_knockedDown || m_healthFixes))
            LOG_INFO("NPCs: knocked out %u copies with the host's; put %u copies hurt only here back to the host's "
                "health (in 30 s)", m_knockedDown, m_healthFixes);
        m_knockedDown = m_healthFixes = 0;
        if (!first && (m_holdsStarted || !m_heldActors.empty()))
            LOG_INFO("NPCs: holding %zu still that the host's game doesn't share (%u newly held in 30 s)",
                m_heldActors.size(), m_holdsStarted);
        m_holdsStarted = 0;
        m_madeCount = m_unmadeCount = m_dressed = 0;
        m_shareMs = m_updateMs = 0;
        m_shares = m_updates = m_brought = m_reloads = 0;
        m_attacks = m_attacksShown = m_attacksNotShown = m_kills = m_healthSets = m_idles = 0;
        m_deadHere = m_weapons = m_stoodUp = m_reloadsShown = m_bodiesMoved = 0;
    }
}
