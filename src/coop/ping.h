// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace coop
{
    constexpr uint32_t kPingLifeMs = 15000;
    constexpr uint32_t kPingSendGapMs = 1000;
    constexpr uint32_t kPingRelayGapMs = 500;
    constexpr int kPingSlots = 5;
    constexpr float kPingUnitsPerMetre = 70.0f;

    class PingGate
    {
    public:
        explicit PingGate(uint32_t gapMs) : m_gap(gapMs) {}
        bool Allow(uint32_t now)
        {
            if (m_used && now - m_last < m_gap)
                return false;
            m_used = true;
            m_last = now;
            return true;
        }

    private:
        uint32_t m_gap;
        uint32_t m_last = 0;
        bool m_used = false;
    };

    // Checked once by each receiver when a ping arrives. A null pose means that position isn't known yet.
    inline bool PingInRange(const Pose* me, const Pose* sender, int radius)
    {
        if (!me || !sender || me->space != sender->space)
            return false;
        if (radius <= 0)
            return true;
        float dx = sender->x - me->x, dy = sender->y - me->y, dz = sender->z - me->z;
        return dx * dx + dy * dy + dz * dz <= static_cast<float>(radius) * static_cast<float>(radius);
    }

    struct ActivePing
    {
        bool used = false;
        uint8_t player = 0;
        std::string who, name;
        float x = 0, y = 0, z = 0;
        uint32_t until = 0;
    };

    // One ping per player. Each game runs the 15 seconds on its own clock.
    class PingBoard
    {
    public:
        void Add(uint8_t player, const std::string& who, const std::string& name, float x, float y, float z, uint32_t now)
        {
            ActivePing* slot = nullptr;
            for (ActivePing& p : m_slots)
                if (p.used && p.player == player)
                    slot = &p;
            for (ActivePing& p : m_slots)
                if (!slot && !p.used)
                    slot = &p;
            if (!slot)
            {
                slot = &m_slots[0];
                for (ActivePing& p : m_slots)
                    if (static_cast<int32_t>(p.until - slot->until) < 0)
                        slot = &p;
            }
            *slot = ActivePing{true, player, who, name, x, y, z, now + kPingLifeMs};
        }
        void Expire(uint32_t now)
        {
            for (ActivePing& p : m_slots)
                if (p.used && static_cast<int32_t>(now - p.until) >= 0)
                    p = ActivePing{};
        }
        void Remove(uint8_t player)
        {
            for (ActivePing& p : m_slots)
                if (p.used && p.player == player)
                    p = ActivePing{};
        }
        void Clear() { m_slots.fill(ActivePing{}); }
        const std::array<ActivePing, kPingSlots>& Slots() const { return m_slots; }

    private:
        std::array<ActivePing, kPingSlots> m_slots;
    };

    // dx and dy run from the viewer to the spot, +Y being north.
    inline std::string PingLine(const std::string& who, const std::string& name, float dx, float dy)
    {
        static const char* const kDirections[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        float degrees = std::atan2(dx, dy) * 180.0f / 3.14159265f;
        int sector = static_cast<int>(std::floor((degrees + 22.5f) / 45.0f));
        sector = ((sector % 8) + 8) % 8;
        int metres = static_cast<int>(std::sqrt(dx * dx + dy * dy) / kPingUnitsPerMetre + 0.5f);
        std::string line = who + ": " + name + " (" + std::to_string(metres) + " m " + kDirections[sector] + ")";
        // The game's text reads a line starting with one of these as markup.
        if (line[0] == '<' || line[0] == '{')
            line[0] = '(';
        return line;
    }

    struct PingLabelSpot
    {
        float x = 0.5f, y = 0.5f;
    };

    // Keeps a label wholly on screen, and stops labels at the same spot covering each other.
    inline PingLabelSpot PlacePingLabel(float x, float y, int slot)
    {
        constexpr float kEdgeX = 0.14f, kEdgeY = 0.05f, kLineHeight = 0.035f;
        return {std::clamp(x, kEdgeX, 1.0f - kEdgeX), std::clamp(y - kLineHeight * static_cast<float>(slot), kEdgeY, 1.0f - kEdgeY)};
    }
}
