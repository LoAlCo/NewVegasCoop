// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "coop/gear_lockers.h"
#include "coop/activator_uses.h"
#include "coop/chat.h"
#include "coop/quests.h"
#include "coop/talk_log.h"
#include "coop/vats_feel.h"
#include "coop/balance.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <utility>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <deque>
#include <functional>
#include <intrin.h>
#include "coop/attack_versions.h"
#include "coop/clock.h"
#include "coop/shot_counter.h"
#include "game/addresses.h"
#include "game/mover.h"
#include "log.h"
#include "nvse/PluginAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"
#include <map>
#include <set>
#include <string>
#include "game/internal.h"

namespace game
{
    namespace
    {
        namespace npcl
        {
            constexpr uint32_t kFlags = 0x34, kBaseData = 0x30, kRace = 0x110, kFaceGen = 0x134, kFaceGenSize = 0x20,
                kFaceValues = 0x0C, kFaceCount = 0x18, kHair = 0x1B8, kHairLength = 0x1BC, kEyes = 0x1C0,
                kHairColor = 0x1D8, kHeadParts = 0x1DC;
            constexpr uint32_t kFaceCounts[3] = {50, 30, 50};
        }

        uint32_t IdAt(const void* npc, uint32_t offset)
        {
            const TESForm* form = At<const TESForm*>(npc, offset);
            return form ? form->refID : 0;
        }

        template <typename T>
        T* FormOfType(uint32_t id, uint8_t type)
        {
            TESForm* form = id ? LookupForm(id) : nullptr;
            return form && form->typeID == type ? reinterpret_cast<T*>(form) : nullptr;
        }
    }

    bool ReadMyLooks(net::PlayerLooks& out)
    {
        TESObjectREFR* player = Player();
        const TESForm* npc = player ? player->baseForm : nullptr;
        if (!npc || npc->typeID != kFormType_TESNPC)
            return false;
        out = net::PlayerLooks{};
        out.female = (At<uint32_t>(npc, npcl::kFlags) & 1) != 0;
        out.race = IdAt(npc, npcl::kRace);
        out.hair = IdAt(npc, npcl::kHair);
        out.eyes = IdAt(npc, npcl::kEyes);
        float length = At<float>(npc, npcl::kHairLength);
        out.hairLength = std::isfinite(length) && length >= 0 && length <= 10 ? length : 0;
        out.hairColor = At<uint32_t>(npc, npcl::kHairColor);
        size_t at = 0;
        for (int i = 0; i < 3; ++i)
        {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(npc) + npcl::kFaceGen + i * npcl::kFaceGenSize;
            const float* values = *reinterpret_cast<const float* const*>(data + npcl::kFaceValues);
            uint32_t count = *reinterpret_cast<const uint32_t*>(data + npcl::kFaceCount);
            for (uint32_t j = 0; j < npcl::kFaceCounts[i]; ++j, ++at)
            {
                float v = values && count == npcl::kFaceCounts[i] ? values[j] : 0;
                out.face[at] = std::isfinite(v) && v > -net::kMaxFaceValue && v < net::kMaxFaceValue ? v : 0;
            }
        }
        const uint8_t* node = reinterpret_cast<const uint8_t*>(npc) + npcl::kHeadParts;
        for (int n = 0; node && n < 32 && out.headParts.size() < net::kMaxHeadParts; ++n)
        {
            if (const TESForm* part = *reinterpret_cast<const TESForm* const*>(node))
                if ((part->refID >> 24) != 0xFF)
                    out.headParts.push_back(part->refID);
            node = *reinterpret_cast<const uint8_t* const*>(node + 4);
        }
        for (const char* c = Name(player); *c && out.name.size() < net::kMaxNameLength; ++c)
            out.name += static_cast<unsigned char>(*c) < 0x20 || *c == 0x7F ? '?' : *c;
        if ((out.race >> 24) == 0xFF) out.race = 0;
        if ((out.hair >> 24) == 0xFF) out.hair = 0;
        if ((out.eyes >> 24) == 0xFF) out.eyes = 0;
        return true;
    }

        bool LooksOntoBase(TESForm* base, const net::PlayerLooks& looks, int& shared)
        {
        if (!base || base->typeID != kFormType_TESNPC ||
            std::find(g_standInBases.begin(), g_standInBases.end(), base->refID) == g_standInBases.end())
            return false;
        auto* npc = reinterpret_cast<uint8_t*>(base);
        if (!looks.name.empty())
            SetName(static_cast<TESActorBase*>(base)->fullName.name, looks.name.c_str());
        if (TESForm* race = FormOfType<TESForm>(looks.race, kFormType_TESRace))
            if ((At<uint32_t>(race, addr::kRaceFlags) & (addr::kRacePlayable | addr::kRaceChild)) == addr::kRacePlayable)
                *reinterpret_cast<TESForm**>(npc + npcl::kRace) = race;
        if (((At<uint32_t>(npc, npcl::kFlags) & 1) != 0) != looks.female)
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(addr::kSetActorBaseFlag)(
                npc + npcl::kBaseData, 1, looks.female ? 1 : 0, 1);
        if (TESForm* hair = FormOfType<TESForm>(looks.hair, kFormType_TESHair))
            *reinterpret_cast<TESForm**>(npc + npcl::kHair) = hair;
        if (TESForm* eyes = FormOfType<TESForm>(looks.eyes, kFormType_TESEyes))
            *reinterpret_cast<TESForm**>(npc + npcl::kEyes) = eyes;
        *reinterpret_cast<float*>(npc + npcl::kHairLength) = looks.hairLength;
        *reinterpret_cast<uint32_t*>(npc + npcl::kHairColor) = looks.hairColor;
        const uint8_t* traveler = reinterpret_cast<const uint8_t*>(LookupForm(ids::kStandInBase));
        size_t at = 0;
        for (int i = 0; i < 3; ++i)
        {
            uint8_t* data = npc + npcl::kFaceGen + i * npcl::kFaceGenSize;
            float*& values = *reinterpret_cast<float**>(data + npcl::kFaceValues);
            uint32_t count = *reinterpret_cast<uint32_t*>(data + npcl::kFaceCount);
            if (count != npcl::kFaceCounts[i])
            {
                at += npcl::kFaceCounts[i];
                continue;
            }
            const float* theirs = traveler ? *reinterpret_cast<const float* const*>(
                traveler + npcl::kFaceGen + i * npcl::kFaceGenSize + npcl::kFaceValues) : nullptr;
            if (!values || values == theirs)
            {
                ++shared;
                values = static_cast<float*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(
                    count * sizeof(float)));
                if (!values)
                    return false;
            }
            for (uint32_t j = 0; j < count; ++j)
                values[j] = looks.face[at++];
        }
        uint8_t* node = npc + npcl::kHeadParts;
        *reinterpret_cast<void**>(node) = nullptr;
        *reinterpret_cast<void**>(node + 4) = nullptr;
        bool first = true;
        for (uint32_t id : looks.headParts)
        {
            TESForm* part = FormOfType<TESForm>(id, kFormType_BGSHeadPart);
            if (!part)
                continue;
            if (!first)
            {
                auto* next = static_cast<uint8_t*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(8));
                if (!next)
                    break;
                *reinterpret_cast<void**>(next + 4) = nullptr;
                *reinterpret_cast<void**>(node + 4) = next;
                node = next;
            }
            *reinterpret_cast<TESForm**>(node) = part;
            first = false;
        }
        return true;
        }

    namespace
    {
        int s_toneFrames = 0;
        bool s_toneWas = false;
    }

    bool RemakeFace(TESObjectREFR* standIn)
    {
        if (!standIn || !standIn->GetNiNode())
            return false;
        bool& egt = *reinterpret_cast<bool*>(addr::kLoadFaceGenEgt);
        if (!s_toneFrames)
            s_toneWas = egt;
        egt = true;
        s_toneFrames = 2;
        reinterpret_cast<void(__thiscall*)(TESObjectREFR*)>(addr::kRemakeHead)(standIn);
        return true;
    }

    const void* FaceNode(TESObjectREFR* standIn)
    {
        if (!standIn || !standIn->GetNiNode() || !IsActor(standIn))
            return nullptr;
        auto get = reinterpret_cast<void*(__thiscall*)(TESObjectREFR*, UInt32)>(
            (*reinterpret_cast<void***>(standIn))[addr::kVtblFaceGenNodeBiped / 4]);
        return get(standIn, 0);
    }

    void FaceToneFrame()
    {
        if (s_toneFrames && --s_toneFrames == 0)
            *reinterpret_cast<bool*>(addr::kLoadFaceGenEgt) = s_toneWas;
    }

    bool ApplyLooks(TESObjectREFR* standIn, const net::PlayerLooks& looks, bool* headRemade)
    {
        if (headRemade)
            *headRemade = false;
        TESForm* base = standIn ? standIn->baseForm : nullptr;
        bool wasFemale = base && (At<uint32_t>(base, npcl::kFlags) & 1) != 0;
        int shared = 0;
        if (!LooksOntoBase(base, looks, shared))
            return false;
        if (wasFemale != looks.female && g_update3D)
        {
            g_scripts->CallFunctionAlt(g_update3D, standIn, 0);
            LOG_INFO("Looks: %08X is now %s: its body loaded again", standIn->refID, looks.female ? "female" : "male");
            return true;
        }
        bool remade = RemakeFace(standIn);
        if (headRemade)
            *headRemade = remade;
        LOG_INFO("Looks: %08X made to look like its player (%s, race %08X, hair %08X, %zu head parts%s)", standIn->refID,
            looks.female ? "female" : "male", looks.race, looks.hair, looks.headParts.size(),
            shared ? "; its face's numbers were the Traveler's: given its own" : "");
        return true;
    }
}
