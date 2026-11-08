// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/objects.h"

#include "coop/clock.h"

#include <cmath>

namespace coop
{
    net::ObjectState* HostObjects::Find(uint32_t ref, bool& added)
    {
        added = false;
        auto it = m_known.find(ref);
        if (it != m_known.end())
            return &it->second;
        if (m_known.size() >= kMaxKnownObjects)
            return nullptr;
        added = true;
        net::ObjectState& s = m_known[ref];
        s.ref = ref;
        return &s;
    }

    bool HostObjects::Destroyed(uint32_t ref, bool destroyed)
    {
        bool added;
        net::ObjectState* s = Find(ref, added);
        if (!s)
            return false;
        if (added || s->destroyed != destroyed)
            m_changed.push_back(ref);
        s->destroyed = destroyed;
        return true;
    }

    bool HostObjects::Played(uint32_t ref, bool destroyed, uint16_t group, bool init)
    {
        if (group >= net::kAnimGroupCount)
            return true;
        bool added;
        net::ObjectState* s = Find(ref, added);
        if (!s)
            return false;
        s->destroyed = destroyed;
        s->group = group;
        s->init = init;
        ++s->plays;
        m_changed.push_back(ref);
        return true;
    }

    bool HostObjects::Health(uint32_t ref, float health, float full, bool destroyed)
    {
        if (health < 0)
            return true;
        if (!m_known.count(ref) && health >= full - kHealthStep)
            return true;
        bool added;
        net::ObjectState* s = Find(ref, added);
        if (!s)
            return false;
        if (added || s->health < 0 || std::fabs(s->health - health) >= kHealthStep || s->destroyed != destroyed)
        {
            s->health = health;
            s->destroyed = destroyed;
            m_changed.push_back(ref);
        }
        return true;
    }

    std::vector<net::ObjectState> HostObjects::Take(uint32_t now)
    {
        std::vector<net::ObjectState> out;
        if (IsDue(now, m_nextResend))
        {
            m_nextResend = now + kObjectResendMs;
            m_changed.clear();
            for (auto& [ref, s] : m_known)
                out.push_back(s);
            return out;
        }
        for (uint32_t ref : m_changed)
        {
            auto it = m_known.find(ref);
            bool sent = false;
            for (const net::ObjectState& o : out)
                sent = sent || o.ref == ref;
            if (it != m_known.end() && !sent)
                out.push_back(it->second);
        }
        m_changed.clear();
        return out;
    }

    void HostObjects::Clear()
    {
        m_known.clear();
        m_changed.clear();
        m_nextResend = 0;
    }

    void FriendObjects::Heard(const net::ObjectState& host)
    {
        auto it = m_objects.find(host.ref);
        if (it == m_objects.end())
        {
            if (m_objects.size() >= kMaxKnownObjects)
                return;
            it = m_objects.emplace(host.ref, Object{}).first;
        }
        it->second.host = host;
    }

    void FriendObjects::Decide(uint32_t ref, Object& o, const LocalObject& here, std::vector<ObjectAction>& out)
    {
        if (!here.found)
            return;
        ObjectAction a;
        a.ref = ref;
        bool ownAhead = here.destroyed && !o.host.destroyed && o.host.health >= 0 && here.health >= 0 &&
            here.health < o.host.health - kHealthStep;
        if (here.destroyed != o.host.destroyed && !ownAhead)
        {
            a.setDestroyed = true;
            a.destroyed = o.host.destroyed;
        }
        if (o.host.group != net::kNoAnimGroup && here.model &&
            (!o.playedAny || o.playedCount != o.host.plays || o.playedModel != here.model))
        {
            a.play = true;
            a.group = o.host.group;
            a.init = o.host.init;
            o.playedAny = true;
            o.playedCount = o.host.plays;
            o.playedModel = here.model;
        }
        if (o.host.health >= 0 && here.health >= 0 && here.health > o.host.health + kHealthStep)
            a.damage = here.health - o.host.health;
        if (a.setDestroyed || a.play || a.damage > 0)
            out.push_back(a);
    }

    std::vector<net::ObjectLow> FriendObjects::Lows(const std::vector<SeenObject>& seen, uint32_t now)
    {
        std::vector<net::ObjectLow> out;
        for (const SeenObject& o : seen)
        {
            if (!(o.health >= 0) || out.size() >= net::kMaxObjectLowsPerMessage)
                continue;
            auto known = m_objects.find(o.ref);
            float host = known != m_objects.end() && known->second.host.health >= 0 ? known->second.host.health : o.full;
            float margin = o.margin > 0 && std::isfinite(o.margin) ? o.margin : 0;
            if (!(o.health < host - margin - kHealthStep))
                continue;
            auto it = m_reported.find(o.ref);
            if (it != m_reported.end() &&
                (o.health > it->second.health - kHealthStep || !IsDue(now, it->second.at + kObjectLowEveryMs)))
                continue;
            if (it == m_reported.end() && m_reported.size() >= kMaxKnownObjects)
                continue;
            m_reported[o.ref] = {o.health, now};
            out.push_back({o.ref, o.health > net::kMaxObjectHealth ? net::kMaxObjectHealth : o.health});
        }
        return out;
    }
}
