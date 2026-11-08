// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/activator_uses.h"
#include "coop/coop.h"

#include <cstdint>
#include <string>
#include <vector>

class TESObjectREFR;
namespace game
{
    class Progress;

    class Activations
    {
    public:
        void Install(coop::Coop& coop, Progress& progress);
        void Update(uint32_t now);

    private:
        void Replay(const net::ActivatorUse& use);
        bool Ours(uint32_t id);
        bool HostsQuest(uint32_t quest);
        void RunQuestCommands(uint32_t now);
        coop::Coop* m_coop = nullptr;
        Progress* m_progress = nullptr;
        std::vector<std::string> m_loadOrder;
        bool m_keeping = false, m_sendingQuestCommands = false;
        coop::QuestCommandGate m_questGate;
        uint32_t m_used = 0, m_replayed = 0, m_refused = 0, m_questRun = 0, m_nextLog = 0;
    };
}
