// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/quests.h"

#include "coop/clock.h"
#include "coop/quests.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/npcs.h"
#include "log.h"

#include <windows.h>

namespace game
{
    namespace
    {
        constexpr uint32_t kLookMs = 1000;
        constexpr size_t kResendPerLook = 16;
        constexpr uint32_t kQuietAfterJoinMs = 10000;
        constexpr uint32_t kSwitchedResendMs = 10000;

        net::QuestState ToState(const QuestInfo& q)
        {
            net::QuestState s;
            s.quest = q.id;
            s.flags = q.flags;
            s.stage = q.stage;
            s.done = q.done;
            for (auto& [id, status] : q.objectives)
                s.objectives.push_back({id, static_cast<uint8_t>(status & 3)});
            return s;
        }

        bool Started(const QuestInfo& q)
        {
            return (q.flags & (coop::kQuestRunning | coop::kQuestCompleted | coop::kQuestShown | coop::kQuestFailed)) ||
                !q.done.empty();
        }
    }

    void Quests::Stop()
    {
        if (!m_detached.empty())
        {
            PutBackQuestScripts(m_detached);
            LOG_INFO("Quests: put back %zu quest scripts", m_detached.size());
        }
        m_detached.clear();
        m_detachedAll = false;
        m_prints.clear();
        m_loadOrder.clear();
        m_joins = 0;
        m_switched.clear();
    }

    void Quests::Enables(coop::Coop& coop, uint32_t now, bool host)
    {
        std::vector<EnableChange> changes = TakeEnables();
        if (m_loadOrder.empty())
            m_loadOrder = LoadOrder();
        auto ours = [&](uint32_t ref) { return coop::HostRunsQuestsOf(ref, m_loadOrder, coop.QuestMods()); };
        if (host)
        {
            std::vector<net::RefEnable> send;
            for (const EnableChange& c : changes)
            {
                if (!ours(c.ref))
                    continue;
                m_switched[c.ref] = c.enabled;
                send.push_back({c.ref, c.enabled});
            }
            if (coop::IsDue(now, m_nextSwitchedResend))
            {
                m_nextSwitchedResend = now + kSwitchedResendMs;
                for (auto& [ref, enabled] : m_switched)
                    send.push_back({ref, enabled});
            }
            if (!send.empty())
            {
                coop.SendRefEnables(send);
                m_enablesSent += static_cast<uint32_t>(send.size());
            }
            return;
        }
        for (const EnableChange& c : changes)
            if (IsActor(LookupRef(c.ref)))
                LOG_DEBUG("Quests: our game switched actor %08X %s", c.ref, c.enabled ? "on" : "off");
        for (const net::RefEnable& e : coop.TakeRefEnables())
        {
            TESObjectREFR* ref = ours(e.ref) ? LookupRef(e.ref) : nullptr;
            if (!ref || IsEnabled(ref) == e.enabled)
                continue;
            if (e.enabled && m_npcs && m_npcs->KeepsOff(e.ref))
            {
                LOG_DEBUG("Quests: %08X is switched on in the host's game but not near it; keeping ours off", e.ref);
                continue;
            }
            SetEnabled(ref, e.enabled);
            ++m_enablesSet;
            LOG_DEBUG("Quests: %08X switched %s (as on the host)", e.ref, e.enabled ? "on" : "off");
        }
        TakeEnables();
    }

    void Quests::Update(coop::Coop& coop, uint32_t now, const Npcs& npcs)
    {
        m_npcs = &npcs;
        if (!coop.InGame())
        {
            if (m_detachedAll || !m_prints.empty())
                Stop();
            return;
        }
        bool host = coop.GetRole() == coop::Role::Host;
        m_questMods = &coop.QuestMods();
        if (host)
            Host(coop, now);
        else
            Friend(coop, now);
        Enables(coop, now, host);

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_looks || m_set))
                LOG_INFO("Quests: %u looks, %.3f ms on average; %u quest updates sent, %u set here; %u things "
                    "switched on or off sent, %u set here", m_looks, m_looks ? m_lookMs / m_looks : 0.0, m_sent, m_set,
                    m_enablesSent, m_enablesSet);
            m_nextLog = now + 60000;
            m_lookMs = 0;
            m_looks = m_sent = m_set = m_enablesSent = m_enablesSet = 0;
        }
    }

    void Quests::Host(coop::Coop& coop, uint32_t now)
    {
        if (!coop::IsDue(now, m_nextLook))
            return;
        m_nextLook = now + kLookMs;
        LARGE_INTEGER start, end, freq;
        QueryPerformanceCounter(&start);
        std::vector<std::pair<uint32_t, uint32_t>> prints;
        prints.reserve(m_prints.size());
        QuestFingerprints(prints);
        std::vector<net::QuestState> send;
        bool everything = coop.Joins() != m_joins;
        if (m_loadOrder.empty())
            m_loadOrder = LoadOrder();
        m_joins = coop.Joins();
        for (size_t i = 0; i < prints.size(); ++i)
        {
            bool changed = i >= m_prints.size() || m_prints[i] != prints[i];
            bool again = !prints.empty() && i >= m_resendAt && i < m_resendAt + kResendPerLook;
            if ((!changed && !everything && !again) ||
                !coop::HostRunsQuestsOf(prints[i].first, m_loadOrder, coop.QuestMods()))
                continue;
            QuestInfo q;
            if (ReadQuest(prints[i].first, q) && (Started(q) || (changed && i < m_prints.size())))
                send.push_back(ToState(q));
        }
        m_resendAt = m_resendAt + kResendPerLook >= prints.size() ? 0 : m_resendAt + kResendPerLook;
        m_prints = std::move(prints);
        if (!send.empty())
        {
            coop.SendQuestStates(send);
            m_sent += static_cast<uint32_t>(send.size());
        }
        QueryPerformanceCounter(&end);
        QueryPerformanceFrequency(&freq);
        m_lookMs += 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
        ++m_looks;
    }

    void Quests::Friend(coop::Coop& coop, uint32_t now)
    {
        if (!m_detachedAll)
        {
            m_loadOrder = LoadOrder();
            m_detached = DetachQuestScripts([](uint32_t id, const void* c) {
                auto* q = static_cast<const Quests*>(c);
                return coop::HostRunsQuestsOf(id, q->m_loadOrder, *q->m_questMods); }, this);
            m_detachedAll = true;
            m_quietUntil = now + kQuietAfterJoinMs;
            LOG_INFO("Quests: detached %zu quest scripts (the host's game runs the story)", m_detached.size());
        }

        bool quiet = !coop::IsDue(now, m_quietUntil);
        for (const net::QuestState& host : coop.TakeQuestStates())
        {
            QuestInfo mine;
            if (!coop::HostRunsQuestsOf(host.quest, m_loadOrder, coop.QuestMods()) || !ReadQuest(host.quest, mine))
                continue;
            net::QuestState before = ToState(mine);
            if (before == host)
                continue;
            int commands = 0;
            for (const net::QuestObjective& o : host.objectives)
            {
                uint8_t was = 0;
                for (const net::QuestObjective& b : before.objectives)
                    if (b.id == o.id)
                        was = b.status;
                if ((o.status & 1) != (was & 1))
                    commands += ShowObjective(host.quest, o.id, (o.status & 1) != 0);
                if ((o.status & 2) != (was & 2))
                    commands += CompleteObjective(host.quest, o.id, (o.status & 2) != 0);
            }
            coop::QuestNews news = coop::CompareQuest(before, host);
            if (news.completed)
                commands += CompleteQuest(host.quest);
            QuestInfo q;
            q.id = host.quest;
            q.flags = host.flags;
            q.stage = host.stage;
            q.done = host.done;
            for (const net::QuestObjective& o : host.objectives)
                q.objectives.push_back({o.id, o.status});
            WriteQuest(q);
            ++m_set;
            QuestInfo after;
            ReadQuest(host.quest, after);
            net::QuestState now = ToState(after);
            LOG_DEBUG("Quests: %08X %s set to the host's (flags %02X -> %02X, stage %u -> %u, %zu -> %zu stages done, "
                "%d game commands)%s", host.quest, mine.name.c_str(), before.flags, host.flags, before.stage, host.stage,
                before.done.size(), host.done.size(), commands, now == host ? "" : "; STILL DIFFERS");
            if (!(now == host))
            {
                std::string mineText, hostText;
                for (auto& o : now.objectives)
                    mineText += std::to_string(o.id) + ":" + std::to_string(o.status) + " ";
                for (auto& o : host.objectives)
                    hostText += std::to_string(o.id) + ":" + std::to_string(o.status) + " ";
                LOG_DEBUG("Quests:   ours flags %02X stage %u done %zu objectives [%s]; host's flags %02X stage %u done "
                    "%zu objectives [%s]", now.flags, now.stage, now.done.size(), mineText.c_str(), host.flags,
                    host.stage, host.done.size(), hostText.c_str());
            }
            if (!quiet && news.failed)
            {
                std::string name = QuestName(host.quest);
                if (!name.empty())
                    Notify(("Quest failed: " + name).c_str());
            }
        }
    }
}
