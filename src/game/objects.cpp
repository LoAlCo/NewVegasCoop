// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/objects.h"

#include "coop/clock.h"
#include "coop/quests.h"
#include "game/game.h"
#include "log.h"

#include "nvse/GameObjects.h"

#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 250;
        constexpr uint32_t kSendMs = 100;
        constexpr uint32_t kListMs = 2000;
        constexpr uint32_t kHealthLookMs = 250;
        constexpr uint32_t kHoldRewardsMs = 3000;
    }

    bool Objects::Ours(coop::Coop& coop, uint32_t ref)
    {
        if (m_loadOrder.empty())
            m_loadOrder = LoadOrder();
        return coop::HostRunsQuestsOf(ref, m_loadOrder, coop.QuestMods());
    }

    void Objects::Forget()
    {
        m_host.Clear();
        m_friend.Clear();
        m_destructibles.clear();
        m_cell = nullptr;
        TakeObjectChanges();
    }

    void Objects::ListDestructibles(coop::Coop& coop, uint32_t now)
    {
        TESObjectREFR* player = Player();
        const void* cell = player ? player->parentCell : nullptr;
        if (cell == m_cell && !coop::IsDue(now, m_nextList))
            return;
        m_cell = cell;
        m_nextList = now + kListMs;
        std::vector<TESObjectREFR*> found;
        DestructiblesNear(found, LoadedReach());
        size_t before = m_destructibles.size();
        m_destructibles.clear();
        for (TESObjectREFR* r : found)
            if (Ours(coop, r->refID))
                m_destructibles.push_back(r->refID);
        if (m_destructibles.size() != before && !m_destructibles.empty())
        {
            TESObjectREFR* first = LookupRef(m_destructibles[0]);
            LOG_DEBUG("Objects: %zu destructible objects near us; the first %08X (health %.1f of %.1f)",
                m_destructibles.size(), m_destructibles[0], ObjectHealth(first), ObjectFullHealth(first));
        }
    }

    void Objects::Update(coop::Coop& coop, uint32_t now)
    {
        bool host = coop.InGame() && coop.GetRole() == coop::Role::Host;
        if (host != m_watching)
        {
            WatchObjects(host);
            m_watching = host;
        }
        if (!coop.InGame())
        {
            if (m_host.Known() || m_friend.Known())
                Forget();
            return;
        }
        if (host)
            Host(coop, now);
        else
            Friend(coop, now);

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_noted || m_sent || m_destroyedSet || m_played || m_damaged || m_hitsSent ||
                    m_hitsApplied || m_looks))
                LOG_INFO("Objects: %u script changes noted, %u object states sent, %u friends' lows followed; %u looks "
                    "(%.3f ms on average), %u destroyed flags set, %u animations played, %u damaged as on the host, "
                    "%u lows sent; %u not kept (list full); %zu known, %zu destructible near us",
                    m_noted, m_sent, m_hitsApplied, m_looks, m_looks ? m_lookMs / m_looks : 0.0, m_destroyedSet,
                    m_played, m_damaged, m_hitsSent, m_full, host ? m_host.Known() : m_friend.Known(),
                    m_destructibles.size());
            m_nextLog = now + 60000;
            m_lookMs = 0;
            m_looks = m_noted = m_sent = m_destroyedSet = m_played = m_full = m_damaged = m_hitsSent = m_hitsApplied = 0;
        }
    }

    void Objects::Host(coop::Coop& coop, uint32_t now)
    {
        for (auto& [player, low] : coop.TakeObjectLows())
        {
            TESObjectREFR* r = Ours(coop, low.ref) ? LookupRef(low.ref) : nullptr;
            coop::Pose them, it;
            if (!r || ObjectFullHealth(r) <= 0 || !coop.PlayerPose(player, them) || !GetPose(r, it) ||
                !coop::Within(them, it, LoadedReach()))
            {
                LOG_WARN("Objects: %s says %08X is down to %.1f: not a destructible object near them here; ignored",
                    coop.NameOf(player).c_str(), low.ref, low.health);
                continue;
            }
            float before = ObjectHealth(r);
            float target = low.health < 0 ? 0 : low.health;
            if (before - target < coop::kHealthStep)
                continue;
            HoldRewardsFrom(r, kHoldRewardsMs);
            DamageObjectBy(r, before - target);
            ++m_hitsApplied;
            LOG_INFO("Objects: %s's %08X is down to %.1f: ours too (was %.1f)", coop.NameOf(player).c_str(), low.ref,
                target, before);
        }
        ListDestructibles(coop, now);
        if (coop::IsDue(now, m_nextHealthLook))
        {
            m_nextHealthLook = now + kHealthLookMs;
            for (uint32_t id : m_destructibles)
                if (TESObjectREFR* r = LookupRef(id))
                    if (!m_host.Health(id, ObjectHealth(r), ObjectFullHealth(r), IsDestroyed(r)))
                        ++m_full;
        }
        for (const ObjectChange& c : TakeObjectChanges())
        {
            if (!Ours(coop, c.ref))
                continue;
            bool kept = c.played ? m_host.Played(c.ref, c.destroyed, c.group, c.init) :
                m_host.Destroyed(c.ref, c.destroyed);
            if (!kept)
            {
                ++m_full;
                continue;
            }
            ++m_noted;
            if (c.played)
            {
                const char* name = PlayableGroupName(c.group);
                LOG_DEBUG("Objects: a script played %s (%u) on %08X%s", name ? name : "?", c.group, c.ref,
                    c.destroyed ? " (destroyed)" : "");
            }
            else
                LOG_DEBUG("Objects: a script set %08X %s", c.ref, c.destroyed ? "destroyed" : "not destroyed");
        }
        if (!coop::IsDue(now, m_nextSend))
            return;
        m_nextSend = now + kSendMs;
        std::vector<net::ObjectState> send = m_host.Take(now);
        if (!send.empty())
        {
            coop.SendObjectStates(send);
            m_sent += static_cast<uint32_t>(send.size());
        }
    }

    void Objects::Friend(coop::Coop& coop, uint32_t now)
    {

        for (const net::ObjectState& s : coop.TakeObjectStates())
            if (Ours(coop, s.ref))
                m_friend.Heard(s);
        if (!coop::IsDue(now, m_nextLook))
            return;
        m_nextLook = now + kLookMs;
        LARGE_INTEGER start, end, freq;
        QueryPerformanceCounter(&start);
        std::vector<coop::ObjectAction> actions = m_friend.Due([](uint32_t ref) {
            coop::LocalObject here;
            TESObjectREFR* r = LookupRef(ref);
            if (!r)
                return here;
            here.found = true;
            here.destroyed = IsDestroyed(r);
            here.model = Model(r);
            here.health = ObjectHealth(r);
            return here;
        });
        for (const coop::ObjectAction& a : actions)
        {
            TESObjectREFR* r = LookupRef(a.ref);
            if (!r)
                continue;
            if (a.setDestroyed)
            {
                SetDestroyedFlag(r, a.destroyed);
                ++m_destroyedSet;
                LOG_INFO("Objects: %08X set %s (as on the host)%s", a.ref, a.destroyed ? "destroyed" : "not destroyed",
                    IsDestroyed(r) == a.destroyed ? "" : "; the flag didn't change");
            }
            if (a.damage > 0)
            {
                float before = ObjectHealth(r);
                HoldRewardsFrom(r, kHoldRewardsMs);
                DamageObjectBy(r, a.damage);
                ++m_damaged;
                LOG_INFO("Objects: %08X damaged by %.1f as on the host (health %.1f, to %.1f)", a.ref, a.damage, before,
                    before - a.damage);
            }
            if (a.play)
            {
                const char* name = PlayableGroupName(a.group);
                bool played = PlayObjectGroup(r, a.group, a.init);
                if (played)
                    ++m_played;
                if (played)
                    LOG_DEBUG("Objects: %08X plays %s (%u) as on the host", a.ref, name ? name : "?", a.group);
                else
                    LOG_WARN("Objects: %08X couldn't play %s (%u) as on the host", a.ref, name ? name : "?", a.group);
            }
        }
        ListDestructibles(coop, now);
        std::vector<coop::SeenObject> seen;
        for (uint32_t id : m_destructibles)
            if (TESObjectREFR* r = LookupRef(id))
                seen.push_back({id, ObjectHealth(r), ObjectFullHealth(r), ObjectSelfDamage(r) * 0.5f});
        std::vector<net::ObjectLow> lows = m_friend.Lows(seen, now);
        for (const net::ObjectLow& l : lows)
            LOG_INFO("Objects: our %08X is down to %.1f, lower than the host's; told the host", l.ref, l.health);
        if (!lows.empty())
        {
            coop.SendObjectLows(lows);
            m_hitsSent += static_cast<uint32_t>(lows.size());
        }
        QueryPerformanceCounter(&end);
        QueryPerformanceFrequency(&freq);
        m_lookMs += (end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
        ++m_looks;
    }
}
