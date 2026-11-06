// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/visitor_companions.h"

#include "coop/clock.h"
#include "coop/visitor_companions.h"
#include "game/standins.h"
#include "log.h"

#include <algorithm>
#include <map>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 1000;
    }

    void VisitorCompanions::Update(coop::Coop& coop, const StandIns& standIns, Npcs& npcs, const coop::Pose& me,
        uint32_t now, bool bothKeep)
    {
        if (coop.GetRole() == coop::Role::Host)
        {
            coop.SetCompanionsBoth(bothKeep);
            m_running = true;
            UpdateHost(coop, standIns, me, now);
        }
        else if (coop.GetRole() == coop::Role::Friend && coop.InGame())
        {
            m_running = true;
            UpdateVisitor(coop, standIns, npcs, me, now);
        }
        else if (m_running)
            Stop(npcs);
    }

    void VisitorCompanions::UpdateVisitor(coop::Coop& coop, const StandIns& standIns, Npcs& npcs, const coop::Pose& me,
        uint32_t now)
    {
        for (const net::CompanionHurt& hurt : coop.TakeCompanionHurts())
            if (m_own.count(hurt.ref))
                if (TESObjectREFR* real = LookupRef(hurt.ref); real && !IsDead(real))
                {
                    ChangeHealth(real, -hurt.damage);
                    LOG_DEBUG("Companions: %s (%08X) took %.1f on the host", Name(real), hurt.ref, hurt.damage);
                }

        if (coop::IsDue(now, m_nextLook))
        {
            m_nextLook = now + kLookMs;
            const bool both = coop.CompanionsBoth();
            std::set<uint32_t> own;
            std::vector<uint32_t> toShare;
            for (uint32_t id : Teammates())
            {
                TESObjectREFR* ref = LookupRef(id);
                if (!ref || !net::IsSharedRef(id) || standIns.IsStandIn(ref))
                    continue;
                own.insert(id);
                uint8_t leader = 0;
                if (coop::ShareCompanionWithHost(both, coop.CompanionLeader(id, leader), coop.Npcs().count(id) != 0))
                    toShare.push_back(id);
            }
            if (own != m_own)
            {
                for (uint32_t id : own)
                    if (!m_own.count(id))
                        LOG_INFO("Companions: %s (%08X) is with us; the host's game gets to see it", Name(LookupRef(id)), id);
                m_own = own;
            }
            npcs.SetKeptApart(both ? own : std::set<uint32_t>{});
            m_mine.ShareOnly(std::move(toShare));
            std::vector<uint32_t> watch(own.begin(), own.end());
            if (watch != m_watching)
            {
                WatchHits(watch);
                m_watching = std::move(watch);
            }
        }
        m_mine.Share(coop, now, me, standIns);
    }

    void VisitorCompanions::UpdateHost(coop::Coop& coop, const StandIns& standIns, const coop::Pose& me, uint32_t now)
    {
        const coop::CompanionCopies& copies = coop.Copies();
        m_copies.UpdateFrom(copies.Npcs(), coop, now, me, standIns);
        for (const net::NpcAttack& attack : coop.TakeCompanionAttacks())
            m_copies.Attack(attack.ref, attack.shots, attack.reload, attack.release);

        // Every frame: a new copy mustn't reach its owner as an NPC to copy, even once.
        std::map<uint32_t, uint8_t> owners;
        std::vector<uint32_t> watch;
        for (const auto& [key, npc] : copies.Npcs())
            if (TESObjectREFR* copy = m_copies.OurRefOf(key))
            {
                owners[RefId(copy)] = coop::CompanionCopyOwner(key);
                watch.push_back(RefId(copy));
            }
        coop.SetCopyOwners(std::move(owners));
        std::sort(watch.begin(), watch.end());
        if (watch != m_watching)
        {
            WatchHits(watch);
            m_watching = std::move(watch);
        }
    }

    bool VisitorCompanions::TookHit(const Hit& hit, coop::Coop& coop, const StandIns& standIns, Npcs& npcs)
    {
        if (m_watching.empty() || !coop.InGame())
            return false;
        TESObjectREFR* target = LookupRef(hit.targetRef);
        TESObjectREFR* attacker = LookupRef(hit.attackerRef);
        coop::CompanionHitFacts f;
        f.host = coop.GetRole() == coop::Role::Host;
        uint8_t owner = 0;
        if (f.host)
        {
            f.targetIsCopy = coop.CopyOwner(hit.targetRef, owner);
            uint8_t attackerOwner = 0;
            f.attackerIsCopy = coop.CopyOwner(hit.attackerRef, attackerOwner);
            f.attackerIsFriendly = !attacker || attacker == Player() || standIns.IsStandIn(attacker) ||
                f.attackerIsCopy || IsTeammate(attacker);
        }
        else
        {
            // With only one of a companion showing, ours may be the host's copy: then it's like any copy.
            f.targetIsOurCompanion = m_own.count(hit.targetRef) != 0 && !npcs.IsCopy(hit.targetRef);
            f.attackerIsOurCompanion = m_own.count(hit.attackerRef) != 0 && !npcs.IsCopy(hit.attackerRef);
            f.attackerIsCopyOrStandIn = standIns.IsStandIn(attacker) || npcs.IsCopy(hit.attackerRef);
            f.targetIsStandIn = standIns.IsStandIn(target);
        }

        switch (coop::RouteCompanionHit(f))
        {
        case coop::CompanionHit::NotOurs:
            return false;
        case coop::CompanionHit::Done:
            return true;
        case coop::CompanionHit::Report:
            if (uint32_t hostRef = npcs.HostRefOf(hit.targetRef))
            {
                LOG_DEBUG("Companions: %08X hit %08X for %.1f; telling the host", hit.attackerRef, hostRef, hit.damage);
                coop.ReportHit(hostRef, hit.damage);
            }
            return true;
        case coop::CompanionHit::UndoAndTellOwner:
            if (uint32_t real = coop.Copies().RealRefOf(m_copies.HostRefOf(hit.targetRef)))
                coop.HurtCompanion(owner, real, hit.damage);
            [[fallthrough]];
        case coop::CompanionHit::Undo:
            ChangeHealth(target, hit.damage);
            return true;
        }
        return false;
    }

    void VisitorCompanions::Stop(Npcs& npcs)
    {
        m_running = false;
        m_copies.ReleaseAll();
        npcs.SetKeptApart({});
        m_mine.ShareOnly({});
        m_own.clear();
        m_watching.clear();
        WatchHits({});
    }

    void VisitorCompanions::Forget()
    {
        m_copies.Forget();
        m_mine.Forget();
        m_own.clear();
        m_watching.clear();
        WatchHits({});
    }
}
