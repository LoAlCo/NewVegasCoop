// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

namespace game
{
    void KeepWorldRunningInEscapeMenu();
    void EscapeMenuLive(bool live);
    // A co-op box (a question, the trade window, its "how many?") is what's up: the world keeps running behind it.
    void CoopBoxesLive(bool live);
}
