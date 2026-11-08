// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"

#include <cstdint>
#include <string>
#include <vector>

class TESObjectREFR;
class TESForm;
namespace game
{
    class Progress;

    class Terminals
    {
    public:
        void Install(coop::Coop& coop, Progress& progress);
        void Update(uint32_t now);
        bool Chosen(void* entry, TESObjectREFR* ref);
        std::string LabPick(int index);

    private:
        void Replay(const net::TerminalUse& use);
        bool Ours(uint32_t id);
        coop::Coop* m_coop = nullptr;
        Progress* m_progress = nullptr;
        std::vector<std::string> m_loadOrder;
        std::vector<net::TerminalUse> m_mine;
        uint32_t m_chosen = 0, m_replayed = 0, m_refused = 0, m_nextLog = 0;
    };
}
