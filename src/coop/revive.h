// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"

#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr uint32_t kReviveAfterMs = 2000;
    constexpr uint32_t kReviveRetryMs = 3000;
    constexpr uint32_t kReviveGapMs = 500;
    constexpr uint32_t kReviveMinOffMs = 250;
    constexpr uint32_t kReviveTimeoutMs = 5000;
    constexpr uint32_t kReviveForgetMs = 30000;
    constexpr uint32_t kMaxReviveTries = 5;

    // Brings back NPCs that are dead here but alive on the host, one at a time. The game rebuilds an actor's whole
    // animation set, on the spot, when it goes from dead to alive with its 3D loaded: that crashed a visitor on a body
    // that died in their save. Without 3D the game skips that, so: switch the body off, wait for its 3D to go, revive,
    // switch it back on, and the game loads it as it loads any NPC. Never revives while the body has 3D.
    class ReviveSteps
    {
    public:
        enum class Do { Nothing, SwitchOff, Resurrect, SwitchOn };
        enum class News
        {
            None,
            Waiting,     // due or under way, but not safe right now (a menu or a loading screen)
            Started,
            Revived,
            BodyStayed,  // its 3D didn't go in time: not revived, try again later
            StillDead,   // the game's revive didn't take: try again later
            GaveUp,      // out of tries: stays dead here
        };
        struct Step
        {
            Do action = Do::Nothing;
            News news = News::None;
            uint32_t tries = 0;
        };

        // Call for a body we aren't busy with whenever it's near, and for one we're busy with every update.
        // safe: no menu or loading screen.
        Step Look(uint32_t ref, uint32_t now, bool dead, bool enabled, bool has3D, bool safe)
        {
            Step out;
            if (!dead && !m_bodies.count(ref))
                return out;
            Body& b = m_bodies[ref];
            out.tries = b.tries;
            switch (b.phase)
            {
            case Phase::Idle:
                if (!dead)
                {
                    b.deadSince = 0;
                    if (!b.aliveSince)
                        b.aliveSince = NonZero(now);
                    else if (now - b.aliveSince >= kReviveForgetMs)
                        m_bodies.erase(ref);
                    return out;
                }
                b.aliveSince = 0;
                // Out of sight for a while (we moved away, or sat through a loading screen): it has just come into
                // view, so it gets the whole wait again.
                if (!b.deadSince || now - b.lastSeen > kReviveAfterMs)
                    b.deadSince = NonZero(now);
                b.lastSeen = now;
                if (!safe)
                    b.unsafeAt = NonZero(now);
                if (b.gaveUp || !enabled || now - b.deadSince < kReviveAfterMs || !IsDue(now, b.nextTry))
                    return out;
                if (b.tries >= kMaxReviveTries)
                {
                    b.gaveUp = true;
                    out.news = News::GaveUp;
                    return out;
                }
                if (!safe)
                {
                    if (!b.toldWaiting)
                        out.news = News::Waiting;
                    b.toldWaiting = true;
                    return out;
                }
                if (b.unsafeAt && now - b.unsafeAt < kReviveAfterMs)
                    return out;
                if (Busy() || !IsDue(now, m_nextStart))
                    return out;
                b.toldWaiting = false;
                b.phase = Phase::Unloading;
                b.phaseAt = now;
                out.tries = ++b.tries;
                m_nextStart = NonZero(now + kReviveGapMs);
                out.action = Do::SwitchOff;
                out.news = News::Started;
                return out;

            case Phase::Unloading:
                if (!dead)
                    return Finish(b, out, News::Revived, now);
                // A menu or loading screen came up with the body off. Back on at once, so a save made from that menu
                // doesn't keep it switched off. This try doesn't count.
                if (!safe)
                {
                    --b.tries;
                    out.tries = b.tries;
                    b.unsafeAt = NonZero(now);
                    b.toldWaiting = true;
                    return Finish(b, out, News::Waiting, now);
                }
                if (now - b.phaseAt >= kReviveTimeoutMs)
                    return Finish(b, out, News::BodyStayed, now);
                if (has3D || now - b.phaseAt < kReviveMinOffMs)
                    return out;
                b.phase = Phase::Reviving;
                b.phaseAt = now;
                out.action = Do::Resurrect;
                return out;

            case Phase::Reviving:
                if (!dead)
                    return Finish(b, out, News::Revived, now);
                if (now - b.phaseAt >= kReviveTimeoutMs)
                    return Finish(b, out, News::StillDead, now);
                return out;
            }
            return out;
        }

        bool Busy(uint32_t ref) const
        {
            auto it = m_bodies.find(ref);
            return it != m_bodies.end() && it->second.phase != Phase::Idle;
        }

        bool Busy() const
        {
            for (const auto& [ref, b] : m_bodies)
                if (b.phase != Phase::Idle)
                    return true;
            return false;
        }

        std::vector<uint32_t> Bodies() const
        {
            std::vector<uint32_t> refs;
            for (const auto& [ref, b] : m_bodies)
                refs.push_back(ref);
            return refs;
        }

        // The body is no one's to revive any more. True if we had it switched off and it needs switching back on.
        bool Drop(uint32_t ref)
        {
            bool off = Busy(ref);
            m_bodies.erase(ref);
            return off;
        }

        // Co-op's over: the bodies still switched off need switching back on.
        std::vector<uint32_t> TakeSwitchedOff()
        {
            std::vector<uint32_t> refs;
            for (const auto& [ref, b] : m_bodies)
                if (b.phase != Phase::Idle)
                    refs.push_back(ref);
            Clear();
            return refs;
        }

        void Clear()
        {
            m_bodies.clear();
            m_nextStart = 0;
        }

    private:
        enum class Phase { Idle, Unloading, Reviving };
        struct Body
        {
            Phase phase = Phase::Idle;
            uint32_t deadSince = 0;
            uint32_t aliveSince = 0;
            uint32_t lastSeen = 0;
            uint32_t unsafeAt = 0;
            uint32_t phaseAt = 0;
            uint32_t nextTry = 0;
            uint32_t tries = 0;
            bool gaveUp = false;
            bool toldWaiting = false;
        };

        static uint32_t NonZero(uint32_t time) { return time ? time : 1; }

        // Always asks to switch it on, even if it reads as on: that also cancels a switch-off still in the game's queue,
        // and does nothing to a body that's on.
        Step Finish(Body& b, Step out, News news, uint32_t now)
        {
            b.phase = Phase::Idle;
            b.lastSeen = now;
            if (news == News::Revived)
                b.deadSince = 0;
            else
                b.nextTry = NonZero(now + kReviveRetryMs);
            out.news = news;
            out.action = Do::SwitchOn;
            return out;
        }

        std::map<uint32_t, Body> m_bodies;
        uint32_t m_nextStart = 0;
    };
}
