// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "coop/gear_lockers.h"
#include "coop/activator_uses.h"
#include "coop/chat.h"
#include "coop/quests.h"
#include "coop/talk_log.h"
#include "coop/vats_feel.h"
#include "coop/balance.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <utility>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <deque>
#include <functional>
#include <intrin.h>
#include "coop/attack_versions.h"
#include "coop/clock.h"
#include "coop/shot_counter.h"
#include "game/addresses.h"
#include "game/mover.h"
#include "log.h"
#include "nvse/PluginAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"
#include <map>
#include <set>
#include <string>
#include "game/internal.h"

namespace game
{
    void SetUpGearLockers(const std::vector<std::string>& loadOrder)
    {
        g_gearLockers.Clear();
        size_t found = 0;
        for (const coop::GearLockerId& l : coop::kGearLockers)
            for (size_t i = 0; i < loadOrder.size() && i < 0xFF; ++i)
                if (_stricmp(loadOrder[i].c_str(), l.file) == 0)
                {
                    uint32_t id = static_cast<uint32_t>(i) << 24 | l.id;
                    TESObjectREFR* ref = LookupRef(id);
                    if (!IsContainerRef(ref))
                    {
                        LOG_WARN("Gear: %s's locker %08X isn't a container here: left out", l.where, id);
                        break;
                    }
                    g_gearLockers.Add(id, l.line);
                    ++found;
                    break;
                }
        LOG_INFO("Gear: %zu gear lockers (each game's copy holds its own player's gear)", found);
        g_rewardBox = g_rewardQuest = 0;
        g_rewardItems.clear();
        const coop::RewardBox& r = coop::kHonestHeartsReward;
        for (size_t i = 0; i < loadOrder.size() && i < 0xFF; ++i)
            if (_stricmp(loadOrder[i].c_str(), r.file) == 0)
            {
                uint32_t top = static_cast<uint32_t>(i) << 24;
                if (!IsContainerRef(LookupRef(top | r.box)) || !LookupForm(top | r.quest))
                {
                    LOG_WARN("Gear: %s's reward box %08X isn't as expected: shared as any box", r.file, top | r.box);
                    break;
                }
                g_rewardBox = top | r.box;
                g_rewardQuest = top | r.quest;
                for (const coop::RewardItem& item : r.items)
                    if (TESForm* form = LookupForm(top | item.id); form && IsItem(form->refID))
                        g_rewardItems.push_back({top | item.id, item.health});
                g_gearLockers.AddReward(g_rewardBox);
                LOG_INFO("Gear: %s's reward box %08X is each player's own (%zu items)", r.file, g_rewardBox,
                    g_rewardItems.size());
                break;
            }
    }

    bool IsGearLocker(uint32_t ref) { return g_gearLockers.IsPrivate(ref); }

    void UpdateRewardBoxes(bool host)
    {
        if (!g_rewardBox || !g_rewardFlag || !g_rewardMark || !g_rewardAdd || !Player())
            return;
        TESObjectREFR* player = Player();
        if (Number(g_rewardFlag, player) != 0)
            return;
        QuestInfo q;
        if (!ReadQuest(g_rewardQuest, q) ||
            std::find(q.done.begin(), q.done.end(), coop::kHonestHeartsReward.stage) == q.done.end())
            return;
        if (host)
        {
            g_scripts->CallFunctionAlt(g_rewardMark, player, 0);
            LOG_INFO("Gear: the reward stage is done here; our player's box was filled by the game's own stage");
            return;
        }
        TESObjectREFR* box = LookupRef(g_rewardBox);
        std::vector<Item> mine, inBox;
        if (!IsContainerRef(box) || !Contents(player, mine) || !Contents(box, inBox))
            return;
        auto count = [](const std::vector<Item>& items, uint32_t id) {
            int32_t n = 0;
            for (const Item& i : items)
                if (i.id == id)
                    n += i.count;
            return n;
        };
        size_t n = g_rewardItems.size();
        std::vector<coop::RewardItem> add = coop::RewardToAdd(g_rewardItems,
            [&](uint32_t id) { return count(mine, id) + count(inBox, id); });
        for (const coop::RewardItem& i : add)
            if (TESForm* form = LookupForm(i.id))
                g_scripts->CallFunctionAlt(g_rewardAdd, box, 2, form, FloatArg(i.health));
        g_scripts->CallFunctionAlt(g_rewardMark, player, 0);
        Contents(box, inBox);
        LOG_INFO("Gear: our own reward box %08X filled: %zu of %zu items added (the rest were ours already); it holds "
            "%zu kinds", g_rewardBox, add.size(), n, inBox.size());
    }

    std::vector<net::PlayerCommand> GearForJoiner()
    {
        return g_gearLockers.ForJoiner([](uint32_t id) {
            std::vector<Item> items;
            TESObjectREFR* ref = LookupRef(id);
            return ref && Contents(ref, items) && !items.empty();
        });
    }
}
