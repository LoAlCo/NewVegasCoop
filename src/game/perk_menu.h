// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <vector>

namespace game
{
    void TakeOverLevelUpPerks();
    void SetPerksTakenByOthers(const std::vector<uint32_t>& perks);
    void UpdateLevelUpNote();
    constexpr char kPartyPerkTakenText[] =
        "Someone else in your party already has this perk, it applies to the whole group and doesn't stack.";
    constexpr float kPartyPerkRed[3] = {255, 60, 60};
}
