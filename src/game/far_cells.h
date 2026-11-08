// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "coop/far_area.h"

namespace game
{
    int FarCellState(int x, int y);
    bool AttachFarCell(int x, int y, std::string& why);
    bool DetachFarCell(int x, int y, std::string& why);
    bool IsGridCell(int x, int y);
    void UpdateFarCells(uint32_t now);
    bool InstallFarCellGuard();
    int FarCellCount();
    void LetGoOfFarCells();
    void UpdateFarArea(bool hosting, const std::vector<coop::FriendCell>& friends, uint32_t usedMb, uint32_t now);
    bool InFarCell(float x, float y);
}
