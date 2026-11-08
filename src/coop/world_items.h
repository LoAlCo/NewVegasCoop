// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/desync.h"
#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace coop
{
    constexpr float kItemMoveDistance = 32;
    constexpr float kItemTurnAngle = 0.35f;
    constexpr float kItemNudge = 2;
    constexpr float kItemNudgeAngle = 0.03f;
    constexpr uint32_t kOwnItemMs = 2000;
    constexpr uint32_t kHeldResendMs = 2000;
    constexpr uint32_t kHostItemStaleMs = 7000;
    constexpr size_t kMaxKnownItems = 4000;

    struct LocalItem
    {
        uint32_t ref = 0;
        uint32_t base = 0;
        int32_t count = 1;
        bool present = true;
        float x = 0, y = 0, z = 0;
        bool nearMe = false;
        float rx = 0, ry = 0, rz = 0;
        bool held = false;
        bool following = false;
        bool movable = false;
        bool deleted = false;
        uint32_t parent = 0;
        bool parentOn = false;
        uint8_t health = net::kNoCondition;
        uint8_t mods = 0;
    };

    struct ItemPose
    {
        float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
        bool Differs(const ItemPose& other, float distance, float angle) const;
    };

    struct ItemActions
    {
        std::vector<net::ItemState> make;
        std::vector<uint32_t> remove;
        std::vector<uint32_t> enable;
        struct Parent { uint32_t ref; bool on; };
        std::vector<Parent> parents;
        struct Move
        {
            uint32_t ref;
            float x, y, z;
            float rx = 0, ry = 0, rz = 0;
            bool held = false;
        };
        std::vector<Move> move;
        std::vector<net::ItemTake> takes;
        std::vector<net::ItemDrop> drops;
        std::vector<net::ItemMove> moved;
        std::vector<Disagreement> differ;
    };

    class FriendItems
    {
    public:
        void Heard(const net::ItemState& host, uint32_t now = 0);
        ItemActions Look(const std::vector<LocalItem>& local, bool sameArea = true, uint32_t now = 0);
        void Made(uint32_t hostRef, uint32_t ourRef);
        // A visitor's own item for the host's, and back. Items from the game's data share their ref on every screen;
        // ones made while playing only match through a copy, and 0 means there's none.
        uint32_t OurRefOf(uint32_t hostRef) const;
        uint32_t HostRefOf(uint32_t ourRef) const;
        void Clear();
        size_t Copies() const { return m_copies.size(); }

    private:
        std::map<uint32_t, net::ItemState> m_host;
        std::map<uint32_t, uint32_t> m_heardAt;
        std::map<uint32_t, uint32_t> m_copies;
        struct Seen
        {
            bool present; int32_t count; uint32_t base; bool nearMe; ItemPose pose; bool movable;
            uint8_t health = net::kNoCondition; uint8_t mods = 0;
        };
        std::map<uint32_t, Seen> m_seen;
        struct Own { ItemPose sent; bool held = false; uint32_t movedAt = 0, sentAt = 0; };
        std::map<uint32_t, Own> m_own;
        std::set<uint32_t> m_holding;
        std::map<uint32_t, uint32_t> m_followed;
        std::set<uint32_t> m_dropped;
        std::set<uint32_t> m_removed;
        std::set<uint32_t> m_taken;
        std::set<uint32_t> m_switchedOn;
        std::map<uint32_t, uint32_t> m_goneHere;
        struct ParentGap { uint32_t firstWord = 0; bool asked = false; };
        std::map<uint32_t, ParentGap> m_parentGaps;
        void Taken(uint32_t ourRef, uint32_t hostRef);
        bool m_started = false;
    };
}
