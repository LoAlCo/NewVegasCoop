// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <vector>

namespace coop
{
    std::vector<net::ItemCount> ItemDiff(const std::vector<net::ItemCount>& from,
        const std::vector<net::ItemCount>& to);

    int32_t TotalOf(const std::vector<net::ItemCount>& items, uint32_t item);

    // How many of each item the player had at the last look. Only what's in the watched container is counted, so
    // looking at a container costs the same however much the player carries.
    using FormCounts = std::map<uint32_t, int32_t>;
    std::vector<uint32_t> FormsToCount(const FormCounts& before, const std::vector<net::ItemCount>& local);
    std::vector<net::ItemCount> CountChanges(const FormCounts& before, const FormCounts& now);
    FormCounts CountsToKeep(const FormCounts& now, const std::vector<net::ItemCount>& local);

    struct ContainerTake
    {
        std::vector<net::ItemCount> removes;
        int32_t taken = 0;
        int32_t missing = 0;
    };
    // A player's copy of a stack can read differently from the host's (its condition, say), so the host takes the
    // kind they asked for first and then others of the same item. Only what truly isn't there is given back.
    ContainerTake PlanContainerTake(const std::vector<net::ItemCount>& have, const net::ItemCount& want);

    // Notices a body's contents changing, so a friend's copy can match without looking at it. What it drops as it
    // dies goes into its inventory for a while afterwards (settling), which isn't anyone looting it: only something
    // leaving counts until then.
    class BodyLootWatch
    {
    public:
        bool Look(uint32_t ref, const std::vector<net::ItemCount>& items, bool settling);
        void KeepOnly(const std::vector<uint32_t>& refs);
        size_t Size() const { return m_seen.size(); }

    private:
        std::map<uint32_t, std::vector<net::ItemCount>> m_seen;
    };

    // Each game rolls a container's random loot itself as its cell loads, so a friend's copy holds different loot
    // from the host's until it's matched. The host shares the containers near each friend ahead of time: a new one
    // is read at once (a few per look, so walking into a full room costs little), the rest one at a time in turn to
    // notice the host's game changing them. A container out of reach and back is shared again, since the friend's
    // game may have rolled it afresh.
    class ContainerShareWatch
    {
    public:
        // The refs near a friend now. Returns the ones to read this look.
        std::vector<uint32_t> ToRead(const std::vector<uint32_t>& inReach, size_t maxNew);
        // What a read found. True if friends should be told: the first read of it, or a change since the last.
        bool Read(uint32_t ref, const std::vector<net::ItemCount>& items);
        size_t Size() const { return m_seen.size(); }

    private:
        std::map<uint32_t, std::vector<net::ItemCount>> m_seen;
        std::vector<uint32_t> m_near;
        uint32_t m_last = 0;
    };

    class FriendContainer
    {
    public:
        bool View(uint32_t ref, bool open = true);
        uint32_t Viewing() const { return m_ref; }
        // playerChange: how the player's own inventory changed since the last look. With no menu seen open, a body
        // losing what the player gained (or gaining what they lost) is still the player's doing: loot mods and menus
        // we can't see count too, while the game's own changes to its copy of the body don't.
        std::vector<net::ItemCount> Look(const std::vector<net::ItemCount>& local,
            std::vector<net::ContainerChange>& changes, const std::vector<net::ItemCount>& playerChange = {});
        void Closing(const std::vector<net::ItemCount>& local, std::vector<net::ContainerChange>& changes);
        void Settle(const std::vector<net::ItemCount>& local) { m_snapshot = local; }
        void Heard(const net::ContainerContents& m);
        void Reset() { *this = FriendContainer(); }

    private:
        uint32_t m_ref = 0;
        bool m_open = false;
        uint32_t m_seq = 0;
        bool m_hasSnapshot = false;
        std::vector<net::ItemCount> m_snapshot;
        bool m_hasHost = false;
        net::ContainerContents m_host;
    };
}
