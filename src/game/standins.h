// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"
#include "game/puppet.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class TESObjectREFR;

namespace game
{
    class StandIns
    {
    public:
        void Update(const coop::Coop& coop, uint32_t now, const coop::Pose& myPose, bool cutscene = false);
        void RemoveAll();
        void Forget();
        std::vector<uint32_t> RefIds() const;
        TESObjectREFR* Character(uint8_t playerId) const;
        void Attack(uint8_t playerId, uint8_t shots, bool release = false, bool paths = false);
        void FireAlong(const net::ShotPath& path);
        bool IsStandIn(const TESObjectREFR* ref) const { return PlayerIdOf(ref) >= 0; }
        int PlayerIdOf(const TESObjectREFR* ref) const;

    private:
        struct Entry
        {
            TESObjectREFR* ref = nullptr;
            uint32_t refId = 0;
            Puppet puppet;
            uint32_t gearChanges = 0;
            bool hasGear = false;
            bool downed = false;
            bool onMap = false;
            std::string anim;
            std::vector<std::string> modAnims;
            uint32_t nextHoldAt = 0;
            uint32_t nextAnimCheckAt = 0;
            bool ghost = false;
            bool hidden = false;
            uint32_t nextCalmAt = 0;
            bool loading = true;
            uint32_t spawnedAt = 0;
            uint32_t retryAt = 0;
            uint32_t nextBringAt = 0;
            uint32_t looksSeen = 0;
            uint32_t nextLooksAt = 0;
        };
        bool Spawn(const coop::RemotePlayer& remote, Entry& e, uint32_t now, const net::PlayerLooks* looks);
        static TESObjectREFR* Live(const Entry& e);
        static void ShowOnMap(Entry& e, bool on);
        static bool Furniture(const coop::Coop& coop, uint8_t id, Entry& e, const coop::Pose& pose, uint32_t now);

        std::map<uint8_t, Entry> m_entries;
        std::map<uint8_t, bool> m_tooFar;
    };
}
