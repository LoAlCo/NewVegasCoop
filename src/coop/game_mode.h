// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coop
{
    enum class GameMode : uint8_t { Unset = 0, Normal = 1, Balanced = 2 };

    struct SaveMode
    {
        GameMode mode = GameMode::Unset;
        bool friendlyFire = false;
        bool operator==(const SaveMode& o) const { return mode == o.mode && friendlyFire == o.friendlyFire; }
    };

    std::vector<std::string> SaveModeButtons();
    SaveMode SaveModeFromButton(int button);
    SaveMode ModeFromSwitches(bool coopBalanced, bool friendlyFire);
    std::string Describe(const SaveMode& m);
}
