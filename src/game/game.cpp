// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/game.h"
#include "game/internal.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "coop/gear_lockers.h"
#include "coop/activator_uses.h"
#include "coop/chat.h"
#include "coop/chat_history.h"
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

extern const _FormHeap_Free FormHeap_Free = reinterpret_cast<_FormHeap_Free>(game::addr::kFormHeapFree);

namespace game
{
    TESObjectREFR* Player()
    {
        return *reinterpret_cast<TESObjectREFR**>(addr::kThePlayer);
    }

    TESForm* LookupForm(uint32_t formId)
    {
        auto* forms = *reinterpret_cast<NiTPointerMap<TESForm>**>(addr::kFormMap);
        return forms ? forms->Lookup(formId) : nullptr;
    }

    TESObjectREFR* LookupRef(uint32_t refId)
    {
        TESForm* form = LookupForm(refId);
        return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
    }

    uint32_t RefId(const TESObjectREFR* ref)
    {
        return ref ? ref->refID : 0;
    }

    bool GetPose(const TESObjectREFR* ref, coop::Pose& out)
    {
        if (!ref || !ref->parentCell)
            return false;
        const TESObjectCELL* cell = ref->parentCell;
        out.space = cell->worldSpace ? cell->worldSpace->refID : cell->refID;
        out.x = ref->posX;
        out.y = ref->posY;
        out.z = ref->posZ;
        out.heading = ref->rotZ;
        out.pitch = ref->rotX;
        return true;
    }

    namespace
    {
        int StanceBits(TESObjectREFR* ref)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_compiled || !ref || !g_scripts->CallFunction(g_playerFlags, ref, nullptr, &result, 0))
                return 0;
            return static_cast<int>(result.GetNumber());
        }
    }

    uint8_t StanceFlags(TESObjectREFR* ref)
    {
        return static_cast<uint8_t>(StanceBits(ref)) & coop::Pose::kKnownFlags;
    }

    void Stance(TESObjectREFR* ref, uint8_t& flags, int& sitting)
    {
        int bits = StanceBits(ref);
        flags = static_cast<uint8_t>(bits) & coop::Pose::kKnownFlags;
        sitting = (bits >> 2) & 7;
    }

    std::vector<uint32_t> PlayerGear()
    {
        return Gear(Player(), false);
    }

    std::vector<uint32_t> Gear(TESObjectREFR* ref, bool armourOnly)
    {
        std::vector<uint32_t> items;
        if (!g_compiled || !IsActor(ref))
            return items;
        for (int slot = 0; slot < kEquipSlots; ++slot)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_scripts->CallFunction(g_equipped, ref, nullptr, &result, 1, static_cast<uint32_t>(slot)))
                continue;
            TESForm* item = result.GetTESForm();
            if (item && (!armourOnly || item->IsArmor()))
                items.push_back(item->refID);
        }
        return items;
    }

    std::vector<net::WornItem> WornGear(TESObjectREFR* ref)
    {
        std::vector<net::WornItem> items;
        NVSEArrayVarInterface::Element result;
        if (!g_wornGear || !g_arrays || !IsActor(ref) || !g_scripts->CallFunction(g_wornGear, ref, nullptr, &result, 0))
            return items;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (!size || size == static_cast<uint32_t>(-1) || size % 2 || size > 2 * kEquipSlots)
            return items;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (!g_arrays->GetElements(array, elements.data(), nullptr))
            return items;
        for (uint32_t i = 0; i < size; i += 2)
        {
            TESForm* item = elements[i].GetTESForm();
            double health = elements[i + 1].GetNumber();
            if (!item || !item->IsArmor() ||
                std::any_of(items.begin(), items.end(), [&](const net::WornItem& o) { return o.item == item->refID; }))
                continue;
            items.push_back({item->refID, health >= 0 && health <= 100 ? static_cast<uint8_t>(health) :
                net::kNoCondition});
        }
        std::sort(items.begin(), items.end(),
            [](const net::WornItem& a, const net::WornItem& b) { return a.item < b.item; });
        return items;
    }

    bool Wear(TESObjectREFR* ref, const net::WornItem& item)
    {
        TESForm* form = LookupForm(item.item);
        if (!g_wearAt || !IsActor(ref) || !form || !form->IsArmor())
            return false;
        uint32_t health = item.health <= 100 ? item.health : 100;
        g_scripts->CallFunctionAlt(g_wearAt, ref, 2, form, health);
        return true;
    }

    bool InDocMitchellsHouse()
    {
        TESObjectREFR* player = Player();
        return player && player->parentCell && player->parentCell->refID == ids::kDocMitchellHouse;
    }

    bool IsPastIntro()
    {
        TESObjectREFR* player = Player();
        if (!player || !player->parentCell || QuestStage(ids::kBennyIntroQuest) >= 0)
            return false;
        TESForm* quest = LookupForm(ids::kIntroQuest);
        bool tutorialDone = quest && quest->typeID == kFormType_TESQuest &&
            (static_cast<TESQuest*>(quest)->flags & layout::kQuestCompleted) != 0;
        return tutorialDone || !InDocMitchellsHouse();
    }

    namespace
    {
        TESObjectREFR* Place(TESForm* base)
        {
            if (!g_compiled || !Player())
                return nullptr;
            NVSEArrayVarInterface::Element result;
            if (!base || !g_scripts->CallFunction(g_spawn, Player(), nullptr, &result, 1, base))
                return nullptr;
            TESForm* form = result.GetTESForm();
            return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
        }
    }

    TESObjectREFR* PlaceActor(uint32_t baseId)
    {
        TESForm* base = LookupForm(baseId);
        if (!base || (base->typeID != kFormType_TESNPC && base->typeID != kFormType_TESCreature &&
            base->typeID != kFormType_TESLevCreature && base->typeID != kFormType_TESLevCharacter))
            return nullptr;
        return Place(base);
    }

        void SetName(String& s, const char* name)
        {
            size_t len = 0;
            while (name[len] && len < 63)
                ++len;
            auto* text = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(UInt32)>(addr::kFormHeapAllocate)(
                static_cast<UInt32>(len + 1)));
            if (!text)
                return;
            for (size_t i = 0; i < len; ++i)
                text[i] = static_cast<unsigned char>(name[i]) < 0x20 ? '?' : name[i];
            text[len] = 0;
            s.m_data = text;
            s.m_dataLen = s.m_bufLen = static_cast<UInt16>(len);
        }

    TESObjectREFR* SpawnStandIn(const char* name, const net::PlayerLooks* looks)
    {
        TESForm* original = LookupForm(ids::kStandInBase);
        if (!g_compiled || !Player() || !name || !original || original->typeID != kFormType_TESNPC)
            return nullptr;
        NVSEArrayVarInterface::Element result;
        if (!g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, original))
            return nullptr;
        TESForm* copy = result.GetTESForm();
        if (!copy || copy->typeID != kFormType_TESNPC)
            return nullptr;
        auto* base = static_cast<TESActorBase*>(copy);
        SetName(base->fullName.name, name);
        base->container.formCountList.m_listHead.data = nullptr;
        base->container.formCountList.m_listHead.next = nullptr;
        if (g_noTalk)
            base->scriptable.script = g_noTalk;
        else
            LOG_WARN("Friends' characters can be talked to (the no-talking script didn't compile)");
        if (g_standInBases.size() >= kMaxStandInBases)
            g_standInBases.erase(g_standInBases.begin());
        g_standInBases.push_back(copy->refID);
        LOG_DEBUG("Character type %08X for %s", copy->refID, name);
        int shared = 0;
        if (looks && LooksOntoBase(copy, *looks, shared))
            LOG_INFO("Looks: %s's character made looking like them (%s)", name, looks->female ? "female" : "male");
        return Place(copy);
    }

    bool Is3DLoaded(TESObjectREFR* ref)
    {
        return ref && ref->GetNiNode();
    }

    void BringToPlayer(TESObjectREFR* ref)
    {
        MoveOnto(ref, Player());
    }

    void MoveOnto(TESObjectREFR* ref, TESObjectREFR* to)
    {
        if (g_compiled && ref && to)
            g_scripts->CallFunctionAlt(g_bringToPlayer, ref, 1, to);
    }

    void Move(TESObjectREFR* ref, const coop::Pose& pose)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_move, ref, 4, FloatArg(pose.x), FloatArg(pose.y), FloatArg(pose.z),
                FloatArg(pose.heading * kRadToDeg));
    }

    bool TravelTo(const coop::Pose& pose)
    {
        TESForm* place = LookupForm(pose.space);
        if (!g_compiled || !Player() || !place ||
            (place->typeID != kFormType_TESObjectCELL && place->typeID != kFormType_TESWorldSpace))
            return false;
        TESObjectREFR* player = Player();
        if (player->parentCell && player->parentCell->worldSpace && player->parentCell->worldSpace == place)
            g_scripts->CallFunctionAlt(g_moveBy, player, 4, FloatArg(pose.x - player->posX),
                FloatArg(pose.y - player->posY), FloatArg(pose.z - player->posZ), FloatArg(pose.heading * kRadToDeg));
        else
            g_scripts->CallFunctionAlt(g_travel, player, 5, place, FloatArg(pose.x), FloatArg(pose.y),
                FloatArg(pose.z), FloatArg(pose.heading * kRadToDeg));
        return true;
    }

    void Turn(TESObjectREFR* ref, float heading)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_turn, ref, 1, FloatArg(heading * kRadToDeg));
    }

    namespace
    {
        constexpr uintptr_t kAimSlots = 0x1C4;
        constexpr uintptr_t kAnimSequences = 0xE0;

        void** AimSlots(TESObjectREFR* ref)
        {
            BaseProcess* process = IsActor(ref) ? static_cast<Actor*>(ref)->baseProcess : nullptr;
            if (!process || process->processLevel > 1)
                return nullptr;
            return reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(process) + kAimSlots);
        }

        const char* SequenceName(const void* sequence)
        {
            const char* path = sequence ? *reinterpret_cast<const char* const*>(
                reinterpret_cast<const uint8_t*>(sequence) + 0x08) : nullptr;
            return path ? path : "-";
        }
    }

    void SetPitch(TESObjectREFR* ref, float pitch)
    {
        if (!IsActor(ref) || !std::isfinite(pitch))
            return;
        ref->rotX = pitch;
        void** slots = AimSlots(ref);
        AnimData* anim = slots ? ref->GetAnimData() : nullptr;
        if (!anim)
            return;
        void** playing = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(anim) + kAnimSequences);
        if (playing[4] && playing[5] && playing[6])
            for (int i = 0; i < 3; ++i)
                slots[i] = playing[4 + i];
    }

    void LogAimSlots(TESObjectREFR* ref)
    {
        void** slots = AimSlots(ref);
        AnimData* anim = slots ? ref->GetAnimData() : nullptr;
        if (!anim)
        {
            LOG_INFO("Aim slots: no high process or animation data");
            return;
        }
        void** playing = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(anim) + kAnimSequences);
        auto group = [anim](int layer) {
            return *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(anim) + process::kAnimGroupIds +
                2 * layer);
        };
        LOG_INFO("Aim slots: tilted %s | %s | %s; playing %s | %s | %s; pitch %.2f; groups %04X (idle layer), %04X %04X "
            "%04X (weapon layers); weapon %s", SequenceName(slots[0]), SequenceName(slots[1]), SequenceName(slots[2]),
            SequenceName(playing[4]), SequenceName(playing[5]), SequenceName(playing[6]), ref->rotX, group(0), group(4),
            group(5), group(6), (StanceFlags(ref) & coop::Pose::kWeaponOut) ? "out" : "away");
    }

    namespace
    {
        bool g_dragging = false;
        bool g_dragAlwaysRun = false;
        std::vector<int> g_dragControlsOff;
    }

    bool* PlayerAlwaysRun()
    {
        TESObjectREFR* player = Player();
        return player ? reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(player) + layout::kPlayerAlwaysRun) : nullptr;
    }

    void SetDragging(bool on)
    {
        TESObjectREFR* player = Player();
        bool* alwaysRun = PlayerAlwaysRun();
        if (on == g_dragging || !player || !alwaysRun || !g_controlDisabled || !g_disableControl)
            return;
        g_dragging = on;
        if (on)
        {
            SetWeaponOut(player, false);
            for (int control : {layout::kControlAttack, layout::kControlAimBlock, layout::kControlReadyItem,
                     layout::kControlRun, layout::kControlAlwaysRun, layout::kControlJump, layout::kControlVats})
            {
                NVSEArrayVarInterface::Element result;
                if (g_scripts->CallFunction(g_controlDisabled, player, nullptr, &result, 1, static_cast<uint32_t>(control)) &&
                    result.GetNumber() == 0)
                {
                    g_scripts->CallFunction(g_disableControl, player, nullptr, nullptr, 2, static_cast<uint32_t>(control), 1u);
                    g_dragControlsOff.push_back(control);
                }
            }
            g_dragAlwaysRun = *alwaysRun;
            *alwaysRun = false;
        }
        else
        {
            for (int control : g_dragControlsOff)
                g_scripts->CallFunction(g_disableControl, player, nullptr, nullptr, 2, static_cast<uint32_t>(control), 0u);
            g_dragControlsOff.clear();
            *alwaysRun = g_dragAlwaysRun;
        }
        LOG_INFO("Dragging: %s (always-run %s)", on ? "walking only, no weapon" : "back to normal",
            on ? (g_dragAlwaysRun ? "was on: off till we let go" : "was off") : (*alwaysRun ? "on again" : "off"));
    }

    void SetWeaponOut(TESObjectREFR* ref, bool out)
    {
        if (IsActor(ref))
            reinterpret_cast<void(__thiscall*)(TESObjectREFR*, bool)>(addr::kActorSetWeaponOut)(ref, out);
    }

    bool Jump(TESObjectREFR* ref)
    {
        if (!IsActor(ref) || !static_cast<Actor*>(ref)->baseProcess)
            return false;
        return reinterpret_cast<int(__thiscall*)(TESObjectREFR*)>(addr::kMobileObjectJump)(ref) != 0;
    }

    void SetAlert(TESObjectREFR* ref, bool alert)
    {
        if (g_alert && IsActor(ref))
            g_scripts->CallFunctionAlt(g_alert, ref, 1, static_cast<uint32_t>(alert ? 1 : 0));
    }

    void SetRestrained(TESObjectREFR* ref, bool on)
    {
        if (g_restrain && IsActor(ref))
            g_scripts->CallFunctionAlt(g_restrain, ref, 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    void SetCombatOff(TESObjectREFR* ref, bool off)
    {
        if (g_combatOff && IsActor(ref))
            g_scripts->CallFunctionAlt(g_combatOff, ref, 1, static_cast<uint32_t>(off ? 1 : 0));
    }

    void EvaluatePackage(TESObjectREFR* ref)
    {
        if (g_evaluate && IsActor(ref))
            g_scripts->CallFunctionAlt(g_evaluate, ref, 0);
    }

    uint16_t MovementFlags(TESObjectREFR* ref)
    {
        return static_cast<uint16_t>(ReadMover(ref).flags);
    }

    void SetMovementFlags(TESObjectREFR* ref, uint16_t flags)
    {
        AskMovementFlags(ref, flags);
    }

    void RemoveAllItems(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_removeAllItems, ref, 0);
    }

    namespace
    {
        TESForm* TempClone(TESForm* form)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_clone || !form || !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, form))
                return nullptr;
            return result.GetTESForm();
        }

        BGSExplosion* HarmlessExplosion(BGSExplosion* explosion, bool quiet = false)
        {
            if (!explosion)
                return nullptr;
            auto& made = quiet ? g_quietExplosions : g_harmlessExplosions;
            if (auto it = made.find(explosion->refID); it != made.end())
                return it->second;
            TESForm* copy = TempClone(explosion);
            if (!copy || copy->typeID != kFormType_BGSExplosion)
                return nullptr;
            auto* e = static_cast<BGSExplosion*>(copy);
            e->damage = 0;
            e->force = 0;
            e->RADlevel = 0;
            e->RADradius = 0;
            e->dissipationTime = 0;
            e->placedObj = nullptr;
            e->enchantable.enchantItem = nullptr;
            e->SetFlag(BGSExplosion::kFlags_KnockDownAlways, false);
            e->SetFlag(BGSExplosion::kFlags_KnockDownByFormula, false);
            if (quiet)
            {
                e->model.SetModelPath(const_cast<char*>(""));
                e->light = nullptr;
                e->sound1 = nullptr;
                e->sound2 = nullptr;
                e->impactDataSet = nullptr;
                e->imageSpaceModForm.unk04 = 0;
                e->ISradius = 0;
            }
            made[explosion->refID] = e;
            return e;
        }

        BGSProjectile* HarmlessProjectile(BGSProjectile* projectile)
        {
            if (!projectile)
                return nullptr;
            if (auto it = g_harmlessProjectiles.find(projectile->refID); it != g_harmlessProjectiles.end())
                return it->second;
            BGSExplosion* explosion = nullptr;
            if (projectile->explosionForm && !(explosion = HarmlessExplosion(projectile->explosionForm, true)))
                return nullptr;
            TESForm* copy = TempClone(projectile);
            if (!copy || copy->typeID != kFormType_BGSProjectile)
                return nullptr;
            auto* p = static_cast<BGSProjectile*>(copy);
            p->explosionForm = explosion;
            p->impactForce = 0;
            p->SetFlag(BGSProjectile::kFlags_CanBePicked, false);
            p->SetFlag(BGSProjectile::kFlags_CanBeDisabled, false);
            g_harmlessProjectiles[projectile->refID] = p;
            LOG_DEBUG("Harmless copy %08X of projectile %08X (explosion %08X)", p->refID, projectile->refID,
                explosion ? explosion->refID : 0);
            return p;
        }

        bool IsHarmlessProjectile(const BGSProjectile* projectile)
        {
            for (auto& [original, copy] : g_harmlessProjectiles)
                if (copy == projectile)
                    return true;
            return false;
        }

        TESForm* HarmlessCopy(TESForm* weapon)
        {
            auto it = g_harmless.find(weapon->refID);
            if (it != g_harmless.end())
                return it->second;
            for (auto& [original, copy] : g_harmless)
                if (copy == weapon)
                    return copy;
            NVSEArrayVarInterface::Element result;
            if (g_harmless.size() >= kMaxHarmless ||
                !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, weapon))
                return nullptr;
            TESForm* copy = result.GetTESForm();
            if (!copy || !copy->IsWeapon())
                return nullptr;
            auto* w = static_cast<TESObjectWEAP*>(copy);
            w->attackDmg.damage = 0;
            w->criticalDamage = 0;
            w->criticalPercent = 0;
            w->criticalEffect = nullptr;
            w->enchantable.enchantItem = nullptr;
            w->limbDamageMult = 0;
            if (w->projectile)
            {
                BGSProjectile* harmless = HarmlessProjectile(w->projectile);
                if (harmless)
                    w->projectile = harmless;
                else
                    LOG_WARN("No harmless copy of %08X's projectile %08X: its shots won't be shown", weapon->refID,
                        w->projectile->refID);
            }
            g_harmless[weapon->refID] = copy;
            LOG_DEBUG("Harmless copy %08X of weapon %08X", copy->refID, weapon->refID);
            return copy;
        }

        bool IsGun(const TESObjectWEAP* w)
        {
            return w->eWeaponType >= TESObjectWEAP::kWeapType_OneHandPistol &&
                w->eWeaponType <= TESObjectWEAP::kWeapType_TwoHandLauncher;
        }

        bool IsThrown(const TESObjectWEAP* w)
        {
            return w->eWeaponType >= TESObjectWEAP::kWeapType_OneHandGrenade &&
                w->eWeaponType <= TESObjectWEAP::kWeapType_OneHandThrown;
        }

        bool IsMine(const TESObjectWEAP* w)
        {
            return w->eWeaponType == TESObjectWEAP::kWeapType_OneHandMine ||
                w->eWeaponType == TESObjectWEAP::kWeapType_OneHandLunchboxMine;
        }

        bool CanFire(const TESObjectWEAP* weapon)
        {
            return weapon && g_fire && weapon->projectile && IsHarmlessProjectile(weapon->projectile);
        }

        int CurrentAction(TESObjectREFR* ref)
        {
            BaseProcess* process = IsActor(ref) ? static_cast<Actor*>(ref)->baseProcess : nullptr;
            if (!process || process->processLevel != 0)
                return -1;
            return static_cast<HighProcess*>(process)->currentAction;
        }

        TESObjectWEAP* HeldWeapon(TESObjectREFR* ref)
        {
            NVSEArrayVarInterface::Element result;
            TESForm* held = g_scripts->CallFunction(g_equipped, ref, nullptr, &result, 1, static_cast<uint32_t>(5)) ?
                result.GetTESForm() : nullptr;
            return held && held->IsWeapon() ? static_cast<TESObjectWEAP*>(held) : nullptr;
        }

        bool WeaponLayerPlays(TESObjectREFR* ref)
        {
            AnimData* anim = IsActor(ref) ? ref->GetAnimData() : nullptr;
            if (!anim)
                return false;
            auto* playing = reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(anim) +
                process::kAnimSequences);
            return playing[process::kWeaponLayer] != nullptr;
        }

        void PlayVersions(TESObjectREFR* ref, const std::array<Script*, coop::kAttackVersions>& anims)
        {
            int left = coop::PlayAttackVersions(
                [&](int version) {
                    if (anims[version])
                        g_scripts->CallFunctionAlt(anims[version], ref, 0);
                },
                [&] { return WeaponLayerPlays(ref); });
            if (left)
                LOG_DEBUG("%08X's attack: nothing in its weapon layer, so %d of its up and down versions left out",
                    ref->refID, left);
        }

        void PlayAttackWith(TESObjectREFR* ref, TESObjectWEAP* weapon, uint8_t shots)
        {
            if (weapon && IsThrown(weapon))
            {
                const auto& anims = IsMine(weapon) ? g_mineAnim : g_throwAnim;
                PlayVersions(ref, anims[0] ? anims : g_attackAnim);
                return;
            }
            PlayVersions(ref, g_attackAnim);
            if (!weapon || !IsGun(weapon) || !CanFire(weapon))
                return;
            for (uint8_t i = 0; i < shots; ++i)
                g_scripts->CallFunctionAlt(g_fire, ref, 1, weapon);
        }

        void ReleaseWith(TESObjectREFR* ref, TESObjectWEAP* weapon)
        {
            if (weapon && IsThrown(weapon) && CanFire(weapon))
                g_scripts->CallFunctionAlt(g_fire, ref, 1, weapon);
        }
    }

    bool AddAndEquip(TESObjectREFR* ref, uint32_t formId, coop::GivenRounds* given)
    {
        TESForm* item = LookupForm(formId);
        if (!g_compiled || !ref || !item || !(item->IsArmor() || item->IsWeapon()))
            return false;
        if (item->IsWeapon() && !(item = HarmlessCopy(item)))
            return false;
        g_scripts->CallFunctionAlt(g_addAndEquip, ref, 1, item);
        if (item->IsWeapon() && IsGun(static_cast<TESObjectWEAP*>(item)) && g_addItems)
        {
            TESForm* ammo = static_cast<TESObjectWEAP*>(item)->ammo.ammo;
            if (ammo && ammo->typeID == kFormType_BGSListForm)
                ammo = static_cast<BGSListForm*>(ammo)->GetNthForm(0);
            if (ammo && ammo->typeID == kFormType_TESAmmo)
            {
                g_scripts->CallFunctionAlt(g_addItems, ref, 2, ammo, static_cast<uint32_t>(kPuppetAmmo));
                if (given)
                    given->Add(ammo->refID, kPuppetAmmo);
            }
        }
        return true;
    }

    void RemoveItem(TESObjectREFR* ref, uint32_t formId)
    {
        TESForm* item = LookupForm(formId);
        if (item && item->IsWeapon())
        {
            auto it = g_harmless.find(formId);
            item = it == g_harmless.end() ? nullptr : it->second;
        }
        if (g_compiled && ref && item)
            g_scripts->CallFunctionAlt(g_removeItem, ref, 1, item);
    }

    MyAttack TakeMyAttacks()
    {
        TESObjectREFR* player = Player();
        if (!g_compiled || !g_rounds || !player)
            return {};
        NVSEArrayVarInterface::Element result;
        TESForm* held = g_scripts->CallFunction(g_equipped, player, nullptr, &result, 1, static_cast<uint32_t>(5)) ?
            result.GetTESForm() : nullptr;
        auto* weapon = held && held->IsWeapon() ? static_cast<TESObjectWEAP*>(held) : nullptr;
        uint32_t weaponId = weapon ? weapon->refID : 0;
        int shots = 0;
        bool gun = weapon && IsGun(weapon);
        bool reloadStarted = false;
        int action = CurrentAction(player);
        bool attackStarted = action == HighProcess::kAnimAction_Attack && g_myAction != HighProcess::kAnimAction_Attack;
        if (gun)
        {
            int rounds = static_cast<int>(Number(g_rounds, player));
            uint32_t gone = 0;
            if (weaponId == g_myWeapon && g_myRounds > rounds)
            {
                int perShot = weapon->ammoUse > 1 ? weapon->ammoUse : 1;
                gone = static_cast<uint32_t>((g_myRounds - rounds + perShot - 1) / perShot);
            }
            g_myRounds = rounds;
            reloadStarted = IsReloadAction(action) && !IsReloadAction(g_myAction);
            shots = static_cast<int>(g_myShots.Look(attackStarted, IsAttackPhase(action), gone, reloadStarted));
        }
        else
        {
            g_myRounds = -1;
            bool thrown = weapon && IsThrown(weapon);
            if (thrown ? IsAttackPhase(action) && !IsAttackPhase(g_myAction) : attackStarted)
                shots = 1;
        }
        bool release = weapon && IsThrown(weapon) && IsThrowRelease(action) && !IsThrowRelease(g_myAction);
        g_myWeapon = weaponId;
        g_myAction = action;
        if (shots > 0)
            LOG_DEBUG("We attacked: %d %s with %08X", shots, gun ? "shots" : "swing", weaponId);
        if (release)
            LOG_DEBUG("Our throw left our hand (%08X)", weaponId);
        if (reloadStarted)
            LOG_DEBUG("We started reloading %08X (%d rounds in it now)", weaponId, g_myRounds);
        return {static_cast<uint8_t>(shots < 255 ? shots : 255), release, reloadStarted};
    }

    void PlayAttack(TESObjectREFR* ref, const std::vector<uint32_t>& worn, uint8_t shots)
    {
        if (!g_compiled || !g_attackAnim[0] || !ref)
            return;
        TESObjectWEAP* weapon = nullptr;
        for (uint32_t id : worn)
            if (auto it = g_harmless.find(id); it != g_harmless.end())
                weapon = static_cast<TESObjectWEAP*>(it->second);
        PlayAttackWith(ref, weapon, shots);
    }

    void PlayNpcAttack(TESObjectREFR* ref, uint8_t shots)
    {
        if (!g_compiled || !g_attackAnim[0] || !ref)
            return;
        TESObjectWEAP* held = HeldWeapon(ref);
        PlayAttackWith(ref, held ? static_cast<TESObjectWEAP*>(HarmlessCopy(held)) : nullptr, shots);
    }

    void ReleaseThrow(TESObjectREFR* ref, const std::vector<uint32_t>& worn)
    {
        if (!g_compiled || !ref)
            return;
        TESObjectWEAP* weapon = nullptr;
        for (uint32_t id : worn)
            if (auto it = g_harmless.find(id); it != g_harmless.end())
                weapon = static_cast<TESObjectWEAP*>(it->second);
        ReleaseWith(ref, weapon);
    }

    void ReleaseNpcThrow(TESObjectREFR* ref)
    {
        if (!g_compiled || !ref)
            return;
        TESObjectWEAP* held = HeldWeapon(ref);
        ReleaseWith(ref, held ? static_cast<TESObjectWEAP*>(HarmlessCopy(held)) : nullptr);
    }

    uint32_t HeldWeaponId(TESObjectREFR* ref)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        if (!held)
            return 0;
        for (auto& [original, copy] : g_harmless)
            if (copy == held)
                return original;
        return held->refID;
    }

    bool HoldsHarmless(TESObjectREFR* ref)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        for (auto& [original, copy] : g_harmless)
            if (held && copy == held)
                return true;
        return false;
    }

    int CountShots(TESObjectREFR* ref, GunState& state)
    {
        TESObjectWEAP* held = g_compiled && ref ? HeldWeapon(ref) : nullptr;
        uint32_t id = held ? held->refID : 0;
        for (auto& [original, copy] : g_harmless)
            if (held && copy == held)
                id = original;
        bool switched = id != state.weapon;
        state.weapon = id;
        if (!held || !IsGun(held) || !g_npcRounds)
        {
            state.rounds = -1;
            return -1;
        }
        int rounds = static_cast<int>(Number(g_npcRounds, ref));
        int shots = 0;
        if (!switched && state.rounds >= 0 && rounds < state.rounds)
        {
            int perShot = held->ammoUse > 1 ? held->ammoUse : 1;
            shots = (state.rounds - rounds + perShot - 1) / perShot;
        }
        state.rounds = rounds;
        return shots;
    }

    void StopIdle(TESObjectREFR* ref)
    {
        if (g_compiled && g_stopIdle && ref)
            g_scripts->CallFunctionAlt(g_stopIdle, ref, 0);
    }

    namespace
    {
        uint8_t* g_loadSave = nullptr;
        bool (*g_mayLoad)(const std::string&) = nullptr;

        bool __fastcall LoadSaveHook(void* manager, void*, const char* name, uint32_t a, uint32_t b, uint32_t c)
        {
            if (g_mayLoad && name)
            {
                std::string save(name);
                size_t slash = save.find_last_of("\\/");
                if (slash != std::string::npos)
                    save.erase(0, slash + 1);
                if (save.size() > 4 && _stricmp(save.c_str() + save.size() - 4, ".fos") == 0)
                    save.resize(save.size() - 4);
                if (!g_mayLoad(save))
                    return false;
            }
            using Fn = bool(__thiscall*)(void*, const char*, uint32_t, uint32_t, uint32_t);
            return reinterpret_cast<Fn>(g_loadSave)(manager, name, a, b, c);
        }
    }

    bool InstallLoadCheck(bool (*mayLoad)(const std::string& save))
    {
        g_mayLoad = mayLoad;
        if (!g_loadSave)
        {
            static const uint8_t kStart[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x2C, 0x02, 0x00, 0x00};
            g_loadSave = HookStart(addr::kLoadSave, kStart, sizeof(kStart), reinterpret_cast<const void*>(&LoadSaveHook));
        }
        return g_loadSave != nullptr;
    }

    void NoteMadeCharacters(const std::vector<uint32_t>& refIds)
    {
        if (!g_compiled || !g_madeClear || !g_madeAdd || !Player())
            return;
        g_scripts->CallFunctionAlt(g_madeClear, Player(), 0);
        for (uint32_t id : refIds)
            if (TESObjectREFR* ref = LookupRef(id))
                g_scripts->CallFunctionAlt(g_madeAdd, Player(), 1, ref);
    }

    std::vector<uint32_t> SavedMadeCharacters()
    {
        std::vector<uint32_t> ids;
        if (!g_compiled || !g_madeCount || !g_madeGet || !Player())
            return ids;
        int count = static_cast<int>(Number(g_madeCount, Player()));
        constexpr int kMaxSaved = 128;
        for (int i = 0; i < count && i < kMaxSaved; ++i)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_scripts->CallFunction(g_madeGet, Player(), nullptr, &result, 1, static_cast<uint32_t>(i)))
                continue;
            TESForm* form = result.GetTESForm();
            if (form && form->GetIsReference() && (form->refID >> 24) == 0xFF)
                ids.push_back(form->refID);
        }
        return ids;
    }

    void ResetAnimation(TESObjectREFR* ref, int how)
    {
        if (!g_compiled || !IsActor(ref))
            return;
        if (how == 1)
            StopIdle(ref);
        else if (how == 2 && g_idleGroup)
            g_scripts->CallFunctionAlt(g_idleGroup, ref, 0);
        else if (how == 3 && g_forceStopIdle)
            g_scripts->CallFunctionAlt(g_forceStopIdle, ref, 0);
    }

    void Resurrect(TESObjectREFR* ref)
    {
        if (g_compiled && g_resurrect && ref)
            g_scripts->CallFunctionAlt(g_resurrect, ref, 0);
    }

    uintptr_t ReloadFunction(TESObjectREFR* ref)
    {
        if (!IsActor(ref))
            return 0;
        return (*reinterpret_cast<uintptr_t* const*>(ref))[addr::kActorReloadSlot];
    }

    bool Reload(TESObjectREFR* ref, int animType)
    {
        TESObjectWEAP* weapon = g_compiled && IsActor(ref) ? HeldWeapon(ref) : nullptr;
        uintptr_t function = ReloadFunction(ref);
        if (!weapon || function != addr::kActorReload)
            return false;
        using ReloadFn = int(__thiscall*)(TESObjectREFR*, TESObjectWEAP*, uint32_t, uint8_t);
        reinterpret_cast<ReloadFn>(function)(ref, weapon, static_cast<uint32_t>(animType), 0);
        return true;
    }

    bool InScriptAnimation(TESObjectREFR* ref)
    {
        return CurrentAction(ref) == HighProcess::kAnimAction_Force_Script_Anim;
    }

    bool IsAttackAction(int action)
    {
        return action == HighProcess::kAnimAction_Attack;
    }

    bool IsAttackPhase(int action)
    {
        return action >= HighProcess::kAnimAction_Attack && action <= HighProcess::kAnimAction_Attack_Throw_Release;
    }

    DWORD g_mainThread = 0;
    void NoteThread(const char* hook, std::atomic<int>& said)
    {
        bool main = GetCurrentThreadId() == g_mainThread;
        int which = main ? 1 : 2;
        if (!g_mainThread || (said.fetch_or(which) & which))
            return;
        if (main)
            LOG_DEBUG("%s runs on the game's main thread", hook);
        else
            LOG_WARN("%s ran on another thread than the game's main one", hook);
    }

    namespace
    {
        uintptr_t g_explosionInit = 0;
        std::vector<SeenExplosion> g_explosions;
        std::mutex g_explosionsLock;

        bool IsOurHarmless(const TESForm* form)
        {
            for (auto* list : {&g_harmlessExplosions, &g_quietExplosions})
                for (auto& [original, copy] : *list)
                    if (copy == form)
                        return true;
            return false;
        }

        void __fastcall ExplosionInitHook(TESObjectREFR* explosion)
        {
            bool fresh = explosion && !*reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(explosion) +
                addr::kExplosionProcess);
            reinterpret_cast<void(__fastcall*)(TESObjectREFR*)>(g_explosionInit)(explosion);
            static std::atomic<int> s_saidThread{0};
            NoteThread("The explosion hook", s_saidThread);
            if (!fresh || !explosion->baseForm || explosion->baseForm->typeID != kFormType_BGSExplosion ||
                IsOurHarmless(explosion->baseForm))
                return;
            coop::Pose at;
            if (!GetPose(explosion, at))
                return;
            auto* source = *reinterpret_cast<TESObjectREFR* const*>(reinterpret_cast<const uint8_t*>(explosion) +
                addr::kExplosionSource);
            std::lock_guard<std::mutex> lock(g_explosionsLock);
            if (g_explosions.size() >= 32)
                return;
            uint8_t sourceType = source && source->baseForm ? source->baseForm->typeID : 0;
            bool objectsOwn = (sourceType == kFormType_TESObjectACTI || sourceType == kFormType_BGSTalkingActivator) &&
                ObjectFullHealth(source) > 0;
            g_explosions.push_back({explosion->baseForm->refID, at.space, at.x, at.y, at.z, source && source == Player(),
                objectsOwn});
            LOG_DEBUG("Explosion %08X (%08X) at %.0f, %.0f, %.0f by %08X", explosion->refID, explosion->baseForm->refID,
                at.x, at.y, at.z, source ? source->refID : 0);
        }
    }

    bool WatchExplosions()
    {
        if (g_explosionInit)
            return true;
        g_mainThread = GetCurrentThreadId();
        auto* slot = reinterpret_cast<uintptr_t*>(addr::kExplosionInitSlot);
        DWORD old;
        if (VirtualProtect(slot, 4, PAGE_READWRITE, &old))
        {
            g_explosionInit = *slot;
            *slot = reinterpret_cast<uintptr_t>(&ExplosionInitHook);
            VirtualProtect(slot, 4, old, &old);
        }
        if (g_explosionInit && g_explosionInit != addr::kExplosionInit)
            LOG_INFO("Explosions: another plugin hooks their start too (%08X); ours passes on to it",
                static_cast<unsigned>(g_explosionInit));
        return g_explosionInit != 0;
    }

    std::vector<SeenExplosion> TakeExplosions()
    {
        std::lock_guard<std::mutex> lock(g_explosionsLock);
        return std::move(g_explosions);
    }

    bool ShowExplosion(uint32_t formId, uint32_t space, float x, float y, float z)
    {
        TESForm* form = LookupForm(formId);
        TESForm* where = LookupForm(space);
        if (!g_compiled || !g_placeAt || !Player() || !form || form->typeID != kFormType_BGSExplosion || !where ||
            (where->typeID != kFormType_TESWorldSpace && where->typeID != kFormType_TESObjectCELL))
            return false;
        BGSExplosion* harmless = HarmlessExplosion(static_cast<BGSExplosion*>(form));
        if (!harmless)
            return false;
        g_scripts->CallFunctionAlt(g_placeAt, Player(), 5, harmless, where, FloatArg(x), FloatArg(y), FloatArg(z));
        return true;
    }

    bool IsThrowRelease(int action)
    {
        return action == HighProcess::kAnimAction_Attack_Throw_Release;
    }

    bool IsReloadAction(int action)
    {
        return action == HighProcess::kAnimAction_Reload;
    }

    void Remove(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_remove, ref, 0);
    }

    namespace
    {
        std::string Safe(const char* text)
        {
            std::string safe(text ? text : "");
            std::replace(safe.begin(), safe.end(), '|', '/');
            return safe;
        }
    }

    void Notify(const char* text)
    {
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_notify, Player(), 1, Safe(text).c_str());
    }

    void Notify(const char* text, float seconds)
    {
        if (!g_compiled || !Player())
            return;
        g_scripts->CallFunctionAlt(g_notifyFor, Player(), 2, Safe(text).c_str(), FloatArg(seconds));
    }

    bool AiStateBits(TESObjectREFR* ref, uint32_t& out)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_aiState || !IsActor(ref) || !g_scripts->CallFunction(g_aiState, ref, nullptr, &result, 0))
            return false;
        out = static_cast<uint32_t>(result.GetNumber());
        return true;
    }

    int AnimAction(TESObjectREFR* ref) { return CurrentAction(ref); }

    void ConsolePrint(const char* text)
    {
        if (g_print && Player())
            g_scripts->CallFunctionAlt(g_print, Player(), 1, text);
    }

    namespace
    {

        bool SameType(uint8_t type, uint8_t wanted) { return type == wanted; }

        bool ItemType(uint8_t type, uint8_t)
        {
            using IsInventoryTypeFn = bool(__cdecl*)(UInt32 formType);
            return reinterpret_cast<IsInventoryTypeFn>(addr::kIsInventoryObjectType)(type);
        }

        void AddRefs(const TESObjectCELL* cell, TypeTest test, uint8_t baseType, std::vector<TESObjectREFR*>& out,
            const TESObjectREFR* around, float range, bool skipPersistent)
        {
            if (!cell)
                return;
            for (auto* node = &cell->objectList.m_listHead; node; node = node->next)
            {
                TESObjectREFR* ref = node->data;
                if (!ref || !ref->baseForm || !test(ref->baseForm->typeID, baseType))
                    continue;
                if (skipPersistent && (ref->flags & layout::kRefPersistent))
                    continue;
                if (around)
                {
                    float dx = ref->posX - around->posX, dy = ref->posY - around->posY;
                    if (dx * dx + dy * dy > range * range)
                        continue;
                }
                out.push_back(ref);
            }
        }

    }

    void RefsNear(TypeTest test, uint8_t baseType, std::vector<TESObjectREFR*>& out, float range)
    {
        out.clear();
        TESObjectREFR* player = Player();
        TESObjectCELL* cell = player ? player->parentCell : nullptr;
        if (!cell)
            return;
        const void* world = At<const void*>(cell, layout::kCellWorldSpace);
        if (!world)
        {
            AddRefs(cell, test, baseType, out, nullptr, 0, false);
            return;
        }
        const void* tes = *reinterpret_cast<const void* const*>(layout::kTesSingleton);
        const void* grid = tes ? At<const void*>(tes, layout::kTesGridCells) : nullptr;
        if (grid)
        {
            uint32_t size = At<uint32_t>(grid, layout::kGridSize);
            auto* cells = At<TESObjectCELL* const*>(grid, layout::kGridCellArray);
            if (cells && size <= 15)
                for (uint32_t i = 0; i < size * size; ++i)
                    AddRefs(cells[i], test, baseType, out, player, range, true);
        }
        AddRefs(At<const TESObjectCELL*>(world, layout::kWorldPersistentCell), test, baseType, out, player, range, false);
    }

    int LoadedGrid()
    {
        const void* tes = *reinterpret_cast<const void* const*>(layout::kTesSingleton);
        const void* grid = tes ? At<const void*>(tes, layout::kTesGridCells) : nullptr;
        uint32_t size = grid ? At<uint32_t>(grid, layout::kGridSize) : 0;
        return size >= 3 && size <= 15 && (size & 1) ? static_cast<int>(size) : 5;
    }

    bool InMenuMode()
    {
        return reinterpret_cast<bool(__cdecl*)()>(addr::kInMenuMode)();
    }

    namespace
    {
        bool ReadMenuStack(uint32_t (&ids)[addr::kMenuStackSlots])
        {
            __try
            {
                auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
                if (!manager)
                    return false;
                for (int i = 0; i < addr::kMenuStackSlots; ++i)
                    ids[i] = reinterpret_cast<const uint32_t*>(manager + addr::kMenuStack)[i];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        const char* MenuName(uint32_t id)
        {
            switch (id)
            {
            case 1001: return "message";
            case 1002: return "inventory";
            case 1003: return "stats";
            case 1004: return "HUD";
            case 1007: return "loading";
            case 1008: return "container";
            case 1009: return "dialogue";
            case 1012: return "sleep/wait";
            case 1013: return "pause menu";
            case 1014: return "lockpick";
            case 1016: return "quantity";
            case 1023: return "map";
            case 1027: return "level up";
            case 1053: return "barter";
            case 1055: return "hacking";
            case 1056: return "VATS";
            case 1057: return "computer";
            default: return nullptr;
            }
        }
    }

    void OpenMenus(char* out, size_t size)
    {
        if (!out || !size)
            return;
        out[0] = 0;
        uint32_t ids[addr::kMenuStackSlots] = {};
        if (!ReadMenuStack(ids))
        {
            std::snprintf(out, size, "?");
            return;
        }
        size_t used = 0;
        for (uint32_t id : ids)
        {
            if (!id || used + 1 >= size)
                continue;
            const char* name = MenuName(id);
            int n = name ? std::snprintf(out + used, size - used, "%s%s", used ? ", " : "", name)
                         : std::snprintf(out + used, size - used, "%s%u", used ? ", " : "", id);
            if (n > 0)
                used += static_cast<size_t>(n) < size - used ? static_cast<size_t>(n) : size - used - 1;
        }
    }

    bool QuantityPromptOpen()
    {
        uint32_t ids[addr::kMenuStackSlots] = {};
        if (!ReadMenuStack(ids))
            return false;
        for (uint32_t id : ids)
            if (id == 1016)
                return true;
        return false;
    }

    bool SetFieldOfView(int degrees)
    {
        static float s_world = 0;
        TESObjectREFR* player = Player();
        if (!player || !g_fovNow)
            return false;
        NVSEArrayVarInterface::Element world, hands;
        if (!g_scripts->CallFunction(g_fovNow, player, nullptr, &world, 1, 0u) ||
            !g_scripts->CallFunction(g_fovNow, player, nullptr, &hands, 1, 1u))
            return false;
        if (!s_world)
            s_world = static_cast<float>(world.GetNumber());
        float want = degrees ? static_cast<float>(degrees) : s_world;
        if (want < 30 || want > 150)
            return false;
        char line[64];
        std::snprintf(line, sizeof(line), "fov %.0f %.0f", want, hands.GetNumber());
        RunConsoleLine(line);
        LOG_INFO("Field of view: %.0f (the game's own: %.0f)", want, s_world);
        return true;
    }

    int ExteriorCellBuffer()
    {
        const auto* setting = reinterpret_cast<const uint8_t*>(addr::kSettingExteriorCellBuffer);
        const char* name = *reinterpret_cast<const char* const*>(setting + 8);
        auto at = reinterpret_cast<uintptr_t>(name);
        if (at < 0x00400000 || at >= 0x02000000 || std::strcmp(name, "uExterior Cell Buffer:General") != 0)
            return -1;
        return static_cast<int>(*reinterpret_cast<const uint32_t*>(setting + 4));
    }

    float LoadedReach()
    {
        return static_cast<float>((LoadedGrid() - 1) / 2) * 4096.0f;
    }

    namespace
    {
        bool MovableType(uint8_t type, uint8_t)
        {
            return type == kFormType_BGSMovableStatic || type == kFormType_TESObjectCONT;
        }

        std::map<uint32_t, bool> g_movable;

        bool IsMovable(TESObjectREFR* ref)
        {
            auto it = g_movable.find(ref->refID);
            if (it != g_movable.end())
                return it->second;
            NVSEArrayVarInterface::Element result;
            if (!g_isMobile || !ref->GetNiNode() || !g_scripts->CallFunction(g_isMobile, ref, nullptr, &result, 0))
                return false;
            if (g_movable.size() > 20000)
                g_movable.clear();
            return g_movable[ref->refID] = result.GetNumber() != 0;
        }
    }

    void MovablesNear(std::vector<TESObjectREFR*>& out, float range)
    {
        std::vector<TESObjectREFR*> candidates;
        RefsNear(MovableType, 0, candidates, range);
        out.clear();
        for (TESObjectREFR* ref : candidates)
            if ((ref->refID >> 24) != 0xFF && IsMovable(ref))
                out.push_back(ref);
    }

    bool Get3DPosition(TESObjectREFR* ref, float& x, float& y, float& z)
    {
        const auto* node = ref ? reinterpret_cast<const uint8_t*>(ref->GetNiNode()) : nullptr;
        if (!node)
            return false;
        const auto* p = reinterpret_cast<const float*>(node + layout::kNodeWorldTranslate);
        x = p[0];
        y = p[1];
        z = p[2];
        return true;
    }

    void DoorsNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_TESObjectDOOR, out, range);
    }

    void ContainersNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_TESObjectCONT, out, range);
    }

    namespace
    {
        struct ListNode { void* data; ListNode* next; };

        template <typename F> void ForEachIn(const void* list, F f)
        {
            for (auto* node = static_cast<const ListNode*>(list); node; node = node->next)
                if (node->data)
                    f(node->data);
        }

        TESForm* QuestById(uint32_t id)
        {
            TESForm* form = LookupForm(id);
            return form && form->typeID == kFormType_TESQuest ? form : nullptr;
        }

        void ReadQuestInto(const TESForm* quest, QuestInfo& q)
        {
            q.id = quest->refID;
            q.flags = At<uint8_t>(quest, layout::kQuestFlags);
            q.stage = At<uint8_t>(quest, layout::kQuestCurrentStage);
            q.delay = At<float>(quest, layout::kQuestDelay);
            const char* name = At<const char*>(quest, layout::kQuestEditorId);
            uint16_t length = At<uint16_t>(quest, layout::kQuestEditorId + 4);
            q.name = name && length < 200 ? std::string(name, length) : std::string();
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestStages, [&](void* stage)
            {
                if (At<uint8_t>(stage, layout::kStageDone))
                    q.done.push_back(At<uint8_t>(stage, layout::kStageId));
            });
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl)
                    q.objectives.push_back({At<uint32_t>(item, layout::kObjectiveId),
                        At<uint32_t>(item, layout::kObjectiveStatus)});
            });
        }
    }

    void ReadQuests(std::vector<QuestInfo>& out, bool all)
    {
        out.clear();
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            QuestInfo q;
            ReadQuestInto(static_cast<TESForm*>(quest), q);
            if (all || (q.flags & 0x63) || !q.done.empty())
                out.push_back(std::move(q));
        });
    }

    bool ReadQuest(uint32_t id, QuestInfo& out)
    {
        TESForm* quest = QuestById(id);
        if (!quest)
            return false;
        out = QuestInfo{};
        ReadQuestInto(quest, out);
        return true;
    }

    void SetQuestFlags(uint32_t id, uint8_t flags)
    {
        if (TESForm* quest = QuestById(id))
            *reinterpret_cast<uint8_t*>(reinterpret_cast<char*>(quest) + layout::kQuestFlags) = flags;
    }

    void SetQuestStageDone(uint32_t id, uint8_t stage, bool done)
    {
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestStages, [&](void* s)
            {
                if (At<uint8_t>(s, layout::kStageId) == stage)
                    static_cast<uint8_t*>(s)[layout::kStageDone] = done ? 1 : 0;
            });
    }

    void SetQuestObjective(uint32_t id, uint32_t objective, uint32_t status)
    {
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<const char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl &&
                    At<uint32_t>(item, layout::kObjectiveId) == objective)
                    *reinterpret_cast<uint32_t*>(static_cast<char*>(item) + layout::kObjectiveStatus) = status;
            });
    }

    void QuestFingerprints(std::vector<std::pair<uint32_t, uint32_t>>& out)
    {
        out.clear();
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            uint32_t h = 2166136261u;
            auto mix = [&](uint32_t v)
            {
                for (int i = 0; i < 4; ++i, v >>= 8)
                    h = (h ^ (v & 0xFF)) * 16777619u;
            };
            mix(At<uint8_t>(quest, layout::kQuestFlags));
            mix(At<uint8_t>(quest, layout::kQuestCurrentStage));
            ForEachIn(static_cast<char*>(quest) + layout::kQuestStages, [&](void* st)
            {
                mix(At<uint8_t>(st, layout::kStageId) | (At<uint8_t>(st, layout::kStageDone) << 8));
            });
            ForEachIn(static_cast<char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl)
                {
                    mix(At<uint32_t>(item, layout::kObjectiveId));
                    mix(At<uint32_t>(item, layout::kObjectiveStatus));
                }
            });
            out.push_back({static_cast<TESForm*>(quest)->refID, h});
        });
    }

    void WriteQuest(const QuestInfo& q)
    {
        TESForm* quest = QuestById(q.id);
        if (!quest)
            return;
        char* base = reinterpret_cast<char*>(quest);
        base[layout::kQuestFlags] = static_cast<char>(q.flags);
        base[layout::kQuestCurrentStage] = static_cast<char>(q.stage);
        ForEachIn(base + layout::kQuestStages, [&](void* st)
        {
            uint8_t id = At<uint8_t>(st, layout::kStageId);
            bool done = std::find(q.done.begin(), q.done.end(), id) != q.done.end();
            static_cast<uint8_t*>(st)[layout::kStageDone] = done ? 1 : 0;
        });
        ForEachIn(base + layout::kQuestObjectives, [&](void* item)
        {
            if (*static_cast<uintptr_t*>(item) != addr::kQuestObjectiveVtbl)
                return;
            uint32_t id = At<uint32_t>(item, layout::kObjectiveId);
            for (auto& [objective, status] : q.objectives)
                if (objective == id)
                    *reinterpret_cast<uint32_t*>(static_cast<char*>(item) + layout::kObjectiveStatus) = status;
        });
    }

    namespace
    {
        std::string GameString(const void* at)
        {
            const char* text = At<const char*>(at, 0);
            uint16_t length = At<uint16_t>(at, 4);
            return text && length < 1000 ? std::string(text, length) : std::string();
        }
    }

    std::string QuestName(uint32_t id)
    {
        TESForm* quest = QuestById(id);
        return quest ? GameString(reinterpret_cast<char*>(quest) + layout::kQuestName) : std::string();
    }

    std::string ObjectiveText(uint32_t id, uint32_t objective)
    {
        std::string text;
        if (TESForm* quest = QuestById(id))
            ForEachIn(reinterpret_cast<char*>(quest) + layout::kQuestObjectives, [&](void* item)
            {
                if (*static_cast<uintptr_t*>(item) == addr::kQuestObjectiveVtbl &&
                    At<uint32_t>(item, layout::kObjectiveId) == objective)
                    text = GameString(static_cast<char*>(item) + layout::kObjectiveText);
            });
        return text;
    }

    namespace
    {
        std::vector<coop::DetachedSlot> g_detachedQuestScripts;
        Cmd_Execute g_hasScriptCommand = nullptr;
        bool g_inHasScriptCommand = false;

        bool HasScriptCommandWithQuestScripts(COMMAND_ARGS)
        {
            if (g_inHasScriptCommand)
                return g_hasScriptCommand(PASS_COMMAND_ARGS);
            g_inHasScriptCommand = true;
            bool done = coop::WithDetachedPutBack(g_detachedQuestScripts, [&] {
                return g_hasScriptCommand(PASS_COMMAND_ARGS);
            });
            g_inHasScriptCommand = false;
            return done;
        }

        void GuardHasScriptCommand()
        {
            if (g_hasScriptCommand || !g_cmdTable)
                return;
            auto* info = const_cast<CommandInfo*>(g_cmdTable->GetByName("HasScriptCommand"));
            if (!info || !info->execute)
                return LOG_WARN("Quests: no HasScriptCommand to guard");
            g_hasScriptCommand = info->execute;
            info->execute = HasScriptCommandWithQuestScripts;
            LOG_INFO("Quests: HasScriptCommand sees detached quests' scripts while it reads");
        }
    }

    size_t DetachQuestScripts(bool (*accept)(uint32_t id, const void* context), const void* context)
    {
        GuardHasScriptCommand();
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return 0;
        size_t detached = 0;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerQuests, [&](void* quest)
        {
            void** slot = reinterpret_cast<void**>(static_cast<char*>(quest) + layout::kQuestScript);
            uint32_t id = static_cast<TESForm*>(quest)->refID;
            if (*slot && accept(id, context))
            {
                g_detachedQuestScripts.push_back({slot, *slot});
                *slot = nullptr;
                ++detached;
            }
        });
        return detached;
    }

    size_t PutBackQuestScripts()
    {
        size_t count = g_detachedQuestScripts.size();
        for (const coop::DetachedSlot& d : g_detachedQuestScripts)
            if (!*d.slot)
                *d.slot = d.script;
        g_detachedQuestScripts.clear();
        return count;
    }

    std::vector<std::pair<uint32_t, double>> QuestVariables(uint32_t id)
    {
        std::vector<std::pair<uint32_t, double>> out;
        TESForm* quest = QuestById(id);
        const void* locals = quest ? At<const void*>(quest, layout::kQuestLocals) : nullptr;
        const void* vars = locals ? At<const void*>(locals, layout::kLocalsVars) : nullptr;
        if (vars)
            ForEachIn(vars, [&](void* v)
            {
                out.push_back({At<uint32_t>(v, layout::kVarId), At<double>(v, layout::kVarValue)});
            });
        return out;
    }

    void* QuestScript(uint32_t id)
    {
        TESForm* quest = QuestById(id);
        return quest ? At<void*>(quest, layout::kQuestScript) : nullptr;
    }

    void* SwapQuestScript(uint32_t id, void* script)
    {
        TESForm* quest = QuestById(id);
        if (!quest)
            return nullptr;
        void** slot = reinterpret_cast<void**>(reinterpret_cast<char*>(quest) + layout::kQuestScript);
        void* old = *slot;
        *slot = script;
        return old;
    }

    bool ShowObjective(uint32_t quest, uint32_t objective, bool on)
    {
        TESForm* q = QuestById(quest);
        return g_objectiveShown && q && Player() &&
            g_scripts->CallFunctionAlt(g_objectiveShown, Player(), 3, q, objective, static_cast<uint32_t>(on ? 1 : 0));
    }

    bool CompleteObjective(uint32_t quest, uint32_t objective, bool on)
    {
        TESForm* q = QuestById(quest);
        return g_objectiveDone && q && Player() &&
            g_scripts->CallFunctionAlt(g_objectiveDone, Player(), 3, q, objective, static_cast<uint32_t>(on ? 1 : 0));
    }

    bool CompleteQuest(uint32_t quest)
    {
        TESForm* q = QuestById(quest);
        return g_completeQuest && q && Player() && g_scripts->CallFunctionAlt(g_completeQuest, Player(), 1, q);
    }

    bool InDialogue()
    {
        return g_inDialogue && Player() && Number(g_inDialogue, Player()) != 0;
    }

    int32_t PlayerXp() { return Player() ? static_cast<int32_t>(Number(g_xp, Player())) : 0; }
    int32_t PlayerKarma() { return Player() ? static_cast<int32_t>(Number(g_karma, Player())) : 0; }

    void GiveXp(int32_t xp)
    {
        if (g_giveXp && Player() && xp > 0)
            g_scripts->CallFunctionAlt(g_giveXp, Player(), 1, static_cast<uint32_t>(xp));
    }

    void GiveKarma(int32_t karma)
    {
        if (g_giveKarma && Player() && karma)
            g_scripts->CallFunctionAlt(g_giveKarma, Player(), 1, static_cast<uint32_t>(karma));
    }

    std::vector<ReputationInfo> ReadReputations()
    {
        std::vector<ReputationInfo> out;
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return out;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerReputations, [&](void* rep)
        {
            out.push_back({static_cast<TESForm*>(rep)->refID, GameString(static_cast<char*>(rep) + layout::kReputationName),
                At<float>(rep, layout::kReputationFame), At<float>(rep, layout::kReputationInfamy)});
        });
        return out;
    }

    void WriteReputation(uint32_t form, float fame, float infamy)
    {
        TESForm* rep = LookupForm(form);
        if (!rep || rep->typeID != kFormType_TESReputation)
            return;
        *reinterpret_cast<float*>(reinterpret_cast<char*>(rep) + layout::kReputationFame) = fame;
        *reinterpret_cast<float*>(reinterpret_cast<char*>(rep) + layout::kReputationInfamy) = infamy;
    }

    void Greet(TESObjectREFR* actor)
    {
        if (g_greet && actor)
            g_scripts->CallFunctionAlt(g_greet, actor, 0);
    }

    TESObjectREFR* DialoguePartner()
    {
        const void* menu = *reinterpret_cast<const void* const*>(addr::kDialogMenu);
        return menu ? At<TESObjectREFR*>(menu, layout::kDialogMenuPartner) : nullptr;
    }

    Spoken SpokenNow()
    {
        Spoken out;
        const void* menu = *reinterpret_cast<const void* const*>(addr::kDialogMenu);
        const void* const* const* topicAt = menu ? At<const void* const* const*>(menu, layout::kDialogMenuTopicNode) :
            nullptr;
        const void* const* topicNode = topicAt ? *topicAt : nullptr;
        const char* topic = topicNode ? static_cast<const char*>(*topicNode) : nullptr;
        if (!topic)
            return out;
        const void* const* next = At<const void* const*>(topic, layout::kMenuTopicResponseNode);
        const void* response = nullptr;
        int guard = 0;
        for (const void* const* node = reinterpret_cast<const void* const*>(topic + layout::kMenuTopicResponses);
             node && guard < 64; node = static_cast<const void* const*>(node[1]), ++guard)
        {
            if (node == next)
                break;
            response = node[0];
        }
        if (!response)
            return out;
        out.id = reinterpret_cast<uintptr_t>(response);
        const char* text = *static_cast<const char* const*>(response);
        out.text = text ? text : "";
        const TESForm* info = At<const TESForm*>(topic, layout::kMenuTopicInfo);
        const TESForm* topicForm = At<const TESForm*>(topic, layout::kMenuTopicTopic);
        out.info = info ? info->refID : 0;
        out.topic = topicForm ? topicForm->refID : 0;
        out.response = static_cast<int>(At<uint32_t>(response, layout::kResponseNumber));
        return out;
    }

    bool IsTalking(const TESObjectREFR* actor)
    {
        return actor && At<uint8_t>(actor, layout::kIsTalking) != 0;
    }

    namespace
    {
        bool IsMine(const TESObjectREFR* ref)
        {
            if (!ref || ref->typeID != kFormType_GrenadeProjectile || !ref->baseForm ||
                ref->baseForm->typeID != kFormType_BGSProjectile)
                return false;
            uint16_t flags = static_cast<const BGSProjectile*>(ref->baseForm)->projFlags;
            return (flags & layout::kMineBaseMask) == layout::kMineBaseFlags;
        }

        constexpr size_t kMaxPuppets = 8;
        std::atomic<TESObjectREFR*> g_minePuppets[kMaxPuppets] = {};
        std::atomic<bool> g_mineFriendGame{false};
        uintptr_t g_mineReacts = 0;

        bool __fastcall MineReactsHook(TESObjectREFR* mine, void*, TESObjectREFR* target, TESObjectREFR* shooter,
            void* ownerFaction)
        {
            if (target)
            {
                for (const auto& puppet : g_minePuppets)
                    if (puppet.load(std::memory_order_relaxed) == target)
                    {
                        static std::atomic<uint32_t> s_logged{0};
                        if (mine && s_logged.exchange(mine->refID) != mine->refID)
                            LOG_DEBUG("Mines: %08X not set off by a friend's character (%08X)", mine->refID,
                                target->refID);
                        return false;
                    }
                if (g_mineFriendGame.load(std::memory_order_relaxed) && mine && (mine->refID >> 24) != 0xFF)
                    return false;
            }
            using Fn = bool(__thiscall*)(TESObjectREFR*, TESObjectREFR*, TESObjectREFR*, void*);
            uintptr_t original = g_mineReacts ? g_mineReacts : addr::kMineReactsToTarget;
            return reinterpret_cast<Fn>(original)(mine, target, shooter, ownerFaction);
        }
    }

    void MinesNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(SameType, kFormType_BGSProjectile, out, range);
        out.erase(std::remove_if(out.begin(), out.end(), [](TESObjectREFR* ref) { return !IsMine(ref); }), out.end());
    }

    int MineState(const TESObjectREFR* ref)
    {
        if (!IsMine(ref))
            return -1;
        if (ref->flags & (layout::kRefDeleted | layout::kRefDisabled))
            return net::kMineGone;
        return (At<uint32_t>(ref, layout::kProjFlags) & layout::kProjTurnedOff) ? net::kMineOff : 0;
    }

    bool TurnOffMine(TESObjectREFR* mine)
    {
        TESObjectREFR* player = Player();
        if (MineState(mine) != 0 || !player)
            return false;
        using Fn = bool(__thiscall*)(TESObjectREFR*, TESObjectREFR*, bool);
        reinterpret_cast<Fn>(addr::kProjectileTurnOff)(mine, player, true);
        return MineState(mine) == net::kMineOff;
    }

    bool MineExplosion(TESObjectREFR* mine)
    {
        if (MineState(mine) < 0 || !g_compiled || !g_placeAt || !Player() || !mine->parentCell)
            return false;
        auto* explosion = static_cast<BGSProjectile*>(mine->baseForm)->explosionForm;
        const void* world = At<const void*>(mine->parentCell, layout::kCellWorldSpace);
        const void* where = world ? world : mine->parentCell;
        if (!explosion)
            return false;
        g_scripts->CallFunctionAlt(g_placeAt, Player(), 5, explosion, where, FloatArg(mine->posX), FloatArg(mine->posY),
            FloatArg(mine->posZ));
        return true;
    }

    namespace
    {
        std::atomic<bool> g_holdLevelUp{false};
        uintptr_t g_combatCount = 0;

        int __fastcall LevelUpCombatCount(void* player, void*)
        {
            uintptr_t original = g_combatCount ? g_combatCount : addr::kPlayerCombatCount;
            int count = reinterpret_cast<int(__thiscall*)(void*)>(original)(player);
            return count < 1 && g_holdLevelUp.load(std::memory_order_relaxed) ? 1 : count;
        }
    }

    bool HoldLevelUp(bool fighting)
    {
        g_holdLevelUp.store(fighting, std::memory_order_relaxed);
        static int hooked = -1;
        if (hooked >= 0)
            return hooked == 1;
        hooked = 0;
        for (uintptr_t site : addr::kLevelUpCombatCalls)
        {
            uintptr_t was = HookCall(site, reinterpret_cast<const void*>(&LevelUpCombatCount));
            if (!was)
            {
                LOG_WARN("Level up: %08X isn't a call; a friend's level-up screen may open mid-fight",
                    static_cast<unsigned>(site));
                return false;
            }
            if (was != addr::kPlayerCombatCount)
                LOG_INFO("Level up: another plugin hooks its combat check too (%08X); ours passes on to it",
                    static_cast<unsigned>(was));
            g_combatCount = was;
        }
        hooked = 1;
        return true;
    }

    bool SetMineRules(TESObjectREFR* const* puppets, size_t count, bool friendGame)
    {
        for (size_t i = 0; i < kMaxPuppets; ++i)
            g_minePuppets[i].store(i < count ? puppets[i] : nullptr, std::memory_order_relaxed);
        g_mineFriendGame.store(friendGame, std::memory_order_relaxed);
        if (g_mineReacts)
            return true;
        static bool tried = false;
        if (tried)
            return false;
        tried = true;
        uintptr_t was = HookCall(addr::kMineReactsCall, reinterpret_cast<const void*>(&MineReactsHook));
        if (!was)
        {
            LOG_WARN("Mines: couldn't hook their check (%08X isn't a call); friends' characters will set them off",
                static_cast<unsigned>(addr::kMineReactsCall));
            return false;
        }
        g_mineReacts = was;
        if (was != addr::kMineReactsToTarget)
            LOG_INFO("Mines: another plugin hooks their check too (%08X); ours passes on to it", static_cast<unsigned>(was));
        return true;
    }

    namespace
    {
        uintptr_t g_makeOptions = 0;
        bool g_optionsMade = false;
        void (*g_onHost)() = nullptr;
        void (*g_onJoin)() = nullptr;
        void __cdecl HostClicked() { if (g_onHost) g_onHost(); }
        void __cdecl JoinClicked() { if (g_onJoin) g_onJoin(); }

        struct SimpleArray
        {
            void* vtable;
            uint8_t** items;
            uint32_t count;
        };

        void* MakeOption(const char* name, void (*clicked)(), uint32_t flags = 1)
        {
            void* option = reinterpret_cast<void* (__cdecl*)(size_t)>(addr::kFormHeapAllocate)(0x10);
            if (!option)
                return nullptr;
            using Init = void*(__thiscall*)(void*, const char*, void (*)(), uint32_t);
            reinterpret_cast<Init>(addr::kStartMenuOptionInit)(option, name, clicked, flags);
            using Append = int(__thiscall*)(void*, void**);
            reinterpret_cast<Append>(addr::kSimpleArrayAppend)(reinterpret_cast<void*>(addr::kStartMenuOptions), &option);
            return option;
        }

        void __cdecl MakeOptions()
        {
            reinterpret_cast<void(__cdecl*)()>(g_makeOptions)();
            g_optionsMade = true;
            auto* list = reinterpret_cast<SimpleArray*>(addr::kStartMenuOptions);
            size_t load = list->count;
            for (uint32_t i = 0; i < list->count; ++i)
            {
                uint8_t* o = list->items[i];
                const char* name = o ? *reinterpret_cast<const char**>(o + 4) : nullptr;
                uintptr_t clicked = o ? *reinterpret_cast<uintptr_t*>(o + 8) : 0;
                LOG_DEBUG("Main menu option %u: %s (flags %X, function %08X)", i, name ? name : "?",
                    o ? *reinterpret_cast<uint32_t*>(o + 0xC) : 0, static_cast<unsigned>(clicked));
                if (clicked == addr::kLoadOption)
                    load = i + 1;
            }
            uint32_t before = list->count;
            MakeOption("Host Co-op", &HostClicked);
            MakeOption("Join Co-op", &JoinClicked);
            if (list->count != before + 2)
                return LOG_WARN("Main menu: couldn't add Host and Join");
            for (uint32_t k = 0; k < 2 && load < before; ++k)
            {
                uint8_t* ours = list->items[before + k];
                std::memmove(&list->items[load + k + 1], &list->items[load + k], (before - load) * sizeof(uint8_t*));
                list->items[load + k] = ours;
            }
            LOG_INFO("Main menu: added Host Co-op and Join Co-op (after option %u of %u)",
                static_cast<unsigned>(load < before ? load : before), before);
        }
    }

    bool MainMenuMade() { return g_optionsMade; }

    bool AddCoopToMainMenu(void (*onHost)(), void (*onJoin)())
    {
        if (g_makeOptions)
            return true;
        g_onHost = onHost;
        g_onJoin = onJoin;
        g_makeOptions = HookCall(addr::kMakeStartMenuOptionsCall, reinterpret_cast<const void*>(&MakeOptions));
        if (g_makeOptions && g_makeOptions != addr::kMakeStartMenuOptions)
            LOG_INFO("Main menu: another plugin makes its options too (%08X); ours are added after",
                static_cast<unsigned>(g_makeOptions));
        return g_makeOptions != 0;
    }

    void CloseMenu()
    {
        if (g_closeMenu && Player())
            g_scripts->CallFunctionAlt(g_closeMenu, Player(), 0);
    }

    std::vector<EnableChange> TakeEnables()
    {
        return std::move(g_enables);
    }

    void SetEnabled(TESObjectREFR* ref, bool enabled)
    {
        Script* s = enabled ? g_enable : g_disable;
        if (s && ref)
            g_scripts->CallFunctionAlt(s, ref, 0);
    }

    bool IsEnabled(const TESObjectREFR* ref)
    {
        return ref && !(ref->flags & layout::kRefDisabled);
    }

    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> PlayerObjectives(size_t& targets)
    {
        std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> out;
        targets = 0;
        TESObjectREFR* player = Player();
        if (!player)
            return out;
        ForEachIn(reinterpret_cast<char*>(player) + layout::kPlayerObjectives, [&](void* o)
        {
            const TESForm* quest = At<const TESForm*>(o, 0x10);
            out.emplace_back(quest ? quest->refID : 0, At<uint32_t>(o, layout::kObjectiveId),
                At<uint32_t>(o, layout::kObjectiveStatus));
        });
        ForEachIn(reinterpret_cast<char*>(player) + layout::kPlayerTargets, [&](void*) { ++targets; });
        return out;
    }

    uint32_t TrackedQuest()
    {
        TESObjectREFR* player = Player();
        TESForm* quest = player ? At<TESForm*>(player, layout::kPlayerActiveQuest) : nullptr;
        return quest ? quest->refID : 0;
    }

    void ItemsNear(std::vector<TESObjectREFR*>& out, float range)
    {
        RefsNear(ItemType, 0, out, range);
    }

    WorldItem ReadWorldItem(TESObjectREFR* ref)
    {
        WorldItem w;
        if (!ref || !ref->baseForm)
            return w;
        w.base = ref->baseForm->refID;
        w.flags = ref->flags;
        w.inCell = ref->parentCell != nullptr;
        w.x = ref->posX;
        w.y = ref->posY;
        w.z = ref->posZ;
        w.rx = std::remainder(ref->rotX, 6.2831853f);
        w.ry = std::remainder(ref->rotY, 6.2831853f);
        w.rz = std::remainder(ref->rotZ, 6.2831853f);
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        if (const void* count = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
            layout::kExtraCount))
            w.count = At<int16_t>(count, layout::kCountValue);
        return w;
    }

    TESObjectREFR* OpenContainer()
    {
        const void* menu = *reinterpret_cast<const void* const*>(addr::kContainerMenu);
        if (menu)
            return At<TESObjectREFR*>(menu, layout::kContainerMenuRef);
        const void* barter = *reinterpret_cast<const void* const*>(addr::kBarterMenu);
        TESObjectREFR* merchant = barter ? At<TESObjectREFR*>(barter, layout::kBarterMenuMerchant) : nullptr;
        if (!merchant)
            return nullptr;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&merchant->extraDataList,
            layout::kExtraMerchantContainer);
        return extra ? At<TESObjectREFR*>(extra, layout::kMerchantContainerRef) : nullptr;
    }

    TESObjectREFR* CrosshairRef()
    {
        NVSEArrayVarInterface::Element result;
        if (!g_crosshair || !Player() || !g_scripts->CallFunction(g_crosshair, Player(), nullptr, &result, 0))
            return nullptr;
        TESForm* at = result.GetTESForm();
        return at ? LookupRef(at->refID) : nullptr;
    }

    bool Contents(TESObjectREFR* ref, std::vector<Item>& out)
    {
        out.clear();
        NVSEArrayVarInterface::Element result;
        if (!g_contents || !g_arrays || !ref || !g_scripts->CallFunction(g_contents, ref, nullptr, &result, 0))
            return false;
        auto* array = result.GetArray();
        if (!array)
            return false;
        uint32_t size = g_arrays->GetArraySize(array);
        if (size == static_cast<uint32_t>(-1) || size % 4 || size > 4 * kMaxItemKinds)
            return false;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
            return false;
        for (uint32_t i = 0; i < size; i += 4)
        {
            uint32_t id = elements[i].GetFormID();
            double count = elements[i + 1].GetNumber(), health = elements[i + 2].GetNumber(),
                mods = elements[i + 3].GetNumber();
            if (!id || count < 1)
                continue;
            Item item{id, static_cast<int32_t>(count), health >= 0 && health <= 100 ? static_cast<uint8_t>(health) :
                net::kNoCondition, mods >= 0 && mods <= 7 ? static_cast<uint8_t>(mods) : uint8_t(0)};
            auto same = std::find_if(out.begin(), out.end(), [&](const Item& o) {
                return o.id == item.id && o.health == item.health && o.mods == item.mods; });
            if (same != out.end())
                same->count += item.count;
            else
                out.push_back(item);
        }
        return out.size() <= kMaxItemKinds;
    }

    void ChangeItem(TESObjectREFR* ref, uint32_t itemId, int32_t delta)
    {
        TESForm* item = LookupForm(itemId);
        if (g_addItem && ref && item && delta)
            g_scripts->CallFunctionAlt(g_addItem, ref, 2, item, static_cast<uint32_t>(delta));
    }

    void ChangeItem(TESObjectREFR* ref, const net::ItemCount& kind)
    {
        if (kind.health == net::kNoCondition || !g_changeKind)
            return ChangeItem(ref, kind.item, kind.count);
        TESForm* item = LookupForm(kind.item);
        if (ref && item && kind.count)
            g_scripts->CallFunctionAlt(g_changeKind, ref, 4, item, static_cast<uint32_t>(kind.count),
                static_cast<uint32_t>(kind.health), static_cast<uint32_t>(kind.mods));
    }

    void Activate(TESObjectREFR* ref)
    {
        if (g_activate && ref)
            g_scripts->CallFunctionAlt(g_activate, ref, 0);
    }

    bool IsItem(uint32_t formId)
    {
        TESForm* form = LookupForm(formId);
        using IsInventoryTypeFn = bool(__cdecl*)(UInt32 formType);
        return form && reinterpret_cast<IsInventoryTypeFn>(addr::kIsInventoryObjectType)(form->typeID);
    }

    bool IsLootable(TESObjectREFR* ref)
    {
        if (!ref || !ref->baseForm)
            return false;
        return ref->baseForm->typeID == kFormType_TESObjectCONT || (IsActor(ref) && IsDead(ref));
    }

    TESObjectREFR* TopEnableParent(TESObjectREFR* ref)
    {
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        TESObjectREFR* top = nullptr;
        for (int depth = 0; ref && depth < 8; ++depth)
        {
            const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
                layout::kExtraEnableStateParent);
            TESObjectREFR* parent = extra ? At<TESObjectREFR*>(extra, layout::kEnableParentRef) : nullptr;
            if (!parent || parent == ref)
                break;
            top = ref = parent;
        }
        return top;
    }

    bool IsThere(const WorldItem& item)
    {
        return item.base && !(item.flags & (layout::kRefDeleted | layout::kRefDisabled));
    }

    Condition ItemCondition(TESObjectREFR* ref)
    {
        Condition c;
        NVSEArrayVarInterface::Element result;
        if (!g_itemCondition || !ref || !ref->baseForm || (ref->baseForm->typeID != kFormType_TESObjectWEAP &&
            ref->baseForm->typeID != kFormType_TESObjectARMO) ||
            !g_scripts->CallFunction(g_itemCondition, ref, nullptr, &result, 0))
            return c;
        int n = static_cast<int>(result.GetNumber());
        if (n >= 0 && n % 256 <= 100)
            c.health = static_cast<uint8_t>(n % 256);
        if (n >= 0 && n / 256 <= 7)
            c.mods = static_cast<uint8_t>(n / 256);
        return c;
    }

    namespace
    {
        void SetWorldWeaponMods(TESObjectREFR* ref, uint8_t mods)
        {
            if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESObjectWEAP)
                return;
            for (uint32_t slot = 0; slot < 3; ++slot)
                if (!At<const void*>(ref->baseForm, layout::kWeaponMods + 4 * slot))
                    mods &= ~(1u << slot);
            if (!mods)
                return;
            using GetByTypeFn = void*(__thiscall*)(void* list, uint8_t type);
            if (void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
                layout::kExtraWeaponModFlags))
                *(static_cast<uint8_t*>(extra) + layout::kWeaponModFlagsValue) = mods;
            else
            {
                auto* made = static_cast<uint32_t*>(reinterpret_cast<void* (__cdecl*)(size_t)>(addr::kFormHeapAllocate)(
                    layout::kExtraWeaponModFlagsSize));
                if (!made)
                    return;
                made[0] = static_cast<uint32_t>(layout::kVtblExtraWeaponModFlags);
                made[1] = layout::kExtraWeaponModFlags;
                made[2] = 0;
                made[3] = 0;
                *(reinterpret_cast<uint8_t*>(made) + layout::kWeaponModFlagsValue) = mods;
                using AddFn = void*(__thiscall*)(void* list, void* extra);
                if (!reinterpret_cast<AddFn>(addr::kExtraListAdd)(&ref->extraDataList, made))
                    return;
            }
            if (g_update3D)
                g_scripts->CallFunctionAlt(g_update3D, ref, 0);
        }
    }

    TESObjectREFR* PlaceItem(uint32_t base, int count, float x, float y, float z, uint8_t health, uint8_t mods)
    {
        TESForm* form = LookupForm(base);
        NVSEArrayVarInterface::Element result;
        if (!g_placeItem || !Player() || !form || !IsItem(base) || count < 1 ||
            !g_scripts->CallFunction(g_placeItem, Player(), nullptr, &result, 6, form, static_cast<uint32_t>(count),
                FloatArg(x), FloatArg(y), FloatArg(z), static_cast<uint32_t>(health)))
            return nullptr;
        TESForm* made = result.GetTESForm();
        TESObjectREFR* ref = made ? LookupRef(made->refID) : nullptr;
        if (ref && mods)
            SetWorldWeaponMods(ref, mods);
        return ref;
    }

    void MoveItem(TESObjectREFR* ref, float x, float y, float z, float rx, float ry, float rz)
    {
        constexpr float kDegrees = 57.2957795f;
        if (g_moveItem && ref)
            g_scripts->CallFunctionAlt(g_moveItem, ref, 6, FloatArg(x), FloatArg(y), FloatArg(z), FloatArg(rx * kDegrees),
                FloatArg(ry * kDegrees), FloatArg(rz * kDegrees));
    }

    std::vector<uint32_t> Followers()
    {
        std::vector<uint32_t> out;
        NVSEArrayVarInterface::Element result;
        if (!g_followers || !g_arrays || !Player() || !g_scripts->CallFunction(g_followers, Player(), nullptr, &result, 0))
            return out;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (size == static_cast<uint32_t>(-1) || size > 64)
            return out;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
            return out;
        for (const auto& e : elements)
            if (uint32_t id = e.GetFormID())
                out.push_back(id);
        return out;
    }

    namespace
    {
        std::vector<uint32_t> FormList(Script* script, TESObjectREFR* on)
        {
            std::vector<uint32_t> out;
            NVSEArrayVarInterface::Element result;
            if (!script || !g_arrays || !on || !g_scripts->CallFunction(script, on, nullptr, &result, 0))
                return out;
            auto* array = result.GetArray();
            uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
            if (size == static_cast<uint32_t>(-1) || size > 64)
                return out;
            std::vector<NVSEArrayVarInterface::Element> elements(size);
            if (size && !g_arrays->GetElements(array, elements.data(), nullptr))
                return out;
            for (const auto& e : elements)
                if (uint32_t id = e.GetFormID())
                    out.push_back(id);
            return out;
        }

        uint32_t FormResult(Script* script, TESObjectREFR* on)
        {
            NVSEArrayVarInterface::Element result;
            if (!script || !on || !g_scripts->CallFunction(script, on, nullptr, &result, 0))
                return 0;
            TESForm* form = result.GetTESForm();
            return form ? form->refID : 0;
        }
    }

    std::vector<uint32_t> Teammates() { return FormList(g_teammates, Player()); }

    bool IsWaiting(TESObjectREFR* ref) { return ref && Number(g_isWaiting, ref) != 0; }

    void SetWaiting(TESObjectREFR* ref, bool waiting)
    {
        if (g_setWaiting && ref)
            g_scripts->CallFunctionAlt(g_setWaiting, ref, 1, static_cast<uint32_t>(waiting ? 1 : 0));
    }

    TESForm* FollowPackage(TESObjectREFR* target)
    {
        TESForm* original = LookupForm(ids::kDefaultFollowPlayerClose);
        NVSEArrayVarInterface::Element result;
        if (!g_followPackage || !original || !target || !Player() ||
            !g_scripts->CallFunction(g_followPackage, Player(), nullptr, &result, 2, original, target))
            return nullptr;
        return result.GetTESForm();
    }

    void GivePackage(TESObjectREFR* ref, TESForm* package)
    {
        if (g_givePackage && ref && package)
            g_scripts->CallFunctionAlt(g_givePackage, ref, 1, package);
    }

    void DropPackage(TESObjectREFR* ref)
    {
        if (g_dropPackage && ref)
            g_scripts->CallFunctionAlt(g_dropPackage, ref, 0);
    }

    uint32_t CurrentPackage(TESObjectREFR* ref) { return FormResult(g_currentPackage, ref); }

    bool InCombat(TESObjectREFR* ref) { return ref && Number(g_inCombat, ref) != 0; }

    bool CompanionWheelOpen() { return Player() && Number(g_wheelOpen, Player()) != 0; }

    bool IsTeammate(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        return g_isTeammate && ref && IsActor(ref) && g_scripts->CallFunction(g_isTeammate, ref, nullptr, &result, 0) &&
            result.GetNumber() != 0;
    }

    uint32_t GrabbedItem()
    {
        NVSEArrayVarInterface::Element result;
        if (!g_grabbed || !Player() || !g_scripts->CallFunction(g_grabbed, Player(), nullptr, &result, 0))
            return 0;
        TESForm* form = result.GetTESForm();
        return form ? form->refID : 0;
    }

    void RemoveWorldItem(TESObjectREFR* ref)
    {
        if (!ref)
            return;
        if ((ref->refID >> 24) == 0xFF)
            Remove(ref);
        else if (g_disable)
            g_scripts->CallFunctionAlt(g_disable, ref, 0);
    }

    std::vector<std::string> LoadedMods()
    {
        std::vector<std::string> mods;
        auto data = *reinterpret_cast<const uint8_t**>(addr::kDataHandler);
        if (!data)
            return mods;
        uint32_t count = *reinterpret_cast<const uint32_t*>(data + layout::kDataHandlerModCount);
        auto list = reinterpret_cast<const uint8_t* const*>(data + layout::kDataHandlerMods);
        for (uint32_t i = 0; i < count && i < 0xFF; ++i)
        {
            if (!list[i])
                return {};
            const char* name = reinterpret_cast<const char*>(list[i] + layout::kModFileName);
            mods.emplace_back(name, strnlen(name, 0x104));
        }
        return mods;
    }

    std::vector<std::string> LoadOrder()
    {
        std::vector<std::string> mods;
        NVSEArrayVarInterface::Element result;
        if (!g_loadOrder || !g_arrays || !Player() ||
            !g_scripts->CallFunction(g_loadOrder, Player(), nullptr, &result, 0) || !result.GetArray())
            return mods;
        uint32_t size = g_arrays->GetArraySize(result.GetArray());
        if (size == static_cast<uint32_t>(-1) || size > 255)
            return mods;
        std::vector<NVSEArrayVarInterface::Element> elements(size);
        if (size && !g_arrays->GetElements(result.GetArray(), elements.data(), nullptr))
            return mods;
        for (const auto& e : elements)
            mods.push_back(e.GetString() ? e.GetString() : "?");
        return mods;
    }

    void RefreshItemList()
    {
        if (g_refreshItems && Player())
            g_scripts->CallFunctionAlt(g_refreshItems, Player(), 0);
    }

    DoorInfo ReadDoor(TESObjectREFR* ref)
    {
        DoorInfo info;
        bool box = ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectCONT;
        if (!ref || !ref->baseForm || (ref->baseForm->typeID != kFormType_TESObjectDOOR && !box))
            return info;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        auto getByType = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType);
        if (box)
            info.open = 3;
        else if (g_openState)
            info.open = static_cast<int>(Number(g_openState, ref));
        info.leadsAway = getByType(&ref->extraDataList, layout::kExtraTeleport) != nullptr;
        if (const void* lock = getByType(&ref->extraDataList, layout::kExtraLock))
        {
            auto* data = *reinterpret_cast<const uint8_t* const*>(static_cast<const char*>(lock) + layout::kLockData);
            if (data)
            {
                info.lockLevel = data[layout::kLockLevel];
                info.locked = (data[layout::kLockFlags] & layout::kLockLocked) != 0;
            }
        }
        return info;
    }

    void SetDoorOpen(TESObjectREFR* ref, bool open)
    {
        if (g_setOpen && ref)
            g_scripts->CallFunctionAlt(g_setOpen, ref, 1, static_cast<uint32_t>(open ? 1 : 0));
    }

    uint8_t PileKind(TESObjectREFR* actor)
    {
        if (!g_pileKind || !actor || !IsActor(actor))
            return 0;
        int kind = static_cast<int>(Number(g_pileKind, actor));
        return kind >= net::kPileGoo && kind <= net::kPileLast ? static_cast<uint8_t>(kind) : 0;
    }

    void MakePile(TESObjectREFR* actor, uint8_t kind)
    {
        if (g_compiled && g_makePile && actor && IsActor(actor) && kind >= net::kPileGoo && kind <= net::kPileLast)
            g_scripts->CallFunctionAlt(g_makePile, actor, 1, static_cast<uint32_t>(kind));
    }

    int GameSaysLocked(TESObjectREFR* ref)
    {
        return g_isLocked && ref ? static_cast<int>(Number(g_isLocked, ref)) : -1;
    }

    void SetDoorLocked(TESObjectREFR* ref, bool locked, int level)
    {
        if (g_lock && ref)
            g_scripts->CallFunctionAlt(g_lock, ref, 2, static_cast<uint32_t>(locked ? 1 : 0),
                static_cast<uint32_t>(level));
    }

    bool IsActor(const TESObjectREFR* ref)
    {
        return ref && (ref->typeID == kFormType_Character || ref->typeID == kFormType_Creature);
    }

    void NearbyActors(std::vector<TESObjectREFR*>& out)
    {
        out.clear();
        auto* manager = reinterpret_cast<const uint8_t*>(addr::kProcessManager);
        auto* objects = *reinterpret_cast<TESObjectREFR* const* const*>(manager + process::kManagerObjects);
        uint32_t capacity = *reinterpret_cast<const uint16_t*>(manager + process::kManagerCapacity);
        uint32_t used = *reinterpret_cast<const uint16_t*>(manager + process::kManagerObjectCount);
        uint32_t begin = *reinterpret_cast<const uint32_t*>(manager + process::kManagerBegin);
        uint32_t end = *reinterpret_cast<const uint32_t*>(manager + process::kManagerEnd);
        static uint32_t s_nextWarn = 0;
        if ((begin > end || end > used) && coop::IsDue(GetTickCount(), s_nextWarn))
        {
            s_nextWarn = GetTickCount() + 10000;
            LOG_WARN("Nearby actors list looks odd: slots %u to %u, %u in use, room for %u", begin, end, used,
                capacity);
        }
        if (!objects || begin > end || end > capacity)
            return;
        for (uint32_t i = begin; i < end; ++i)
            if (IsActor(objects[i]))
                out.push_back(objects[i]);
    }

    const char* Name(const TESObjectREFR* ref)
    {
        if (!IsActor(ref) || !ref->baseForm ||
            (ref->baseForm->typeID != kFormType_TESNPC && ref->baseForm->typeID != kFormType_TESCreature))
            return "";
        const char* name = static_cast<const TESActorBase*>(ref->baseForm)->fullName.name.m_data;
        return name ? name : "";
    }

    BaseInfo DescribeBase(TESObjectREFR* ref)
    {
        BaseInfo info;
        if (!ref || !ref->baseForm)
            return info;
        info.base = ref->baseForm->refID;
        info.baseType = ref->baseForm->typeID;
        if (!IsActor(ref) || (info.base >> 24) != 0xFF)
            return info;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        const void* extra = reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList,
            layout::kExtraLeveledCreature);
        if (!extra)
            return info;
        auto form = [&](uint32_t offset)
        {
            return *reinterpret_cast<TESForm* const*>(static_cast<const char*>(extra) + offset);
        };
        if (TESForm* f = form(layout::kLeveledBaseForm))
        {
            info.leveledBase = f->refID;
            info.leveledBaseType = f->typeID;
        }
        if (TESForm* f = form(layout::kLeveledActorBase))
        {
            info.leveledActor = f->refID;
            info.leveledActorType = f->typeID;
        }
        return info;
    }

    float Health(TESObjectREFR* ref) { return Number(g_health, ref); }
    int PlayerLevel() { return g_level ? static_cast<int>(Number(g_level, Player())) : 0; }
    float MaxHealth(TESObjectREFR* ref) { return Number(g_maxHealth, ref); }

    float PlayerMedicine() { return g_medicine ? Number(g_medicine, Player()) : 0; }

    bool CastOnSelf(TESObjectREFR* ref, uint32_t formId)
    {
        TESForm* form = LookupForm(formId);
        if (!g_pcCast || !ref || !form)
            return false;
        g_scripts->CallFunctionAlt(g_pcCast, ref, 1, form);
        return true;
    }

    void ChangeHealth(TESObjectREFR* ref, float amount)
    {
        if (g_compiled && ref && amount != 0)
            g_scripts->CallFunctionAlt(g_changeHealth, ref, 1, FloatArg(amount));
    }

    void StopFighting(TESObjectREFR* ref)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_stopFighting, ref, 0);
    }

    namespace
    {
        bool SetBaseEssential(TESForm* form, bool on)
        {
            if (!form || form->typeID != kFormType_TESNPC)
                return false;
            UInt32& flags = static_cast<TESActorBase*>(form)->baseData.flags;
            if (((flags & layout::kActorBaseEssential) != 0) == on)
                return false;
            flags = on ? flags | layout::kActorBaseEssential : flags & ~layout::kActorBaseEssential;
            return true;
        }
    }

    void SetPlayerEssential(bool on)
    {
        if (SetBaseEssential(LookupForm(ids::kPlayerBase), on))
            LOG_INFO("Player %s", on ? "can be downed (essential)" : "is no longer essential");
    }

    void SetEssential(TESObjectREFR* ref, bool on)
    {
        if (ref && SetBaseEssential(ref->baseForm, on))
            LOG_DEBUG("Base %08X %s essential", ref->baseForm->refID, on ? "made" : "no longer");
    }

    void Kill(TESObjectREFR* ref, uint16_t limbs)
    {
        if (!g_compiled || !ref)
            return;
        if (!limbs)
            g_scripts->CallFunctionAlt(g_kill, ref, 1, static_cast<uint32_t>(-1));
        for (uint32_t i = 0; i < 15; ++i)
            if (limbs & (1u << i))
                g_scripts->CallFunctionAlt(g_kill, ref, 1, i);
    }

    void Unprotect(TESObjectREFR* ref)
    {
        if (g_compiled && g_unprotect && ref)
            g_scripts->CallFunctionAlt(g_unprotect, ref, 0);
    }

    void KillPlain(TESObjectREFR* ref)
    {
        if (g_compiled && g_killPlain && ref)
            g_scripts->CallFunctionAlt(g_killPlain, ref, 0);
    }

    bool AiThreadsRunning() { return *reinterpret_cast<const volatile uint8_t*>(addr::kAiThreadsRunning) != 0; }

    uint16_t LimbsGone(TESObjectREFR* ref)
    {
        float mask = ref ? Number(g_limbsGone, ref) : 0;
        return mask > 0 && mask < 0x8000 ? static_cast<uint16_t>(mask) : 0;
    }

    void SetMaxHealth(TESObjectREFR* ref, float health)
    {
        if (g_compiled && g_setMaxHealth && ref && health > 0)
            g_scripts->CallFunctionAlt(g_setMaxHealth, ref, 1, FloatArg(health));
    }

    bool IsGeneric(TESObjectREFR* actor)
    {
        BaseInfo b = DescribeBase(actor);
        return net::IsMadeWhilePlaying(b.base) && b.leveledBase && !net::IsMadeWhilePlaying(b.leveledBase) &&
            (b.leveledBaseType == kFormType_TESNPC || b.leveledBaseType == kFormType_TESCreature ||
             b.leveledBaseType == kFormType_TESLevCharacter || b.leveledBaseType == kFormType_TESLevCreature);
    }

    int ThinAmmo(TESObjectREFR* body, int keptPercent)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_thinAmmo || !body ||
            !g_scripts->CallFunction(g_thinAmmo, body, nullptr, &result, 1, static_cast<uint32_t>(keptPercent)))
            return 0;
        return static_cast<int>(result.GetNumber());
    }

    TESObjectREFR* PlaceExtraEnemy(TESObjectREFR* like, TESObjectREFR* target)
    {
        BaseInfo b = DescribeBase(like);
        TESForm* base = b.leveledBase ? LookupForm(b.leveledBase) : nullptr;
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_placeExtra || !base || !target || !IsGeneric(like) ||
            !g_scripts->CallFunction(g_placeExtra, like, nullptr, &result, 2, base, target))
            return nullptr;
        TESForm* made = result.GetTESForm();
        return made ? LookupRef(made->refID) : nullptr;
    }

    void ForceTarget(TESObjectREFR* actor, TESObjectREFR* target)
    {
        if (g_compiled && g_forceTarget && actor)
            g_scripts->CallFunctionAlt(g_forceTarget, actor, 1, target);
    }

    uint32_t CombatTarget(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_combatTarget || !ref || !g_scripts->CallFunction(g_combatTarget, ref, nullptr, &result, 0))
            return 0;
        TESForm* target = result.GetTESForm();
        return target ? target->refID : 0;
    }

    uint32_t PlayedIdle(TESObjectREFR* ref)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_playedIdle || !ref || !g_scripts->CallFunction(g_playedIdle, ref, nullptr, &result, 0))
            return 0;
        TESForm* idle = result.GetTESForm();
        return idle ? idle->refID : 0;
    }

    bool PlayIdle(TESObjectREFR* ref, uint32_t idleId)
    {
        TESForm* idle = LookupForm(idleId);
        if (!g_compiled || !g_playIdle || !ref || !idle || idle->typeID != kFormType_TESIdleForm)
            return false;
        if (*reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(idle) + layout::kIdleChildren))
        {
            static std::set<uint32_t> s_skipped;
            if (s_skipped.size() < 256 && s_skipped.insert(idleId).second)
                LOG_INFO("Idles: not playing %08X on copies (it has a list of children, which crashed JIP's PlayIdleEx)",
                    idleId);
            return false;
        }
        g_scripts->CallFunctionAlt(g_playIdle, ref, 1, idle);
        return true;
    }

    void KillPlayer()
    {
        SetPlayerEssential(false);
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_kill, Player(), 1, static_cast<uint32_t>(-1));
    }

    void KnockOut(TESObjectREFR* ref)
    {
        ChangeHealth(ref, -(Health(ref) + 10));
    }

    void SetGhost(TESObjectREFR* ref, bool on)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_ghost, ref, 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    bool IsGhost(TESObjectREFR* ref)
    {
        if (!IsActor(ref))
            return false;
        BaseProcess* process = static_cast<Actor*>(ref)->baseProcess;
        if (const void* cached = process ? At<const void*>(process, process::kCachedValues) : nullptr)
            return (At<uint32_t>(cached, process::kCachedFlags) & 0x10000000) != 0;
        using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
        return reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(&ref->extraDataList, layout::kExtraGhost) !=
            nullptr;
    }

    void SetUnconscious(TESObjectREFR* ref, bool out)
    {
        if (g_compiled && ref)
            g_scripts->CallFunctionAlt(g_unconscious, ref, 1, static_cast<uint32_t>(out ? 1 : 0));
    }

    void WakeUp(TESObjectREFR* ref)
    {
        if (!IsKnockedOut(ref))
            return;
        SetUnconscious(ref, false);
        if (IsKnockedOut(ref))
        {
            LOG_DEBUG("SetUnconscious didn't wake them (life state %d); setting it to alive", LifeState(ref));
            static_cast<Actor*>(ref)->lifeState = 0;
        }
    }

    void SetPlayerControls(bool on)
    {
        if (g_compiled && Player())
            g_scripts->CallFunctionAlt(g_controls, Player(), 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    int LifeState(const TESObjectREFR* ref)
    {
        return IsActor(ref) ? static_cast<int>(static_cast<const Actor*>(ref)->lifeState) : -1;
    }

    bool IsDead(const TESObjectREFR* ref)
    {
        int state = LifeState(ref);
        return state == static_cast<int>(layout::kLifeStateDying) || state == static_cast<int>(layout::kLifeStateDead);
    }

    bool IsKnockedOut(const TESObjectREFR* ref)
    {
        int state = LifeState(ref);
        return state == static_cast<int>(layout::kLifeStateUnconscious) ||
            state == static_cast<int>(layout::kLifeStateEssentialDown);
    }

    bool InLoadingScreen()
    {
        return g_loadingScreen && Player() && Number(g_loadingScreen, Player()) != 0;
    }

    bool InMenu()
    {
        return Number(g_menuMode, Player()) != 0;
    }

    bool IsKeyPressed(int key)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_keyPressed || !Player() ||
            !g_scripts->CallFunction(g_keyPressed, Player(), nullptr, &result, 1, static_cast<uint32_t>(key)))
            return false;
        return result.GetNumber() != 0;
    }

    namespace
    {
        std::mutex g_chatMutex;
        coop::ChatDraft g_chatDraft;
        bool g_chatTyping = false;
        bool g_chatChanged = false;
        bool g_chatTalking = false;
        std::vector<std::string> g_chatSent;
        HWND g_gameWindow = nullptr;
        WNDPROC g_gameWndProc = nullptr;
        std::vector<int> g_chatKeysOff;
        bool g_entryTyping = false;
        coop::ChatDraft g_entryDraft;
        EntryState g_entryState = EntryState::None;
        bool g_chatHudMade = false;
        bool g_chatShown = false;
        int g_chatScroll = 0;

        LRESULT CALLBACK ChatWndProc(HWND window, UINT message, WPARAM w, LPARAM l)
        {
            if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSCHAR)
            {
                std::lock_guard<std::mutex> lock(g_chatMutex);
                if (g_entryTyping)
                {
                    if (message == WM_CHAR)
                    {
                        coop::ChatDraft::Done done = g_entryDraft.Type(static_cast<unsigned char>(w));
                        if (done != coop::ChatDraft::Done::No)
                        {
                            g_entryTyping = false;
                            g_entryState = done == coop::ChatDraft::Done::Send ? EntryState::Done : EntryState::Cancelled;
                        }
                    }
                    return 0;
                }
                if (g_chatTyping)
                {
                    if (message == WM_KEYDOWN)
                    {
                        constexpr int kPage = 10;
                        if (w == VK_PRIOR)
                            g_chatScroll += kPage;
                        else if (w == VK_NEXT)
                            g_chatScroll -= kPage;
                        else if (w == VK_UP)
                            ++g_chatScroll;
                        else if (w == VK_DOWN)
                            --g_chatScroll;
                    }
                    if (message == WM_CHAR)
                    {
                        coop::ChatDraft::Done done = g_chatDraft.Type(static_cast<unsigned char>(w));
                        if (done == coop::ChatDraft::Done::Send)
                        {
                            std::string text = g_chatDraft.Take();
                            if (!text.empty() && g_chatSent.size() < 8)
                                g_chatSent.push_back(std::move(text));
                        }
                        if (done != coop::ChatDraft::Done::No)
                            g_chatTyping = false;
                        g_chatChanged = true;
                    }
                    return 0;
                }
            }
            return CallWindowProcA(g_gameWndProc, window, message, w, l);
        }

        BOOL CALLBACK FindGameWindow(HWND window, LPARAM)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
                return TRUE;
            g_gameWindow = window;
            return FALSE;
        }

        bool HookGameWindow()
        {
            if (g_gameWndProc)
                return true;
            EnumWindows(FindGameWindow, 0);
            if (!g_gameWindow)
                return false;
            g_gameWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChatWndProc)));
            if (g_gameWndProc)
                LOG_INFO("Chat: typing goes through the game's window");
            return g_gameWndProc != nullptr;
        }

        void MakeChatHud()
        {
            NVSEArrayVarInterface::Element result;
            if (g_chatHudMade || !g_chatHud || !Player())
                return;
            static const char kLine[] =
                "<rect name=\"NVCoopChat\"><visible>0</visible>"
                "<text name=\"NVCoopChatText\">"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div></x><y>600</y>"
                "<font>1</font><justify>&center;</justify><wrapwidth>1100</wrapwidth>"
                "<systemcolor>1</systemcolor><string> </string></text></rect>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kLine);
            for (int slot = 0; slot < kChatAboveSlots; ++slot)
            {
                std::string xml = "<text name=\"NVCoopAbove" + std::to_string(slot) + "\"><visible>0</visible>"
                    "<user0>0.5</user0><user1>0.5</user1>"
                    "<x><copy src=\"screen()\" trait=\"width\"/><mul src=\"me()\" trait=\"user0\"/></x>"
                    "<y><copy src=\"screen()\" trait=\"height\"/><mul src=\"me()\" trait=\"user1\"/><sub>30</sub></y>"
                    "<font>1</font><justify>&center;</justify><wrapwidth>600</wrapwidth>"
                    "<systemcolor>1</systemcolor><string> </string></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            static const char kRevive[] =
                "<text name=\"NVCoopRevive\"><visible>0</visible>"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div></x>"
                "<y><copy src=\"screen()\" trait=\"height\"/><mul>0.58</mul></y>"
                "<font>2</font><justify>&center;</justify><wrapwidth>900</wrapwidth>"
                "<systemcolor>1</systemcolor><string> </string></text>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kRevive);
            for (int slot = 0; slot < kTalkSlots; ++slot)
            {
                std::string n = std::to_string(slot);
                std::string text = "<locus>1</locus><y>24</y><alpha>0</alpha><font>3</font><justify>&left;</justify>"
                    "<wrapwidth>600</wrapwidth><brightness>-1</brightness><string> </string>";
                std::string xml = "<image name=\"NVCoopTalkBar" + n + "\"><visible>0</visible>"
                    "<filename>interface\\shared\\solid_black.dds</filename>"
                    "<x>22</x><y>24</y><depth>1</depth><width>616</width><height>20</height><zoom>-1</zoom>"
                    "<alpha>0</alpha></image>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                for (int e = 0; e < kTalkEdges; ++e)
                {
                    xml = "<text name=\"NVCoopTalkEdge" + n + "x" + std::to_string(e) + "\"><visible>0</visible>"
                        "<x>30</x><depth>2</depth>" + text + "<red>0</red><green>0</green><blue>0</blue></text>";
                    g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                }
                xml = "<text name=\"NVCoopTalk" + n + "\"><visible>0</visible><x>30</x><depth>3</depth>" + text +
                    "<systemcolor>1</systemcolor><red>255</red><green>255</green><blue>255</blue></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
                xml = "<image name=\"NVCoopTalkLine" + n + "\"><visible>0</visible>"
                    "<filename>interface\\shared\\line\\glow_tip_line_horizontal.dds</filename>"
                    "<x>30</x><y>24</y><depth>4</depth><width>740</width><height>8</height><zoom>-1</zoom><alpha>0</alpha>"
                    "<brightness>-1</brightness><red>255</red><green>255</green><blue>255</blue></image>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            for (int slot = 0; slot < kMarkSlots; ++slot)
            {
                std::string xml = "<text name=\"NVCoopMark" + std::to_string(slot) + "\"><visible>0</visible>"
                    "<user0>0.5</user0><user1>0.5</user1>"
                    "<x><copy src=\"screen()\" trait=\"width\"/><mul src=\"me()\" trait=\"user0\"/></x>"
                    "<y><copy src=\"screen()\" trait=\"height\"/><mul src=\"me()\" trait=\"user1\"/><sub>75</sub></y>"
                    "<font>2</font><justify>&center;</justify><wrapwidth>400</wrapwidth>"
                    "<systemcolor>1</systemcolor><string> </string></text>";
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            }
            static const char kFeedBar[] =
                "<image name=\"NVCoopFeedBar\"><visible>0</visible>"
                "<filename>interface\\shared\\solid_black.dds</filename>"
                "<x>0</x><y>0</y><depth>1</depth><width>516</width><height>20</height><zoom>-1</zoom>"
                "<alpha>0</alpha></image>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kFeedBar);
            static const char kFeedText[] =
                "<text name=\"NVCoopFeedText\"><visible>0</visible><locus>1</locus><x>0</x><y>0</y><depth>3</depth>"
                "<alpha>0</alpha><font>3</font><justify>4</justify><wrapwidth>769</wrapwidth>"
                "<systemcolor>1</systemcolor><string> </string></text>";
            g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, kFeedText);
            g_chatHudMade = true;
        }

        struct ChatAbove
        {
            uint32_t who = 0;
            std::string text;
            uint32_t until = 0;
            bool changed = false;
            bool shown = false;
        };
        ChatAbove g_above[kChatAboveSlots];

        void ShowChatLine(const std::string& text, bool show)
        {
            NVSEArrayVarInterface::Element result;
            MakeChatHud();
            if (g_chatShow && Player())
                g_scripts->CallFunction(g_chatShow, Player(), nullptr, &result, 2, text.empty() ? " " : text.c_str(),
                    static_cast<uint32_t>(show ? 1 : 0));
        }

        void KeepKeysFromGame(bool keep)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_keyDisabled || !g_disableKey || !Player())
                return;
            if (keep)
            {
                for (int key = 1; key < 264; ++key)
                    if (g_scripts->CallFunction(g_keyDisabled, Player(), nullptr, &result, 1, static_cast<uint32_t>(key)) &&
                        result.GetNumber() == 0)
                    {
                        g_scripts->CallFunction(g_disableKey, Player(), nullptr, nullptr, 2, static_cast<uint32_t>(key), 1u);
                        g_chatKeysOff.push_back(key);
                    }
            }
            else
            {
                for (int key : g_chatKeysOff)
                    g_scripts->CallFunction(g_disableKey, Player(), nullptr, nullptr, 2, static_cast<uint32_t>(key), 0u);
                g_chatKeysOff.clear();
            }
        }
    }

    bool StartTextEntry()
    {
        if (!HookGameWindow())
            return false;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            if (g_chatTyping)
                return false;
            g_entryDraft = coop::ChatDraft();
            g_entryTyping = true;
            g_entryState = EntryState::Typing;
        }
        if (g_chatKeysOff.empty())
            KeepKeysFromGame(true);
        return true;
    }

    void StopTextEntry()
    {
        bool chat;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            g_entryTyping = false;
            g_entryDraft = coop::ChatDraft();
            g_entryState = EntryState::None;
            chat = g_chatTyping;
        }
        if (!chat && !g_chatKeysOff.empty())
            KeepKeysFromGame(false);
    }

    EntryState TextEntryState()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_entryState;
    }

    size_t TextEntryLength()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_entryDraft.Text().size();
    }

    std::string TakeTextEntry()
    {
        std::string text;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            if (g_entryState == EntryState::Done)
                text = g_entryDraft.Take();
        }
        StopTextEntry();
        return text;
    }

    void TypeIntoEntry(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (!g_entryTyping)
            return;
        for (char c : text)
            g_entryDraft.Type(static_cast<unsigned char>(c));
        g_entryTyping = false;
        g_entryState = EntryState::Done;
    }

    bool OpenChat()
    {
        if (!HookGameWindow())
            return false;
        std::lock_guard<std::mutex> lock(g_chatMutex);
        g_chatTyping = true;
        g_chatChanged = true;
        return true;
    }

    bool SetMouth(TESObjectREFR* actor, float opening)
    {
        if (!IsActor(actor))
            return false;
        auto* face = reinterpret_cast<uint8_t*(__thiscall*)(TESObjectREFR*)>(addr::kActorFaceAnimData)(actor);
        if (!face)
            return false;
        bool set = false;
        for (int group : {0x4C, 0xC8, 0xDC})
        {
            uint8_t* keyframes = face + group;
            auto* values = *reinterpret_cast<float**>(keyframes + 0x0C);
            uint32_t count = *reinterpret_cast<uint32_t*>(keyframes + 0x10);
            if (!values || count != 16)
                continue;
            values[1] = opening;
            set = true;
        }
        return set;
    }

    int QuestStage(uint32_t quest)
    {
        NVSEArrayVarInterface::Element result;
        TESForm* form = LookupForm(quest);
        if (!g_questStage || !form || !Player() ||
            !g_scripts->CallFunction(g_questStage, Player(), nullptr, &result, 1, form))
            return -1;
        return static_cast<int>(result.GetNumber());
    }

    void HoldTutorial(bool hold)
    {
        if (g_tutorialTimer && Player())
            g_scripts->CallFunctionAlt(g_tutorialTimer, Player(), 1, hold ? 0u : 1u);
    }

    bool LeaveTutorial()
    {
        using namespace ids;
        TESForm* forms[] = {LookupForm(kTutorialQuest), LookupForm(kSunnyQuest), LookupForm(kDocsDoor),
            LookupForm(kPipBoy), LookupForm(kPipBoyGlove), LookupForm(kVaultSuit21), LookupForm(kCourierNote)};
        for (TESForm* f : forms)
            if (!f)
                return false;
        if (!g_leaveTutorial || !Player())
            return false;
        g_scripts->CallFunction(g_leaveTutorial, Player(), nullptr, nullptr, 7, forms[0], forms[1], forms[2], forms[3],
            forms[4], forms[5], forms[6]);
        RemoveTutorialFades();
        return true;
    }

    void RemoveTutorialFades()
    {
        for (uint32_t fade : ids::kTutorialFades)
        {
            char line[64];
            std::snprintf(line, sizeof(line), "RemoveImageSpaceModifier %08X", fade);
            RunConsoleLine(line);
        }
    }

    bool ActivateByPlayer(uint32_t ref, bool moveHereFirst)
    {
        TESForm* what = LookupForm(ref);
        if (!what || !g_activateBy || !Player())
            return false;
        g_scripts->CallFunction(g_activateBy, Player(), nullptr, nullptr, 2, what, moveHereFirst ? 1u : 0u);
        return true;
    }

    bool GiveStarterGear()
    {
        using namespace ids;
        uint32_t ids[] = {kStimpak, kCaps, kLockpick, k9mmPistol, k9mmAmmo, kLaserPistol, kEnergyCells, kDynamite,
            kStraightRazor, kBoxingGloves};
        TESForm* f[10];
        for (int i = 0; i < 10; ++i)
            if (!(f[i] = LookupForm(ids[i])))
                return false;
        if (!g_starterGear || !Player())
            return false;
        g_scripts->CallFunction(g_starterGear, Player(), nullptr, nullptr, 10, f[0], f[1], f[2], f[3], f[4], f[5], f[6],
            f[7], f[8], f[9]);
        return true;
    }

    namespace
    {
        struct Tuned { const char* name; float original = 0; bool read = false; };
        Tuned g_tuned[] = {{"fDiffMultHPToPCVE"}, {"fDiffMultHPToPCE"}, {"fDiffMultHPToPCN"}, {"fDiffMultHPToPCH"},
            {"fDiffMultHPToPCVH"}, {"fDiffMultHPByPCVE"}, {"fDiffMultHPByPCE"}, {"fDiffMultHPByPCN"}, {"fDiffMultHPByPCH"},
            {"fDiffMultHPByPCVH"}, {"fAVDCarryWeightsBase"},
            {"iXPRewardKillOpponentVeryEasy"}, {"iXPRewardKillOpponentEasy"}, {"iXPRewardKillOpponentAverage"},
            {"iXPRewardKillOpponentHard"}, {"iXPRewardKillOpponentVeryHard"}, {"iXPRewardKillNPCVeryEasy"},
            {"iXPRewardKillNPCEasy"}, {"iXPRewardKillNPCAverage"}, {"iXPRewardKillNPCHard"}, {"iXPRewardKillNPCVeryHard"}};
        struct TunedEffect { uint32_t item; int magnitude = -1, duration = -1; };
        TunedEffect g_tunedEffects[] = {{ids::kStimpak}, {ids::kSuperStimpak}};

        struct PartMult { TESForm* list; int part; float original; };
        std::vector<PartMult> g_partMults;
        bool g_partsRead = false;

        float BodyPartMult(TESForm* list, int part, float value)
        {
            NVSEArrayVarInterface::Element result;
            if (!g_bodyPartMult || !Player() || !g_scripts->CallFunction(g_bodyPartMult, Player(), nullptr, &result, 3,
                    list, static_cast<uint32_t>(part), *reinterpret_cast<uint32_t*>(&value)))
                return -1;
            return static_cast<float>(result.GetNumber());
        }

        void ReadBodyParts()
        {
            g_partsRead = true;
            NVSEArrayVarInterface::Element result;
            if (!g_bodyPartLists || !g_arrays || !Player() ||
                !g_scripts->CallFunction(g_bodyPartLists, Player(), nullptr, &result, 0) || !result.GetArray())
                return;
            uint32_t size = g_arrays->GetArraySize(result.GetArray());
            if (size == static_cast<uint32_t>(-1) || size > 4096)
                return;
            std::vector<NVSEArrayVarInterface::Element> lists(size);
            if (size && !g_arrays->GetElements(result.GetArray(), lists.data(), nullptr))
                return;
            for (const auto& e : lists)
                if (TESForm* list = e.GetTESForm())
                    for (int part = 1; part <= 12; ++part)
                    {
                        float mult = BodyPartMult(list, part, -1);
                        if (mult > 0)
                            g_partMults.push_back({list, part, mult});
                    }
            LOG_INFO("Balance: %u body part lists, %zu parts' damage read", size, g_partMults.size());
        }

        std::vector<TESForm*> LoadedOfType(uint8_t type)
        {
            std::vector<TESForm*> out;
            NVSEArrayVarInterface::Element result;
            if (!g_loadedOfType || !g_arrays || !Player() || !g_scripts->CallFunction(g_loadedOfType, Player(), nullptr,
                    &result, 1, static_cast<uint32_t>(type)) || !result.GetArray())
                return out;
            uint32_t size = g_arrays->GetArraySize(result.GetArray());
            if (size == static_cast<uint32_t>(-1) || size > 200000)
                return out;
            std::vector<NVSEArrayVarInterface::Element> forms(size);
            if (size && !g_arrays->GetElements(result.GetArray(), forms.data(), nullptr))
                return out;
            for (const auto& e : forms)
                if (TESForm* form = e.GetTESForm(); form && form->typeID == type)
                    out.push_back(form);
            return out;
        }

        struct ArmorDR { TESForm* armor; uint16_t original; bool heavy; };
        std::vector<ArmorDR> g_armors;
        struct SupplyList { TESForm* list; uint8_t original; };
        std::vector<SupplyList> g_supplyLists;
        std::vector<SupplyList> g_boxLists;
        struct AmmoValue { TESForm* ammo; uint32_t original; };
        std::vector<AmmoValue> g_ammoValues;
        struct AmmoCount { uint8_t* count; int32_t original; bool wide; };
        std::vector<AmmoCount> g_ammoCounts;
        bool g_armorAndLootRead = false;
        struct StyleWas { TESForm* style; float coverRadius, takeCover; uint8_t sideStep; uint16_t flags; };
        std::vector<StyleWas> g_styles;

        void ReadArmorAndLoot()
        {
            g_armorAndLootRead = true;
            int light = 0, checked = 0, differ = 0;
            for (TESForm* armor : LoadedOfType(layout::kFormTypeArmor))
            {
                uint32_t flags = At<uint32_t>(armor, layout::kArmorBipedFlags);
                uint16_t dr = At<uint16_t>(armor, layout::kArmorRating);
                if (checked < 20 && g_armorDR)
                {
                    NVSEArrayVarInterface::Element result;
                    if (g_scripts->CallFunction(g_armorDR, Player(), nullptr, &result, 1, armor))
                    {
                        ++checked;
                        differ += static_cast<int>(result.GetNumber()) != dr;
                    }
                }
                if (flags & (layout::kArmorMedium | layout::kArmorHeavy))
                    g_armors.push_back({armor, dr, (flags & layout::kArmorHeavy) != 0});
                else
                    ++light;
            }
            int food = 0, medicine = 0;
            for (TESForm* style : LoadedOfType(layout::kFormTypeCombatStyle))
                g_styles.push_back({style, At<float>(style, layout::kStyleCoverRadius),
                    At<float>(style, layout::kStyleTakeCover), At<uint8_t>(style, layout::kStyleSideStep),
                    At<uint16_t>(style, layout::kStyleFlags)});
            auto forEachNode = [](const uint8_t* node, auto&& use) {
                for (int n = 0; node && n < 1000; node = *reinterpret_cast<const uint8_t* const*>(node + 4), ++n)
                {
                    const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(node);
                    if (!data)
                        break;
                    use(data);
                }
            };
            std::set<const TESForm*> boxLists, kitLists;
            std::function<void(const TESForm*, std::set<const TESForm*>&)> follow =
                [&](const TESForm* list, std::set<const TESForm*>& into) {
                    if (!list || list->typeID != layout::kFormTypeLevItem || !into.insert(list).second)
                        return;
                    forEachNode(reinterpret_cast<const uint8_t*>(list) + layout::kLevListEntries, [&](const uint8_t* e) {
                        follow(*reinterpret_cast<const TESForm* const*>(e), into);
                    });
                };
            auto itemsOf = [&](uint8_t type, uint32_t offset, std::set<const TESForm*>& into) {
                for (TESForm* holder : LoadedOfType(type))
                    forEachNode(reinterpret_cast<const uint8_t*>(holder) + offset, [&](const uint8_t* e) {
                        follow(*reinterpret_cast<const TESForm* const*>(e + 4), into);
                    });
            };
            itemsOf(layout::kFormTypeContainer, layout::kContainerItemsOfBox, boxLists);
            itemsOf(layout::kFormTypeNpc, layout::kContainerItemsOfActor, kitLists);
            itemsOf(layout::kFormTypeCreature, layout::kContainerItemsOfActor, kitLists);
            for (TESForm* list : LoadedOfType(layout::kFormTypeLevItem))
            {
                if (boxLists.count(list) && !kitLists.count(list))
                {
                    g_boxLists.push_back({list, At<uint8_t>(list, layout::kLevListChanceNone)});
                    continue;
                }
                bool supplies = false, other = false;
                int entries = 0;
                for (auto* node = reinterpret_cast<const uint8_t*>(list) + layout::kLevListEntries; node && entries < 1000;
                     node = *reinterpret_cast<const uint8_t* const*>(node + 4), ++entries)
                {
                    const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(node);
                    if (!entry)
                        break;
                    const TESForm* item = *reinterpret_cast<const TESForm* const*>(entry);
                    uint8_t kind = item && item->typeID == layout::kFormTypeAlchemy ?
                        At<uint8_t>(item, layout::kAlchFlags) & (layout::kAlchFood | layout::kAlchMedicine) : 0;
                    food += (kind & layout::kAlchFood) != 0;
                    medicine += (kind & layout::kAlchMedicine) != 0;
                    (kind ? supplies : other) = true;
                }
                if (supplies && !other)
                    g_supplyLists.push_back({list, At<uint8_t>(list, layout::kLevListChanceNone)});
            }
            int ammoChecked = 0, ammoDiffer = 0;
            for (TESForm* ammo : LoadedOfType(layout::kFormTypeAmmo))
            {
                uint32_t value = At<uint32_t>(ammo, layout::kAmmoValue);
                if (ammoChecked < 20 && g_itemValue)
                {
                    NVSEArrayVarInterface::Element result;
                    if (g_scripts->CallFunction(g_itemValue, Player(), nullptr, &result, 1, ammo))
                    {
                        ++ammoChecked;
                        ammoDiffer += static_cast<uint32_t>(result.GetNumber()) != value;
                    }
                }
                g_ammoValues.push_back({ammo, value});
            }
            auto isAmmo = [](const TESForm* form) { return form && form->typeID == layout::kFormTypeAmmo; };
            for (const SupplyList& l : g_boxLists)
            {
                if (At<uint32_t>(l.list, layout::kLevListGlobal))
                    continue;
                forEachNode(reinterpret_cast<const uint8_t*>(l.list) + layout::kLevListEntries, [&](const uint8_t* e) {
                    if (isAmmo(*reinterpret_cast<const TESForm* const*>(e)))
                        g_ammoCounts.push_back({const_cast<uint8_t*>(e) + layout::kLevEntryCount,
                            *reinterpret_cast<const int16_t*>(e + layout::kLevEntryCount), false});
                });
            }
            size_t ammoInLists = g_ammoCounts.size();
            for (TESForm* box : LoadedOfType(layout::kFormTypeContainer))
                forEachNode(reinterpret_cast<const uint8_t*>(box) + layout::kContainerItemsOfBox, [&](const uint8_t* e) {
                    if (isAmmo(*reinterpret_cast<const TESForm* const*>(e + 4)))
                        g_ammoCounts.push_back({const_cast<uint8_t*>(e), *reinterpret_cast<const int32_t*>(e), true});
                });
            LOG_INFO("Balance: %zu kinds of ammo (value read as xNVSE does on %d of %d); %zu ammo entries in lists only "
                "boxes and vendors use, %zu in boxes' own items", g_ammoValues.size(), ammoChecked - ammoDiffer, ammoChecked,
                ammoInLists, g_ammoCounts.size() - ammoInLists);
            LOG_INFO("Balance: %zu medium and heavy armors (%d light; DR read as xNVSE does on %d of %d), %zu loot lists "
                "of only food, drink and medicine (%d food and %d medicine entries in loot lists), %zu only boxes and "
                "vendors use (of %zu they use; %zu kit out characters), %zu combat styles", g_armors.size(), light,
                checked - differ, checked, g_supplyLists.size(), food, medicine, g_boxLists.size(), boxLists.size(),
                kitLists.size(), g_styles.size());
        }

        int EffectTrait(uint32_t item, int trait, int value)
        {
            NVSEArrayVarInterface::Element result;
            TESForm* form = LookupForm(item);
            if (!g_effectTrait || !form || !Player() ||
                !g_scripts->CallFunction(g_effectTrait, Player(), nullptr, &result, 4, form, 0u,
                    static_cast<uint32_t>(trait), static_cast<uint32_t>(value)))
                return -1;
            return static_cast<int>(result.GetNumber());
        }
    }

    bool ApplyBalance(const coop::Balance* balance)
    {
        if (!Player())
            return false;
        for (Tuned& t : g_tuned)
            if (!t.read)
            {
                std::string value = Evaluate((std::string("GetNumericGameSetting ") + t.name).c_str());
                char* end = nullptr;
                t.original = std::strtof(value.c_str(), &end);
                t.read = end != value.c_str();
                if (!t.read)
                    LOG_WARN("Balance: couldn't read %s (%s)", t.name, value.c_str());
            }
        for (TunedEffect& e : g_tunedEffects)
            if (e.magnitude < 0)
            {
                e.magnitude = EffectTrait(e.item, 0, -1);
                e.duration = EffectTrait(e.item, 2, -1);
            }
        for (const Tuned& t : g_tuned)
        {
            if (!t.read)
                continue;
            float value = t.original;
            if (balance)
            {
                std::string name = t.name;
                if (name.compare(0, 15, "fDiffMultHPToPC") == 0)
                    value = t.original * balance->damageToPlayers;
                else if (name.compare(0, 15, "fDiffMultHPByPC") == 0)
                    value = t.original * balance->damageByPlayers;
                else if (name == "fAVDCarryWeightsBase")
                    value = balance->carryWeightBase;
                else if (name.compare(0, 13, "iXPRewardKill") == 0)
                    value = std::round(t.original * balance->killXpMult);
            }
            char line[128];
            if (t.name[0] == 'i')
                std::snprintf(line, sizeof(line), "SetNumericGameSetting %s %d", t.name, static_cast<int>(value));
            else
                std::snprintf(line, sizeof(line), "SetNumericGameSetting %s %.4f", t.name, value);
            RunConsoleLine(line);
        }
        if (!g_partsRead)
            ReadBodyParts();
        for (const PartMult& p : g_partMults)
        {
            bool head = p.part <= 2;
            float times = !balance ? 1.0f : head ? balance->headshotMult : balance->limbMult;
            BodyPartMult(p.list, p.part, p.original * times);
        }
        if (!g_armorAndLootRead)
            ReadArmorAndLoot();
        for (const ArmorDR& a : g_armors)
        {
            int extra = !balance ? 0 : a.heavy ? balance->heavyArmorDR : balance->mediumArmorDR;
            *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(a.armor) + layout::kArmorRating) =
                static_cast<uint16_t>(a.original + extra);
        }
        for (const StyleWas& s : g_styles)
        {
            auto* style = reinterpret_cast<uint8_t*>(s.style);
            bool usesCover = s.takeCover > 0;
            *reinterpret_cast<float*>(style + layout::kStyleTakeCover) = balance && usesCover ?
                (std::max)(s.takeCover, static_cast<float>(balance->takeCoverAtLeast)) : s.takeCover;
            *reinterpret_cast<float*>(style + layout::kStyleCoverRadius) = balance && usesCover ?
                s.coverRadius * balance->coverSearchMult : s.coverRadius;
            *(style + layout::kStyleSideStep) = balance && usesCover ?
                static_cast<uint8_t>((std::max)(static_cast<int>(s.sideStep), balance->sideStepChance)) : s.sideStep;
            *reinterpret_cast<uint16_t*>(style + layout::kStyleFlags) = balance ?
                static_cast<uint16_t>(s.flags | layout::kStyleFleeWhenHurt) : s.flags;
        }
        for (const SupplyList& l : g_supplyLists)
            *(reinterpret_cast<uint8_t*>(l.list) + layout::kLevListChanceNone) =
                balance ? coop::ScarceChanceNone(l.original, balance->suppliesKept) : l.original;
        for (const SupplyList& l : g_boxLists)
            *(reinterpret_cast<uint8_t*>(l.list) + layout::kLevListChanceNone) =
                balance ? coop::ScarceChanceNone(l.original, balance->boxLootKept) : l.original;
        for (const AmmoValue& a : g_ammoValues)
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(a.ammo) + layout::kAmmoValue) =
                balance ? coop::AmmoValue(a.original, *balance) : a.original;
        for (const AmmoCount& c : g_ammoCounts)
        {
            int32_t count = balance ? coop::AmmoFound(c.original, *balance) : c.original;
            if (c.wide)
                *reinterpret_cast<int32_t*>(c.count) = count;
            else
                *reinterpret_cast<int16_t*>(c.count) = static_cast<int16_t>(count);
        }
        for (const TunedEffect& e : g_tunedEffects)
        {
            if (e.magnitude < 0 || e.duration < 0)
                continue;
            bool super = e.item == ids::kSuperStimpak;
            int magnitude = !balance ? e.magnitude : super ? balance->superStimpakPerSecond : balance->stimpakPerSecond;
            int duration = !balance ? e.duration : super ? balance->superStimpakSeconds : balance->stimpakSeconds;
            EffectTrait(e.item, 0, magnitude);
            EffectTrait(e.item, 2, duration);
        }
        SetLevelUpBalance(balance);
        LOG_INFO("Balance: %s (damage to players x%.2f, by players x%.2f, carry weight %.0f, Stimpak %d/s for %d s, "
            "skill points a level %d + half of Intelligence, kill XP x%.2f, ammo worth x%.1f and found x%.2f)",
            balance ? "co-op balanced" : "the game's own", balance ? balance->damageToPlayers : 1.0f,
            balance ? balance->damageByPlayers : 1.0f, balance ? balance->carryWeightBase : g_tuned[10].original,
            balance ? balance->stimpakPerSecond : g_tunedEffects[0].magnitude,
            balance ? balance->stimpakSeconds : g_tunedEffects[0].duration,
            balance ? balance->skillPointsBase : coop::kGameSkillPointsBase, balance ? balance->killXpMult : 1.0f,
            balance ? balance->ammoValueMult : 1.0f, balance ? balance->ammoFindKept : 1.0f);
        return true;
    }

    bool KeyDown(int key)
    {
        HWND front = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId(front, &process);
        if (!front || process != GetCurrentProcessId() || key <= 0 || key > 255)
            return false;
        UINT vk = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VSC_TO_VK_EX);
        if (!vk)
            vk = MapVirtualKeyA(0xE000 | static_cast<UINT>(key), MAPVK_VSC_TO_VK_EX);
        return vk && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
    }

    void ShowTalking(bool talking)
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (g_chatTalking != talking)
        {
            g_chatTalking = talking;
            g_chatChanged = true;
        }
    }

    void CancelChat()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        if (!g_chatTyping)
            return;
        g_chatTyping = false;
        g_chatDraft.Type(0x1B);
        g_chatChanged = true;
    }

    bool ChatTyping()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        return g_chatTyping;
    }

    void TypeChat(unsigned char c)
    {
        if (g_gameWindow)
            PostMessageA(g_gameWindow, WM_CHAR, c, 0);
    }

    void ShowChatAbove(int slot, const TESObjectREFR* who, const std::string& text)
    {
        if (slot < 0 || slot >= kChatAboveSlots || !who)
            return;
        uint32_t ms = 4000 + 70 * static_cast<uint32_t>(text.size());
        g_above[slot] = ChatAbove{who->refID, text, GetTickCount() + (ms < 12000 ? ms : 12000), true, g_above[slot].shown};
    }

    namespace
    {
        struct Mark { uint32_t who = 0; std::string name; bool downed = false, shown = false; };
        Mark g_marks[kMarkSlots];
    }

    void ShowFriendMarkers(const std::vector<FriendMark>& friends)
    {
        for (int slot = 0; slot < kMarkSlots; ++slot)
        {
            Mark& m = g_marks[slot];
            TESObjectREFR* who = slot < static_cast<int>(friends.size()) ? friends[slot].who : nullptr;
            const std::string name = who ? friends[slot].text : std::string();
            bool downed = who && friends[slot].downed;
            float x = 0, y = 0;
            bool show = false;
            NVSEArrayVarInterface::Element result;
            if (who && g_onScreen && g_arrays && g_mark[slot] && Player())
            {
                MakeChatHud();
                if (g_scripts->CallFunction(g_onScreen, who, nullptr, &result, 1, who) && result.GetArray() &&
                    g_arrays->GetArraySize(result.GetArray()) == 3)
                {
                    NVSEArrayVarInterface::Element e[3];
                    if (g_arrays->GetElements(result.GetArray(), e, nullptr) && e[0].GetNumber() != 0)
                    {
                        x = static_cast<float>(e[1].GetNumber());
                        y = static_cast<float>(e[2].GetNumber());
                        show = x >= 0 && x <= 1 && y >= 0 && y <= 1;
                    }
                }
            }
            if (!show && !m.shown)
                continue;
            bool changed = !m.shown || m.who != (who ? who->refID : 0) || m.name != name || m.downed != downed;
            if (Player() && g_mark[slot])
                g_scripts->CallFunction(g_mark[slot], Player(), nullptr, nullptr, 5, name.empty() ? " " : name.c_str(),
                    *reinterpret_cast<uint32_t*>(&x), *reinterpret_cast<uint32_t*>(&y),
                    static_cast<uint32_t>(!show ? 0 : changed ? 2 : 1), static_cast<uint32_t>(downed ? 1 : 0));
            m.who = show ? who->refID : 0;
            m.name = show ? name : std::string();
            m.downed = show && downed;
            m.shown = show;
        }
    }

    void UpdateChatAbove()
    {
        uint32_t now = GetTickCount();
        for (int slot = 0; slot < kChatAboveSlots; ++slot)
        {
            ChatAbove& a = g_above[slot];
            if (!a.who && !a.shown)
                continue;
            NVSEArrayVarInterface::Element result;
            TESObjectREFR* who = a.who ? LookupRef(a.who) : nullptr;
            float x = 0, y = 0;
            bool show = who && static_cast<int32_t>(now - a.until) < 0 && DistanceToPlayer(who) < 3500 && g_onScreen &&
                g_arrays && g_chatAbove[slot];
            if (show)
            {
                MakeChatHud();
                show = false;
                if (g_scripts->CallFunction(g_onScreen, who, nullptr, &result, 1, who) && result.GetArray() &&
                    g_arrays->GetArraySize(result.GetArray()) == 3)
                {
                    NVSEArrayVarInterface::Element e[3];
                    if (g_arrays->GetElements(result.GetArray(), e, nullptr) && e[0].GetNumber() != 0)
                    {
                        x = static_cast<float>(e[1].GetNumber());
                        y = static_cast<float>(e[2].GetNumber());
                        show = x >= 0 && x <= 1 && y >= 0 && y <= 1;
                    }
                }
            }
            else if (static_cast<int32_t>(now - a.until) >= 0)
                a.who = 0;
            if (!show && !a.shown)
                continue;
            if (Player() && g_chatAbove[slot])
                g_scripts->CallFunction(g_chatAbove[slot], Player(), nullptr, nullptr, 4, a.text.c_str(),
                    *reinterpret_cast<uint32_t*>(&x), *reinterpret_cast<uint32_t*>(&y),
                    static_cast<uint32_t>(!show ? 0 : a.changed || !a.shown ? 2 : 1));
            a.changed = a.changed && !show;
            a.shown = show;
        }
    }

    std::vector<std::string> UpdateChat()
    {
        bool typing, changed, talking;
        std::string text;
        std::vector<std::string> sent;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            typing = g_chatTyping;
            changed = g_chatChanged;
            g_chatChanged = false;
            talking = g_chatTalking;
            text = g_chatDraft.Text();
            sent = std::move(g_chatSent);
            g_chatSent.clear();
        }
        bool entry;
        {
            std::lock_guard<std::mutex> lock(g_chatMutex);
            entry = g_entryState == EntryState::Typing;
        }
        bool keep = typing || entry;
        if (keep != !g_chatKeysOff.empty() && (keep || !g_chatKeysOff.empty()))
            KeepKeysFromGame(keep);
        bool show = typing || talking;
        if (changed || show != g_chatShown)
        {
            ShowChatLine(typing ? "Say: " + text + "_" : std::string("((( Talking )))"), show);
            g_chatShown = show;
        }
        return sent;
    }

    float DistanceToPlayer(const TESObjectREFR* ref)
    {
        TESObjectREFR* player = Player();
        if (!ref || !player)
            return 1e9f;
        float dx = ref->posX - player->posX, dy = ref->posY - player->posY, dz = ref->posZ - player->posZ;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool IsControlPressed(int control)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !Player() ||
            !g_scripts->CallFunction(g_controlPressed, Player(), nullptr, &result, 1, static_cast<uint32_t>(control)))
            return false;
        return result.GetNumber() != 0;
    }

    void MakeCompanion(TESObjectREFR* ref)
    {
        if (!g_compiled || !ref)
            return;
        TESForm* faction = LookupForm(ids::kPlayerFaction);
        if (!faction || faction->typeID != kFormType_TESFaction)
        {
            LOG_WARN("PlayerFaction not found; stand-ins are teammates only");
            faction = nullptr;
        }
        g_scripts->CallFunctionAlt(g_makeCompanion, ref, 1, faction);
    }

    std::vector<Hit> TakeHits()
    {
        std::vector<Hit> hits;
        for (const PendingHit& p : g_myHits)
        {
            TESObjectREFR* ref = LookupRef(p.refId);
            float lost = ref ? p.healthAtHit - Health(ref) : 0;
            LOG_DEBUG("%08X hit %08X for %.1f", p.attackerId, p.refId, lost);
            if (lost > 0)
                hits.push_back({p.refId, p.attackerId, lost});
        }
        g_myHits.clear();
        return hits;
    }

    void ApplyHit(TESObjectREFR* target, TESObjectREFR* by, float damage)
    {
        if (g_compiled && target && by)
            g_scripts->CallFunctionAlt(g_hit, Player(), 3, target, by, FloatArg(damage));
    }

    namespace
    {
        const void* MarkerData(const TESObjectREFR* marker);
    }

    std::vector<TESObjectREFR*> MapMarkers()
    {
        std::vector<TESObjectREFR*> out;
        const void* data = *reinterpret_cast<const void* const*>(addr::kDataHandler);
        if (!data)
            return out;
        ForEachIn(static_cast<const char*>(data) + layout::kDataHandlerWorldSpaces, [&](void* world)
        {
            const void* cell = At<const void*>(world, layout::kWorldSpaceCell);
            if (!cell)
                return;
            ForEachIn(static_cast<const char*>(cell) + layout::kCellObjects, [&](void* ref)
            {
                if (MarkerData(static_cast<TESObjectREFR*>(ref)))
                    out.push_back(static_cast<TESObjectREFR*>(ref));
            });
        });
        return out;
    }

    namespace
    {
        const void* MarkerData(const TESObjectREFR* marker)
        {
            using GetByTypeFn = const void*(__thiscall*)(const void* list, uint8_t type);
            const void* extra = marker ? reinterpret_cast<GetByTypeFn>(addr::kExtraListGetByType)(
                &marker->extraDataList, layout::kExtraMapMarker) : nullptr;
            return extra ? At<const void*>(extra, layout::kMapMarkerData) : nullptr;
        }
    }

    bool CanTravelTo(const TESObjectREFR* marker)
    {
        const void* data = MarkerData(marker);
        return data && (At<uint8_t>(data, layout::kMarkerFlags) & layout::kMarkerCanTravel);
    }

    std::string MarkerName(const TESObjectREFR* marker)
    {
        const void* data = MarkerData(marker);
        const char* name = data ? At<const char*>(data, layout::kMarkerName) : nullptr;
        return name ? name : "";
    }

    void UnlockTravel(TESObjectREFR* marker)
    {
        if (g_compiled && g_showMap && marker)
            g_scripts->CallFunctionAlt(g_showMap, Player(), 1, marker);
    }

    TESForm* TeleporterItem()
    {
        if (g_teleporter || !g_compiled || !Player())
            return g_teleporter;
        TESForm* original = LookupForm(ids::kStimpak);
        NVSEArrayVarInterface::Element result;
        if (!original || original->typeID != kFormType_AlchemyItem ||
            !g_scripts->CallFunction(g_clone, Player(), nullptr, &result, 1, original))
            return nullptr;
        TESForm* copy = result.GetTESForm();
        if (!copy || copy->typeID != kFormType_AlchemyItem)
            return nullptr;
        auto* item = static_cast<AlchemyItem*>(copy);
        SetName(item->fullName.name, "Co-op Teleporter");
        item->effects.list.m_listHead.data = nullptr;
        item->effects.list.m_listHead.next = nullptr;
        item->value = 0;
        if (g_questObject)
            g_scripts->CallFunctionAlt(g_questObject, Player(), 1, copy);
        g_teleporter = copy;
        LOG_INFO("Teleporter: item %08X made (a copy of %08X)", copy->refID, original->refID);
        return g_teleporter;
    }

    int ItemCount(TESObjectREFR* ref, TESForm* item)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_itemCount || !ref || !item || !g_scripts->CallFunction(g_itemCount, ref, nullptr, &result, 1, item))
            return 0;
        return static_cast<int>(result.GetNumber());
    }

    void SetRefMapMarker(TESObjectREFR* ref, bool on)
    {
        static const CommandInfo* add = g_cmdTable ? g_cmdTable->GetByName("AddRefMapMarker") : nullptr;
        static const CommandInfo* remove = g_cmdTable ? g_cmdTable->GetByName("RemoveRefMapMarker") : nullptr;
        static bool warned = false;
        const CommandInfo* cmd = on ? add : remove;
        if (!cmd || !cmd->execute)
        {
            if (!warned)
                LOG_WARN("Map: no JIP AddRefMapMarker / RemoveRefMapMarker: friends aren't shown on the map");
            warned = true;
            return;
        }
        if (!ref)
            return;
        double result = 0;
        UInt32 offset = 0;
        cmd->execute(cmd->params, nullptr, ref, nullptr, nullptr, nullptr, &result, &offset);
    }

    void SetOnCompass(TESObjectREFR* actor, bool on)
    {
        void* player = Player();
        if (!player || !actor || actor == player)
            return;
        if (on)
        {
            using SetFn = void(__thiscall*)(void* pc, TESObjectREFR* target, uint8_t hostile, uint8_t hidden);
            reinterpret_cast<SetFn>(addr::kCompassTargetSet)(player, actor, 0, 0);
        }
        else
        {
            using RemoveFn = void(__thiscall*)(void* pc, TESObjectREFR* target);
            reinterpret_cast<RemoveFn>(addr::kCompassTargetRemove)(player, actor);
        }
    }

    TESObjectREFR* SitInNearestChair(TESObjectREFR* actor)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_sitNearest || !actor || !g_scripts->CallFunction(g_sitNearest, Player(), nullptr, &result, 1, actor))
            return nullptr;
        TESForm* form = result.GetTESForm();
        return form && form->GetIsReference() ? static_cast<TESObjectREFR*>(form) : nullptr;
    }

    void PlayAnimFile(TESObjectREFR* actor, const std::string& path)
    {
        std::string file = _strnicmp(path.c_str(), "meshes\\", 7) == 0 ? path.substr(7) : path;
        if (!g_console || !actor || file.empty() || file.find('"') != std::string::npos)
            return;
        std::string line = "PlayAnimationPath \"" + file + "\" 0";
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            g_console->RunScriptLine2(line.c_str(), actor, true);
    }

    void SetCollision(TESObjectREFR* actor, bool on)
    {
        if (g_compiled && g_collision && actor)
            g_scripts->CallFunctionAlt(g_collision, actor, 1, static_cast<uint32_t>(on ? 1 : 0));
    }

    void ShowReviveBar(const std::string& text)
    {
        static std::string s_shown;
        if (text == s_shown || !g_reviveBar || !Player())
            return;
        MakeChatHud();
        g_scripts->CallFunction(g_reviveBar, Player(), nullptr, nullptr, 2, text.empty() ? " " : text.c_str(),
            static_cast<uint32_t>(text.empty() ? 0 : 1));
        s_shown = text;
    }

    bool IsStandIn(const TESObjectREFR* ref)
    {
        const TESForm* base = ref ? ref->baseForm : nullptr;
        return g_noTalk && base && base->typeID == kFormType_TESNPC &&
            static_cast<const TESActorBase*>(base)->scriptable.script == g_noTalk;
    }

    namespace
    {
        uintptr_t g_teammateCall = 0;

        bool __fastcall SkipWatcherHook(TESObjectREFR* actor)
        {
            using Fn = bool(__thiscall*)(TESObjectREFR*);
            if (reinterpret_cast<Fn>(g_teammateCall)(actor))
                return true;
            if (!IsStandIn(actor))
                return false;
            static std::atomic<uint32_t> s_skipped{0};
            uint32_t n = ++s_skipped;
            if (n == 1 || n % 3600 == 0)
                LOG_INFO("Sneak: another player's character %08X doesn't count as seeing us (%u so far)",
                    actor->refID, n);
            return true;
        }
    }

    bool InstallAllyDetectionSkip()
    {
        if (g_teammateCall)
            return true;
        uintptr_t was = HookCall(addr::kDetectionTeammateCall, reinterpret_cast<const void*>(&SkipWatcherHook));
        if (!was)
            return false;
        g_teammateCall = was;
        if (was != addr::kActorIsTeammate)
            LOG_INFO("Sneak: another plugin hooks the sneak meter's teammate check too (%08X); ours passes on to it",
                static_cast<unsigned>(was));
        return true;
    }

    void HideTalkPromptOnFriends()
    {
        static const TESObjectREFR* s_logged = nullptr;
        void* hud = *reinterpret_cast<void**>(addr::kHudMainMenu);
        TESObjectREFR* ref = hud ? At<TESObjectREFR*>(hud, layout::kHudPromptRef) : nullptr;
        if (!IsStandIn(ref))
        {
            s_logged = nullptr;
            return;
        }
        using GetFloat = float(__thiscall*)(const void*, uint32_t);
        using SetFloat = void(__thiscall*)(void*, uint32_t, float, bool);
        int hidden = 0;
        for (uint32_t offset : layout::kHudPromptTiles)
        {
            void* tile = At<void*>(hud, offset);
            if (tile && reinterpret_cast<GetFloat>(addr::kTileGetValueFloat)(tile, layout::kTileVisible) != 0)
            {
                reinterpret_cast<SetFloat>(addr::kTileSetValueFloat)(tile, layout::kTileVisible, 0, true);
                ++hidden;
            }
        }
        if (hidden && s_logged != ref)
        {
            s_logged = ref;
            LOG_INFO("Talk prompt hidden on another player's character %08X (%d tiles)", ref->refID, hidden);
        }
    }

    namespace
    {
        const void* ChildTile(const void* parent, const char* name)
        {
            int guard = 0;
            for (const void* node = parent ? At<const void*>(parent, layout::kTileFirstChild) : nullptr;
                 node && guard < 1024; node = At<const void*>(node, layout::kChildNodeNext), ++guard)
            {
                const void* child = At<const void*>(node, layout::kChildNodeTile);
                const char* childName = child ? At<const char*>(child, layout::kTileName) : nullptr;
                if (childName && _stricmp(childName, name) == 0)
                    return child;
            }
            return nullptr;
        }

        float TileValue(const void* tile, uint32_t trait)
        {
            using GetFloat = float(__thiscall*)(const void*, uint32_t);
            return tile ? reinterpret_cast<GetFloat>(addr::kTileGetValueFloat)(tile, trait) : 0;
        }

        void ScaleTile(const void* tile, float scale)
        {
            char* node = tile ? At<char*>(tile, layout::kTileNode) : nullptr;
            if (!node)
                return;
            *reinterpret_cast<float*>(node + layout::kNiLocalScale) = scale;
            char* const* children = At<char* const*>(node, layout::kNiNodeChildren);
            uint16_t slots = At<uint16_t>(node, layout::kNiNodeChildSlots);
            for (uint16_t i = 0; children && i < slots && i < 64; ++i)
                if (children[i])
                    std::fill_n(reinterpret_cast<float*>(children[i] + layout::kNiLocalTranslate), 3, 0.0f);
            struct { float time = 0; uint8_t flags[12] = {}; } update;
            using Fn = void(__thiscall*)(void*, void*);
            reinterpret_cast<Fn>(addr::kNiAVObjectUpdate)(node, &update);
        }

        struct TalkSlot
        {
            const void* tile = nullptr;
            uint32_t id = 0;
            uint32_t look = 0xFFFFFFFF;
            float place[7] = {-1, -1, -1, -1, -1, -1, -1};
            float back[7] = {-1, -1, -1, -1, -1, -1, -1};
            float side[2] = {-1, -1};
        };
        TalkSlot g_talkSlots[kTalkSlots];

        template <size_t N> bool Changed(float (&cached)[N], const float (&now)[N])
        {
            bool same = true;
            for (size_t i = 0; i < N; ++i)
                same = same && std::fabs(now[i] - cached[i]) < 0.5f;
            if (!same)
                std::copy(std::begin(now), std::end(now), cached);
            return !same;
        }

        void PlaceTalkSlot(int slot, const float (&place)[7], const float (&back)[7], const float (&side)[2])
        {
            TalkSlot& s = g_talkSlots[slot];
            if (g_talkSide[slot] && Changed(s.side, side))
                g_scripts->CallFunction(g_talkSide[slot], Player(), nullptr, nullptr, 2, FloatArg(side[0]),
                    FloatArg(side[1]));
            if (g_talkPlace[slot] && Changed(s.place, place))
                g_scripts->CallFunction(g_talkPlace[slot], Player(), nullptr, nullptr, 7, FloatArg(place[0]),
                    FloatArg(place[1]), FloatArg(place[2]), FloatArg(place[3]), FloatArg(place[4]),
                    static_cast<uint32_t>(place[5]), FloatArg(place[6]));
            if (g_talkBack[slot] && Changed(s.back, back))
                g_scripts->CallFunction(g_talkBack[slot], Player(), nullptr, nullptr, 7, FloatArg(back[0]),
                    FloatArg(back[1]), FloatArg(back[2]), FloatArg(back[3]), FloatArg(back[4]), FloatArg(back[5]),
                    FloatArg(back[6]));
        }
    }

    void ShowTalkLog(const std::vector<coop::TalkLog::Shown>& lines, const TalkLogLook& look)
    {
        constexpr float kTop = 24, kAlpha = 235, kLineAlpha = 150;
        constexpr float kBarAlpha = 120, kBarPad = 6, kBarSide = 8;
        constexpr float kDividerPerWidth = 740.0f / 600, kDividerHeight = 8, kTextLine = 32, kUnderLetters = 0.2f;
        static const char kLeftLine[] = "interface\\shared\\line\\glow_tip_line_horizontal.dds";
        static const char kRightLine[] = "interface\\nvcoop\\talk_divider_right.dds";
        constexpr uint32_t kXTrait = 0xFA1, kYTrait = 0xFA2, kHeight = 0xFB0, kWidthTrait = 0xFB1;
        constexpr float kJustifyLeft = 1, kJustifyRight = 4;
        constexpr float kHpFallbackX = 40, kHpFallbackFromBottom = 960 - 803;
        static bool s_showing = true;
        if (!Player() || !g_talkText[0] || !g_talkPlace[0] || !g_talkBack[0] || !g_chatHud ||
            (lines.empty() && !s_showing))
            return;
        MakeChatHud();
        auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
        const void* root = manager ? At<const void*>(manager, layout::kInterfaceMenuRoot) : nullptr;
        const void* hud = ChildTile(root, "HUDMainMenu");
        if (!hud)
            return;
        const void* text[kTalkSlots] = {};
        const void* edges[kTalkSlots][kTalkEdges] = {};
        const void* hitPoints = nullptr;
        int guard = 0;
        for (const void* node = At<const void*>(hud, layout::kTileFirstChild); node && guard < 4096;
             node = At<const void*>(node, layout::kChildNodeNext), ++guard)
        {
            const void* child = At<const void*>(node, layout::kChildNodeTile);
            const char* name = child ? At<const char*>(child, layout::kTileName) : nullptr;
            if (name && !hitPoints && _stricmp(name, "HitPoints") == 0)
                hitPoints = child;
            if (!name || std::strncmp(name, "NVCoopTalk", 10) != 0)
                continue;
            bool isEdge = std::strncmp(name + 10, "Edge", 4) == 0;
            char* end = nullptr;
            const char* digits = name + (isEdge ? 14 : 10);
            if (*digits < '0' || *digits > '9')
                continue;
            long slot = std::strtol(digits, &end, 10);
            if (slot < 0 || slot >= kTalkSlots)
                continue;
            if (!isEdge)
                text[slot] = child;
            else if (*end == 'x' && end[1] >= '0' && end[1] < '0' + kTalkEdges)
                edges[slot][end[1] - '0'] = child;
        }

        float scale = std::clamp(look.scale, 0.2f, 3.0f);
        const float width = std::clamp(look.width, 200.0f, 1200.0f);
        uint32_t lookKey = (look.alignRight ? 1u << 25 : 0) | (look.hudColor ? 1u << 24 : 0) | look.red << 16 |
            look.green << 8 | look.blue;
        int style = std::clamp(look.style, 0, 3);
        bool used[kTalkSlots] = {};
        const float units = *reinterpret_cast<const float*>(addr::kMenuUnitsPerPixel);
        const float settingHeight = static_cast<float>(*reinterpret_cast<const int32_t*>(addr::kDisplayHeightSetting + 4))
            * units;
        const float settingWidth = static_cast<float>(*reinterpret_cast<const int32_t*>(addr::kDisplayWidthSetting + 4))
            * units;
        float screenHeight = TileValue(root, kHeight), screenWidth = TileValue(root, kWidthTrait);
        if (screenHeight < 200 || screenWidth < 200)
            screenHeight = settingHeight, screenWidth = settingWidth;
        float hpX = TileValue(hitPoints, kXTrait), hpTop = TileValue(hitPoints, kYTrait);
        bool hpRead = hitPoints && hpTop > 0;
        if (!hpRead)
            hpX = kHpFallbackX, hpTop = screenHeight - kHpFallbackFromBottom;
        static bool s_sizeLogged = false, s_hpWasRead = false;
        if (!s_sizeLogged || hpRead != s_hpWasRead)
        {
            s_sizeLogged = true;
            s_hpWasRead = hpRead;
            LOG_INFO("Talk log: the screen is %.0f by %.0f in menu units (display settings: %.0f by %.0f); the HP bar "
                "at x %.0f, top %.0f, %.0f high (%s)", screenWidth, screenHeight, settingWidth, settingHeight, hpX,
                hpTop, TileValue(hitPoints, kHeight), hpRead ? "its HUD tile" : "not found: the vanilla HUD's place");
        }
        float heights[kTalkSlots] = {};
        float total = 0;
        size_t count = 0;
        for (const coop::TalkLog::Shown& l : lines)
        {
            int slot = static_cast<int>(l.id % kTalkSlots);
            TalkSlot& s = g_talkSlots[slot];
            if (s.tile != text[slot])
                s = TalkSlot{text[slot]};
            size_t at = count++;
            if (!text[slot] || at >= static_cast<size_t>(kTalkSlots))
                continue;
            used[slot] = true;
            if (s.id != l.id || s.look != lookKey)
            {
                g_scripts->CallFunction(g_talkText[slot], Player(), nullptr, nullptr, 7,
                    l.text.empty() ? " " : l.text.c_str(), FloatArg(look.hudColor ? 255.0f : -1.0f),
                    FloatArg(look.red), FloatArg(look.green), FloatArg(look.blue),
                    FloatArg(look.alignRight ? kJustifyRight : kJustifyLeft), look.alignRight ? kRightLine : kLeftLine);
                s.id = l.id;
                s.look = lookKey;
            }
            ScaleTile(text[slot], scale);
            for (const void* edge : edges[slot])
                ScaleTile(edge, scale);
            float measured = TileValue(text[slot], kHeight);
            heights[at] = (measured > 0 ? measured : kTextLine) * scale;
            total += heights[at] + look.gap;
        }
        coop::TalkLogCorner corner = look.place == 1 ?
            coop::PlaceTalkLog(look.xPercent, look.yPercent, screenWidth, screenHeight, width, total) :
            coop::PlaceTalkLogAboveHp(hpX, hpTop, screenWidth, screenHeight, width, total);
        const float textX = look.alignRight ? corner.x + width : corner.x;
        const float lineWidth = width * kDividerPerWidth;
        const float side[2] = {look.alignRight ? corner.x + width - lineWidth : corner.x, corner.x - kBarSide};
        float y = corner.y;
        count = 0;
        for (const coop::TalkLog::Shown& l : lines)
        {
            int slot = static_cast<int>(l.id % kTalkSlots);
            size_t at = count++;
            if (!text[slot] || at >= static_cast<size_t>(kTalkSlots))
                continue;
            float height = heights[at];
            float alpha = kAlpha * l.alpha;
            const float place[7] = {y, width / scale, alpha, style == 1 || style == 2 ? alpha : 0,
                style == 2 ? 2.0f : 1.0f, style == 1 ? 1.0f : style == 2 ? 2.0f : 0.0f, textX};
            float divider = y + height + look.gap / 2 - kTextLine * kUnderLetters * scale;
            const float back[7] = {divider - kDividerHeight / 2, lineWidth, look.dividers ? kLineAlpha * l.alpha : 0,
                y - kBarPad / 2, height + kBarPad, width + 2 * kBarSide, style == 3 ? kBarAlpha * l.alpha : 0};
            PlaceTalkSlot(slot, place, back, side);
            y += height + look.gap;
        }
        for (int slot = 0; slot < kTalkSlots; ++slot)
            if (!used[slot])
            {
                g_talkSlots[slot].id = 0;
                float x = g_talkSlots[slot].place[6];
                const float hidden[7] = {kTop, width, 0, 0, 1, 0, x < 0 ? textX : x};
                const float hiddenBack[7] = {kTop, width, 0, kTop, 1, width, 0};
                PlaceTalkSlot(slot, hidden, hiddenBack, side);
            }
        s_showing = !lines.empty();
    }

    void ShowChatPanel(const std::string& text, int xPercent, int yPercent, float alpha)
    {
        // The text is 769 wide at this scale; right justified text is drawn leftward from its x
        constexpr float kWidth = 500, kScale = 0.65f, kAlpha = 235, kBarAlpha = 120, kBarPad = 6, kBarSide = 8;
        constexpr float kTextLine = 32;
        constexpr uint32_t kHeight = 0xFB0, kWidthTrait = 0xFB1;
        static bool s_showing = false;
        static const void* s_tile = nullptr;
        static std::string s_text;
        static float s_place[3] = {-1, -1, -1};
        static float s_bar[4] = {-1, -1, -1, -1};
        bool show = !text.empty() && alpha > 0;
        if (!Player() || !g_feedText || !g_feedPlace || !g_feedBar || !g_chatHud || (!show && !s_showing))
            return;
        MakeChatHud();
        auto* manager = *reinterpret_cast<const uint8_t* const*>(addr::kInterfaceManager);
        const void* root = manager ? At<const void*>(manager, layout::kInterfaceMenuRoot) : nullptr;
        const void* tile = ChildTile(ChildTile(root, "HUDMainMenu"), "NVCoopFeedText");
        if (!tile)
            return;
        if (tile != s_tile)
        {
            s_tile = tile;
            s_text.clear();
            std::fill(std::begin(s_place), std::end(s_place), -1.0f);
            std::fill(std::begin(s_bar), std::end(s_bar), -1.0f);
        }
        float height = kTextLine * kScale;
        if (show)
        {
            if (text != s_text)
            {
                g_scripts->CallFunction(g_feedText, Player(), nullptr, nullptr, 1, text.c_str());
                s_text = text;
            }
            ScaleTile(tile, kScale);
            float measured = TileValue(tile, kHeight);
            if (measured > 0)
                height = measured * kScale;
        }
        else
            s_text.clear();
        float screenHeight = TileValue(root, kHeight), screenWidth = TileValue(root, kWidthTrait);
        if (screenHeight < 200)
            screenHeight = static_cast<float>(*reinterpret_cast<const int32_t*>(addr::kDisplayHeightSetting + 4)) *
                *reinterpret_cast<const float*>(addr::kMenuUnitsPerPixel);
        if (screenWidth < 200)
            screenWidth = screenHeight * 16 / 9;
        coop::FeedCorner corner = coop::PlaceFeed(xPercent, yPercent, screenWidth, screenHeight, kWidth, height);
        float shown = show ? std::clamp(alpha, 0.0f, 1.0f) : 0;
        const float place[3] = {corner.right, corner.top, kAlpha * shown};
        const float bar[4] = {corner.right - kWidth - kBarSide, corner.top - kBarPad / 2, height + kBarPad, kBarAlpha * shown};
        if (Changed(s_place, place))
            g_scripts->CallFunction(g_feedPlace, Player(), nullptr, nullptr, 3, FloatArg(place[0]), FloatArg(place[1]),
                FloatArg(place[2]));
        if (Changed(s_bar, bar))
            g_scripts->CallFunction(g_feedBar, Player(), nullptr, nullptr, 4, FloatArg(bar[0]), FloatArg(bar[1]),
                FloatArg(bar[2]), FloatArg(bar[3]));
        s_showing = show;
    }

    int TakeChatScroll()
    {
        std::lock_guard<std::mutex> lock(g_chatMutex);
        int scroll = g_chatScroll;
        g_chatScroll = 0;
        return scroll;
    }

    void SetSpeedMult(TESObjectREFR* actor, int mult)
    {
        if (g_compiled && g_speedMult && actor && mult > 0)
            g_scripts->CallFunctionAlt(g_speedMult, actor, 1, static_cast<uint32_t>(mult));
    }

    void SetAnimOverride(TESObjectREFR* actor, const std::string& path, bool on)
    {
        std::string file = _strnicmp(path.c_str(), "meshes\\", 7) == 0 ? path.substr(7) : path;
        if (!g_console || !actor || file.empty() || file.find('"') != std::string::npos)
            return;
        std::string line = std::string("SetActorAnimationPath 0 ") + (on ? "1" : "0") + " \"" + file + "\"";
        if (g_console->version >= 3 && g_console->RunScriptLine2)
            g_console->RunScriptLine2(line.c_str(), actor, true);
    }

    void SetActorAlpha(TESObjectREFR* actor, float alpha)
    {
        if (g_compiled && g_actorAlpha && actor)
            g_scripts->CallFunctionAlt(g_actorAlpha, actor, 1, FloatArg(alpha));
    }

    uint8_t PlayerDisabledControls()
    {
        const void* player = Player();
        return player ? At<uint8_t>(player, layout::kPlayerDisabledControls) : 0;
    }

    int PlayerPerkRank(uint32_t perk)
    {
        int ranks = PerkRanks(perk);
        return ranks < 0 ? 0 : (ranks & 0xFF);
    }

    TESObjectREFR* KillerOf(TESObjectREFR* actor)
    {
        if (!IsActor(actor) || !IsDead(actor))
            return nullptr;
        return At<TESObjectREFR*>(actor, layout::kActorKiller);
    }

    bool IsCreatureType(TESObjectREFR* ref, int type)
    {
        NVSEArrayVarInterface::Element result;
        if (!g_compiled || !g_creatureType || !ref ||
            !g_scripts->CallFunction(g_creatureType, ref, nullptr, &result, 1, static_cast<uint32_t>(type)))
            return false;
        return result.GetNumber() != 0;
    }

    bool GlobalNumber(uint32_t global, float& value)
    {
        TESForm* form = LookupForm(global);
        if (!form || form->typeID != kFormType_TESGlobal)
            return false;
        value = static_cast<TESGlobal*>(form)->data;
        return true;
    }

    bool SetGlobalNumber(uint32_t global, float value)
    {
        TESForm* form = LookupForm(global);
        if (!form || form->typeID != kFormType_TESGlobal)
            return false;
        static_cast<TESGlobal*>(form)->data = value;
        return true;
    }

    void LevelUpNote(int action)
    {
        if (!g_compiled || !Player())
            return;
        if (action == 0 && g_noteInject)
            g_scripts->CallFunctionAlt(g_noteInject, Player(), 3, FloatArg(kPartyPerkRed[0]), FloatArg(kPartyPerkRed[1]),
                FloatArg(kPartyPerkRed[2]));
        else if (g_noteShow)
            g_scripts->CallFunctionAlt(g_noteShow, Player(), 1, static_cast<uint32_t>(action == 1 ? 1 : 0));
    }
}
