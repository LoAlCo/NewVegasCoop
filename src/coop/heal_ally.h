// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>

namespace coop
{
    constexpr uint32_t kStimpak = 0x00015169;
    constexpr uint32_t kSuperStimpak = 0x000CCEF2;
    constexpr uint32_t kHealHoldMs = 2000;
    constexpr uint32_t kHealGapMs = 1000;
    constexpr uint32_t kFastMetabolism = 0x00094EBF;

    inline bool IsHealItem(uint32_t item) { return item == kStimpak || item == kSuperStimpak; }

    inline float HealBase(uint32_t item, bool fastMetabolism)
    {
        if (item == kStimpak)
            return fastMetabolism ? 36.0f : 30.0f;
        if (item == kSuperStimpak)
            return fastMetabolism ? 72.0f : 60.0f;
        return 0;
    }

    inline float HealAmount(uint32_t item, int medicine, bool fastMetabolism)
    {
        if (medicine < 0)
            medicine = 0;
        if (medicine > 100)
            medicine = 100;
        return HealBase(item, fastMetabolism) * (1.0f + medicine * 0.02f);
    }

    inline float HealCorrection(uint32_t item, int giverMedicine, bool giverFast, int myMedicine, bool myFast)
    {
        return HealAmount(item, giverMedicine, giverFast) - HealAmount(item, myMedicine, myFast);
    }
}
