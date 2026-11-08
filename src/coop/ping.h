// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"
#include "net/protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
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
        uint8_t kind = 0;
    };

    // One ping per player. Each game runs the 15 seconds on its own clock.
    class PingBoard
    {
    public:
        void Add(uint8_t player, const std::string& who, const std::string& name, float x, float y, float z, uint32_t now,
            uint8_t kind = 0)
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
            *slot = ActivePing{true, player, who, name, x, y, z, now + kPingLifeMs, kind};
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

    enum class PingThing : uint8_t
    {
        Nothing,
        Scenery,
        Actor,
        Loot,
        Device,
        Door
    };
    constexpr size_t kMaxPingThingName = 48;

    // Workbenches and the like are ordinary activators in the game's data, so their names are what sets them apart. This
    // catches the DLCs' and other mods' too; in a translated game they just show as a marked spot.
    inline bool IsUtilityName(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char* word : {"workbench", "work bench", "reloading bench", "campfire", "vending"})
            if (name.find(word) != std::string::npos)
                return true;
        return false;
    }

    struct PingLook
    {
        net::PingKind kind = net::PingKind::Spot;
        std::string name;
    };

    // How the menu's Ping Icons option shows them.
    enum class PingIcons : uint8_t
    {
        Off,
        HudColour,
        OwnColours
    };

    // What a ping marks once the player's Recognise Objects choice is applied.
    inline PingThing ChosenPingThing(PingThing seen, bool recognise)
    {
        return recognise || seen == PingThing::Nothing ? seen : PingThing::Scenery;
    }

    // Actors stay plain spots until pings can follow them.
    inline PingLook ClassifyPing(PingThing thing, const std::string& name)
    {
        std::string shown = name.substr(0, kMaxPingThingName);
        switch (thing)
        {
        case PingThing::Nothing:
            return {net::PingKind::Spot, "Over here"};
        case PingThing::Loot:
            return {net::PingKind::Loot, shown.empty() ? "Loot" : shown};
        case PingThing::Device:
            if (IsUtilityName(shown))
                return {net::PingKind::Utility, shown};
            [[fallthrough]];
        case PingThing::Door:
            return {net::PingKind::Spot, shown.empty() ? "Marked spot" : shown};
        default:
            return {net::PingKind::Spot, "Marked spot"};
        }
    }

    // dx and dy run from the viewer to the spot.
    inline std::string PingLine(const std::string& who, const std::string& name, float dx, float dy)
    {
        int metres = static_cast<int>(std::sqrt(dx * dx + dy * dy) / kPingUnitsPerMetre + 0.5f);
        std::string line = who + ": " + name + " (" + std::to_string(metres) + " m)";
        // The game's text reads a line starting with one of these as markup.
        if (line[0] == '<' || line[0] == '{')
            line[0] = '(';
        return line;
    }

    struct PingLabelSpot
    {
        float x = 0.5f, y = 0.5f;
    };

    // Keeps an icon and the line under it wholly on screen.
    inline PingLabelSpot PlacePingLabel(float x, float y)
    {
        constexpr float kEdgeX = 0.14f, kEdgeTop = 0.04f, kEdgeBottom = 0.1f;
        return {std::clamp(x, kEdgeX, 1.0f - kEdgeX), std::clamp(y, kEdgeTop, 1.0f - kEdgeBottom)};
    }

    // Icons sit exactly on their spots unless two would cover each other: then the later one moves up.
    inline void SpreadPingLabels(std::array<PingLabelSpot, kPingSlots>& spots, const std::array<bool, kPingSlots>& shown)
    {
        constexpr float kWidth = 0.2f, kHeight = 0.09f;
        for (int slot = 1; slot < kPingSlots; ++slot)
        {
            if (!shown[slot])
                continue;
            for (int tries = 0; tries < kPingSlots; ++tries)
            {
                bool clear = true;
                for (int other = 0; other < slot; ++other)
                    if (shown[other] && std::abs(spots[slot].x - spots[other].x) < kWidth &&
                        std::abs(spots[slot].y - spots[other].y) < kHeight)
                    {
                        spots[slot].y = spots[other].y - kHeight;
                        clear = false;
                    }
                if (clear)
                    break;
            }
        }
    }
}
