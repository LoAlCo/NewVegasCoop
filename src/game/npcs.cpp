// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/npcs.h"
#include "game/far_interior.h"
#include "game/far_cells.h"

#include "coop/copy_kill.h"
#include "coop/levelled_roll.h"
#include "coop/npc_share.h"

#include "coop/clock.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/internal.h"
#include "game/standins.h"
#include "log.h"

#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kStatsEveryMs = 30000;
        constexpr uint32_t kPuppetStatsEveryMs = 30000;
        float ShowDistance() { return LoadedReach() - 192; }
        constexpr float kShowMargin = 800;
        constexpr uint32_t kBringEveryMs = 1000;
        constexpr uint32_t kBodyRereadMs = 2000;
        constexpr uint32_t kGearEveryMs = 2000;
        constexpr uint32_t kMaxDressings = 3;
        constexpr uint32_t kGunCheckEveryMs = 1000;
        constexpr uint32_t kMakeEveryMs = 250;
        constexpr uint32_t kSettleMs = 1500;
        constexpr uint32_t kReviveAfterMs = 2000;
        constexpr uint32_t kReviveEveryMs = 3000;
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

        uint32_t RolledAs(TESObjectREFR* actor)
        {
            BaseInfo b = DescribeBase(actor);
            bool actorBase = b.leveledActorType == layout::kFormTypeNpc || b.leveledActorType == layout::kFormTypeCreature;
            return actorBase && !net::IsMadeWhilePlaying(b.leveledActor) ? b.leveledActor : 0;
        }
    }

    void Npcs::Share(coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        AnswerSpotAsks(coop);
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

        NearbyActors(m_nearby);
        std::vector<coop::Pose> players{myPose};
        for (const auto& [id, remote] : coop.Remotes())
            if (coop::Pose p; remote.motion.Sample(now, p) && (p.space == myPose.space || InRunPlace(p.space, p.x, p.y)))
                players.push_back(p);
        auto nearestPlayer = [&players](const coop::Pose& at) {
            float nearest = 3.0e38f;
            for (const coop::Pose& p : players)
                if (p.space == at.space)
                    nearest = (std::min)(nearest, std::hypot(at.x - p.x, at.y - p.y));
            return nearest;
        };
        std::vector<std::pair<std::pair<bool, float>, std::pair<uint32_t, net::NpcState>>> found;
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
        for (TESObjectREFR* actor : m_nearby)
        {
            timeNext(actor);
            uint32_t ref = RefId(actor);
            coop::Pose at;
            uint32_t base = 0;
            if (net::IsMadeWhilePlaying(ref))
            {
                BaseInfo b = DescribeBase(actor);
                base = b.leveledBase ? b.leveledBase : b.base;
            }
            if (actor == Player() || standIns.IsStandIn(actor) || !net::IsSharedRef(ref) || !IsEnabled(actor) ||
                (net::IsMadeWhilePlaying(ref) && (!base || net::IsMadeWhilePlaying(base))))
            {
                ++notShared;
                continue;
            }
            if (!GetPose(actor, at) || (at.space != myPose.space && !InRunPlace(at.space, at.x, at.y)))
            {
                ++elsewhere;
                continue;
            }
            bool dead = IsDead(actor), down = !dead && IsKnockedOut(actor);
            Watched w;
            if (auto it = m_watched.find(ref); it != m_watched.end())
                w = it->second;
            uint8_t flags = net::kNpcDead;
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
                if (coop::IsDue(now, w.nextLootAt))
                {
                    w.nextLootAt = now + kBodyRereadMs + ref % 512;
                    std::vector<Item> items;
                    if (Contents(actor, items))
                    {
                        std::vector<net::ItemCount> list;
                        for (const Item& i : items)
                            list.push_back({i.id, i.count, i.health, i.mods});
                        coop.SetBodyContents(ref, std::move(list));
                    }
                }
            }
            else
                w.diedAt = 0, w.pile = 0, w.nextLootAt = 0;
            w.last = n;
            w.lastSpace = at.space;
            w.leftAt = 0;
            found.push_back({{dead, nearestPlayer(at)}, {at.space, n}});
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
        for (const auto& [ref, w] : m_watched)
        {
            if (watched.count(ref) || !w.lastSpace || (w.last.flags & net::kNpcDead))
                continue;
            uint32_t left = w.leftAt ? w.leftAt : now;
            TESObjectREFR* actor = now - left <= kLeftWordMs ? LookupRef(ref) : nullptr;
            if (!actor || !IsActor(actor) || IsDead(actor) || actor == Player() || standIns.IsStandIn(actor))
                continue;
            Watched& keep = watched[ref] = w;
            keep.leftAt = left;
            if (Is3DLoaded(actor))
                continue;
            net::NpcState n = w.last;
            n.x = actor->posX;
            n.y = actor->posY;
            n.z = actor->posZ;
            n.moves = 0;
            if (!w.leftSaid)
                LOG_DEBUG("NPCs: %s (%08X) left what we run, to %.0f, %.0f: its spot sent to friends for a moment",
                    Name(actor), ref, n.x, n.y);
            keep.leftSaid = true;
            found.push_back({{false, 0.0f}, {w.lastSpace, n}});
        }
        m_watched.swap(watched);
        std::vector<coop::NpcCandidate> candidates;
        for (const auto& f : found)
            candidates.push_back({f.second.first, f.first.first, f.first.second});
        std::vector<net::NpcState> npcs;
        std::vector<uint32_t> spaces;
        for (size_t i : coop::PickNpcs(candidates, coop::kMaxNpcs))
        {
            spaces.push_back(found[i].second.first);
            npcs.push_back(found[i].second.second);
        }
        if (found.size() > coop::kMaxNpcs && coop::IsDue(now, m_nextCapNote))
        {
            m_nextCapNote = now + 30000;
            std::map<uint32_t, std::pair<int, int>> byPlace;
            for (const auto& f : found)
                ++byPlace[f.second.first].first;
            for (uint32_t space : spaces)
                ++byPlace[space].second;
            std::string places;
            for (const auto& [space, n] : byPlace)
            {
                char part[64];
                snprintf(part, sizeof(part), "%s%08X %d of %d", places.empty() ? "" : ", ", space, n.second, n.first);
                places += part;
            }
            LOG_INFO("NPCs: %zu near the group, more than the %zu shared at once; each place gets its share (%s)",
                found.size(), coop::kMaxNpcs, places.c_str());
        }
        if (found.empty() != m_foundNone)
        {
            m_foundNone = found.empty();
            LOG_INFO("NPCs: %s near us (%zu actors: %u players, switched off or ones a friend can't make, %u in "
                "another place; we're in %08X)", m_foundNone ? "no one to share" : "sharing again", m_nearby.size(),
                notShared, elsewhere, myPose.space);
        }
        coop.SetNpcs(myPose.space, std::move(npcs), std::move(spaces));

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

    void Npcs::Update(coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
    {
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        UpdateCopies(coop, now, myPose, standIns);
        HoldUnshared(coop, now, myPose, standIns);
        m_updateMs += MsSince(start);
        ++m_updates;
        LogStats(now, coop.Npcs().size());
    }

    namespace
    {
        uint32_t PlaceAnywhere(const TESObjectREFR* ref)
        {
            const TESObjectCELL* cell = ref ? ref->parentCell : nullptr;
            if (!cell && ref)
            {
                const void* extra = ExtraOf(ref, kExtraData_PersistentCell);
                cell = extra ? *reinterpret_cast<const TESObjectCELL* const*>(static_cast<const char*>(extra) + 0xC)
                    : nullptr;
            }
            if (!cell)
                return 0;
            return cell->worldSpace ? cell->worldSpace->refID : cell->refID;
        }
    }

    void Npcs::AnswerSpotAsks(coop::Coop& coop)
    {
        for (const coop::Coop::SpotAskFrom& a : coop.TakeNpcSpotAsks())
        {
            std::vector<net::NpcSpot> spots;
            for (uint32_t ref : a.ask.refs)
            {
                net::NpcSpot s;
                s.ref = ref;
                TESObjectREFR* actor = LookupRef(ref);
                if (actor && IsActor(actor) && actor != Player() && (s.space = PlaceAnywhere(actor)) != 0)
                {
                    s.known = true;
                    s.x = actor->posX;
                    s.y = actor->posY;
                    s.z = actor->posZ;
                }
                spots.push_back(s);
            }
            coop.AnswerNpcSpots(a.playerId, spots);
        }
    }

    void Npcs::AskWhereHeldAre(coop::Coop& coop, uint32_t now, const coop::Pose& myPose)
    {
        for (const net::NpcSpot& s : coop.TakeNpcSpots())
        {
            auto h = m_heldActors.find(s.ref);
            TESObjectREFR* actor = h != m_heldActors.end() ? LookupRef(s.ref) : nullptr;
            if (!actor || !IsActor(actor) || !s.known)
                continue;
            coop::Pose there{s.space, s.x, s.y, s.z, 0};
            bool outOfView = s.space != myPose.space ||
                (!IsInteriorPlace(myPose.space) && !IsGridCell(static_cast<int>(std::floor(s.x / 4096.0f)),
                    static_cast<int>(std::floor(s.y / 4096.0f))));
            if (!outOfView)
                continue;
            h->second.spot = there;
            bool moved = s.space == myPose.space ? (Move(actor, there), true) : MoveIntoPlace(actor, there);
            LOG_INFO("NPCs: %s (%08X): the host's game has it out of our view (%.0f, %.0f in %08X)%s", Name(actor),
                s.ref, s.x, s.y, s.space, moved ? ": ours goes there too" : "; not a place here: held");
        }
        std::vector<uint32_t> ask;
        for (const auto& [ref, h] : m_heldActors)
        {
            auto asked = m_spotAskedAt.find(ref);
            if (net::IsMadeWhilePlaying(ref) || (asked != m_spotAskedAt.end() && now - asked->second < kSpotAskEveryMs))
                continue;
            if (ask.size() < net::kMaxNpcSpotAsks)
            {
                ask.push_back(ref);
                m_spotAskedAt[ref] = now;
            }
        }
        for (auto it = m_spotAskedAt.begin(); it != m_spotAskedAt.end();)
            it = m_heldActors.count(it->first) ? std::next(it) : m_spotAskedAt.erase(it);
        coop.AskNpcSpots(ask);
    }

    void Npcs::HoldUnshared(coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns)
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
            std::vector<coop::LocalActor> nearby;
            nearby.reserve(m_nearby.size());
            for (TESObjectREFR* actor : m_nearby)
            {
                coop::LocalActor a;
                a.ref = RefId(actor);
                a.playerOrStandIn = actor == Player() || standIns.IsStandIn(actor);
                a.ourCopy = m_entries.count(a.ref) || (net::IsMadeWhilePlaying(a.ref) && HostRefOf(a.ref) != 0);
                a.sharedByHost = coop.Npcs().count(a.ref) != 0;
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
            for (auto it = m_lastHostSpot.begin(); it != m_lastHostSpot.end();)
                it = now - it->second.at > kHostSpotMs ? m_lastHostSpot.erase(it) : std::next(it);
            for (uint32_t ref : changes.start)
            {
                TESObjectREFR* actor = LookupRef(ref);
                coop::Pose spot;
                if (!actor || !GetPose(actor, spot))
                    continue;
                if (auto last = m_lastHostSpot.find(ref); last != m_lastHostSpot.end() &&
                    last->second.pose.space == myPose.space && !IsInteriorPlace(myPose.space) &&
                    !IsGridCell(static_cast<int>(std::floor(last->second.pose.x / 4096.0f)),
                        static_cast<int>(std::floor(last->second.pose.y / 4096.0f))))
                {
                    LOG_DEBUG("NPCs: %s (%08X) walked out of our view in the host's game (to %.0f, %.0f): ours goes "
                        "there too, not held", Name(actor), ref, last->second.pose.x, last->second.pose.y);
                    Move(actor, last->second.pose);
                    m_lastHostSpot.erase(last);
                    continue;
                }
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
            AskWhereHeldAre(coop, now, myPose);
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
        const auto& shared = coop.Npcs();
        TakeBackWhenClosed();

        for (auto it = m_made.begin(); it != m_made.end();)
        {
            uint32_t ref = it->first;
            ++it;
            if (!shared.count(ref) && !InventoryBusy(Find(ref)))
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
        for (auto it = m_switchedOff.begin(); it != m_switchedOff.end();)
            it = shared.count(it->first) ? std::next(it) : m_switchedOff.erase(it);

        const float showDistance = ShowDistance();
        for (auto& [ref, npc] : shared)
        {
            coop::Pose pose;
            float reach = m_entries.count(ref) || m_made.count(ref) ? showDistance + kShowMargin : showDistance;
            bool sampled = npc.motion.Sample(now, pose);
            if (coop::Pose newestSpot; npc.motion.Newest(newestSpot))
                m_lastHostSpot[ref] = {newestSpot, now};
            bool here = sampled && pose.space == myPose.space &&
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
                else if (it != m_entries.end() && sampled && pose.space == myPose.space && !made)
                {
                    coop::Pose newest = pose;
                    npc.motion.Newest(newest);
                    if (TESObjectREFR* ours = Find(ref); ours && IsActor(ours) && newest.space == myPose.space)
                    {
                        LOG_DEBUG("NPCs: %s (%08X) went out of our view in the host's game (to %.0f, %.0f): ours too",
                            Name(ours), ref, newest.x, newest.y);
                        Release(ref);
                        Move(ours, newest);
                    }
                }
                continue;
            }

            if (!IsEnabled(actor))
            {
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
                bool pocketsOpen = !IsDead(actor) && InventoryBusy(actor);
                if (pocketsOpen)
                    CloseScreenForChange("They were killed in the host's game");
                switch (aiRunning || pocketsOpen ? coop::CopyKill::Do::Nothing : kill.Step(now, IsDead(actor)))
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
                continue;
            }
            m_killed.erase(ref);
            if (!IsDead(actor) && !IsKnockedOut(actor))
                m_nextRevive.erase(ref);
            if (IsDead(actor))
            {
                auto due = m_nextRevive.find(ref);
                if (due == m_nextRevive.end())
                    m_nextRevive[ref] = now + kReviveAfterMs;
                else if (coop::IsDue(now, due->second) && !InventoryBusy(actor))
                {
                    LOG_INFO("NPCs: %s (%08X) is dead here but alive on the host (health %.0f / %.0f there); bringing "
                        "it back", Name(actor), ref, npc.health, npc.maxHealth);
                    m_nextRevive[ref] = now + kReviveEveryMs;
                    ++m_deadHere;
                    Resurrect(actor);
                    if (it != m_entries.end())
                        it->second.hostHealth = -1;
                }
                continue;
            }
            if (npc.down)
                continue;
            if (IsKnockedOut(actor))
            {
                auto due = m_nextRevive.find(ref);
                if (due == m_nextRevive.end())
                    m_nextRevive[ref] = now + kReviveAfterMs;
                else if (coop::IsDue(now, due->second))
                {
                    LOG_INFO("NPCs: %s (%08X) is knocked out here but up on the host; waking it", Name(actor), ref);
                    m_nextRevive[ref] = now + kReviveEveryMs;
                    ++m_deadHere;
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
                if (coop::IsDue(now, e.nextBringAt))
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
            if (coop::IsDue(now, e.nextSitCheck))
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
            e.puppet.Update(actor, pose, now);

            if (npc.health != e.hostHealth)
            {
                float max = MaxHealth(actor);
                if (std::fabs(max - npc.maxHealth) > 0.5f)
                {
                    if (e.ownMaxHealth == 0)
                        e.ownMaxHealth = max;
                    SetMaxHealth(actor, npc.maxHealth);
                }
                float change = npc.health - Health(actor);
                if (std::fabs(change) >= 0.5f)
                {
                    ChangeHealth(actor, change);
                    ++m_healthSets;
                }
                e.hostHealth = npc.health;
            }
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
            bool pockets = InventoryBusy(actor);
            if (settled && !pockets && npc.gearChanges && coop::IsDue(now, e.nextGearCheck))
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
            if (settled && !pockets && (npc.weapon != e.weapon || coop::IsDue(now, e.nextWeaponCheck)))
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
            if (npc.idle != e.idle)
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
                if (!InventoryBusy(actor))
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

    bool Npcs::OursIsMade(uint32_t ref) const { return net::IsMadeWhilePlaying(ref) || m_rerolled.count(ref); }

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
        TESObjectREFR* actor = TakeParked(from);
        bool reused = actor != nullptr;
        if (!actor)
            actor = PlaceActor(from);
        if (!actor)
        {
            LOG_WARN("NPCs: couldn't make a copy of the host's %08X from %08X (not an NPC or creature here: mods "
                "differ?)", ref, from);
            m_nextMakeAt = now + 5000;
            return nullptr;
        }
        Move(actor, pose);
        Made m;
        m.localId = RefId(actor);
        m.from = from;
        m_made[ref] = m;
        ++m_madeCount;
        LOG_DEBUG("NPCs: %s %08X %s, our copy of the host's %08X (from %08X)", reused ? "reused parked" : "made",
            RefId(actor), Name(actor), ref, from);
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
        for (auto it = m_takeBacks.begin(); it != m_takeBacks.end();)
        {
            TESObjectREFR* actor = LookupRef(it->ourRef);
            if (InventoryBusy(actor))
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
        auto made = m_made.find(ref);
        uint32_t from = made != m_made.end() ? made->second.from : 0;
        Release(ref);
        if (actor)
            Park(actor, from);
        m_made.erase(ref);
        m_killed.erase(ref);
        m_nextRevive.erase(ref);
        m_localDeaths.erase(ref);
        m_piled.erase(ref);
        ++m_unmadeCount;
        LOG_DEBUG("NPCs: parked our copy of %08X", ref);
    }

    TESObjectREFR* Npcs::BodyOf(uint32_t hostRef, uint32_t now) const
    {
        if (m_entries.count(hostRef))
            return nullptr;
        TESObjectREFR* actor = Find(hostRef);
        if (!actor || !IsActor(actor) || !IsDead(actor))
            return nullptr;
        if (auto made = m_made.find(hostRef); made != m_made.end() &&
            (!made->second.loadedAt || now - made->second.loadedAt < kSettleMs))
            return nullptr;
        for (const TakeBack& gifts : m_takeBacks)
            if (gifts.ourRef == RefId(actor))
                return nullptr;
        return actor;
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

    void Npcs::ReleaseAll()
    {
        ReleaseHolds();
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
        m_switchedOff.clear();
    }

    void Npcs::Forget()
    {
        m_holds.ReleaseAll();
        m_heldActors.clear();
        m_entries.clear();
        m_made.clear();
        m_rerolled.clear();
        m_killed.clear();
        m_nextRevive.clear();
        m_switchedOff.clear();
        for (auto it = m_localDeaths.begin(); it != m_localDeaths.end();)
            it = net::IsMadeWhilePlaying(it->first) ? m_localDeaths.erase(it) : std::next(it);
        for (auto it = m_piled.begin(); it != m_piled.end();)
            it = net::IsMadeWhilePlaying(*it) ? m_piled.erase(it) : std::next(it);
        m_watched.clear();
    }

    void Npcs::LogStats(uint32_t now, size_t shared)
    {
        if (!coop::IsDue(now, m_nextStats))
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
                "weapons; stood up %u; brought back %u that died or fell here but not on the host (in 30 s); took "
                "%.2f ms a frame on average", m_entries.size(), shared, m_brought, m_attacksShown, m_attacksNotShown,
                m_reloadsShown, m_kills, m_healthSets, m_idles, m_weapons, m_stoodUp, m_deadHere,
                m_updates ? m_updateMs / m_updates : 0.0);
        if (!first && (m_madeCount || m_unmadeCount || !m_made.empty()))
            LOG_INFO("NPCs: %zu copies we made of ones the host's game made while playing or rolled differently "
                "(%zu of ours switched off for that); made %u, parked %u, dressed %u times (in 30 s); %zu parked now, "
                "%llu reused since the start", m_made.size(), m_rerolled.size(), m_madeCount, m_unmadeCount, m_dressed,
                std::get<0>(ParkedCounts()), std::get<2>(ParkedCounts()));
        if (!first && (m_holdsStarted || !m_heldActors.empty()))
            LOG_INFO("NPCs: holding %zu still that the host's game doesn't share (%u newly held in 30 s)",
                m_heldActors.size(), m_holdsStarted);
        m_holdsStarted = 0;
        m_madeCount = m_unmadeCount = m_dressed = 0;
        m_shareMs = m_updateMs = 0;
        m_shares = m_updates = m_brought = m_reloads = 0;
        m_attacks = m_attacksShown = m_attacksNotShown = m_kills = m_healthSets = m_idles = 0;
        m_deadHere = m_weapons = m_stoodUp = m_reloadsShown = 0;
    }
}
