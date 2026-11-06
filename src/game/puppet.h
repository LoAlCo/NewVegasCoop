// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/smoothing.h"
#include "coop/shot_paths.h"
#include "coop/steer.h"
#include "net/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

class TESObjectREFR;

namespace game
{
    class Puppet
    {
    public:
        void Start(std::string who, uint32_t statsEveryMs);
        void Release(TESObjectREFR* actor);
        void Update(TESObjectREFR* actor, const coop::Pose& want, uint32_t now, const coop::Pose* latest = nullptr);
        void SetGear(TESObjectREFR* actor, const std::vector<uint32_t>& gear);
        void Attack(TESObjectREFR* actor, uint8_t shots, bool release = false);
        void FireAlong(TESObjectREFR* actor, const net::ShotPath& path);

    private:
        void UpdateWeaponOut(TESObjectREFR* actor, bool want, uint32_t now);
        void LogStats(uint32_t now);

        std::string m_who;
        uint32_t m_statsEveryMs = 1000;
        coop::Steer m_steer;
        float m_lastWantX = 0, m_lastWantY = 0, m_wantVx = 0, m_wantVy = 0;
        uint32_t m_lastWantAt = 0;
        coop::ShotPaths m_shotPaths;
        bool m_jumping = false;
        uint32_t m_putBackAt = 0;
        std::vector<uint32_t> m_worn;
        bool m_weaponOut = false;
        uint32_t m_nextWeaponCheck = 0;
        uint32_t m_asked = 0, m_askedAt = 0;
        bool m_waiting = false;
        uint32_t m_nextStats = 0;
        float m_errorSum = 0, m_errorMax = 0;
        uint32_t m_frames = 0, m_snaps = 0, m_lagMax = 0, m_lagCount = 0, m_lagSum = 0, m_jumps = 0;
        uint32_t m_checked = 0, m_switchedOff = 0, m_replaced = 0, m_ignored = 0;
        uint32_t m_behindSum = 0, m_behindMax = 0, m_behindCount = 0, m_gaitChanges = 0, m_speedChanges = 0;
        bool m_wasMoving = false, m_wasRunning = false;
        int m_speedMult = 100;
        uint32_t m_speedSetAt = 0;
        float m_ownRunSpeed = 0;
    };
}
