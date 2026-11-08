// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace coop
{
    constexpr uint8_t kQuestRunning = 0x01;
    constexpr uint8_t kQuestCompleted = 0x02;
    constexpr uint8_t kQuestShown = 0x20;
    constexpr uint8_t kQuestFailed = 0x40;

    struct DetachedSlot
    {
        void** slot;
        void* script;
        bool putBack = false;
    };

    template <typename F> auto WithDetachedPutBack(std::vector<DetachedSlot>& detached, F f)
    {
        for (DetachedSlot& d : detached)
            if ((d.putBack = !*d.slot))
                *d.slot = d.script;
        auto result = f();
        for (DetachedSlot& d : detached)
            if (d.putBack)
            {
                *d.slot = nullptr;
                d.putBack = false;
            }
        return result;
    }

    struct QuestNews
    {
        bool added = false;
        bool completed = false;
        bool failed = false;
        std::vector<uint32_t> shown;
        std::vector<uint32_t> finished;
    };

    QuestNews CompareQuest(const net::QuestState& before, const net::QuestState& after);

    bool IsGameFile(const std::string& name);
    bool FromGameFile(uint32_t formId, const std::vector<std::string>& loadOrder);
    bool IsPerPlayerQuest(uint32_t formId, const std::vector<std::string>& loadOrder);
    bool HostRunsQuestsOf(uint32_t formId, const std::vector<std::string>& loadOrder,
        const std::vector<std::string>& questMods);
    std::vector<std::string> WithStoryMods(const std::vector<std::string>& listed,
        const std::vector<std::string>& loadOrder);
}
