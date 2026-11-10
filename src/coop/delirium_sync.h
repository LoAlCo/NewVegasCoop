// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

// FNV Delirium (a separate ragdoll hit-reaction plugin) <-> the co-op messages. No game code here, so the tests can
// check it. The plugin side (finding the API, calling it) is in game/delirium.cpp.

#include "coop/DeliriumApi.h"
#include "coop/smoothing.h"
#include "net/protocol.h"

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace coop
{
    // 30 a second while an NPC reacts (the NpcStates tick is 50 ms): the copy's fall stays closer to the owner's.
    constexpr uint32_t kDeliriumRootEveryMs = 33;
    // After a reaction, the copy is slid over to where the owner's stands instead of snapped there.
    constexpr uint32_t kBlendAfterReactionMs = 2000;
    constexpr float kBlendSpeed = 250;          // units a second
    constexpr float kBlendFarthest = 800;       // farther than this: snapped as before

    // One frame of sliding from at toward want (x, y; z and heading follow along). False: close enough already, or
    // too far to slide.
    inline bool BlendStep(const Pose& at, const Pose& want, float seconds, float closeEnough, Pose& out)
    {
        float dx = want.x - at.x, dy = want.y - at.y;
        float gap = std::sqrt(dx * dx + dy * dy);
        if (!(gap > closeEnough) || gap > kBlendFarthest || at.space != want.space || !(seconds > 0))
            return false;
        float step = kBlendSpeed * (seconds > 0.1f ? 0.1f : seconds);
        float k = step >= gap ? 1.0f : step / gap;
        out = at;
        out.x = at.x + dx * k;
        out.y = at.y + dy * k;
        out.z = at.z + (want.z - at.z) * k;
        out.heading = at.heading;
        return true;
    }

    inline net::DeliriumStart ToMessage(const DeliriumReaction& r, uint32_t sharedRef)
    {
        net::DeliriumStart m;
        m.ref = sharedRef;
        m.part = static_cast<int8_t>(r.part < net::kMinBodyPart || r.part > net::kMaxBodyPart ? -1 : r.part);
        for (int i = 0; i < 3; ++i)
        {
            m.impactLocal[i] = r.impactLocal[i];
            m.dir[i] = r.dir[i];
        }
        m.health = r.health;
        m.carried[0] = r.carried[0];
        m.carried[1] = r.carried[1];
        m.seed = r.seed;
        m.flags = static_cast<uint8_t>((r.fire ? net::kDeliriumFire : 0) | (r.again ? net::kDeliriumAgain : 0) |
            (r.death ? net::kDeliriumDeath : 0));
        return m;
    }

    inline DeliriumReaction ToReaction(const net::DeliriumStart& m, uint32_t localRef)
    {
        DeliriumReaction r{};
        r.refId = localRef;
        r.part = m.part;
        for (int i = 0; i < 3; ++i)
        {
            r.impactLocal[i] = m.impactLocal[i];
            r.dir[i] = m.dir[i];
        }
        r.health = m.health;
        r.carried[0] = m.carried[0];
        r.carried[1] = m.carried[1];
        r.seed = m.seed;
        r.fire = (m.flags & net::kDeliriumFire) ? 1 : 0;
        r.again = (m.flags & net::kDeliriumAgain) ? 1 : 0;
        r.death = (m.flags & net::kDeliriumDeath) ? 1 : 0;
        return r;
    }

    inline net::DeliriumResult ToMessage(const DeliriumEvent& ev, uint32_t sharedRef)
    {
        net::DeliriumResult m;
        m.ref = sharedRef;
        m.outcome = ev.outcome >= net::kDeliriumFell && ev.outcome <= net::kDeliriumEnded ?
            static_cast<uint8_t>(ev.outcome) : net::kDeliriumEnded;
        for (int i = 0; i < 3; ++i)
            m.pos[i] = ev.pos[i];
        m.heading = ev.heading;
        return m;
    }

    // What the shooter's Delirium saw of its own hit, for the Hit message. False: nothing worth sending.
    inline bool ToHitWhere(const DeliriumHit& h, net::HitWhere& out)
    {
        net::HitWhere w;
        w.part = static_cast<int8_t>(h.part < net::kMinBodyPart || h.part > net::kMaxBodyPart ? -1 : h.part);
        for (int i = 0; i < 3; ++i)
        {
            w.impact[i] = h.impact[i];
            w.dir[i] = h.dir[i];
        }
        w.fire = h.fire != 0;
        if (!net::IsGoodHitWhere(w))
        {
            float length = std::sqrt(w.dir[0] * w.dir[0] + w.dir[1] * w.dir[1] + w.dir[2] * w.dir[2]);
            if (!(length > 0.001f) || !std::isfinite(length))
                return false;
            for (float& v : w.dir)
                v /= length;
            if (!net::IsGoodHitWhere(w))
                return false;
        }
        out = w;
        return true;
    }

    // The hit the host gives its Delirium for a visitor's shot (the host applied it with plain damage).
    inline DeliriumHit ToDeliriumHit(uint32_t localRef, float damage, bool died, const net::HitWhere* where)
    {
        DeliriumHit h{};
        h.refId = localRef;
        h.part = where ? where->part : -1;
        for (int i = 0; i < 3; ++i)
        {
            h.impact[i] = where ? where->impact[i] : 0;
            h.dir[i] = where ? where->dir[i] : 0;
        }
        h.damage = damage;
        h.died = died ? 1 : 0;
        h.fire = where && where->fire ? 1 : 0;
        return h;
    }

    // The owner's side: which of its NPCs are reacting now, and when to send their roots.
    class DeliriumRootClock
    {
    public:
        void Started(uint32_t ref) { if (m_reacting.size() < 64) m_reacting.insert(ref); }
        void Stopped(uint32_t ref) { m_reacting.erase(ref); }
        void Clear() { m_reacting.clear(); m_next = 0; }
        bool Due(uint32_t now)
        {
            if (m_reacting.empty() || static_cast<int32_t>(now - m_next) < 0)
                return false;
            m_next = now + kDeliriumRootEveryMs;
            return true;
        }
        const std::set<uint32_t>& Reacting() const { return m_reacting; }
        // Each reacting ref: getRoot(ref, root) false means it stopped (it's dropped). At most kMaxDeliriumRoots.
        template <class GetRoot>
        net::DeliriumRoots Collect(uint32_t now, GetRoot getRoot)
        {
            net::DeliriumRoots m;
            m.timeMs = now;
            for (auto it = m_reacting.begin(); it != m_reacting.end();)
            {
                net::DeliriumRoot root;
                root.ref = *it;
                if (!getRoot(*it, root.root))
                {
                    it = m_reacting.erase(it);
                    continue;
                }
                bool good = true;
                for (int i = 0; i < 7; ++i)
                    good = good && std::isfinite(root.root[i]) && std::fabs(root.root[i]) < 1e7f;
                for (int i = 3; i < 6; ++i)
                    root.root[i] = std::fmax(-net::kMaxDeliriumSpeed, std::fmin(net::kMaxDeliriumSpeed, root.root[i]));
                if (good)
                    root.root[6] = std::fmod(root.root[6], 360.0f);
                if (good && m.roots.size() < net::kMaxDeliriumRoots)
                    m.roots.push_back(root);
                ++it;
            }
            return m;
        }

    private:
        std::set<uint32_t> m_reacting;
        uint32_t m_next = 0;
    };
}
