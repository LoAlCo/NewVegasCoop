// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/activations.h"

#include "coop/clock.h"
#include "coop/quests.h"
#include "game/game.h"
#include "game/progress.h"
#include "log.h"

#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"

#include <map>

namespace game
{
    namespace
    {
        constexpr uint32_t kHoldRewardsMs = 3000;

        const char* QuestCommandName(net::QuestCommandKind kind)
        {
            switch (kind)
            {
            case net::QuestCommandKind::SetStage: return "SetStage";
            case net::QuestCommandKind::ShowObjective: return "SetObjectiveDisplayed";
            case net::QuestCommandKind::CompleteObjective: return "SetObjectiveCompleted";
            case net::QuestCommandKind::CompleteQuest: return "CompleteQuest";
            }
            return "?";
        }
    }

    void Activations::Install(coop::Coop& coop, Progress& progress)
    {
        m_coop = &coop;
        m_progress = &progress;
    }

    bool Activations::Ours(uint32_t id)
    {
        if (m_loadOrder.empty())
            m_loadOrder = LoadOrder();
        return coop::HostRunsQuestsOf(id, m_loadOrder, m_coop->QuestMods());
    }

    void Activations::Replay(const net::ActivatorUse& use)
    {
        bool host = m_coop->GetRole() == coop::Role::Host;
        std::string who = m_coop->NameOf(use.player);
        TESObjectREFR* ref = Ours(use.ref) ? LookupRef(use.ref) : nullptr;
        bool ok = UsedLikeActivator(ref);
        if (ok && host)
        {
            coop::Pose them, it;
            ok = m_coop->PlayerPose(use.player, them) && GetPose(ref, it) && coop::Within(them, it, LoadedReach());
        }
        if (!ok)
        {
            ++m_refused;
            LOG_WARN("Activations: %s used %08X: not an activator %shere; not run", who.c_str(), use.ref,
                host ? "near them " : "");
            return;
        }
        std::vector<Item> before, after;
        bool counted = Contents(Player(), before);
        int32_t xp = PlayerXp(), karma = PlayerKarma();
        HoldRewardsFrom(ref, kHoldRewardsMs);
        HoldPlayerEffects(true);
        DialogueLineRunning(true);
        ActivateAsPlayer(ref);
        DialogueLineRunning(false);
        HoldPlayerEffects(false);
        ++m_replayed;
        int putBack = 0;
        if (counted && Contents(Player(), after))
        {
            std::map<uint32_t, int32_t> change;
            for (const Item& i : after)
                change[i.id] += i.count;
            for (const Item& i : before)
                change[i.id] -= i.count;
            for (auto& [id, delta] : change)
                if (delta)
                {
                    ChangeItem(Player(), id, -delta);
                    ++putBack;
                }
        }
        int gear = GiveBackAfterReplay();
        int32_t gainedXp = PlayerXp() - xp, gainedKarma = PlayerKarma() - karma;
        if ((gainedXp || gainedKarma) && m_progress)
        {
            m_progress->Expect(gainedXp, gainedKarma);
            LOG_WARN("Activations: %s's use still gave us %d XP, %d karma (kept, not shared)", who.c_str(), gainedXp,
                gainedKarma);
        }
        LOG_INFO("Activations: %s used %08X; ours activated as our player (%d item kinds put back, %d gear lockers' "
            "give-backs done for ours)", who.c_str(), use.ref, putBack, gear);
        if (host)
            m_coop->SendActivatorUse(use);
    }

    bool Activations::HostsQuest(uint32_t quest)
    {
        return Ours(quest) && !coop::IsPerPlayerQuest(quest, m_loadOrder);
    }

    void Activations::RunQuestCommands(uint32_t now)
    {
        for (const coop::Coop::QuestCommandFrom& q : m_coop->TakeQuestCommands())
        {
            const net::QuestCommand& c = q.command;
            std::string who = m_coop->NameOf(q.playerId);
            const char* skipped = nullptr;
            bool done = false;
            if (!HostsQuest(c.quest))
                skipped = "not a quest the host runs";
            else if (!m_questGate.Take(q.playerId, now))
                skipped = "too many this second";
            else
                done = RunQuestCommand(c, &skipped);
            if (done)
                ++m_questRun;
            else
                ++m_refused;
            LOG_INFO("Quests: %s's object script did %s %08X %u %u: %s", who.c_str(), QuestCommandName(c.kind),
                c.quest, c.number, c.on, done ? "done here" : skipped ? skipped : "the game didn't");
        }
    }

    void Activations::Update(uint32_t now)
    {
        if (!m_coop)
            return;
        bool in = m_coop->InGame();
        bool friendIn = in && m_coop->GetRole() == coop::Role::Friend;
        if (in != m_keeping)
        {
            KeepActivations(in);
            m_keeping = in;
        }
        if (friendIn != m_sendingQuestCommands)
        {
            if (friendIn)
                SendQuestCommandsOf([this](uint32_t quest) { return HostsQuest(quest); });
            else
                SendQuestCommandsOf(nullptr);
            m_sendingQuestCommands = friendIn;
        }
        if (!in)
            return;
        if (friendIn)
            for (const net::QuestCommand& c : TakeQuestCommands())
                m_coop->SendQuestCommand(c);
        else if (m_coop->GetRole() == coop::Role::Host)
            RunQuestCommands(now);
        UpdateOwnUseLock(now, InDialogue());
        for (uint32_t ref : TakeActivations())
        {
            if (!Ours(ref))
                continue;
            ++m_used;
            LOG_INFO("Activations: we used %08X", ref);
            m_coop->SendActivatorUse({0, ref});
        }
        for (const net::ActivatorUse& use : m_coop->TakeActivatorUses())
            Replay(use);
        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_used || m_replayed || m_refused || m_questRun))
                LOG_INFO("Activations: %u used here, %u run for others, %u friends' quest commands run, %u refused",
                    m_used, m_replayed, m_questRun, m_refused);
            m_nextLog = now + 60000;
            m_used = m_replayed = m_refused = m_questRun = 0;
        }
    }
}
