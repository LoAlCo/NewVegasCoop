// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "coop/far_area.h"

class TESObjectCELL;

namespace game
{
    bool AttachFarInterior(uint32_t cellId, std::string& why);
    bool DetachFarInterior(uint32_t cellId, std::string& why);
    int FarInteriorState(uint32_t cellId, bool& hidden, std::vector<uint32_t>& actors);
    bool ShowFarInterior(uint32_t cellId, bool show);
    void LetGoOfFarInteriors();
    bool IsInteriorPlace(uint32_t space);
    bool IsFarInterior(uint32_t space);
    std::vector<const TESObjectCELL*> FarInteriorCells();
    uint32_t FarInteriorsChanged();
    void UpdateFarInteriors(bool hosting, const std::vector<coop::FriendPlace>& friends, uint32_t usedMb, uint32_t now);
    bool InstallFarInteriorGuard();
    bool InstallEnterGuard();
    std::string FootstepStats();
}
