// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

namespace coop::vats
{
    struct GameSetting
    {
        const char* name;
        float value;
    };

    constexpr GameSetting kSettings[] = {
        {"fVATSTargetFOVMultNear", 0.0f},
        {"fVATSTargetFOVMultFar", 0.0f},
        {"fVATSTargetSelectCamPanTime", 0.25f},
        {"fVATSCamZoomInTime", 0.25f},
        {"fVATSPlaybackDelay", 0.04f},
    };

    inline float FovFloor(float worldFov)
    {
        return worldFov >= 10.0f && worldFov <= 170.0f ? worldFov : 0.0f;
    }

    constexpr int kLifeStateEssentialDown = 6;
    inline bool DeathPlaybackAllowed(int lifeState) { return lifeState != kLifeStateEssentialDown; }
}
