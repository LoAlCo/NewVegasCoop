// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>

namespace coop
{
    enum class RollMatch { Unknown, Same, Different };

    inline RollMatch CompareRolls(uint32_t hostRoll, uint32_t ourRoll)
    {
        if (!hostRoll || !ourRoll)
            return RollMatch::Unknown;
        return hostRoll == ourRoll ? RollMatch::Same : RollMatch::Different;
    }

    inline uint32_t MakeCopyFrom(uint32_t base, uint32_t hostRoll) { return hostRoll ? hostRoll : base; }
}
