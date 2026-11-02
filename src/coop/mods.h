// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <string>
#include <vector>

namespace coop
{
    using ModList = std::vector<net::ModFile>;

    bool IsGameArchive(const std::string& archive);
    bool IsMadeFromOwnGame(const std::string& name);
    bool ArchiveBelongsTo(const std::string& archive, const std::string& plugin);

    net::Hash ModsFingerprint(const ModList& mods);

    struct ModDifference
    {
        std::vector<std::string> missing;
        std::vector<std::string> different;
        std::vector<std::string> extra;
        bool orderDiffers = false;
        size_t orderAt = 0;
        std::string orderHost, orderMine;
        std::vector<std::string> missingDlls, differentDlls;

        bool Blocks() const { return !missing.empty() || !different.empty() || !extra.empty() || orderDiffers; }
        bool Any() const { return Blocks() || !missingDlls.empty() || !differentDlls.empty(); }
    };
    ModDifference CompareMods(const ModList& host, const ModList& mine);

    std::string DescribeForScreen(const ModDifference& d, const std::string& hostName);
    std::vector<std::string> DescribeForLog(const ModDifference& d);

    ModList ScanMods(const std::string& dataFolder, const std::vector<std::string>& plugins,
        const std::string& cacheFile, const std::string& skipDll);
}
