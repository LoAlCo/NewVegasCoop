// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>

namespace coop
{
    constexpr uint32_t kGlideMs = 450;
    constexpr uint32_t kHeldStayMs = 10000;

    // A world item we may move: still a reference, in a place, not deleted (taken) and not switched off.
    inline bool CanMoveItem(bool exists, bool inCell, bool deleted, bool disabled)
    {
        return exists && inCell && !deleted && !disabled;
    }

    // Lights count as inventory objects to the game, but only carryable ones are items; the rest are fixtures.
    constexpr uint8_t kLightFormType = 0x1E;
    constexpr uint32_t kLightCanBeCarried = 2;
    inline bool IsSyncedItemType(uint8_t formType, uint32_t lightFlags)
    {
        return formType != kLightFormType || (lightFlags & kLightCanBeCarried) != 0;
    }

    enum class GlideStep : uint8_t
    {
        Move,       // still on its way: place it at the in-between spot
        PlaceLast,  // arrived: place it at the target once, then let physics have it
        Done,       // a carried item's hold has run out: stop
    };

    // Loose items are placed once on arrival; only an item a friend carries is held at their spot every frame.
    inline GlideStep StepGlide(uint32_t age, bool held)
    {
        if (age < kGlideMs)
            return GlideStep::Move;
        if (!held)
            return GlideStep::PlaceLast;
        return age > kGlideMs + kHeldStayMs ? GlideStep::Done : GlideStep::Move;
    }
}
