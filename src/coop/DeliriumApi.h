// FNV Delirium: interface for other NVSE plugins (written for New Vegas Co-op).
//
// Copy this header into the other plugin. Find the interface at run time, after every plugin has loaded (NVSE's
// PostLoad message or later):
//
//     HMODULE dll = GetModuleHandleA("FNVDelirium.dll");
//     auto get = dll ? reinterpret_cast<const DeliriumApi* (*)()>(GetProcAddress(dll, "Delirium_GetApi")) : nullptr;
//     const DeliriumApi* api = get ? get() : nullptr;
//     if (api && api->version >= 1) { ... }   // missing: Delirium isn't installed, use the game's own reactions
//
// Every call is main-thread only (the game loop, where the co-op mod runs). Positions are game units (world), headings
// degrees (the game's: 0 = north/+Y, clockwise), speeds game units per second.
//
// How a synced reaction works (see COOP_SYNC_NOTES.md):
//  - The NPC's OWNER (the co-op host for world NPCs) runs Delirium as in single player. Each time Delirium starts or
//    adds to a reaction it sends a ReactionStart event; the co-op mod sends that to the other players.
//  - A COPY of that NPC (a visitor's) never reacts to its own hits: SetCopyCheck tells Delirium which refs are copies.
//    StartReaction runs the owner's reaction on the copy (same hit, same random seed: the same RDR2 choices).
//  - While it runs, the owner's GetRoot (pelvis position, speed, heading) is sent 15-20 times a second; the copy's
//    SteerRoot pulls its body toward it. Limbs are simulated on each machine.
//  - The owner's outcome (Outcome events: fell, recovered, fire out, ended) is sent and forced on the copy with
//    ForceOutcome, so every copy ends the same way, at the same spot.
//  - While IsReacting(ref) is true the copy's body belongs to Delirium: don't steer, move, turn or wake it.
#pragma once
#include <cstdint>

#define DELIRIUM_API_VERSION 1

// A hit Delirium should treat like one from the game's hit handler (the host applying a visitor's shot with
// DamageActorValue never goes through it).
struct DeliriumHit {
    std::uint32_t refId;
    std::int32_t part;        // the game's body part: 0 torso, 1-2 head, 3-4 left arm, 5-6 right arm, 7-9 left leg,
                              // 10-12 right leg, 13 brain, 14 weapon, -1 unknown
    float impact[3];          // where it hit (0,0,0: unknown)
    float dir[3];             // the way the shot travelled (0,0,0: away from the attacker / unknown)
    float damage;
    std::uint8_t died;        // this hit killed it
    std::uint8_t fire;        // a fire hit (flamer, incendiary...)
    std::uint8_t pad[2];
};

// One reaction (or one more hit on a reaction already running), as the owner ran it.
struct DeliriumReaction {
    std::uint32_t refId;
    std::int32_t part;        // the game's body part (as DeliriumHit)
    float impactLocal[3];     // the impact point relative to the NPC's position
    float dir[3];             // unit direction of the hit
    float health;             // Delirium's health for it (how weak the legs are)
    float carried[2];         // the NPC's own speed when hit (x, y), carried into the stumble
    std::uint32_t seed;       // random seed: the same reaction choices on every machine
    std::uint8_t fire;        // on fire: writhe
    std::uint8_t again;       // 1: another hit on a reaction already running
    std::uint8_t death;       // 1: it died (dying on its feet)
    std::uint8_t pad;
};

enum DeliriumOutcome : std::uint32_t {
    DeliriumFell = 1,         // the body went down: the game's ragdoll carries on (it gets up as usual)
    DeliriumRecovered = 2,    // back on its feet: pos / heading = where the NPC stands now
    DeliriumFireOut = 3,      // stopped writhing (the reaction may go on)
    DeliriumEnded = 4,        // Delirium let go for another reason (time up, the game took the body)
};

enum DeliriumEventKind : std::uint32_t {
    DeliriumReactionStart = 1,
    DeliriumOutcomeEvent = 2,
};

struct DeliriumEvent {
    std::uint32_t kind;       // DeliriumEventKind
    std::uint32_t refId;
    DeliriumReaction reaction;    // kind == DeliriumReactionStart
    std::uint32_t outcome;    // kind == DeliriumOutcomeEvent: DeliriumOutcome
    float pos[3];             // outcome: where the NPC is (recovered: where it stands)
    float heading;            // outcome: its heading
};

struct DeliriumApi {
    std::uint32_t version;    // DELIRIUM_API_VERSION
    std::uint32_t size;       // sizeof(DeliriumApi)
    // Events from this machine's own (owned) reactions; nullptr stops them. Called inside Delirium's frame.
    void (*SetListener)(void (*listener)(const DeliriumEvent* ev));
    // Which refs are another machine's NPCs (copies): Delirium ignores their hits and waits for StartReaction.
    void (*SetCopyCheck)(bool (*isCopy)(std::uint32_t refId));
    // Owner: a hit the game's hit handler didn't see (a visitor's shot applied on the host).
    void (*ReportHit)(const DeliriumHit* hit);
    // Copy: run the owner's reaction (or its next hit). False: Delirium couldn't (not a human, disabled...).
    bool (*StartReaction)(const DeliriumReaction* reaction);
    // Owner: the pelvis now: out[0..2] position, out[3..5] speed, out[6] heading. False: not reacting.
    bool (*GetRoot)(std::uint32_t refId, float out[7]);
    // Copy: the owner's latest root (as GetRoot gave it); the copy's body is pulled toward it.
    void (*SteerRoot)(std::uint32_t refId, const float root[7]);
    // Copy: the owner's outcome (pos / heading as in the Outcome event).
    void (*ForceOutcome)(std::uint32_t refId, std::uint32_t outcome, const float pos[3], float heading);
    // Delirium has this NPC's body now (reacting, recovering, or waiting for the game to let it fall).
    bool (*IsReacting)(std::uint32_t refId);
    // The last hit the game's hit handler gave this ref in the past second (part, impact, direction), e.g. a visitor
    // adding it to the hit it sends the host, which then passes it to ReportHit. False: none.
    bool (*GetLastHit)(std::uint32_t refId, DeliriumHit* out);
};

extern "C" __declspec(dllexport) const DeliriumApi* Delirium_GetApi();
