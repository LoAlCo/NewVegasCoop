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

    // Notices a body's contents changing, so a friend's copy can match without looking at it. Each game rolls what an
    // NPC carries itself, so a new body is shared at once (friends loot them seconds after the kill). What it drops
    // as it dies goes into its inventory for a while afterwards (settling), which isn't anyone looting it: until then
    // only something leaving is shared, and whatever differs from what was shared goes once it has settled.
    class BodyLootWatch
    {
    public:
        // True if friends should be told what's in it now.
        bool Look(uint32_t ref, const std::vector<net::ItemCount>& items, bool settling);
        bool Knows(uint32_t ref) const { return m_seen.count(ref) != 0; }
        void KeepOnly(const std::vector<uint32_t>& refs);
        size_t Size() const { return m_seen.size(); }

    private:
        struct Seen
        {
            std::vector<net::ItemCount> items;
            std::vector<net::ItemCount> shared;
        };
        std::map<uint32_t, Seen> m_seen;
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

    // The host's record of containers its game can't see, matched to the first friend to offer theirs. Kept until the
    // host's game has the container (it's then set to the record) or the host quits.
    class UnseenContainers
    {
    public:
        // A friend's contents, as of their change seq. True if it became the record (the first offer wins).
        bool Offered(uint32_t ref, uint8_t playerId, uint32_t seq, const std::vector<net::ItemCount>& items);
        const std::vector<net::ItemCount>* Find(uint32_t ref) const;
        // A friend's take (or put) on a recorded container. Returns what's given back (count 0: none). A change the
        // offer already held is only acknowledged. False with no record: the caller waits a little for an offer.
        bool Change(uint32_t ref, uint8_t playerId, uint32_t seq, const net::ItemCount& want, net::ItemCount& refund);
        // The host's game has it now: the record leaves, to set the real one to.
        bool Release(uint32_t ref, std::vector<net::ItemCount>& out);
        size_t Size() const { return m_records.size(); }

    private:
        struct Record
        {
            std::vector<net::ItemCount> items;
            uint8_t by = 0;
            uint32_t seq = 0;
        };
        std::map<uint32_t, Record> m_records;
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
        // What the host was last told of this container (changes since aren't sent yet), and the last change's seq.
        bool Told(uint32_t ref, std::vector<net::ItemCount>& out) const
        {
            if (ref != m_ref || !m_hasSnapshot)
                return false;
            out = m_snapshot;
            return true;
        }
        uint32_t Seq() const { return m_seq; }
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
