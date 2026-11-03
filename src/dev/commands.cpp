// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "dev/commands.h"

#include "dev/inspect_text.h"
#include "game/addresses.h"
#include "game/dialogue.h"
#include "game/sleepwait.h"
#include "game/game.h"
#include "game/inspector.h"
#include "game/mirror.h"
#include "game/mover.h"
#include "game/npcs.h"
#include "log.h"

#include "nvse/PluginAPI.h"
#include "nvse/CommandTable.h"
#include "nvse/GameObjects.h"

#include <windows.h>
#include <psapi.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace dev
{
    namespace
    {
        NVSEScriptInterface* g_scripts = nullptr;
        game::Inspector* g_inspector = nullptr;
        game::Mirror* g_mirror = nullptr;
        uint32_t g_dummy = 0;
        bool g_pitching = false;
        float g_pitch = 0;
        uint32_t g_pickedDoor = 0;
        uint32_t g_pickedQuest = 0;
        struct WatchHit { uint32_t eip; uint32_t stack[16]; double ms; float before, after; };
        const float* g_watchFloat = nullptr;
        WatchHit g_hits[30];
        volatile LONG g_hitCount = 0;
        PVOID g_watchHandler = nullptr;
        uintptr_t g_watchAddress = 0;
        DWORD g_gameThread = 0;

        LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e)
        {
            if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1))
                return EXCEPTION_CONTINUE_SEARCH;
            LONG n = InterlockedIncrement(&g_hitCount) - 1;
            if (n < 30)
            {
                g_hits[n].eip = e->ContextRecord->Eip;
                LARGE_INTEGER t, f;
                QueryPerformanceCounter(&t);
                QueryPerformanceFrequency(&f);
                g_hits[n].ms = 1000.0 * t.QuadPart / f.QuadPart;
                g_hits[n].after = g_watchFloat ? *g_watchFloat : 0;
                const uint32_t* sp = reinterpret_cast<const uint32_t*>(e->ContextRecord->Esp);
                for (int i = 0; i < 16; ++i)
                    g_hits[n].stack[i] = 0;
                int found = 0;
                for (int i = 0; i < 4096 && found < 16; ++i)
                    if (sp[i] > 0x401000 && sp[i] < 0x1000000)
                    {
                        bool seen = false;
                        for (int k = 0; k < found; ++k)
                            seen |= g_hits[n].stack[k] == sp[i];
                        if (!seen)
                            g_hits[n].stack[found++] = sp[i];
                    }
            }
            e->ContextRecord->Dr6 = 0;
            if (n >= 29)
                e->ContextRecord->Dr7 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        DWORD WINAPI SetWatch(LPVOID)
        {
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_gameThread);
            if (!t)
                return 1;
            SuspendThread(t);
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            GetThreadContext(t, &c);
            c.Dr0 = g_watchAddress;
            c.Dr7 = (g_watchAddress ? 1 : 0) | (1u << 16) | (3u << 18);
            if (!g_watchAddress)
                c.Dr7 = 0;
            SetThreadContext(t, &c);
            ResumeThread(t);
            CloseHandle(t);
            return 0;
        }

        float g_faceWas[256] = {};
        uint32_t g_faceWasAt = 0;
        void* g_detachedScript = nullptr;

        void Say(const char* fmt, ...)
        {
            char buf[512];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            LOG_INFO("Command: %s", buf);
            game::ConsolePrint(buf);
        }

        TESObjectREFR* Target(TESObjectREFR* selected)
        {
            if (selected)
                return selected;
            return g_inspector->Watching() ? game::LookupRef(g_inspector->Watching()) : nullptr;
        }

        bool g_spawnTestMoves = true;

        void Help()
        {
            Say("coop inspect: log what the selected actor (or you) is doing; coop stop: stop that");
            Say("coop check: check the memory map again");
            Say("coop menus: which menus are open (as the crash log says)");
            Say("coop spawn: a lab dummy next to you (inspected); coop remove: delete it");
            Say("coop flags N: make the selected or inspected actor move with flags N (1 forward, 512 run, ...)");
            Say("coop release: give it back to its AI's path");
            Say("coop mirror: the lab dummy (or selected actor) copies you like a friend's puppet; again to stop");
            Say("coop npcs: list the actors near you (the game's high-process list)");
            Say("coop doors: list the doors near you; coop door N: the selected door 0 closes, 1 opens, 2 locks, 3 unlocks");
            Say("coop doorpick K: coop door N changes door K of that list when none is selected");
            Say("coop boxes: list the containers near you; coop openbox K: open container K of that list");
            Say("coop mines: list the mines near you; coop mineuse K: use mine K (disarm, then pick up); coop minego K: "
                "step onto it");
            Say("coop box: what the open container screen shows; coop boxadd N: N stimpaks into it (negative: out)");
            Say("coop items: list the items lying near you; coop itempick K: pick up item K of that list");
            Say("coop where N: where reference N (decimal id; or the selected one) is, turned how, and its 3D");
            Say("coop body N: what actor N (decimal id; or the selected one) wears and holds");
            Say("coop hide N / coop show N: switch reference N off or on in this game only (desync lab)");
            Say("coop party: who's following us (companions not told to wait)");
            Say("coop quests: list started quests; coop questpick K: pick one; then coop queststage N (mark stage N done),");
            Say("  coop questobj N (show objective N), coop questobjdone N (complete it), coop questflags F (set its flags)");
            Say("  coop questvars (its script's variables), coop questscript 0 / 1 (detach its script / put it back)");
            Say("coop restrain: switch the selected or mirrored actor's AI routine off (again: back on)");
            Say("coop evp: the selected or mirrored actor picks its AI routine afresh");
            Say("coop reload [type]: the selected or mirrored actor reloads its gun (type 1: with the animation)");
            Say("coop spawnway 0|1, then coop spawntest N: N Powder Gangers at once (0: each moved at once, 1: "
                "left where they appear)");
            Say("coop copyof [N]: place a new actor made from the selected one's form (1 levelled-from, 2 actor "
                "base)");
            Say("coop stand [N]: move the selected actor out of its seat, then way N out of its animation (0-3; "
                "11-13 without the move)");
            Say("coop ganger: a Powder Ganger next to you becomes the lab dummy (it can die)");
            Say("coop arm N: the selected, mirrored or lab dummy actor holds a harmless copy of weapon N (decimal "
                "form id), facing you; coop throw: it attacks with it; coop letgo: its throw leaves its hand");
        }

        uint32_t g_armed = 0;
    game::Spoken g_lastSpoken;
    game::Sound g_labSound;
    uint32_t g_pendingSayer = 0;
    uint32_t g_pendingSayerWas = 0;
    int g_pendingResponse = 0;
    std::string g_lastSpokenText;

    uint32_t Peek(uintptr_t at)
    {
        __try
        {
            return at < 0x10000 ? 0xDEAD0BAD : *reinterpret_cast<const uint32_t*>(at);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xDEAD0BAD;
        }
    }

    void Run(const char* action, int number, bool hasNumber, TESObjectREFR* selected)
        {
            float gx, gy, gz;
            coop::Pose here;
            if (_strnicmp(action, "goto:", 5) == 0)
            {
                if (sscanf_s(action + 5, "%f:%f:%f", &gx, &gy, &gz) != 3 || !game::GetPose(game::Player(), here))
                    return Say("coop goto:X:Y:Z (in this worldspace)");
                here.x = gx;
                here.y = gy;
                here.z = gz;
                return Say(game::TravelTo(here) ? "Went to %.0f, %.0f, %.0f" : "Couldn't go to %.0f, %.0f, %.0f", gx, gy,
                    gz);
            }
            if (_stricmp(action, "menus") == 0)
            {
                char menus[96];
                game::OpenMenus(menus, sizeof(menus));
                return Say("Menus open: %s", menus[0] ? menus : "none");
            }
            if (_stricmp(action, "inspect") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::Player();
                if (g_inspector->Watch(who))
                    Say("Inspecting %08X (lines go to NVCoop.log)", game::RefId(who));
                else
                    Say("%08X isn't an actor", game::RefId(who));
            }
            else if (_stricmp(action, "stop") == 0)
            {
                g_inspector->Watch(nullptr);
                Say("Inspector off");
            }
            else if (_stricmp(action, "check") == 0)
                Say(game::CheckMoverMap() ? "Memory map checks out" : "Memory map is WRONG, see NVCoop.log");
            else if (_stricmp(action, "spawn") == 0)
            {
                if (TESObjectREFR* old = game::LookupRef(g_dummy))
                    game::Remove(old);
                TESObjectREFR* dummy = game::SpawnStandIn("Lab dummy");
                g_dummy = dummy ? game::RefId(dummy) : 0;
                if (dummy && g_inspector->Watch(dummy))
                    Say("Lab dummy %08X is next to you, and inspected", g_dummy);
                else
                    Say("Couldn't make a lab dummy");
            }
            else if (_stricmp(action, "ganger") == 0)
            {
                if (TESObjectREFR* old = game::LookupRef(g_dummy))
                    game::Remove(old);
                TESObjectREFR* ganger = game::PlaceActor(game::ids::kLvlPowderGanger);
                g_dummy = ganger ? game::RefId(ganger) : 0;
                if (ganger)
                    Say("Lab dummy %08X: a Powder Ganger next to you", g_dummy);
                else
                    Say("Couldn't place a Powder Ganger");
            }
            else if (_stricmp(action, "remove") == 0)
            {
                TESObjectREFR* dummy = game::LookupRef(g_dummy);
                if (dummy)
                {
                    if (g_inspector->Watching() == g_dummy)
                        g_inspector->Watch(nullptr);
                    if (g_mirror->Copier() == g_dummy)
                        g_mirror->Stop();
                    game::Remove(dummy);
                    Say("Lab dummy removed");
                }
                else
                    Say("No lab dummy to remove");
                g_dummy = 0;
            }
            else if (_stricmp(action, "flags") == 0)
            {
                TESObjectREFR* who = Target(selected);
                uint32_t flags = static_cast<uint32_t>(number);
                if (!hasNumber || number < 0)
                    Say("Which flags? e.g. coop flags 513 (Forward + Running)");
                else if (game::AskMovementFlags(who, flags))
                    Say("Told %08X to move %s (%u)", game::RefId(who), MoveFlagNames(flags).c_str(), flags);
                else
                    Say("Couldn't: %08X has a %s mover", game::RefId(who),
                        game::MoverKindName(game::ReadMover(who).kind));
            }
            else if (_stricmp(action, "mirror") == 0)
            {
                if (g_mirror->Copier())
                {
                    g_mirror->Stop();
                    Say("Mirror off");
                    return;
                }
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_dummy);
                if (!who)
                {
                    Run("spawn", 0, false, nullptr);
                    who = game::LookupRef(g_dummy);
                }
                if (!game::IsActor(who) || who == game::Player())
                {
                    Say("Nothing to mirror you: select an NPC, or use coop spawn");
                    return;
                }
                g_mirror->Start(who);
                g_inspector->Watch(who);
                Say("%08X copies you now (inspected; numbers in NVCoop.log)", game::RefId(who));
            }
            else if (_stricmp(action, "restrain") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!game::IsActor(who) || who == game::Player())
                {
                    Say("Select an NPC, or start coop mirror first");
                    return;
                }
                bool on = game::LifeState(who) != 5;
                game::SetRestrained(who, on);
                Say("%08X %s (life state now %d)", game::RefId(who), on ? "restrained: no AI routine" :
                    "free: AI routine back on", game::LifeState(who));
            }
            else if (_stricmp(action, "reload") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!game::IsActor(who) || who == game::Player())
                {
                    Say("Select an NPC holding a gun, or start coop mirror first");
                    return;
                }
                int type = hasNumber ? number : 1;
                game::GunState gun;
                game::CountShots(who, gun);
                int before = game::AnimAction(who);
                bool out = (game::StanceFlags(who) & coop::Pose::kWeaponOut) != 0;
                if (!game::Reload(who, type))
                {
                    Say("Couldn't: %08X holds %08X; its reload slot points at %08X (the game's is %08X)",
                        game::RefId(who), gun.weapon, static_cast<unsigned>(game::ReloadFunction(who)),
                        static_cast<unsigned>(game::addr::kActorReload));
                    return;
                }
                game::GunState after;
                game::CountShots(who, after);
                Say("%08X reloads %08X (type %d, weapon %s): animation action %d -> %d, rounds %d -> %d "
                    "(coop inspect on it logs the animation)", game::RefId(who), gun.weapon, type,
                    out ? "out" : "away", before, game::AnimAction(who), gun.rounds, after.rounds);
            }
            else if (_stricmp(action, "stand") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!game::IsActor(who) || who == game::Player())
                {
                    Say("Select an NPC (sitting), or start coop mirror first");
                    return;
                }
                int way = hasNumber ? number : 0;
                uint8_t flags;
                int before, after;
                game::Stance(who, flags, before);
                coop::Pose at;
                bool move = way < 10 && game::GetPose(who, at);
                if (move)
                {
                    game::BringToPlayer(who);
                    game::Move(who, at);
                }
                game::ResetAnimation(who, way % 10);
                game::Stance(who, flags, after);
                Say("%08X: %s, way %d; sitting %d -> %d, animation action %d (coop inspect on it logs what follows)",
                    game::RefId(who), move ? "moved out" : "not moved", way % 10, before, after,
                    game::AnimAction(who));
            }
            else if (_stricmp(action, "copyof") == 0)
            {
                if (!game::IsActor(selected) || selected == game::Player())
                {
                    Say("Select an NPC or creature first");
                    return;
                }
                game::BaseInfo b = game::DescribeBase(selected);
                uint32_t from = !hasNumber || number == 1 ? b.leveledBase : number == 2 ? b.leveledActor : 0;
                if (!from)
                    from = b.base;
                TESObjectREFR* made = game::PlaceActor(from);
                if (!made)
                {
                    Say("Couldn't place %08X (not an NPC, creature or levelled list here)", from);
                    return;
                }
                game::BaseInfo m = game::DescribeBase(made);
                Say("Placed %08X %s from %08X, next to you (base %08X, levelled from %08X, actor base %08X); the "
                    "selected one is %s (base %08X)", game::RefId(made), game::Name(made), from, m.base, m.leveledBase,
                    m.leveledActor, game::Name(selected), b.base);
            }
            else if (_stricmp(action, "spawntest") == 0)
            {
                int count = hasNumber && number > 0 && number <= 10 ? number : 4;
                coop::Pose me;
                game::GetPose(game::Player(), me);
                int made = 0;
                for (int i = 0; i < count; ++i)
                {
                    TESObjectREFR* a = game::PlaceActor(game::ids::kLvlPowderGanger);
                    if (!a)
                        continue;
                    ++made;
                    if (g_spawnTestMoves)
                    {
                        coop::Pose to = me;
                        to.x += 600 + 80 * i;
                        game::Move(a, to);
                    }
                }
                Say("Placed %d Powder Gangers %s", made, g_spawnTestMoves ? "and moved each at once (way 0)" :
                    "without moving them (way 1)");
            }
            else if (_stricmp(action, "copysteps") == 0)
            {
                uint32_t steps = hasNumber ? static_cast<uint32_t>(number) & 15 : 15;
                game::Npcs::LabCopySteps(steps);
                Say("Copies of a host's NPCs made while playing: strip %s, dress %s, weapon %s, fighting off %s",
                    steps & 1 ? "on" : "off", steps & 2 ? "on" : "off", steps & 4 ? "on" : "off", steps & 8 ? "on" : "off");
            }
            else if (_stricmp(action, "spawnway") == 0)
            {
                g_spawnTestMoves = !hasNumber || number == 0;
                Say("coop spawntest will %s", g_spawnTestMoves ? "move each at once (way 0)" : "leave them (way 1)");
            }
            else if (_stricmp(action, "evp") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!game::IsActor(who) || who == game::Player())
                {
                    Say("Select an NPC, or start coop mirror first");
                    return;
                }
                game::EvaluatePackage(who);
                Say("%08X picks its AI routine afresh (the inspector logs which)", game::RefId(who));
            }
            else if (_stricmp(action, "doors") == 0)
            {
                std::vector<TESObjectREFR*> doors;
                LARGE_INTEGER t0, t1, freq;
                QueryPerformanceCounter(&t0);
                game::DoorsNear(doors, 8192);
                std::vector<game::DoorInfo> infos;
                for (TESObjectREFR* d : doors)
                    infos.push_back(game::ReadDoor(d));
                QueryPerformanceCounter(&t1);
                QueryPerformanceFrequency(&freq);
                Say("%u doors near you (finding and reading them took %.3f ms):", static_cast<unsigned>(doors.size()),
                    1000.0 * (t1.QuadPart - t0.QuadPart) / freq.QuadPart);
                for (size_t i = 0; i < doors.size(); ++i)
                {
                    const game::DoorInfo& d = infos[i];
                    Say("  %u: %08X open state %d, %s (level %d)%s", static_cast<unsigned>(i), game::RefId(doors[i]), d.open,
                        d.locked ? "locked" : "unlocked", d.lockLevel, d.leadsAway ? ", leads away" : "");
                }
            }
            else if (_stricmp(action, "boxes") == 0 || _stricmp(action, "openbox") == 0 ||
                _stricmp(action, "lock") == 0 || _stricmp(action, "unlock") == 0)
            {
                std::vector<TESObjectREFR*> boxes;
                game::ContainersNear(boxes, 8192);
                bool lock = _stricmp(action, "lock") == 0;
                if (lock || _stricmp(action, "unlock") == 0)
                {
                    if (!hasNumber || number < 0 || static_cast<size_t>(number) >= boxes.size())
                        return Say("coop lock|unlock K: K is a container's number from coop boxes");
                    game::SetDoorLocked(boxes[number], lock, 50);
                    return Say("Container %08X %s", game::RefId(boxes[number]), lock ? "locked" : "unlocked");
                }
                if (_stricmp(action, "openbox") == 0)
                {
                    if (!hasNumber || number < 0 || static_cast<size_t>(number) >= boxes.size())
                    {
                        Say("coop openbox K: K is a container's number from coop boxes");
                        return;
                    }
                    game::Activate(boxes[number]);
                    Say("Opened container %08X", game::RefId(boxes[number]));
                    return;
                }
                Say("%u containers near you:", static_cast<unsigned>(boxes.size()));
                for (size_t i = 0; i < boxes.size() && i < 40; ++i)
                {
                    game::DoorInfo lock = game::ReadDoor(boxes[i]);
                    Say("  %u: %08X (base %08X)%s (GetLocked %d)", static_cast<unsigned>(i), game::RefId(boxes[i]),
                        boxes[i]->baseForm ? boxes[i]->baseForm->refID : 0, lock.locked ? ", locked" : "",
                        game::GameSaysLocked(boxes[i]));
                }
            }
            else if (_stricmp(action, "mines") == 0 || _stricmp(action, "mineuse") == 0 ||
                _stricmp(action, "minego") == 0)
            {
                std::vector<TESObjectREFR*> mines;
                game::MinesNear(mines, 8192);
                TESObjectREFR* player = game::Player();
                if (_stricmp(action, "mines") != 0)
                {
                    if (!hasNumber || number < 0 || static_cast<size_t>(number) >= mines.size() || !player)
                        return Say("coop mineuse|minego K: K is a mine's number from coop mines");
                    TESObjectREFR* mine = mines[number];
                    if (_stricmp(action, "minego") == 0)
                        game::MoveOnto(player, mine);
                    else
                        game::Activate(mine);
                    return Say("%s mine %08X", _stricmp(action, "minego") == 0 ? "Stepped onto" : "Used",
                        game::RefId(mine));
                }
                Say("%u mines near you:", static_cast<unsigned>(mines.size()));
                for (size_t i = 0; i < mines.size() && i < 40; ++i)
                {
                    TESObjectREFR* m = mines[i];
                    int state = game::MineState(m);
                    float dx = player ? m->posX - player->posX : 0, dy = player ? m->posY - player->posY : 0;
                    Say("  %u: %08X (base %08X) %s, %.0f away%s", static_cast<unsigned>(i), game::RefId(m),
                        m->baseForm ? m->baseForm->refID : 0,
                        state == net::kMineOff ? "disarmed" : state == net::kMineGone ? "gone" : "armed",
                        std::sqrt(dx * dx + dy * dy), net::IsMadeWhilePlaying(game::RefId(m)) ? " (thrown here)" : "");
                }
            }
            else if (_stricmp(action, "box") == 0 || _stricmp(action, "boxadd") == 0 || _stricmp(action, "boxgun") == 0)
            {
                TESObjectREFR* box = game::OpenContainer();
                if (!box)
                {
                    Say("No container screen is open");
                    return;
                }
                if (_stricmp(action, "boxgun") == 0)
                {
                    net::ItemCount gun{game::ids::k9mmPistolWeapon, hasNumber && number ? number : 1, 47, 3};
                    game::ChangeItem(box, gun);
                    game::RefreshItemList();
                    Say("%d worn, modded 9mm Pistols %s %08X", gun.count < 0 ? -gun.count : gun.count,
                        gun.count < 0 ? "out of" : "into", game::RefId(box));
                }
                if (_stricmp(action, "boxadd") == 0)
                {
                    int n = hasNumber ? number : 1;
                    game::ChangeItem(box, game::ids::kStimpak, n);
                    game::RefreshItemList();
                    Say("%d stimpaks %s %08X, list refreshed", n < 0 ? -n : n, n < 0 ? "out of" : "into",
                        game::RefId(box));
                }
                std::vector<game::Item> items;
                LARGE_INTEGER t0, t1, freq;
                QueryPerformanceCounter(&t0);
                bool ok = game::Contents(box, items);
                QueryPerformanceCounter(&t1);
                QueryPerformanceFrequency(&freq);
                Say("Container screen shows %08X (base %08X): %s, %u kinds of items (reading took %.3f ms)",
                    game::RefId(box), box->baseForm ? box->baseForm->refID : 0, ok ? "read" : "COULDN'T READ",
                    static_cast<unsigned>(items.size()), 1000.0 * (t1.QuadPart - t0.QuadPart) / freq.QuadPart);
                for (const game::Item& item : items)
                    if (item.health == net::kNoCondition)
                        Say("  %08X x%d", item.id, item.count);
                    else
                        Say("  %08X x%d at %u%%, mods %u", item.id, item.count, item.health, item.mods);
            }
            else if (_stricmp(action, "body") == 0)
            {
                TESObjectREFR* who = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                if (!who || !game::IsActor(who))
                    return Say("coop body N: an actor's reference id, decimal (or select one)");
                game::BaseInfo b = game::DescribeBase(who);
                Say("%08X %s (base %08X, levelled from %08X): %s", game::RefId(who), game::Name(who), b.base,
                    b.leveledBase, game::IsDead(who) ? "dead" : "alive");
                for (uint32_t id : game::Gear(who, false))
                    Say("  wears %08X", id);
                for (const net::WornItem& w : game::WornGear(who))
                    Say("  armour %08X at %u%%", w.item, w.health);
                std::vector<game::Item> items;
                if (!game::Contents(who, items))
                    return Say("  COULDN'T READ its items");
                for (const game::Item& item : items)
                    if (item.health == net::kNoCondition)
                        Say("  has %08X x%d", item.id, item.count);
                    else
                        Say("  has %08X x%d at %u%%, mods %u", item.id, item.count, item.health, item.mods);
            }
            else if (_stricmp(action, "where") == 0)
            {
                TESObjectREFR* ref = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                if (!ref)
                    return Say("coop where N: a reference's id, decimal (or select one)");
                game::WorldItem w = game::ReadWorldItem(ref);
                float x = 0, y = 0, z = 0;
                bool loaded = game::Get3DPosition(ref, x, y, z);
                Say("%08X at %.0f %.0f %.0f, turned %.0f %.0f %.0f; its 3D %s %.0f %.0f %.0f", game::RefId(ref), w.x, w.y,
                    w.z, w.rx * 57.29578f, w.ry * 57.29578f, w.rz * 57.29578f, loaded ? "at" : "not loaded", x, y, z);
                if (game::IsActor(ref))
                    Say("  %s, health %.0f", game::IsDead(ref) ? "dead" : "alive", game::Health(ref));
            }
            else if (_stricmp(action, "stopsay") == 0)
            {
                TESObjectREFR* ref = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                Say("Stopping %08X: %s", game::RefId(ref), game::StopSpeaking(ref) ? "it was talking" : "nothing kept");
            }
            else if (_stricmp(action, "radio") == 0)
            {
                TESObjectREFR* ref = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                Say("Pip-Boy radio %s %08X", game::TunePipboyRadio(ref) ? "tuned to" : "can't tune to", game::RefId(ref));
            }
            else if (_stricmp(action, "activate") == 0)
            {
                TESObjectREFR* ref = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                if (!ref)
                    return Say("coop activate N: a reference's id, decimal (or select one)");
                game::Activate(ref);
                Say("Activated %08X", game::RefId(ref));
            }
            else if (_stricmp(action, "party") == 0)
            {
                std::string list;
                for (uint32_t id : game::Followers())
                    list += std::string(" ") + game::Name(game::LookupRef(id)) + " (" + std::to_string(id) + ")";
                Say("Following us:%s", list.empty() ? " nobody" : list.c_str());
            }
            else if (_stricmp(action, "hide") == 0 || _stricmp(action, "show") == 0)
            {
                TESObjectREFR* ref = hasNumber ? game::LookupRef(static_cast<uint32_t>(number)) : selected;
                if (!ref)
                    return Say("coop hide N / coop show N: a reference's id, decimal (or select one)");
                game::SetEnabled(ref, _stricmp(action, "show") == 0);
                Say("%08X switched %s here", game::RefId(ref), _stricmp(action, "show") == 0 ? "on" : "off");
            }
            else if (_stricmp(action, "items") == 0 || _stricmp(action, "itempick") == 0)
            {
                std::vector<TESObjectREFR*> items;
                LARGE_INTEGER t0, t1, freq;
                QueryPerformanceCounter(&t0);
                game::ItemsNear(items, 8192);
                QueryPerformanceCounter(&t1);
                QueryPerformanceFrequency(&freq);
                if (_stricmp(action, "itempick") == 0)
                {
                    if (!hasNumber || number < 0 || static_cast<size_t>(number) >= items.size())
                    {
                        Say("coop itempick K: K is an item's number from coop items");
                        return;
                    }
                    TESObjectREFR* item = items[number];
                    game::WorldItem before = game::ReadWorldItem(item);
                    uint32_t id = game::RefId(item);
                    game::Activate(item);
                    game::WorldItem after = game::ReadWorldItem(game::LookupRef(id));
                    Say("Picked up %08X: flags %08X -> %08X, in a cell %d -> %d, still found by id: %s", id,
                        before.flags, after.flags, before.inCell, after.inCell, game::LookupRef(id) ? "yes" : "no");
                    return;
                }
                coop::Pose me;
                game::GetPose(game::Player(), me);
                Say("%u items near you (finding them took %.3f ms); the 25 nearest:", static_cast<unsigned>(items.size()),
                    1000.0 * (t1.QuadPart - t0.QuadPart) / freq.QuadPart);
                std::vector<std::pair<float, size_t>> nearest;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    float dx = items[i]->posX - me.x, dy = items[i]->posY - me.y;
                    nearest.push_back({dx * dx + dy * dy, i});
                }
                std::sort(nearest.begin(), nearest.end());
                for (size_t n = 0; n < nearest.size() && n < 25; ++n)
                {
                    size_t i = nearest[n].second;
                    game::WorldItem w = game::ReadWorldItem(items[i]);
                    Say("  %u: %08X base %08X x%d flags %08X %s at %.0f %.0f %.0f (%.0f away), turned %.0f %.0f %.0f",
                        static_cast<unsigned>(i), game::RefId(items[i]), w.base, w.count, w.flags,
                        w.inCell ? "in a cell" : "NO CELL", w.x, w.y, w.z, std::sqrt(nearest[n].first),
                        items[i]->rotX * 57.29578f, items[i]->rotY * 57.29578f, items[i]->rotZ * 57.29578f);
                    if (const auto* node = reinterpret_cast<const uint8_t*>(items[i]->GetNiNode()))
                    {
                        const auto* m = reinterpret_cast<const float*>(node + game::layout::kNodeWorldRotate);
                        const auto* p = reinterpret_cast<const float*>(node + game::layout::kNodeWorldTranslate);
                        Say("     3D at %.0f %.0f %.0f, rotation %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f", p[0],
                            p[1], p[2], m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
                    }
                }
            }
            else if (_stricmp(action, "quests") == 0 || _stricmp(action, "questpick") == 0)
            {
                std::vector<game::QuestInfo> quests;
                LARGE_INTEGER t0, t1, freq;
                QueryPerformanceCounter(&t0);
                game::ReadQuests(quests, true);
                QueryPerformanceCounter(&t1);
                QueryPerformanceFrequency(&freq);
                if (_stricmp(action, "questpick") == 0)
                {
                    if (!hasNumber || number < 0 || static_cast<size_t>(number) >= quests.size())
                    {
                        Say("coop questpick K: K is a quest's number from coop quests");
                        return;
                    }
                    g_pickedQuest = quests[number].id;
                    Say("Picked quest %08X %s", g_pickedQuest, quests[number].name.c_str());
                    return;
                }
                size_t started = 0;
                for (const game::QuestInfo& q : quests)
                    started += (q.flags & 0x63) || !q.done.empty();
                Say("%u of %u quests started (reading them all took %.3f ms); tracking %08X; started ones, numbered as in "
                    "the full list (coop questsall):", static_cast<unsigned>(started), static_cast<unsigned>(quests.size()),
                    1000.0 * (t1.QuadPart - t0.QuadPart) / freq.QuadPart, game::TrackedQuest());
                for (size_t i = 0; i < quests.size(); ++i)
                {
                    const game::QuestInfo& q = quests[i];
                    if (!(q.flags & 0x63) && q.done.empty())
                        continue;
                    std::string done, objectives;
                    for (uint8_t st : q.done)
                        done += std::to_string(st) + " ";
                    for (auto& [id, status] : q.objectives)
                        if (status)
                            objectives += std::to_string(id) + ":" + std::to_string(status) + " ";
                    Say("  %u: %08X %s flags %02X stage %u delay %.1f; done [%s] objectives [%s]", static_cast<unsigned>(i),
                        q.id, q.name.c_str(), q.flags, q.stage, q.delay, done.c_str(), objectives.c_str());
                }
            }
            else if (_stricmp(action, "queststage") == 0 || _stricmp(action, "questobj") == 0 ||
                _stricmp(action, "questobjdone") == 0 || _stricmp(action, "questflags") == 0)
            {
                if (!g_pickedQuest || !hasNumber || number < 0)
                {
                    Say("coop questpick K first, then give a number");
                    return;
                }
                if (_stricmp(action, "queststage") == 0)
                    game::SetQuestStageDone(g_pickedQuest, static_cast<uint8_t>(number), true);
                else if (_stricmp(action, "questobj") == 0)
                    game::SetQuestObjective(g_pickedQuest, static_cast<uint32_t>(number), 1);
                else if (_stricmp(action, "questobjdone") == 0)
                    game::SetQuestObjective(g_pickedQuest, static_cast<uint32_t>(number), 3);
                else
                    game::SetQuestFlags(g_pickedQuest, static_cast<uint8_t>(number));
                game::QuestInfo q;
                if (game::ReadQuest(g_pickedQuest, q))
                {
                    std::string done, objectives;
                    for (uint8_t st : q.done)
                        done += std::to_string(st) + " ";
                    for (auto& [id, status] : q.objectives)
                        objectives += std::to_string(id) + ":" + std::to_string(status) + " ";
                    Say("Quest %08X %s now flags %02X stage %u; done [%s] objectives [%s]", q.id, q.name.c_str(), q.flags,
                        q.stage, done.c_str(), objectives.c_str());
                }
            }
            else if (_stricmp(action, "questshow") == 0 || _stricmp(action, "questcomplete") == 0)
            {
                bool ok = _stricmp(action, "questshow") == 0 ?
                    game::ShowObjective(g_pickedQuest, static_cast<uint32_t>(number), true) :
                    game::CompleteObjective(g_pickedQuest, static_cast<uint32_t>(number), true);
                Say("Objective %d of %08X the game's way: %s", number, g_pickedQuest, ok ? "done" : "FAILED");
            }
            else if (_stricmp(action, "questlog") == 0)
            {
                size_t targets = 0;
                auto objectives = game::PlayerObjectives(targets);
                std::string text;
                for (auto& [quest, id, status] : objectives)
                    text += std::to_string(id) + "@" + std::to_string(quest) + ":" + std::to_string(status) + " ";
                Say("Player's objective list (%zu; %zu targets): %s", objectives.size(), targets, text.c_str());
            }
            else if (_stricmp(action, "questsall") == 0)
            {
                std::vector<game::QuestInfo> quests;
                game::ReadQuests(quests, true);
                std::string line;
                for (size_t i = 0; i < quests.size(); ++i)
                {
                    line += std::to_string(i) + ":" + quests[i].name + " ";
                    if (line.size() > 400 || i + 1 == quests.size())
                    {
                        Say("%s", line.c_str());
                        line.clear();
                    }
                }
            }
            else if (_stricmp(action, "questvars") == 0)
            {
                std::string text;
                for (auto& [id, value] : game::QuestVariables(g_pickedQuest))
                    text += std::to_string(id) + "=" + std::to_string(value) + " ";
                Say("Quest %08X script %p variables: %s", g_pickedQuest, game::QuestScript(g_pickedQuest), text.c_str());
            }
            else if (_stricmp(action, "questscript") == 0)
            {
                if (!g_pickedQuest || !hasNumber)
                    return Say("coop questpick K first; coop questscript 0 detaches its script, 1 puts it back");
                if (number == 0 && !g_detachedScript)
                    g_detachedScript = game::SwapQuestScript(g_pickedQuest, nullptr);
                else if (number == 1 && g_detachedScript)
                {
                    game::SwapQuestScript(g_pickedQuest, g_detachedScript);
                    g_detachedScript = nullptr;
                }
                Say("Quest %08X script now %p", g_pickedQuest, game::QuestScript(g_pickedQuest));
            }
            else if (_stricmp(action, "targets") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                int fighting = 0;
                for (TESObjectREFR* a : actors)
                    if (uint32_t target = a != game::Player() && !game::IsDead(a) ? game::CombatTarget(a) : 0)
                    {
                        TESObjectREFR* t = game::LookupRef(target);
                        Say("%08X %s goes for %08X %s", game::RefId(a), game::Name(a), target, t ? game::Name(t) : "?");
                        ++fighting;
                    }
                Say("%d fighting", fighting);
            }
            else if (_stricmp(action, "pile") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                TESObjectREFR* nearest = nullptr;
                for (bool living : {true, false})
                {
                    for (TESObjectREFR* a : actors)
                        if (a != game::Player() && std::strstr(game::Name(a), "Powder Ganger") &&
                            game::IsDead(a) != living &&
                            (!nearest || game::DistanceToPlayer(a) < game::DistanceToPlayer(nearest)))
                            nearest = a;
                    if (nearest)
                        break;
                }
                if (!nearest)
                    return Say("No Powder Ganger near you");
                if (!game::IsDead(nearest))
                    game::Kill(nearest, 0);
                uint8_t kind = static_cast<uint8_t>(hasNumber && number >= 1 && number <= 3 ? number : 1);
                game::MakePile(nearest, kind);
                Say("%08X turned to %s", game::RefId(nearest), kind == 1 ? "goo" : "ash");
            }
            else if (_stricmp(action, "fight") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                int started = 0;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && std::strstr(game::Name(a), "Powder Ganger"))
                    {
                        char line[64];
                        if (hasNumber && number == 1)
                        {
                            std::snprintf(line, sizeof(line), "%08X.RemoveAllItems", game::RefId(a));
                            game::RunConsoleLine(line);
                            std::snprintf(line, sizeof(line), "%08X.AddItem 000BA0F3 20", game::RefId(a));
                            game::RunConsoleLine(line);
                            std::snprintf(line, sizeof(line), "%08X.EquipItem 000BA0F3", game::RefId(a));
                            game::RunConsoleLine(line);
                        }
                        else if (hasNumber && number == 2)
                        {
                            std::snprintf(line, sizeof(line), "%08X.AddItem 00004328 1", game::RefId(a));
                            game::RunConsoleLine(line);
                            std::snprintf(line, sizeof(line), "%08X.EquipItem 00004328 1", game::RefId(a));
                            game::RunConsoleLine(line);
                        }
                        std::snprintf(line, sizeof(line), "%08X.StartCombat player", game::RefId(a));
                        started += game::RunConsoleLine(line) ? 1 : 0;
                    }
                Say("%d Powder Gangers attack you%s", started, !hasNumber ? "" : number == 1 ? ", with dynamite only" :
                    number == 2 ? ", with tire irons only" : "");
            }
            else if (_stricmp(action, "draw") == 0)
            {
                game::SetWeaponOut(game::Player(), !hasNumber || number != 0);
                Say("Our weapon %s", !hasNumber || number != 0 ? "out" : "away");
            }
            else if (_stricmp(action, "draglimits") == 0)
            {
                bool* alwaysRun = game::PlayerAlwaysRun();
                bool was = alwaysRun && *alwaysRun;
                LOG_INFO("Lab: always-run %d", was ? 1 : 0);
                if (!hasNumber)
                    return Say("Always-run %d", was ? 1 : 0);
                game::SetDragging(number != 0);
                Say("Dragging limits %s; always-run was %d, now %d", number ? "on" : "off", was ? 1 : 0,
                    alwaysRun && *alwaysRun ? 1 : 0);
            }
            else if (_stricmp(action, "lookat") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!who)
                    who = game::LookupRef(g_dummy);
                coop::Pose me, at;
                if (!who || !game::GetPose(game::Player(), me) || !game::GetPose(who, at))
                    return Say("Nothing to look at");
                game::Turn(game::Player(), std::atan2(at.x - me.x, at.y - me.y));
                Say("Facing %08X", game::RefId(who));
            }
            else if (_stricmp(action, "arm") == 0 || _stricmp(action, "throw") == 0 || _stricmp(action, "letgo") == 0)
            {
                TESObjectREFR* who = selected ? selected : game::LookupRef(g_mirror->Copier());
                if (!who)
                    who = game::LookupRef(g_dummy);
                if (!game::IsActor(who) || who == game::Player())
                    return Say("Select an NPC, or make a lab dummy (coop spawn) first");
                if (_stricmp(action, "arm") == 0)
                {
                    if (!hasNumber || !game::AddAndEquip(who, static_cast<uint32_t>(number)))
                        return Say("Arm with what? coop arm N (a weapon's form id, decimal)");
                    g_armed = static_cast<uint32_t>(number);
                    coop::Pose me, at;
                    if (game::GetPose(game::Player(), me) && game::GetPose(who, at))
                        game::Turn(who, std::atan2(me.x - at.x, me.y - at.y));
                    game::SetWeaponOut(who, true);
                    Say("%08X holds the harmless copy of %08X, facing you", game::RefId(who), g_armed);
                }
                else if (_stricmp(action, "throw") == 0)
                {
                    game::PlayAttack(who, {g_armed}, 1);
                    Say("%08X attacks with %08X", game::RefId(who), g_armed);
                }
                else
                {
                    game::ReleaseThrow(who, {g_armed});
                    Say("%08X lets go of %08X", game::RefId(who), g_armed);
                }
            }
            else if (_stricmp(action, "face") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                TESObjectREFR* nearest = nullptr;
                float best = 1e30f;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && game::GetPose(a, at))
                    {
                        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                        if (d < best)
                        {
                            best = d;
                            nearest = a;
                        }
                    }
                const char* process = nearest ? *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(nearest) + 0x68) : nullptr;
                const float* face = process ? *reinterpret_cast<const float* const*>(process + 0x178) : nullptr;
                if (!face)
                    return Say("No face animation data on the nearest NPC");
                uint32_t now = GetTickCount();
                float seconds = g_faceWasAt ? (now - g_faceWasAt) / 1000.0f : 0;
                std::string changed;
                for (int i = 0; i < 256; ++i)
                {
                    float v = face[i];
                    if (seconds > 0 && v != g_faceWas[i] && v == v && v > -1e6f && v < 1e6f)
                    {
                        char b[64];
                        snprintf(b, sizeof(b), "+%X:%.3f(%+.3f/s) ", i * 4, v, (v - g_faceWas[i]) / seconds);
                        changed += b;
                    }
                    g_faceWas[i] = v;
                }
                g_faceWasAt = now;
                Say("%s's face data %p: changed in %.2f s: %s", game::Name(nearest), static_cast<const void*>(face), seconds,
                    changed.c_str());
            }
            else if (_stricmp(action, "watchface") == 0 || _stricmp(action, "watchlog") == 0)
            {
                if (_stricmp(action, "watchlog") == 0)
                {
                    LONG n = g_hitCount < 30 ? g_hitCount : 30;
                    Say("Watch: %ld writes", static_cast<long>(g_hitCount));
                    for (LONG i = 0; i < n; ++i)
                    {
                        std::string chain;
                        char b[16];
                        for (uint32_t a : g_hits[i].stack)
                            if (a)
                            {
                                snprintf(b, sizeof(b), " %06X", a);
                                chain += b;
                            }
                        Say("  %+8.2f ms clock %.4f  %08X <-%s", g_hits[i].ms - g_hits[0].ms, g_hits[i].after, g_hits[i].eip,
                            chain.c_str());
                    }
                    g_watchAddress = 0;
                    HANDLE h = CreateThread(nullptr, 0, SetWatch, nullptr, 0, nullptr);
                    if (h)
                    {
                        WaitForSingleObject(h, 2000);
                        CloseHandle(h);
                    }
                    return;
                }
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                TESObjectREFR* nearest = nullptr;
                float best = 1e30f;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && game::GetPose(a, at))
                    {
                        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                        if (d < best)
                        {
                            best = d;
                            nearest = a;
                        }
                    }
                const char* process = nearest ? *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(nearest) + 0x68) : nullptr;
                const char* face = process ? *reinterpret_cast<const char* const*>(process + 0x178) : nullptr;
                if (!face)
                    return Say("No face animation data on the nearest NPC");
                if (!g_watchHandler)
                    g_watchHandler = AddVectoredExceptionHandler(1, WatchHandler);
                g_hitCount = 0;
                g_gameThread = GetCurrentThreadId();
                g_watchAddress = reinterpret_cast<uintptr_t>(face) + 0x9C;
                g_watchFloat = reinterpret_cast<const float*>(g_watchAddress);
                HANDLE h = CreateThread(nullptr, 0, SetWatch, nullptr, 0, nullptr);
                if (h)
                {
                    WaitForSingleObject(h, 2000);
                    CloseHandle(h);
                }
                Say("Watching writes to %s's lip clock at %08X (coop watchlog to see them)", game::Name(nearest),
                    static_cast<unsigned>(g_watchAddress));
            }
            else if (_stricmp(action, "open") == 0)
            {
                TESObjectREFR* open = game::OpenContainer();
                std::vector<game::Item> items;
                bool read = open && game::Contents(open, items);
                int32_t caps = 0;
                for (const game::Item& i : items)
                    if (i.id == 0xF)
                        caps = i.count;
                Say("Open: %08X (%s), %zu kinds of items, %d caps", game::RefId(open), read ? "read" : "not read",
                    items.size(), caps);
            }
            else if (_stricmp(action, "spoken") == 0)
            {
                game::Spoken s = game::SpokenNow();
                if (s.id)
                {
                    g_lastSpoken = s;
                    g_lastSpokenText = s.text;
                    g_lastSpoken.text = g_lastSpokenText.c_str();
                }
                Say("Saying now (%08X, topic %08X line %08X response %d; partner talking %d): %s",
                    static_cast<unsigned>(s.id), s.topic, s.info, s.response,
                    game::IsTalking(game::DialoguePartner()) ? 1 : 0, s.text);
                if (s.id)
                    Say("  response fields: +08 %08X +0C %08X +24 %08X +28 %08X", Peek(s.id + 0x08),
                        Peek(s.id + 0x0C), Peek(s.id + 0x24), Peek(s.id + 0x28));
            }
            else if (_stricmp(action, "sayagain") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                TESObjectREFR* speaker = nullptr;
                float best = 1e30f;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && !game::IsDead(a) && game::GetPose(a, at))
                    {
                        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                        if (d < best)
                            best = d, speaker = a;
                    }
                if (!speaker || !g_lastSpoken.info)
                    return Say("coop spoken in a conversation first, then near someone: coop sayagain [response]");
                g_pendingSayer = game::RefId(speaker);
                g_pendingResponse = hasNumber ? number : 0;
                Say("%s will say line %08X next frame", game::Name(speaker), g_lastSpoken.info);
            }
            else if (_stricmp(action, "pitch") == 0)
            {
                TESObjectREFR* dummy = game::LookupRef(g_dummy);
                if (!dummy)
                    Say("No dummy (coop spawn first)");
                else if (!hasNumber)
                {
                    g_pitching = false;
                    Say("Pitch: stopped");
                }
                else
                {
                    if (!g_pitching)
                        game::AddAndEquip(dummy, 0x001735E3);
                    coop::Pose at;
                    if (game::GetPose(game::Player(), at))
                    {
                        at.x += 200 * std::sin(at.heading);
                        at.y += 200 * std::cos(at.heading);
                        at.heading += 3.14159265f;
                        game::Move(dummy, at);
                    }
                    game::SetAlert(dummy, true);
                    game::SetWeaponOut(dummy, true);
                    g_pitch = static_cast<float>(number);
                    g_pitching = true;
                    Say("Pitch: dummy holds %d degrees", number);
                }
            }
            else if (_stricmp(action, "pitchshot") == 0)
            {
                TESObjectREFR* dummy = game::LookupRef(g_dummy);
                if (!dummy)
                {
                    Say("No dummy (coop spawn, coop pitch N first)");
                    return;
                }
                game::LogAimSlots(dummy);
                game::PlayAttack(dummy, {0x001735E3}, 1);
                game::LogAimSlots(dummy);
                Say("Pitch: dummy shot (aim slots in NVCoop.log)");
            }
            else if (_stricmp(action, "aimslots") == 0)
            {
                if (TESObjectREFR* dummy = game::LookupRef(g_dummy))
                    game::LogAimSlots(dummy);
                Say("Aim slots logged");
            }
            else if (_stricmp(action, "waittest") == 0)
            {
                auto* player = reinterpret_cast<uint8_t*>(game::Player());
                int hours = hasNumber && number > 0 && number <= 24 ? number : 2;
                *reinterpret_cast<int32_t*>(player + 0x654) = hours + 1;
                *reinterpret_cast<uint8_t*>(player + 0x658) = 0;
                Say("Wait test: %d hours asked", hours);
            }
            else if (_stricmp(action, "sleepwait") == 0)
            {
                int hours = hasNumber ? number : 2;
                bool sleep = hours < 0;
                hours = hours < 0 ? -hours : hours;
                game::SleepWait::LabPress(static_cast<uint8_t>(hours > 24 ? 24 : hours < 1 ? 1 : hours), sleep);
                Say("Sleep/wait: pressed (NVCoop.log)");
            }
            else if (_stricmp(action, "speedwatch") == 0)
            {
                Say(game::WatchSpeedChanges() ? "Speed changes are logged now" : "Couldn't watch speed changes");
            }
            else if (_stricmp(action, "hooksof") == 0)
            {
                const wchar_t* names[] = {L"", L"FloatingSubtitlesNVSE.dll", L"itr-nvse.dll", L"johnnyguitar.dll",
                    L"jip_nvse.dll", L"RealTimeMenus.dll"};
                int which = hasNumber && number >= 1 && number <= 5 ? number : 1;
                HMODULE module = GetModuleHandleW(names[which]);
                MODULEINFO info = {};
                if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
                    return Say("That plugin isn't loaded");
                uintptr_t lo = reinterpret_cast<uintptr_t>(info.lpBaseOfDll), hi = lo + info.SizeOfImage;
                int found = 0;
                for (uintptr_t at = 0x401000; at < 0x00FE0000; ++at)
                {
                    uint8_t op = *reinterpret_cast<const uint8_t*>(at);
                    if (op != 0xE8 && op != 0xE9)
                        continue;
                    int32_t rel;
                    std::memcpy(&rel, reinterpret_cast<const void*>(at + 1), 4);
                    uintptr_t to = at + 5 + rel;
                    if (to >= lo && to < hi)
                    {
                        ++found;
                        LOG_INFO("Lab: %08X %s %ls+%X", static_cast<unsigned>(at), op == 0xE8 ? "calls" : "jumps to",
                            names[which], static_cast<unsigned>(to - lo));
                    }
                }
                Say("%d calls and jumps into %ls (see the log)", found, names[which]);
            }
            else if (_stricmp(action, "notify") == 0)
            {
                if (hasNumber && number == 1)
                    game::Notify("Co-op lab: a timed message", 3.0f);
                else
                    game::Notify("Co-op lab: a message");
                Say("Shown");
            }
            else if (_stricmp(action, "lastline") == 0)
            {
                g_lastSpoken = game::Spoken{};
                g_lastSpoken.info = hasNumber ? static_cast<uint32_t>(number) : 0;
                g_lastSpoken.topic = 0xC8;
                g_lastSpokenText.clear();
                g_lastSpoken.text = g_lastSpokenText.c_str();
                Say("Line %08X (topic GREETING) set for coop sayagain", g_lastSpoken.info);
            }
            else if (_stricmp(action, "saystop") == 0)
            {
                bool playing = game::IsPlaying(g_labSound);
                game::StopSound(g_labSound);
                if (TESObjectREFR* chet = game::LookupRef(g_pendingSayerWas))
                    playing = game::StopSpeaking(chet) || playing;
                if (g_lastSpoken.text)
                    game::EndSubtitle(g_lastSpoken.text);
                Say("Stopped (it was %s)", playing ? "playing" : "not playing");
            }
            else if (_stricmp(action, "dialogmem") == 0)
            {
                uint32_t menu = Peek(game::addr::kDialogMenu);
                std::string out;
                char b[64];
                for (uint32_t off = 0x60; off < 0x110; off += 4)
                {
                    snprintf(b, sizeof(b), "+%X=%08X ", off, Peek(menu + off));
                    out += b;
                }
                Say("DialogMenu %08X: %s", menu, out.c_str());
                uint32_t node = Peek(Peek(menu + 0x70)), topic = Peek(node), next = Peek(node + 4);
                Say("node %08X: item %08X next %08X", node, topic, next);
                out.clear();
                for (uint32_t off = 0; off < 0x2C; off += 4)
                {
                    snprintf(b, sizeof(b), "+%X=%08X ", off, Peek(topic + off));
                    out += b;
                }
                Say("topic %08X: %s", topic, out.c_str());
                uint32_t rnode = Peek(topic + 0x20), response = Peek(rnode);
                out.clear();
                for (uint32_t off = 0; off < 0x2C; off += 4)
                {
                    snprintf(b, sizeof(b), "+%X=%08X ", off, Peek(response + off));
                    out += b;
                }
                Say("response node %08X item %08X: %s", rnode, response, out.c_str());
                uint32_t text = Peek(response);
                char said[120] = {};
                for (int i = 0; i + 1 < static_cast<int>(sizeof(said)) && text != 0xDEAD0BAD; i += 4)
                {
                    uint32_t v = Peek(text + i);
                    if (v == 0xDEAD0BAD)
                        break;
                    memcpy(said + i, &v, (sizeof(said) - 1 - i) < 4 ? sizeof(said) - 1 - i : 4);
                }
                Say("text: %s", said);
                for (uint32_t n = topic + 0x0C, i = 0; n && n != 0xDEAD0BAD && i < 8; n = Peek(n + 4), ++i)
                {
                    uint32_t r = Peek(n);
                    uint32_t t = Peek(r);
                    char line[80] = {};
                    for (int k = 0; k + 1 < static_cast<int>(sizeof(line)) && t != 0xDEAD0BAD; k += 4)
                    {
                        uint32_t v = Peek(t + k);
                        if (v == 0xDEAD0BAD)
                            break;
                        memcpy(line + k, &v, (sizeof(line) - 1 - k) < 4 ? sizeof(line) - 1 - k : 4);
                    }
                    Say("response %u at %08X: %s", i, r, line);
                }
            }
            else if (_stricmp(action, "subtitle") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                TESObjectREFR* speaker = nullptr;
                float best = 1e30f;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && !game::IsDead(a) && game::GetPose(a, at))
                    {
                        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                        if (d < best)
                            best = d, speaker = a;
                    }
                if (!speaker)
                    return Say("No one near you");
                game::ShowSubtitle(speaker, "Doc Mitchell: This is a test subtitle from the co-op lab.");
                Say("Subtitle shown as %s's", game::Name(speaker));
            }
            else if (_stricmp(action, "screenlines") == 0)
            {
                int lines = 0, scripts = 0, whole = 0, screens = 0;
                for (uint32_t id = 1; id < 0x01000000; ++id)
                {
                    void* info = game::TopicInfo(id);
                    if (!info)
                        continue;
                    ++lines;
                    for (int end = 0; end < 2; ++end)
                    {
                        int statements = 0;
                        uint16_t op = game::ScreenCommandIn(info, end != 0, &statements);
                        if (statements != 0)
                            ++scripts;
                        if (statements > 0)
                            ++whole;
                        if (op)
                        {
                            ++screens;
                            if (screens <= 40)
                                LOG_INFO("Lab: line %08X (\"%s\") %s script opens a screen (%04X)", id,
                                    game::TopicInfoPrompt(info).c_str(), end ? "end" : "start", op);
                        }
                    }
                }
                Say("%d lines, %d scripts (%d read as whole statements), %d open a screen", lines, scripts, whole,
                    screens);
            }
            else if (_stricmp(action, "friendline") == 0)
            {
                if (!hasNumber || number == 0)
                    return Say("coop friendline <line id in decimal, negative for its end script>");
                TESObjectREFR* speaker = game::DialoguePartner();
                if (!speaker)
                {
                    std::vector<TESObjectREFR*> actors;
                    game::NearbyActors(actors);
                    coop::Pose me, at;
                    game::GetPose(game::Player(), me);
                    float best = 1e30f;
                    for (TESObjectREFR* a : actors)
                        if (a != game::Player() && !game::IsDead(a) && game::GetPose(a, at))
                        {
                            float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                            if (d < best)
                                best = d, speaker = a;
                        }
                }
                if (!speaker)
                    return Say("No one near you");
                uint32_t info = static_cast<uint32_t>(number < 0 ? -number : number);
                game::Dialogue::LabRun(info, number < 0, game::RefId(speaker));
                Say("Ran line %08X (%s) said by %s, as a friend's (see the log)", info, number < 0 ? "end" : "start",
                    game::Name(speaker));
            }
            else if (_stricmp(action, "greet") == 0 || _stricmp(action, "talk") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                TESObjectREFR* nearest = nullptr;
                float best = 1e30f;
                for (TESObjectREFR* a : actors)
                    if (a != game::Player() && !game::IsDead(a) && game::GetPose(a, at))
                    {
                        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y);
                        if (d < best)
                        {
                            best = d;
                            nearest = a;
                        }
                    }
                if (!nearest)
                    return Say("No one near you");
                if (_stricmp(action, "talk") == 0)
                {
                    game::Activate(nearest);
                    Say("You start a conversation with %s (%08X)", game::Name(nearest), game::RefId(nearest));
                    return;
                }
                game::Greet(nearest);
                Say("%s (%08X) starts a conversation with you", game::Name(nearest), game::RefId(nearest));
            }
            else if (_stricmp(action, "switch") == 0)
            {
                TESObjectREFR* door = g_pickedDoor ? game::LookupRef(g_pickedDoor) : nullptr;
                if (!door || !hasNumber)
                    return Say("coop doorpick K first; coop switch 0 disables that door, 1 enables it (Disable / Enable)");
                game::SetEnabled(door, number != 0);
                Say("Door %08X %s", g_pickedDoor, game::IsEnabled(door) ? "enabled" : "disabled");
            }
            else if (_stricmp(action, "doorpick") == 0)
            {
                std::vector<TESObjectREFR*> doors;
                game::DoorsNear(doors, 8192);
                if (!hasNumber || number < 0 || static_cast<size_t>(number) >= doors.size())
                {
                    Say("coop doorpick K: K is a door's number from coop doors");
                    return;
                }
                g_pickedDoor = game::RefId(doors[number]);
                Say("coop door N now changes door %08X (when none is selected)", g_pickedDoor);
            }
            else if (_stricmp(action, "door") == 0)
            {
                if (!selected && g_pickedDoor)
                    selected = game::LookupRef(g_pickedDoor);
                if (!selected || !selected->baseForm || selected->baseForm->typeID != kFormType_TESObjectDOOR || !hasNumber)
                {
                    Say("Click a door in the console, then: coop door 0 (close), 1 (open), 2 (lock), 3 (unlock)");
                    return;
                }
                game::DoorInfo before = game::ReadDoor(selected);
                if (number == 0 || number == 1)
                    game::SetDoorOpen(selected, number == 1);
                else if (number == 2 || number == 3)
                    game::SetDoorLocked(selected, number == 2, before.lockLevel ? before.lockLevel : 50);
                game::DoorInfo after = game::ReadDoor(selected);
                Say("Door %08X: open state %d -> %d, %s -> %s (level %d -> %d)", game::RefId(selected), before.open,
                    after.open, before.locked ? "locked" : "unlocked", after.locked ? "locked" : "unlocked",
                    before.lockLevel, after.lockLevel);
            }
            else if (_stricmp(action, "npcs") == 0)
            {
                std::vector<TESObjectREFR*> actors;
                game::NearbyActors(actors);
                coop::Pose me, at;
                game::GetPose(game::Player(), me);
                Say("%u actors near you:", static_cast<unsigned>(actors.size()));
                for (TESObjectREFR* a : actors)
                {
                    float distance = -1;
                    if (game::GetPose(a, at) && at.space == me.space)
                        distance = std::sqrt((at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y));
                    Say("  %08X %s: %.0f units away, health %.0f / %.0f, life state %d%s", game::RefId(a),
                        game::Name(a), distance, game::Health(a), game::MaxHealth(a), game::LifeState(a),
                        (game::RefId(a) >> 24) == 0xFF ? " (made while playing)" : "");
                    game::BaseInfo b = game::DescribeBase(a);
                    if (b.leveledBase || b.leveledActor)
                        Say("    base %08X (type %u), levelled from %08X (type %u), actor base %08X (type %u)", b.base,
                            b.baseType, b.leveledBase, b.leveledBaseType, b.leveledActor, b.leveledActorType);
                    else
                        Say("    base %08X (type %u)", b.base, b.baseType);
                }
            }
            else if (_stricmp(action, "release") == 0)
            {
                TESObjectREFR* who = Target(selected);
                if (game::ReleaseMovementFlags(who))
                    Say("%08X follows its path again", game::RefId(who));
                else
                    Say("Couldn't: %08X has a %s mover", game::RefId(who),
                        game::MoverKindName(game::ReadMover(who).kind));
            }
            else
                Help();
        }

        ParamInfo kParams[] = {
            {"action", kParamType_String, 0},
            {"number", kParamType_Integer, 1},
        };

        bool Cmd_Coop_Execute(COMMAND_ARGS)
        {
            *result = 0;
            char action[256] = {};
            int number = INT32_MIN;
            if (!g_scripts->ExtractArgsEx(paramInfo, scriptData, opcodeOffsetPtr, scriptObj, eventList, action, &number))
                return true;
            action[sizeof(action) - 1] = 0;
            Run(action, number, number != INT32_MIN, thisObj);
            return true;
        }

        CommandInfo kCoopCommand = {
            "Coop", "", 0, "NVCoop developer commands (coop help)", 0,
            sizeof(kParams) / sizeof(kParams[0]), kParams, Cmd_Coop_Execute, nullptr, nullptr, 0,
        };
    }

    bool RegisterCommands(NVSEInterface* nvse, game::Inspector* inspector, game::Mirror* mirror)
    {
        g_inspector = inspector;
        g_mirror = mirror;
        g_scripts = static_cast<NVSEScriptInterface*>(nvse->QueryInterface(kInterface_Script));
        nvse->SetOpcodeBase(kNVSEOpcodeTest);
        if (g_scripts && nvse->RegisterCommand(&kCoopCommand))
            return true;
        LOG_ERROR("Couldn't add the coop console command");
        return false;
    }
    void EveryFrame()
    {

        if (g_pitching)
            if (TESObjectREFR* dummy = game::LookupRef(g_dummy))
                game::SetPitch(dummy, g_pitch * 0.0174532925f);
        if (g_pendingSayer)
        {
            TESObjectREFR* speaker = game::LookupRef(g_pendingSayer);
            g_pendingSayerWas = g_pendingSayer;
            g_pendingSayer = 0;
            bool said = speaker && game::SayLine(speaker, g_lastSpoken.topic, g_lastSpoken.info, g_pendingResponse,
                g_labSound);
            LOG_INFO("Lab: %s %s line %08X (sound %08X)", speaker ? game::Name(speaker) : "?",
                said ? "says" : "couldn't say", g_lastSpoken.info, g_labSound.id);
        }
    }
}
