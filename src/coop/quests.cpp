// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/quests.h"

#include <cstring>

namespace coop
{
    QuestNews CompareQuest(const net::QuestState& before, const net::QuestState& after)
    {
        QuestNews news;
        auto newly = [&](uint8_t flag) { return !(before.flags & flag) && (after.flags & flag); };
        news.added = newly(kQuestShown);
        news.completed = newly(kQuestCompleted);
        news.failed = newly(kQuestFailed);
        for (const net::QuestObjective& o : after.objectives)
        {
            uint8_t was = 0;
            for (const net::QuestObjective& b : before.objectives)
                if (b.id == o.id)
                    was = b.status;
            if ((o.status & 1) && !(was & 1))
                news.shown.push_back(o.id);
            if ((o.status & 2) && !(was & 2))
                news.finished.push_back(o.id);
        }
        return news;
    }

    bool IsGameFile(const std::string& name)
    {
        static const char* const kFiles[] = {"FalloutNV.esm", "DeadMoney.esm", "HonestHearts.esm", "OldWorldBlues.esm",
            "LonesomeRoad.esm", "GunRunnersArsenal.esm", "ClassicPack.esm", "MercenaryPack.esm", "TribalPack.esm",
            "CaravanPack.esm"};
        for (const char* f : kFiles)
            if (_stricmp(f, name.c_str()) == 0)
                return true;
        return false;
    }

    bool FromGameFile(uint32_t formId, const std::vector<std::string>& loadOrder)
    {
        size_t index = formId >> 24;
        return index < loadOrder.size() && IsGameFile(loadOrder[index]);
    }

    bool IsPerPlayerQuest(uint32_t formId, const std::vector<std::string>& loadOrder)
    {
        struct PerPlayer { const char* file; uint32_t id; };
        static const PerPlayer kQuests[] = {
            {"DeadMoney.esm", 0x006E93},
            {"DeadMoney.esm", 0x00E882},
            {"DeadMoney.esm", 0x00C543},
            {"DeadMoney.esm", 0x013F58},
            {"FalloutNV.esm", 0x0CE959},
        };
        size_t index = formId >> 24;
        if (index >= loadOrder.size())
            return false;
        for (const PerPlayer& q : kQuests)
            if ((formId & 0xFFFFFF) == q.id && _stricmp(loadOrder[index].c_str(), q.file) == 0)
                return true;
        return false;
    }

    bool HostRunsQuestsOf(uint32_t formId, const std::vector<std::string>& loadOrder,
        const std::vector<std::string>& questMods)
    {
        size_t index = formId >> 24;
        if (index >= loadOrder.size() || IsPerPlayerQuest(formId, loadOrder))
            return false;
        if (IsGameFile(loadOrder[index]))
            return true;
        for (const std::string& mod : questMods)
            if (_stricmp(mod.c_str(), loadOrder[index].c_str()) == 0)
                return true;
        return false;
    }

    std::vector<std::string> WithStoryMods(const std::vector<std::string>& listed,
        const std::vector<std::string>& loadOrder)
    {
        static const char* const kStoryMods[] = {"TaleOfTwoWastelands.esm", "NewCalifornia.esm"};
        std::vector<std::string> mods = listed;
        for (const char* story : kStoryMods)
        {
            bool loaded = false, already = false;
            for (const std::string& f : loadOrder)
                loaded |= _stricmp(f.c_str(), story) == 0;
            for (const std::string& m : mods)
                already |= _stricmp(m.c_str(), story) == 0;
            if (loaded && !already)
                mods.push_back(story);
        }
        return mods;
    }
}
