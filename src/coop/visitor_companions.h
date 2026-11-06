// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <vector>

// A visitor's companion stays real in the visitor's game, which runs its AI. The host's game shows a copy made from
// the same base form. The host stays in charge of its NPCs' health: the companion's hits are reported like the
// player's, and what hurts the copy on the host is passed back to the real one.
namespace coop
{
    constexpr size_t kMaxCompanionCopiesPerPlayer = 8;

    // What the host's copy of a visitor's companion is known by. Never a real form: it only keys the copy list, and
    // being in the made-while-playing range makes the copy code spawn an actor for it.
    constexpr uint32_t kCompanionCopyKeys = 0xFFC00000;
    constexpr uint32_t CompanionCopyKey(uint8_t owner, uint8_t slot)
    {
        return kCompanionCopyKeys | (static_cast<uint32_t>(owner) << 8) | slot;
    }
    constexpr bool IsCompanionCopyKey(uint32_t key) { return (key & 0xFFFF0000) == kCompanionCopyKeys; }
    constexpr uint8_t CompanionCopyOwner(uint32_t key) { return static_cast<uint8_t>(key >> 8); }

    // With the host's "both keep theirs" off, one the host also has stays the host's: as before, our game copies the
    // host's, and there's nothing of ours to show.
    bool ShareCompanionWithHost(bool bothKeep, bool inHostsParty, bool hostSharesIt);

    // A copy hurt in its own game only stays wrong while the host's number stands still.
    bool CopyHealthDrifted(float here, float host);

    enum class CompanionHit
    {
        NotOurs,          // the usual hit handling
        Done,             // nothing more to do
        Report,           // a visitor's companion hit something: tell the host
        Undo,             // give the health back
        UndoAndTellOwner, // on the host: give the copy's health back, and hurt the real companion
    };

    struct CompanionHitFacts
    {
        bool host = false;
        // On a visitor: hits to and from our own companions.
        bool targetIsOurCompanion = false;
        bool attackerIsOurCompanion = false;
        bool attackerIsCopyOrStandIn = false;
        bool targetIsStandIn = false;
        // On the host: hits to and from the copies of visitors' companions.
        bool targetIsCopy = false;
        bool attackerIsCopy = false;
        bool attackerIsFriendly = false;
    };
    CompanionHit RouteCompanionHit(const CompanionHitFacts& f);

    // What the host sends a player: a visitor doesn't get a copy of its own companion.
    std::vector<net::NpcState> NpcsForPlayer(const std::vector<net::NpcState>& npcs,
        const std::map<uint32_t, uint8_t>& copyOwners, uint8_t player);
}
