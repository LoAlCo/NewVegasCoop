// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

struct NVSEInterface;
namespace game { class Inspector; class Mirror; class Weather; }

namespace dev
{
    bool RegisterCommands(NVSEInterface* nvse, game::Inspector* inspector, game::Mirror* mirror, game::Weather* weather);
    void EveryFrame();
}
