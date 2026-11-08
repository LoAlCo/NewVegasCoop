// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/mirror.h"

#include "coop/clock.h"
#include "game/game.h"
#include "game/inspector.h"
#include "game/mover.h"
#include "log.h"

namespace game
{
    namespace
    {
        constexpr uint32_t kFeedEveryMs = 50;
        constexpr uint32_t kStatsEveryMs = 1000;
        constexpr uint32_t kGearEveryMs = 500;
        constexpr float kSideOffset = 120;
    }

    void Mirror::Start(TESObjectREFR* actor)
    {
        *this = Mirror();
        m_refId = RefId(actor);
        m_puppet.Start("Mirror", kStatsEveryMs);
        LOG_INFO("Mirror: %08X copies you, %.0f units east of you and %u ms behind", m_refId, kSideOffset,
            coop::Smoother::kDelayMs);
    }

    void Mirror::Stop()
    {
        if (TESObjectREFR* actor = LookupRef(m_refId))
            m_puppet.Release(actor);
        if (m_refId)
            LOG_INFO("Mirror: stopped");
        m_refId = 0;
    }

    void Mirror::Update(uint32_t now)
    {
        if (!m_refId)
            return;
        TESObjectREFR* actor = LookupRef(m_refId);
        TESObjectREFR* player = Player();
        coop::Pose me, at;
        if (!actor || !player || !GetPose(player, me))
            return;

        if (coop::IsDue(now, m_nextFeed))
        {
            m_nextFeed = now + kFeedEveryMs;
            me.moves = static_cast<uint16_t>(ReadMover(player).flags);
            me.flags = StanceFlags(player);
            if (IsJumping(player))
                me.moves |= coop::moves::kJump;
            m_smoother.Add(now, me, now);
        }
        coop::Pose want;
        if (!m_smoother.Sample(now, want))
            return;
        want.x += kSideOffset;

        if (!GetPose(actor, at) || at.space != want.space || !Is3DLoaded(actor))
        {
            MoveOnto(actor, player);
            return;
        }
        if (!m_madeCompanion)
        {
            m_madeCompanion = true;
            MakeCompanion(actor);
        }
        if (coop::IsDue(now, m_nextGear))
        {
            m_nextGear = now + kGearEveryMs;
            m_puppet.SetGear(actor, PlayerGear());
        }
        m_puppet.Update(actor, want, now);
        SetPitch(actor, want.pitch);
    }

    void Mirror::Attack(uint8_t shots, bool release, bool reload)
    {
        if (m_refId)
            m_puppet.Attack(LookupRef(m_refId), shots, release, reload);
    }
}
