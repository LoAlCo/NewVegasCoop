// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

// The trade window: the game's barter screen, with a hidden temporary chest as the other side. The chest holds copies
// of what the partner has put up, every price comes from their stall (0 shows as "--"), and Accept moves nothing:
// the picks are handed to the mod, which sends them as listings and purchases.
namespace game
{
    struct StallEntry
    {
        net::ItemCount item;
        int32_t price = 0;
    };
    struct TradePick
    {
        net::ItemCount item;
        uint8_t itemType = 0;
        bool fromPartner = false;
    };

    // Finds the barter screen's price and Accept calls and redirects them. False (logged) if they aren't where expected.
    bool InstallTradeWindow();
    bool TradeWindowReady();
    bool OpenTradeWindow(const std::string& title, const std::vector<StallEntry>& stall);
    // Our trade window is the barter screen that's open.
    bool TradeWindowShown();
    // Something of ours is still placed (the window is open or closing).
    bool TradeWindowExists();
    // Puts the partner's new stall in the open window. False while picks are marked (they point at the chest's stacks).
    bool RefreshTradeWindow(const std::vector<StallEntry>& stall);
    void CloseTradeWindow();
    // Removes the chest once the window is gone. Call every frame.
    void TidyTradeWindow();
    // A save is loading: the chest goes with the old world, so only forget it.
    void ForgetTradeWindow();
    // The picks from an Accept pressed since the last call, matched to real stacks.
    bool TakeTradeAccept(std::vector<TradePick>& picks);
    std::string ItemName(uint32_t form);
    // Why this kind of item can't be put up, or nullptr: quest items, keys, notes, the teleporter, worn gear, and items
    // made while playing (their IDs differ between games).
    const char* WhyNotTradeable(const net::ItemCount& kind);
}
