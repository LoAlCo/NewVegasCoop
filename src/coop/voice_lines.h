// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

// The voice line wheel: hold a key, pick a phrase, and your character says a matching vanilla NPC line out loud.
// Every line here is in FalloutNV.esm with voice and lip files for all the voices below, and none runs a script.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace coop
{
    struct VoiceLineForm
    {
        uint32_t topic = 0, info = 0;
        const char* text = "";
    };

    struct VoicePhrase
    {
        const char* name = "";
        VoiceLineForm lines[4];
        uint8_t count = 0;
    };

    // Slot 0 is the middle of the wheel; 1-8 go clockwise from the top.
    constexpr int kVoiceSlots = 9;
    constexpr VoicePhrase kVoicePhrases[kVoiceSlots] = {
        {"Hey", {{0x000000D2, 0x0017529D, "Hey."}, {0x000000D2, 0x0017529A, "Hello."}, {0x000000D4, 0x001751C5, "Later."}}, 3},
        {"Help", {{0x000000DE, 0x0017525B, "Help!"}, {0x000000EE, 0x001750E9, "Help me out!"}}, 2},
        {"Look Out", {{0x000000ED, 0x00175210, "Look out!"}, {0x0000011A, 0x001750BD, "Watch out!"}}, 2},
        {"Over Here", {{0x000000EE, 0x001750E6, "Over here!"}}, 1},
        {"Enemy Spotted", {{0x000000EE, 0x001750E8, "I've got one!"}, {0x000000F3, 0x001751DA, "Found you!"}}, 2},
        {"Grenade", {{0x000000CA, 0x001750CF, "Grenade!"}, {0x000000EC, 0x0017528F, "Incoming!"},
            {0x000000EC, 0x0017528E, "Get down!"}, {0x000000CA, 0x001750D0, "Take cover!"}}, 4},
        {"Retreat", {{0x000000DE, 0x00175259, "I'm out of here!"}}, 1},
        {"Taunt", {{0x000000DC, 0x001751E8, "Had enough?"}, {0x000000DC, 0x001751E9, "Come on!"},
            {0x000000DC, 0x001751EA, "You like that?"}, {0x000000F6, 0x0017518B, "Teach you to mess with me."}}, 4},
        {"Thanks", {{0x00000116, 0x0017511B, "Thanks."}}, 1},
    };

    // Generic voices that have every line above (voice type forms in FalloutNV.esm).
    constexpr uint32_t kMaleVoices[] = {
        0x0000002D, // MaleAdult01Default
        0x0002AB62, // MaleAdult01DefaultB
        0x00019FC8, // MaleAdult02
        0x00019FCA, // MaleAdult04
        0x000717E1, // MaleAdult05
        0x0013C8D7, // MaleAdult07
        0x0013C8D8, // MaleAdult08
        0x0013C8D9, // MaleAdult09
        0x00019FD2, // MaleOld01
        0x0013C8DB, // MaleOld02
        0x00061EA7, // MaleGenericGhoul
    };
    constexpr uint32_t kFemaleVoices[] = {
        0x0000002E, // FemaleAdult01Default
        0x00019FCB, // FemaleAdult02
        0x0013C8CD, // FemaleAdult03
        0x00019FCD, // FemaleAdult04
        0x0013C8CF, // FemaleAdult06
        0x0013C8D0, // FemaleAdult07
        0x0013C8D2, // FemaleAdult09
        0x0013C8D3, // FemaleAdult10
        0x00019FD5, // FemaleOld01
        0x0013C8D5, // FemaleOld02
        0x00061EA8, // FemaleGenericGhoul
        0x00143779, // FemaleGhoul02
    };
    constexpr int kMostVoiceChoices = 12;

    // The MCM's Voice choice (0-11, shown as Voice 1-12) for a male or female character. Men have 11 voices, so
    // Voice 12 is Voice 1 again for them.
    inline uint32_t VoiceFor(bool female, int choice)
    {
        size_t n = female ? std::size(kFemaleVoices) : std::size(kMaleVoices);
        size_t i = choice < 0 ? 0 : static_cast<size_t>(choice) % n;
        return female ? kFemaleVoices[i] : kMaleVoices[i];
    }

    inline bool IsWheelVoice(uint32_t voice)
    {
        for (uint32_t v : kMaleVoices)
            if (v == voice)
                return true;
        for (uint32_t v : kFemaleVoices)
            if (v == voice)
                return true;
        return false;
    }

    inline bool IsVoiceLine(uint8_t phrase, uint8_t line)
    {
        return phrase < kVoiceSlots && line < kVoicePhrases[phrase].count;
    }

    // The pointer is how far the view was turned since the wheel opened (radians): x to the right, y up.
    constexpr float kWheelDeadZone = 0.035f;
    constexpr float kWheelReach = 0.09f;

    // Which slot the pointer is on: 0 (middle) inside the dead zone, else 1-8 clockwise from the top.
    inline int WheelSlot(float x, float y)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || x * x + y * y < kWheelDeadZone * kWheelDeadZone)
            return 0;
        constexpr float kTurn = 6.2831853f;
        float angle = std::atan2(x, y);
        if (angle < 0)
            angle += kTurn;
        int sector = static_cast<int>(std::floor((angle + kTurn / 16) / (kTurn / 8))) % 8;
        return 1 + sector;
    }

    // Keeps the pointer within reach, so turning far one way doesn't need turning as far back.
    inline void ClampWheelPointer(float& x, float& y)
    {
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            x = y = 0;
            return;
        }
        float length = std::sqrt(x * x + y * y);
        if (length > kWheelReach)
        {
            x *= kWheelReach / length;
            y *= kWheelReach / length;
        }
    }

    // Where a slot's label sits around the middle (unit circle, y down as on screen).
    inline void WheelSlotPlace(int slot, float& x, float& y)
    {
        if (slot <= 0 || slot >= kVoiceSlots)
        {
            x = y = 0;
            return;
        }
        float angle = (slot - 1) * 6.2831853f / 8;
        x = std::sin(angle);
        y = -std::cos(angle);
    }

    // Lines a player may say: one every so often, and a short burst at most, so the wheel can't be spammed.
    constexpr uint32_t kVoiceLineGapMs = 1200;
    constexpr int kVoiceLinesPerWindow = 4;
    constexpr uint32_t kVoiceLineWindowMs = 8000;

    class VoiceLineLimiter
    {
    public:
        // The host checks with half the gap, since lines can arrive bunched up.
        explicit VoiceLineLimiter(uint32_t gapMs = kVoiceLineGapMs) : m_gap(gapMs) {}

        bool Allow(uint32_t now)
        {
            if (m_any && static_cast<int32_t>(now - m_last) < static_cast<int32_t>(m_gap))
                return false;
            int recent = 0;
            for (int i = 0; i < m_filled; ++i)
                if (static_cast<int32_t>(now - m_times[i]) < static_cast<int32_t>(kVoiceLineWindowMs))
                    ++recent;
            if (recent >= kVoiceLinesPerWindow)
                return false;
            m_times[m_next] = now;
            m_next = (m_next + 1) % kVoiceLinesPerWindow;
            if (m_filled < kVoiceLinesPerWindow)
                ++m_filled;
            m_last = now;
            m_any = true;
            return true;
        }

    private:
        uint32_t m_gap;
        uint32_t m_times[kVoiceLinesPerWindow] = {};
        int m_next = 0, m_filled = 0;
        uint32_t m_last = 0;
        bool m_any = false;
    };
}
