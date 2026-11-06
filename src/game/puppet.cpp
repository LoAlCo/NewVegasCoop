// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/puppet.h"

#include "coop/clock.h"
#include "dev/inspect_text.h"
#include "game/game.h"
#include "game/mover.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game
{
    namespace
    {
        constexpr uint32_t kWeaponCheckEveryMs = 250;
        constexpr uint32_t kDrawTakesMs = 800;
        constexpr float kPutBackDistance = 200;
        constexpr float kTurnAtLeast = 0.01f;
        constexpr uint32_t kPutBackPauseMs = 300;
        constexpr int kSpeedMultMin = 75, kSpeedMultMax = 135, kSpeedMultStep = 5;
        constexpr float kSpeedPerSecondBehind = 200;
        constexpr uint32_t kSpeedEveryMs = 150;
        constexpr float kPi = 3.14159265f;
        constexpr uint32_t kCopiedFlags = 0xFFBF;
        constexpr uint32_t kTakenMask = 0x030F;
        constexpr uint32_t kDirectionMask = 0x000F;

        float AngleDiff(float a, float b)
        {
            float d = std::fmod(a - b, 2 * kPi);
            if (d > kPi) d -= 2 * kPi;
            if (d < -kPi) d += 2 * kPi;
            return d;
        }
    }

    void Puppet::Start(std::string who, uint32_t statsEveryMs)
    {
        *this = Puppet();
        m_who = std::move(who);
        m_statsEveryMs = statsEveryMs;
    }

    void Puppet::Release(TESObjectREFR* actor)
    {
        if (actor)
            ReleaseMovementFlags(actor);
        m_asked = 0;
        m_waiting = false;
        if (actor && m_speedMult != 100)
            SetSpeedMult(actor, 100);
        m_speedMult = 100;
    }

    void Puppet::Update(TESObjectREFR* actor, const coop::Pose& want, uint32_t now, const coop::Pose* latest)
    {
        coop::Pose at;
        if (!actor || !GetPose(actor, at))
            return;

        if (m_lastWantAt && now != m_lastWantAt)
        {
            float dt = static_cast<float>(now - m_lastWantAt) / 1000.0f;
            m_wantVx += ((want.x - m_lastWantX) / dt - m_wantVx) * 0.3f;
            m_wantVy += ((want.y - m_lastWantY) / dt - m_wantVy) * 0.3f;
        }
        m_lastWantX = want.x;
        m_lastWantY = want.y;
        m_lastWantAt = now;

        const coop::Pose& doing = latest && latest->space == at.space ? *latest : want;
        uint32_t theirs = (doing.moves & kCopiedFlags) | ((doing.flags & coop::Pose::kSneaking) ? coop::moves::kSneaking : 0);
        coop::Steer::Input in{at.x, at.y, want.x, want.y, m_wantVx, m_wantVy, want.heading, theirs};
        uint32_t flags = m_steer.Flags(in);
        if (m_steer.Moving() != m_wasMoving || (m_steer.Moving() && m_steer.Running() != m_wasRunning))
            ++m_gaitChanges;
        m_wasMoving = m_steer.Moving();
        m_wasRunning = m_steer.Running();
        float speed = std::sqrt(m_wantVx * m_wantVx + m_wantVy * m_wantVy);
        if (float run = CachedRunSpeed(actor); run > 0 && m_speedMult > 0)
            m_ownRunSpeed = run * 100.0f / static_cast<float>(m_speedMult);
        int base = 100;
        if (doing.runSpeed && m_ownRunSpeed > 0)
        {
            base = static_cast<int>(doing.runSpeed * 100.0f / m_ownRunSpeed + 0.5f);
            base = base < 30 ? 30 : base > 300 ? 300 : base;
        }
        int mult = base;
        if (m_steer.Moving() && speed > 30)
        {
            const coop::Pose& target = latest && latest->space == at.space ? *latest : want;
            float along = ((target.x - at.x) * m_wantVx + (target.y - at.y) * m_wantVy) / speed;
            int catchUp = 100 + static_cast<int>(along / speed * kSpeedPerSecondBehind);
            catchUp = catchUp < kSpeedMultMin ? kSpeedMultMin : catchUp > kSpeedMultMax ? kSpeedMultMax : catchUp;
            mult = base * catchUp / 100;
        }
        if ((std::abs(mult - m_speedMult) >= kSpeedMultStep && now - m_speedSetAt >= kSpeedEveryMs) ||
            (mult == base && m_speedMult != base))
        {
            SetSpeedMult(actor, mult);
            m_speedMult = mult;
            m_speedSetAt = now;
            ++m_speedChanges;
        }
        if (latest && latest->space == at.space && speed > 60)
        {
            float bx = latest->x - at.x, by = latest->y - at.y;
            uint32_t behind = static_cast<uint32_t>(std::sqrt(bx * bx + by * by) / speed * 1000.0f);
            behind = behind > 2000 ? 2000 : behind;
            m_behindSum += behind;
            m_behindMax = behind > m_behindMax ? behind : m_behindMax;
            ++m_behindCount;
        }
        if (m_asked)
        {
            MoverState s = ReadMover(actor);
            ++m_checked;
            if (!s.askedOn)
                ++m_switchedOff;
            else if (s.asked != m_asked)
                ++m_replaced;
            else if ((s.flags & kDirectionMask) != (m_asked & kDirectionMask))
                ++m_ignored;
        }
        if ((flags & kTakenMask) != (m_asked & kTakenMask))
        {
            m_waiting = true;
            m_askedAt = now;
        }
        m_asked = flags;
        HoldMovementFlags(actor, flags);
        if (m_waiting && (ReadMover(actor).flags & kTakenMask) == (flags & kTakenMask))
        {
            uint32_t lag = now - m_askedAt;
            m_waiting = false;
            m_lagSum += lag;
            m_lagMax = lag > m_lagMax ? lag : m_lagMax;
            ++m_lagCount;
            if (lag > 250)
                LOG_INFO("%s: legs took %u ms to start moving %s", m_who.c_str(), lag,
                    dev::MoveFlagNames(flags).c_str());
        }

        bool jumping = (doing.moves & coop::moves::kJump) != 0;
        if (jumping && !m_jumping)
        {
            ++m_jumps;
            if (!Jump(actor))
                LOG_INFO("%s: couldn't jump (no physics controller)", m_who.c_str());
        }
        m_jumping = jumping;
        if (uint32_t overdue = m_shotPaths.TakeOverdue(now))
        {
            FireShots(actor, m_worn, overdue);
            LOG_DEBUG("%s: no path came for %u shots, so fired them from its own aim", m_who.c_str(), overdue);
        }
        UpdateWeaponOut(actor, (doing.flags & coop::Pose::kWeaponOut) != 0, now);
        if (std::fabs(AngleDiff(at.heading, want.heading)) > kTurnAtLeast)
            Turn(actor, want.heading);

        float dx = at.x - want.x, dy = at.y - want.y;
        float error = std::sqrt(dx * dx + dy * dy);
        m_errorSum += error;
        m_errorMax = error > m_errorMax ? error : m_errorMax;
        ++m_frames;
        if (error > kPutBackDistance && now - m_putBackAt >= kPutBackPauseMs)
        {
            m_putBackAt = now;
            Move(actor, want);
            ++m_snaps;
        }
        LogStats(now);
    }

    void Puppet::SetGear(TESObjectREFR* actor, const std::vector<uint32_t>& gear)
    {
        auto has = [](const std::vector<uint32_t>& list, uint32_t id) {
            return std::find(list.begin(), list.end(), id) != list.end();
        };
        std::vector<uint32_t> worn;
        for (uint32_t id : m_worn)
            if (has(gear, id))
                worn.push_back(id);
            else
                RemoveItem(actor, id);
        bool changed = worn.size() != m_worn.size();
        for (uint32_t id : gear)
        {
            if (has(worn, id))
                continue;
            if (AddAndEquip(actor, id))
            {
                worn.push_back(id);
                changed = true;
            }
            else
                LOG_WARN("%s: item %08X isn't armour or a weapon in this game (mods differ?)", m_who.c_str(), id);
        }
        m_worn = std::move(worn);
        if (changed)
            LOG_DEBUG("%s now wears %zu items", m_who.c_str(), m_worn.size());
    }

    void Puppet::UpdateWeaponOut(TESObjectREFR* actor, bool want, uint32_t now)
    {
        if (want != m_weaponOut)
        {
            m_weaponOut = want;
            if (want && InScriptAnimation(actor))
                StopIdle(actor);
            SetAlert(actor, want);
            SetWeaponOut(actor, want);
            m_nextWeaponCheck = now + kDrawTakesMs;
            LOG_DEBUG("%s: told it to %s its weapon", m_who.c_str(), want ? "draw" : "put away");
            return;
        }
        if (!coop::IsDue(now, m_nextWeaponCheck))
            return;
        m_nextWeaponCheck = now + kWeaponCheckEveryMs;
        bool out = (StanceFlags(actor) & coop::Pose::kWeaponOut) != 0;
        if (out != want)
        {
            uint32_t ai = 0;
            AiStateBits(actor, ai);
            LOG_INFO("%s: its weapon is %s but should be %s; telling it again (action %d, idle %08X, AI bits %u)",
                m_who.c_str(), out ? "out" : "away", want ? "out" : "away", AnimAction(actor), PlayedIdle(actor), ai);
            if (want && InScriptAnimation(actor))
                StopIdle(actor);
            SetAlert(actor, want);
            SetWeaponOut(actor, want);
            m_nextWeaponCheck = now + kDrawTakesMs;
        }
    }

    void Puppet::Attack(TESObjectREFR* actor, uint8_t shots, bool release, bool paths)
    {
        if (!actor)
            return;
        if (release)
        {
            ReleaseThrow(actor, m_worn);
            LOG_DEBUG("%s's throw left its hand", m_who.c_str());
            return;
        }
        if (!(StanceFlags(actor) & coop::Pose::kWeaponOut))
        {
            LOG_DEBUG("%s: didn't attack (%u shots): its weapon isn't out", m_who.c_str(), shots);
            return;
        }
        PlayAttack(actor, m_worn, shots, !paths);
        if (paths)
            m_shotPaths.Attacked(shots, GetTickCount(), ShotPathWaitMs(m_worn));
        LOG_DEBUG("%s attacks (%u shots)", m_who.c_str(), shots);
    }

    void Puppet::FireAlong(TESObjectREFR* actor, const net::ShotPath& path)
    {
        if (!actor)
            return;
        m_shotPaths.PathCame(GetTickCount(), !path.mustArrive);
        bool weaponOut = (StanceFlags(actor) & coop::Pose::kWeaponOut) != 0;
        if (!game::FireAlong(actor, m_worn, path, weaponOut))
            LOG_DEBUG("%s: couldn't fire along its path (%08X from %.0f, %.0f, %.0f)", m_who.c_str(), path.projectile,
                path.x, path.y, path.z);
    }

    void Puppet::LogStats(uint32_t now)
    {
        if (!coop::IsDue(now, m_nextStats))
            return;
        bool first = m_nextStats == 0;
        m_nextStats = now + m_statsEveryMs;
        if (first || m_frames == 0)
            return;
        LOG_INFO("%s: off by %.0f on average (most %.0f) units; put back %u times; jumped %u times; legs took %u ms "
            "on average (most %u) for %u changes", m_who.c_str(), m_errorSum / m_frames, m_errorMax, m_snaps, m_jumps,
            m_lagCount ? m_lagSum / m_lagCount : 0, m_lagMax, m_lagCount);
        LOG_INFO("%s: %u ms behind their newest position on average (most %u) while moving; gait changed %u times; "
            "speed set %u times (now %d%%)", m_who.c_str(), m_behindCount ? m_behindSum / m_behindCount : 0, m_behindMax,
            m_gaitChanges, m_speedChanges, m_speedMult);
        if (m_switchedOff || m_replaced || m_ignored)
            LOG_INFO("%s: its AI switched our legs off %u times, replaced our moves %u times, and the game didn't "
                "use them %u times, in %u frames", m_who.c_str(), m_switchedOff, m_replaced, m_ignored, m_checked);
        m_errorSum = m_errorMax = 0;
        m_frames = m_snaps = m_lagMax = m_lagCount = m_lagSum = m_jumps = 0;
        m_checked = m_switchedOff = m_replaced = m_ignored = 0;
        m_behindSum = m_behindMax = m_behindCount = m_gaitChanges = m_speedChanges = 0;
    }
}
