// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/inspector.h"

#include "coop/clock.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/mover.h"
#include "log.h"

#include "nvse/GameObjects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace game
{
    namespace
    {
        constexpr uint32_t kAiEveryMs = 200;
        constexpr uint32_t kMotionEveryMs = 500;
        constexpr uint32_t kMaxLinesPerSecond = 20;

        template <typename T>
        T Field(const void* object, uint32_t offset)
        {
            return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
        }

        const void* AnimDataOf(Actor* actor)
        {
            const void* process = actor->baseProcess;
            if (!process)
                return nullptr;
            using GetAnimDataFn = void*(__thiscall*)(const void*);
            uintptr_t table = *reinterpret_cast<const uintptr_t*>(process);
            auto get = *reinterpret_cast<const GetAnimDataFn*>(table + process::kGetAnimDataOffset);
            const void* anim = get(process);
            return anim && Field<const void*>(anim, process::kAnimActor) == actor ? anim : nullptr;
        }
    }

    std::string AnimGroupName(uint16_t groupId)
    {
        uint32_t index = groupId & 0xFF;
        if (index >= addr::kAnimGroupCount)
            return "?";
        auto name = *reinterpret_cast<const char* const*>(addr::kAnimGroupInfos + index * addr::kAnimGroupInfoSize);
        return name ? name : "?";
    }

    std::string FurnitureAnim(TESObjectREFR* ref)
    {
        const void* anim = IsActor(ref) ? AnimDataOf(static_cast<Actor*>(ref)) : nullptr;
        if (!anim || AnimGroupName(Field<uint16_t>(anim, process::kAnimGroupIds)) != "DynamicIdle")
            return "";
        const void* sequence = Field<const void*>(anim, process::kAnimSequences);
        const char* name = sequence ? Field<const char*>(sequence, process::kSequenceName) : nullptr;
        return name ? name : "";
    }

    namespace
    {
        std::string FileOf(const char* path)
        {
            std::string name(path);
            size_t slash = name.find_last_of("\\");
            name = slash == std::string::npos ? name : name.substr(slash + 1);
            for (char& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return name;
        }

        template <typename F> void ForEachPlaying(TESObjectREFR* ref, const std::string& path, F f)
        {
            const void* anim = IsActor(ref) ? AnimDataOf(static_cast<Actor*>(ref)) : nullptr;
            const void* manager = anim ? Field<const void*>(anim, process::kAnimController) : nullptr;
            if (!manager || path.empty())
                return;
            std::string file = FileOf(path.c_str());
            auto* const* sequences = Field<void* const*>(manager, process::kControllerSequences);
            uint16_t slots = Field<uint16_t>(manager, process::kControllerSequenceSlots);
            for (uint16_t i = 0; sequences && i < slots; ++i)
            {
                void* sequence = sequences[i];
                if (!sequence || Field<uint32_t>(sequence, process::kSequenceState) == 0)
                    continue;
                const char* name = Field<const char*>(sequence, process::kSequenceName);
                if (name && FileOf(name) == file)
                    f(sequence);
            }
        }
    }

    float CachedRunSpeed(TESObjectREFR* ref)
    {
        if (!IsActor(ref) || !static_cast<Actor*>(ref)->baseProcess)
            return 0;
        const void* cached = Field<const void*>(static_cast<Actor*>(ref)->baseProcess, process::kCachedValues);
        if (!cached || !(Field<uint32_t>(cached, process::kCachedFlags) & 0x2000))
            return 0;
        float speed = Field<float>(cached, process::kCachedRunSpeed);
        return speed > 0 && speed < 10000 ? speed : 0;
    }

    std::vector<std::string> ModAnimFiles(TESObjectREFR* ref)
    {
        std::vector<std::string> files;
        const void* anim = IsActor(ref) ? AnimDataOf(static_cast<Actor*>(ref)) : nullptr;
        for (int i = 0; anim && i < dev::kAnimLayers; ++i)
        {
            const void* sequence = Field<const void*>(anim, process::kAnimSequences + 4 * i);
            const char* name = sequence ? Field<const char*>(sequence, process::kSequenceName) : nullptr;
            if (!name || !*name)
                continue;
            std::string file(name);
            bool lower = std::none_of(file.begin(), file.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
            if (lower && file.size() > 3 && file.compare(file.size() - 3, 3, ".kf") == 0 &&
                std::find(files.begin(), files.end(), file) == files.end())
                files.push_back(file);
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    void StopAnimFile(TESObjectREFR* ref, const std::string& path)
    {
        ForEachPlaying(ref, path, [](void* sequence)
        {
            using DeactivateFn = bool(__thiscall*)(void* seq, float easeOut, uint8_t transition);
            uintptr_t table = *reinterpret_cast<const uintptr_t*>(sequence);
            (*reinterpret_cast<const DeactivateFn*>(table + process::kSequenceDeactivate))(sequence, 0.3f, 0);
        });
    }

    bool IsPlayingAnimFile(TESObjectREFR* ref, const std::string& path, bool loop)
    {
        bool playing = false;
        ForEachPlaying(ref, path, [&](void* sequence)
        {
            playing = true;
            if (loop)
                *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(sequence) + process::kSequenceCycle) = 0;
        });
        return playing;
    }

    bool MatchAnimFileTime(TESObjectREFR* ref, TESObjectREFR* other, const std::string& path, float slack, float* moved)
    {
        // A sequence's place is its last scaled time; it plays at (clock + offset) * frequency, so moving the offset
        // moves it along. An offset still at -infinity means it hasn't run a frame yet.
        constexpr float kNotStarted = -1e30f;
        bool found = false;
        float target = 0;
        ForEachPlaying(other, path, [&](void* sequence)
        {
            if (found || Field<float>(sequence, process::kSequenceOffset) < kNotStarted)
                return;
            target = Field<float>(sequence, process::kSequenceScaledTime);
            found = true;
        });
        bool done = false;
        if (!found || !std::isfinite(target))
            return false;
        ForEachPlaying(ref, path, [&](void* sequence)
        {
            auto& offset = *reinterpret_cast<float*>(static_cast<uint8_t*>(sequence) + process::kSequenceOffset);
            float frequency = Field<float>(sequence, process::kSequenceFrequency);
            float now = Field<float>(sequence, process::kSequenceScaledTime);
            if (done || offset < kNotStarted || !(frequency > 0) || !std::isfinite(now))
                return;
            done = true;
            float by = target - now;
            float length = Field<float>(sequence, process::kSequenceEnd) - Field<float>(sequence, process::kSequenceBegin);
            if (length > 0)
            {
                // the short way round the loop
                by = std::fmod(by, length);
                if (by > length / 2)
                    by -= length;
                else if (by < -length / 2)
                    by += length;
            }
            if (std::fabs(by) <= slack)
                by = 0;
            else
                offset += by / frequency;
            if (moved)
                *moved = by;
        });
        return done;
    }

    bool IsJumping(TESObjectREFR* ref)
    {
        if (!IsActor(ref))
            return false;
        const void* anim = AnimDataOf(static_cast<Actor*>(ref));
        if (!anim)
            return false;
        uint32_t index = Field<uint16_t>(anim, process::kAnimGroupIds + 2) & 0xFF;
        if (index >= addr::kAnimGroupCount)
            return false;
        auto name = *reinterpret_cast<const char* const*>(addr::kAnimGroupInfos + index * addr::kAnimGroupInfoSize);
        return name && (std::strncmp(name, "JumpStart", 9) == 0 || std::strncmp(name, "JumpLoop", 8) == 0);
    }

    bool ReadActorState(TESObjectREFR* ref, dev::ActorState& out)
    {
        if (!IsActor(ref))
            return false;
        auto* actor = static_cast<Actor*>(ref);
        coop::Pose pose;
        if (GetPose(ref, pose))
        {
            out.x = pose.x;
            out.y = pose.y;
            out.z = pose.z;
            out.heading = pose.heading;
            out.space = pose.space;
        }
        MoverState m = ReadMover(ref);
        out.mover = MoverKindName(m.kind);
        out.flags = m.flags;
        out.askedFlags = m.asked;
        out.askedOn = m.askedOn;
        out.lifeState = LifeState(ref);
        out.action = AnimAction(ref);
        out.processLevel = actor->baseProcess ? static_cast<int>(Field<uint32_t>(actor->baseProcess, process::kLevel)) : -1;
        const void* anim = AnimDataOf(actor);
        out.animsKnown = anim != nullptr;
        out.lockedControls = ref == Player() ? PlayerDisabledControls() : -1;
        const void* cached = actor->baseProcess ? Field<const void*>(actor->baseProcess, process::kCachedValues) : nullptr;
        uint32_t cachedFlags = cached ? Field<uint32_t>(cached, process::kCachedFlags) : 0;
        out.walkSpeed = (cachedFlags & 0x1000) ? Field<float>(cached, process::kCachedWalkSpeed) : 0;
        out.runSpeed = (cachedFlags & 0x2000) ? Field<float>(cached, process::kCachedRunSpeed) : 0;
        for (int i = 0; i < dev::kAnimLayers; ++i)
            out.anims[i] = anim ? Field<uint16_t>(anim, process::kAnimGroupIds + 2 * i) : 0;
        const void* idle = anim ? Field<const void*>(anim, process::kAnimIdle) : nullptr;
        const TESForm* idleForm = idle && Field<const void*>(idle, process::kIdleActor) == actor ?
            Field<const TESForm*>(idle, process::kIdleForm) : nullptr;
        out.idle = idleForm ? idleForm->refID : 0;
        for (int i = 0; i < dev::kAnimLayers; ++i)
        {
            const void* sequence = anim ? Field<const void*>(anim, process::kAnimSequences + 4 * i) : nullptr;
            const char* name = sequence ? Field<const char*>(sequence, process::kSequenceName) : nullptr;
            out.sequences[i] = name ? name : "";
        }
        return true;
    }

    bool Inspector::Watch(TESObjectREFR* ref)
    {
        dev::ActorState s;
        if (!ref || !ReadActorState(ref, s))
        {
            if (m_refId)
                LOG_INFO("Inspector: stopped following %08X", m_refId);
            m_refId = 0;
            return ref == nullptr;
        }
        s.aiKnown = AiStateBits(ref, s.ai);
        m_refId = RefId(ref);
        m_last = m_lastMotion = s;
        uint32_t now = GetTickCount();
        m_lastMotionAt = now;
        m_nextAi = now + kAiEveryMs;
        m_nextMotion = now + kMotionEveryMs;
        m_lost = false;
        LOG_INFO("Inspector: following %08X: %s", m_refId, dev::Describe(s, AnimGroupName).c_str());
        return true;
    }

    void Inspector::Forget()
    {
        m_lost = false;
    }

    void Inspector::Update(uint32_t now)
    {
        if (!m_refId)
            return;
        TESObjectREFR* ref = LookupRef(m_refId);
        dev::ActorState s = m_last;
        if (!ref || !ReadActorState(ref, s))
        {
            if (!m_lost)
                LOG_INFO("Inspector: %08X isn't loaded right now", m_refId);
            m_lost = true;
            return;
        }
        if (m_lost)
            LOG_INFO("Inspector: %08X is back: %s", m_refId, dev::Describe(s, AnimGroupName).c_str());
        m_lost = false;
        if (coop::IsDue(now, m_nextAi))
        {
            m_nextAi = now + kAiEveryMs;
            s.aiKnown = AiStateBits(ref, s.ai);
        }

        if (now - m_secondStart >= 1000)
        {
            if (m_linesThisSecond > kMaxLinesPerSecond)
                LOG_INFO("Inspector: (%u more changes not logged)", m_linesThisSecond - kMaxLinesPerSecond);
            m_secondStart = now;
            m_linesThisSecond = 0;
        }
        for (const std::string& line : dev::Changes(m_last, s, AnimGroupName))
            if (++m_linesThisSecond <= kMaxLinesPerSecond)
                LOG_INFO("Inspector %08X: %s", m_refId, line.c_str());

        if (coop::IsDue(now, m_nextMotion))
        {
            m_nextMotion = now + kMotionEveryMs;
            bool moved = s.x != m_lastMotion.x || s.y != m_lastMotion.y || s.z != m_lastMotion.z ||
                s.heading != m_lastMotion.heading;
            if (moved)
                LOG_INFO("Inspector %08X: %s", m_refId, dev::Motion(m_lastMotion, s, now - m_lastMotionAt).c_str());
            m_lastMotion = s;
            m_lastMotionAt = now;
        }
        m_last = s;
    }
}
