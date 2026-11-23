// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>
#include <string>

namespace dev
{
    bool WatchCallSite(uintptr_t site, std::string& why);

    std::string RegisterWatchReport();
}
