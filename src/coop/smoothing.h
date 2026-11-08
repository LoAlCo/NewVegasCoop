// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop
{
    struct Pose
    {
        uint32_t space = 0;
        float x = 0, y = 0, z = 0;
        float heading = 0;
        uint8_t flags = 0;
        uint16_t moves = 0;
        float pitch = 0;
        uint16_t runSpeed = 0;

        static constexpr uint8_t kWeaponOut = 0x01;
        static constexpr uint8_t kSneaking = 0x02;
        static constexpr uint8_t kKnownFlags = kWeaponOut | kSneaking;
        static constexpr uint8_t kLightOn = 0x04;
    };

    class Smoother
    {
    public:
        static constexpr uint32_t kDelayMs = 50;

        void Add(uint32_t senderMs, const Pose& pose, uint32_t localMs);

        bool Sample(uint32_t localMs, Pose& out) const;
        bool Newest(Pose& out) const
        {
            if (m_count == 0)
                return false;
            out = m_snaps[m_count - 1].pose;
            return true;
        }

        void Clear() { m_count = 0; m_hasOffset = false; }

    private:
        struct Snap { uint32_t senderMs; Pose pose; };
        static constexpr size_t kMaxSnaps = 16;
        std::array<Snap, kMaxSnaps> m_snaps{};
        size_t m_count = 0;
        int32_t m_offset = 0;
        bool m_hasOffset = false;
    };
}
