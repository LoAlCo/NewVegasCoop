// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    constexpr uint32_t kDoorUseWaitMs = 2000;
    constexpr size_t kMaxKnownDoors = 512;

    // What a door shares. A door that leads away isn't open or closed for anyone else: nobody sees through it, and
    // its saved open flag can differ between two players' saves and doesn't change when the game is told to close it.
    // Its lock still counts.
    inline uint8_t SharedDoorFlags(bool open, bool leadsAway, bool locked)
    {
        return static_cast<uint8_t>((open && !leadsAway ? net::kDoorOpen : 0) | (locked ? net::kDoorLocked : 0));
    }

    struct LocalDoor
    {
        net::DoorState state;
        bool nearMe = false;
    };

    class FriendDoors
    {
    public:
        void Heard(const net::DoorState& host);
        std::vector<net::DoorState> Observe(uint32_t now, const std::vector<LocalDoor>& local,
            std::vector<net::DoorState>& uses);
        void Clear() { m_doors.clear(); }
        size_t Known() const { return m_doors.size(); }

    private:
        struct Door
        {
            net::DoorState host, last;
            bool heard = false, seen = false;
            uint32_t waitUntil = 0;
        };
        std::map<uint32_t, Door> m_doors;
    };
}
