// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/body_place.h"
#include "coop/coop.h"
#include "coop/shot_counter.h"
#include "game/game.h"
#include "game/puppet.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

class TESObjectREFR;

namespace game
{
    class StandIns;

    class Npcs
    {
    public:
        void Share(coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns);
        void Update(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns);
        void Attack(uint32_t ref, uint8_t shots, bool reload, bool release = false);
        void CheckDesyncs(coop::Coop& coop, uint32_t now, const coop::Pose& myPose);
        void ReleaseAll();
        void Forget();
        std::vector<uint32_t> MadeRefIds() const;
        void RestartBodies() { m_bodies.clear(); }
        static void LabCopySteps(uint32_t steps) { s_labSteps = steps; }
        static void SetSmoothBodies(bool on) { s_smoothBodies = on; }
        uint32_t HostRefOf(uint32_t ourRef) const;
        TESObjectREFR* OurRefOf(uint32_t hostRef) const { return Find(hostRef); }

    private:
        struct Entry
        {
            Puppet puppet;
            uint32_t nextBringAt = 0;
            float hostHealth = -1;
            float ownMaxHealth = 0;
            uint32_t idle = 0;
            uint32_t weapon = 0;
            uint32_t gaveWeapon = 0;
            uint32_t nextWeaponCheck = 0;
            uint32_t weaponGifts = 0;
            uint32_t allGifts = 0;
            uint32_t nextSitCheck = 0;
            uint32_t sittingSince = 0;
            uint32_t gearChanges = 0;
            uint32_t nextGearCheck = 0;
            uint32_t dressings = 0;
        };
        void UpdateCopies(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, const StandIns& standIns);
        TESObjectREFR* Find(uint32_t ref) const;
        TESObjectREFR* Make(uint32_t ref, const coop::RemoteNpc& npc, const coop::Pose& pose, uint32_t now);
        void Unmake(uint32_t ref);
        void Release(uint32_t ref);
        struct Body;
        void FollowBody(uint32_t ref, TESObjectREFR* actor, const coop::RemoteNpc& npc, Body& b, uint32_t now);
        void PlaceBody(uint32_t ref, TESObjectREFR* actor, const coop::RemoteNpc& npc, const coop::Pose& host, uint32_t now);
        void LogStats(uint32_t now, size_t shared);
        uint32_t m_nextDesyncCheck = 0, m_nextDesyncLog = 0, m_desyncLooks = 0, m_desyncSeen = 0;

        std::map<uint32_t, Entry> m_entries;
        std::vector<TESObjectREFR*> m_nearby;
        struct Watched
        {
            int action = -1;
            GunState gun;
            coop::ShotCounter shotCounter;
            uint32_t nextGearAt = 0;
            uint32_t nextGunCheck = 0;
            bool isGun = false;
            bool weaponWasOut = false;
            uint16_t limbs = 0;
            uint32_t nextLimbCheck = 0;
            uint32_t diedAt = 0;
            uint8_t pile = 0;
            uint32_t nextBodyLook = 0;
            float bodyX = 0, bodyY = 0, bodyZ = 0, bodyHeading = 0;
        };
        struct Body
        {
            coop::BodyPlacer placer;
            coop::BodyFollower follower;
            bool following = false, saidPull = false, saidNoPull = false;
            uint32_t nextPullTry = 0;
            uint32_t followFrames = 0;
            float followOffSum = 0, followOffMost = 0;
            double followMs = 0;
            uint32_t nextLook = 0;
            uint32_t judgeAt = 0;
            float offBefore = 0;
            bool saidGaveUp = false;
        };
        std::map<uint32_t, Body> m_bodies;
        uint32_t m_bodiesMoved = 0;
        uint32_t m_bodiesPulled = 0;
        static inline bool s_smoothBodies = true;
        struct Made
        {
            uint32_t localId = 0;
            uint32_t loadedAt = 0;
        };
        std::map<uint32_t, Made> m_made;
        static inline uint32_t s_labSteps = 15;
        uint32_t m_nextMakeAt = 0;
        uint32_t m_madeCount = 0, m_unmadeCount = 0, m_dressed = 0;
        std::map<uint32_t, Watched> m_watched;
        struct Killed { uint32_t at = 0; uint16_t limbs = 0; uint8_t pile = 0; };
        std::map<uint32_t, Killed> m_killed;
        std::map<uint32_t, uint32_t> m_nextRevive;
        struct SwitchedOff { uint32_t since = 0; uint32_t nextTry = 0; uint32_t tries = 0; };
        std::map<uint32_t, SwitchedOff> m_switchedOff;
        std::map<uint32_t, uint32_t> m_localDeaths;
        std::set<uint32_t> m_piled;
        uint32_t m_nextShare = 0;
        uint32_t m_nextStats = 0;
        uint32_t m_brought = 0;
        double m_shareMs = 0;
        uint32_t m_shares = 0;
        double m_updateMs = 0;
        uint32_t m_updates = 0;
        uint32_t m_attacks = 0, m_attacksShown = 0, m_attacksNotShown = 0, m_kills = 0, m_healthSets = 0;
        uint32_t m_idles = 0, m_deadHere = 0, m_weapons = 0, m_reloads = 0, m_stoodUp = 0, m_reloadsShown = 0;
        bool m_warnedFull = false;
        bool m_foundNone = true;
    };
}
