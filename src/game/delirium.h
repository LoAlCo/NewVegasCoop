// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

// FNV Delirium, the separate ragdoll hit-reaction plugin, if it's installed. Everything here does nothing when it
// isn't, or while syncing is off (the MCM toggle, or not in a co-op game): the game then behaves as without this.

#include "coop/DeliriumApi.h"

#include <cstdint>
#include <vector>

namespace game::delirium
{
    // Finds the plugin (call once every plugin has loaded) and hooks our copy check and listener into it.
    bool Load(bool (*isCopy)(uint32_t localRef));
    bool Present();
    // Syncing on: the MCM toggle is on and we're in a co-op game. Off: copies aren't marked, events are dropped.
    void SetOn(bool on);
    bool On();

    bool IsReacting(uint32_t localRef);
    std::vector<DeliriumEvent> TakeEvents();
    bool GetRoot(uint32_t localRef, float out[7]);
    bool StartReaction(const DeliriumReaction& reaction);
    void SteerRoot(uint32_t localRef, const float root[7]);
    void ForceOutcome(uint32_t localRef, uint32_t outcome, const float pos[3], float heading);
    void ReportHit(const DeliriumHit& hit);
    bool GetLastHit(uint32_t localRef, DeliriumHit& out);
}
