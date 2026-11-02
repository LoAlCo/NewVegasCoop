// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/game_mode.h"

namespace coop
{
    std::vector<std::string> SaveModeButtons()
    {
        return {"Neither", "Co-op balanced", "Friendly fire", "Both"};
    }

    SaveMode SaveModeFromButton(int button)
    {
        if (button < 0 || button > 3)
            return {};
        return ModeFromSwitches(button == 1 || button == 3, button >= 2);
    }

    SaveMode ModeFromSwitches(bool coopBalanced, bool friendlyFire)
    {
        return SaveMode{coopBalanced ? GameMode::Balanced : GameMode::Normal, friendlyFire};
    }

    std::string Describe(const SaveMode& m)
    {
        if (m.mode == GameMode::Unset)
            return "not known yet";
        return std::string("Co-op balanced ") + (m.mode == GameMode::Balanced ? "on" : "off") + ", friendly fire " +
            (m.friendlyFire ? "on" : "off");
    }
}
