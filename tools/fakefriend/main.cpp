#include "coop/coop.h"
#include "coop/clock.h"
#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    std::atomic<bool> g_quit{false};

    BOOL WINAPI OnCtrlC(DWORD)
    {
        g_quit = true;
        return TRUE;
    }

    constexpr float kWalkDistance = 600;

    struct Phase { const char* name; uint32_t ms; uint8_t flags; float speed; };
    const Phase kPhases[] = {
        {"walking", 6000, 0, 120},
        {"running", 6000, 0, 380},
        {"walking with weapon out", 6000, coop::Pose::kWeaponOut, 120},
        {"standing with weapon out, looking around", 6000, coop::Pose::kWeaponOut, 0},
        {"sneaking", 6000, coop::Pose::kSneaking, 80},
        {"standing still", 4000, 0, 0},
    };

    class Walker
    {
    public:
        Walker(bool still, bool startFar) : m_still(still), m_far(startFar) {}

        void Update(coop::Coop& coop, uint32_t now)
        {
            coop::Pose them;
            if (!FindSomeone(coop, now, them))
                return;
            if (m_still)
            {
                if (!m_hasStart)
                {
                    m_start = Start(them);
                    m_hasStart = true;
                    LOG_INFO("Standing still at (%.0f, %.0f, %.0f)", them.x, them.y, them.z);
                }
                coop.SetMyPose(m_start);
                coop.SetMyGear(m_gear);
                return;
            }
            float dx = them.x - m_start.x, dy = them.y - m_start.y;
            if (!m_hasStart || them.space != m_start.space || (!m_far && dx * dx + dy * dy > 2000.f * 2000.f))
            {
                m_start = Start(them);
                m_hasStart = true;
                m_lastNow = now;
                m_walked = 0;
                LOG_INFO("Walking from (%.0f, %.0f, %.0f) in place %08X", them.x, them.y, them.z, them.space);
            }

            uint32_t cycleMs = 0;
            for (const Phase& p : kPhases)
                cycleMs += p.ms;
            uint32_t t = now % cycleMs;
            size_t phaseIndex = 0;
            while (t >= kPhases[phaseIndex].ms)
                t -= kPhases[phaseIndex++].ms;
            const Phase& phase = kPhases[phaseIndex];
            if (phaseIndex != m_phase)
            {
                m_phase = phaseIndex;
                LOG_INFO("Now %s", phase.name);
            }

            m_walked += (now - m_lastNow) / 1000.f * phase.speed;
            m_lastNow = now;
            if (m_walked > kWalkDistance)
                m_walked = 0;

            coop::Pose me = m_start;
            me.x += m_walked * std::sin(m_start.heading);
            me.y += m_walked * std::cos(m_start.heading);
            if (phase.speed == 0)
                me.heading = std::fmod(me.heading + 0.8f * std::sin(now / 1000.f) + 6.2831853f, 6.2831853f);
            me.flags = phase.flags;
            coop.SetMyPose(me);
            coop.SetMyGear(m_gear);
        }

    private:
        coop::Pose Start(coop::Pose them) const
        {
            if (m_far)
            {
                them.x += 2500 * std::sin(them.heading);
                them.y += 2500 * std::cos(them.heading);
            }
            return them;
        }

        bool FindSomeone(const coop::Coop& coop, uint32_t now, coop::Pose& out)
        {
            for (auto& [_, remote] : coop.Remotes())
                if (remote.motion.Sample(now, out))
                {
                    m_gear = remote.gear;
                    return true;
                }
            return false;
        }

        bool m_still;
        bool m_far;
        coop::Pose m_start;
        bool m_hasStart = false;
        float m_walked = 0;
        std::vector<uint32_t> m_gear;
        uint32_t m_lastNow = 0;
        size_t m_phase = SIZE_MAX;
    };

    std::string NextToExe(const char* name)
    {
        if (std::strpbrk(name, "\\/:"))
            return name;
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir = exe;
        return dir.substr(0, dir.find_last_of("\\/") + 1) + name;
    }

    constexpr const char* kRecordingHeader = "NVCoop recording 1";

    class Recorder
    {
    public:
        ~Recorder() { Close(); }

        bool Open(const std::string& path)
        {
            m_path = path;
            if (fopen_s(&m_file, path.c_str(), "w") != 0 || !m_file)
            {
                LOG_ERROR("Can't write the recording %s", path.c_str());
                return false;
            }
            std::fprintf(m_file, "%s\n", kRecordingHeader);
            LOG_INFO("Recording to %s; press Ctrl+C to stop and save", path.c_str());
            return true;
        }

        void Update(const coop::Coop& coop, uint32_t now, const std::vector<coop::AttackReport>& attacks)
        {
            if (!m_file)
                return;
            coop::Pose pose;
            const coop::RemotePlayer* who = nullptr;
            for (auto& [id, remote] : coop.Remotes())
                if ((m_who < 0 || id == m_who) && remote.motion.Sample(now, pose))
                {
                    who = &remote;
                    if (m_who < 0)
                    {
                        m_who = id;
                        m_start = now;
                        LOG_INFO("Recording %s", remote.name.c_str());
                    }
                    break;
                }
            if (!who)
                return;
            uint32_t ms = now - m_start;
            if (who->gearChanges != m_gearChanges || ms == 0)
            {
                m_gearChanges = who->gearChanges;
                std::fprintf(m_file, "G %u %zu", ms, who->gear.size());
                for (uint32_t item : who->gear)
                    std::fprintf(m_file, " %08X", item);
                std::fprintf(m_file, "\n");
            }
            if (ms == 0 || pose.space != m_last.space || pose.x != m_last.x || pose.y != m_last.y || pose.z != m_last.z ||
                pose.heading != m_last.heading || pose.flags != m_last.flags || pose.moves != m_last.moves)
            {
                m_last = pose;
                std::fprintf(m_file, "P %u %08X %.2f %.2f %.2f %.4f %u %X\n", ms, pose.space, pose.x, pose.y, pose.z,
                    pose.heading, pose.flags, pose.moves);
            }
            for (const coop::AttackReport& attack : attacks)
                if (attack.playerId == m_who)
                    std::fprintf(m_file, "A %u %u\n", ms, attack.shots);
            if (coop::IsDue(now, m_nextNote))
            {
                m_nextNote = now + 10000;
                std::fflush(m_file);
                LOG_INFO("Recorded %u s so far", ms / 1000);
            }
            m_length = ms;
        }

        void Close()
        {
            if (!m_file)
                return;
            std::fclose(m_file);
            m_file = nullptr;
            LOG_INFO("Saved %u s of recording to %s", m_length / 1000, m_path.c_str());
        }

    private:
        FILE* m_file = nullptr;
        std::string m_path;
        int m_who = -1;
        uint32_t m_start = 0, m_length = 0, m_nextNote = 0;
        uint32_t m_gearChanges = 0;
        coop::Pose m_last;
    };

    class Replayer
    {
    public:
        explicit Replayer(bool here) : m_here(here) {}

        bool Load(const std::string& path)
        {
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "r") != 0 || !f)
            {
                LOG_ERROR("Can't open the recording %s", path.c_str());
                return false;
            }
            char line[1024];
            size_t lineNo = 1;
            bool ok = std::fgets(line, sizeof line, f) && std::strncmp(line, kRecordingHeader, std::strlen(kRecordingHeader)) == 0;
            uint32_t lastMs = 0;
            bool hasPose = false;
            while (ok && std::fgets(line, sizeof line, f))
            {
                ++lineNo;
                ok = ParseLine(line, lastMs);
                if (ok)
                {
                    lastMs = m_steps.back().ms;
                    hasPose = hasPose || m_steps.back().kind == 'P';
                    ok = m_steps.size() <= kMaxSteps;
                }
            }
            std::fclose(f);
            if (!ok || !hasPose)
            {
                LOG_ERROR("The recording %s is damaged (line %zu); not playing it", path.c_str(), lineNo);
                m_steps.clear();
                return false;
            }
            LOG_INFO("Loaded %zu steps (%u s) from %s", m_steps.size(), lastMs / 1000, path.c_str());
            return true;
        }

        void Update(coop::Coop& coop, uint32_t now)
        {
            if (m_steps.empty() || !coop.InGame())
                return;
            if (!m_running)
            {
                coop::Pose them;
                if (m_here && !SeeSomeone(coop, now, them))
                    return;
                Restart(now, them);
            }
            uint32_t t = now - m_t0;
            for (; m_next < m_steps.size() && m_steps[m_next].ms <= t; ++m_next)
            {
                const Step& s = m_steps[m_next];
                if (s.kind == 'P')
                    m_pose = s.pose;
                else if (s.kind == 'G')
                    m_gear = s.gear;
                else
                    coop.Attack(s.shots);
            }
            coop::Pose out = m_pose;
            if (m_here && out.space == m_steps[m_first].pose.space)
            {
                out.x += m_dx;
                out.y += m_dy;
                out.z += m_dz;
                out.space = m_space;
            }
            coop.SetMyPose(out);
            coop.SetMyGear(m_gear);
            if (m_next == m_steps.size())
            {
                m_running = false;
                LOG_INFO("Replay finished; starting again");
            }
        }

    private:
        static constexpr size_t kMaxSteps = 2000000;

        struct Step
        {
            char kind;
            uint32_t ms;
            coop::Pose pose;
            std::vector<uint32_t> gear;
            uint8_t shots = 0;
        };

        bool ParseLine(const char* line, uint32_t lastMs)
        {
            if (!std::strchr(line, '\n') && std::strlen(line) >= 1000)
                return false;
            Step s{};
            int used = 0;
            if (line[0] == 'P')
            {
                unsigned flags = 0, moves = 0;
                int got = sscanf_s(line, "P %u %X %f %f %f %f %u %X", &s.ms, &s.pose.space, &s.pose.x, &s.pose.y,
                    &s.pose.z, &s.pose.heading, &flags, &moves);
                if (got != 7 && got != 8)
                    return false;
                s.pose.moves = static_cast<uint16_t>(moves);
                for (float v : {s.pose.x, s.pose.y, s.pose.z, s.pose.heading})
                    if (!std::isfinite(v) || std::fabs(v) > 1e7f)
                        return false;
                if (flags & ~coop::Pose::kKnownFlags)
                    return false;
                s.pose.flags = static_cast<uint8_t>(flags);
            }
            else if (line[0] == 'G')
            {
                unsigned count = 0;
                if (sscanf_s(line, "G %u %u%n", &s.ms, &count, &used) != 2 || count > net::kMaxGearItems)
                    return false;
                const char* p = line + used;
                for (unsigned i = 0; i < count; ++i)
                {
                    uint32_t item = 0;
                    if (sscanf_s(p, " %X%n", &item, &used) != 1)
                        return false;
                    s.gear.push_back(item);
                    p += used;
                }
            }
            else if (line[0] == 'A')
            {
                unsigned shots = 0;
                if (sscanf_s(line, "A %u %u", &s.ms, &shots) != 2 || shots < 1 || shots > net::kMaxShots)
                    return false;
                s.shots = static_cast<uint8_t>(shots);
            }
            else
                return false;
            if (s.ms < lastMs)
                return false;
            s.kind = line[0];
            m_steps.push_back(std::move(s));
            return true;
        }

        static bool SeeSomeone(const coop::Coop& coop, uint32_t now, coop::Pose& out)
        {
            for (auto& [_, remote] : coop.Remotes())
                if (remote.motion.Sample(now, out))
                    return true;
            return false;
        }

        void Restart(uint32_t now, const coop::Pose& them)
        {
            m_t0 = now;
            m_next = 0;
            m_running = true;
            m_first = 0;
            while (m_steps[m_first].kind != 'P')
                ++m_first;
            const coop::Pose& start = m_steps[m_first].pose;
            m_dx = them.x - start.x;
            m_dy = them.y - start.y;
            m_dz = them.z - start.z;
            m_space = them.space;
            if (m_loops++ == 0)
                LOG_INFO("Replaying%s", m_here ? ", starting where you stand" : " in the recorded places");
        }

        bool m_here;
        std::vector<Step> m_steps;
        size_t m_next = 0, m_first = 0;
        uint32_t m_t0 = 0, m_loops = 0;
        bool m_running = false;
        coop::Pose m_pose;
        std::vector<uint32_t> m_gear;
        float m_dx = 0, m_dy = 0, m_dz = 0;
        uint32_t m_space = 0;
    };

    class FakeNpcs
    {
    public:
        FakeNpcs(uint32_t count, bool cycle, uint32_t real) : m_count(count), m_cycle(cycle), m_real(real) {}
        void Update(coop::Coop& coop, uint32_t now)
        {
            if ((!m_count && !m_real) || coop.GetRole() != coop::Role::Host)
                return;
            coop::Pose them;
            bool found = false;
            for (auto& [_, remote] : coop.Remotes())
                found = found || remote.motion.Sample(now, them);
            if (!found)
                return;
            if (m_real && !m_realState.ref)
            {
                m_realState = {m_real, them.x + 300, them.y, them.z, 0, 0, 0, 100, 100, 0, 0, 0};
                LOG_INFO("Sharing NPC %08X (from the game's files), standing still", m_real);
            }
            if (m_cycle && m_npcs.size() == m_count && coop::IsDue(now, m_cycleAt))
            {
                LOG_INFO("Dropping the %zu NPCs; making them again", m_npcs.size());
                m_npcs.clear();
            }
            if (m_npcs.size() < m_count && coop::IsDue(now, m_nextAddAt))
            {
                m_nextAddAt = now + 250;
                uint32_t i = m_serial++;
                float angle = 0.9f * static_cast<float>(i), distance = 400.0f + static_cast<float>(i % 5) * 120.0f;
                net::NpcState n{0xFF100000 + i, them.x + std::cos(angle) * distance,
                    them.y + std::sin(angle) * distance, them.z, angle, 0, 0, 50, 50, 0, kShotgun, kLvlPowderGanger};
                m_npcs.push_back(n);
                LOG_INFO("Sharing NPC %08X (%zu of %u)", n.ref, m_npcs.size(), m_count);
                if (m_npcs.size() == m_count)
                    m_cycleAt = now + 10000;
            }
            std::vector<net::NpcState> npcs = m_npcs;
            if (m_realState.ref)
                npcs.push_back(m_realState);
            coop.SetNpcs(them.space, std::move(npcs));
            for (const net::NpcState& n : m_npcs)
                coop.SetNpcGear(n.ref, {{kHockeyMask, 60}, {kJumpsuit, 80}});
        }

    private:
        static constexpr uint32_t kLvlPowderGanger = 0x0015F315, kShotgun = 0x000E393B;
        static constexpr uint32_t kHockeyMask = 0x00033598, kJumpsuit = 0x00153A0E;
        uint32_t m_count;
        bool m_cycle;
        uint32_t m_real;
        net::NpcState m_realState{};
        std::vector<net::NpcState> m_npcs;
        uint32_t m_serial = 1, m_nextAddAt = 0, m_cycleAt = 0;
    };

    int Usage()
    {
        std::printf("usage:\n  fakefriend join [address] [port] [name] [hit=<ref id>] [still] [down] [revive] [attack] [far]\n"
            "  fakefriend host [port] [name] [hurt] [still] [down] [revive] [attack] [far] [npcs=<count> [cycle]]\n"
            "      [npc=<ref id>]\n"
            "  either can add: record=<file>, replay=<file> [here], lag=<ping>[,<jitter>[,<lost %%>]]\n");
        return 1;
    }
}

int main(int argc, char** argv)
{
    uint32_t hitTarget = 0;
    bool hurt = false;
    bool still = false;
    bool goDown = false;
    bool revive = false;
    bool attack = false;
    bool startFar = false;
    bool here = false;
    uint32_t npcCount = 0;
    bool npcCycle = false;
    uint32_t npcReal = 0;
    const char* recordTo = nullptr;
    const char* replayFrom = nullptr;
    net::FakeLag lag;
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "hurt") == 0)
            hurt = true;
        else if (std::strcmp(argv[i], "still") == 0)
            still = true;
        else if (std::strcmp(argv[i], "down") == 0)
            goDown = true;
        else if (std::strcmp(argv[i], "revive") == 0)
            revive = true;
        else if (std::strcmp(argv[i], "attack") == 0)
            attack = true;
        else if (std::strcmp(argv[i], "far") == 0)
            startFar = true;
        else if (std::strncmp(argv[i], "hit=", 4) == 0)
            hitTarget = std::strtoul(argv[i] + 4, nullptr, 16);
        else if (std::strcmp(argv[i], "here") == 0)
            here = true;
        else if (std::strncmp(argv[i], "npcs=", 5) == 0)
        {
            int count = std::atoi(argv[i] + 5);
            npcCount = static_cast<uint32_t>(count < 0 ? 0 : count > 40 ? 40 : count);
        }
        else if (std::strncmp(argv[i], "npc=", 4) == 0)
            npcReal = std::strtoul(argv[i] + 4, nullptr, 16);
        else if (std::strcmp(argv[i], "cycle") == 0)
            npcCycle = true;
        else if (std::strncmp(argv[i], "record=", 7) == 0)
            recordTo = argv[i] + 7;
        else if (std::strncmp(argv[i], "replay=", 7) == 0)
            replayFrom = argv[i] + 7;
        else if (std::strncmp(argv[i], "lag=", 4) == 0)
        {
            if (sscanf_s(argv[i] + 4, "%u,%u,%u", &lag.pingMs, &lag.jitterMs, &lag.lossPercent) < 1 ||
                lag.pingMs > 5000 || lag.jitterMs > 5000 || lag.lossPercent > 100)
                return Usage();
        }
        else
            args.push_back(argv[i]);
    }
    argc = static_cast<int>(args.size());
    argv = args.data();
    if (argc < 2 || (recordTo && replayFrom))
        return Usage();

    Log::EchoToConsole(true);
    SetConsoleCtrlHandler(OnCtrlC, TRUE);

    Recorder recorder;
    if (recordTo && !recorder.Open(NextToExe(recordTo)))
        return 1;
    Replayer replayer(here);
    if (replayFrom && !replayer.Load(NextToExe(replayFrom)))
        return 1;
    if (recordTo)
        still = true;

    coop::Coop coop;
    coop.GetSession().SetFakeLag(lag);
    bool started = false;
    if (std::strcmp(argv[1], "join") == 0)
    {
        std::string address = argc > 2 ? argv[2] : "127.0.0.1";
        uint16_t port = static_cast<uint16_t>(argc > 3 ? std::atoi(argv[3]) : 27960);
        std::string name = argc > 4 ? argv[4] : "FakeFriend";
        started = coop.Join(address, port, name);
    }
    else if (std::strcmp(argv[1], "host") == 0)
    {
        uint16_t port = static_cast<uint16_t>(argc > 2 ? std::atoi(argv[2]) : 27960);
        std::string name = argc > 3 ? argv[3] : "FakeHost";
        started = coop.Host(port, name);
    }
    else
        return Usage();

    if (!started)
    {
        LOG_ERROR("Couldn't start (port in use?)");
        return 1;
    }

    Walker walker(still, startFar);
    FakeNpcs fakeNpcs(npcCount, npcCycle, npcReal);
    uint32_t nextHit = 0;
    uint32_t nextAttack = 0, attacks = 0;
    float health = 100;
    uint32_t downAt = GetTickCount() + 10000;
    std::map<uint8_t, uint32_t> sawDown;
    while (!g_quit)
    {
        coop.Update();
        uint32_t now = GetTickCount();
        std::vector<coop::AttackReport> attacksSeen = coop.TakeAttacks();
        for (const coop::AttackReport& a : attacksSeen)
            LOG_INFO("%s attacks (%u)", coop.NameOf(a.playerId).c_str(), a.shots);
        recorder.Update(coop, now, attacksSeen);
        fakeNpcs.Update(coop, now);
        if (health > 0 && replayFrom)
            replayer.Update(coop, now);
        else if (health > 0)
            walker.Update(coop, now);
        if (goDown && coop.InGame() && coop::IsDue(now, downAt))
        {
            goDown = false;
            health = 0;
            LOG_INFO("Going down");
        }
        if (coop.TakeRevived())
        {
            health = 30;
            LOG_INFO("Revived! Health 30 / 100");
        }
        net::GameOver over;
        if (coop.TakeGameOver(over))
        {
            LOG_INFO("Game over (%s); back to full health as if reloaded",
                over.reason == net::GameOverReason::BledOut ? "someone bled out" : "everyone is down");
            health = 100;
        }
        for (auto& [id, remote] : coop.Remotes())
        {
            if (!remote.Downed())
            {
                sawDown.erase(id);
                continue;
            }
            auto it = sawDown.emplace(id, now).first;
            if (revive && health > 0 && now - it->second >= 3000)
            {
                LOG_INFO("Reviving %s", remote.name.c_str());
                coop.Revive(id);
                it->second = now;
            }
        }
        if (attack && coop.InGame() && health > 0 && coop::IsDue(now, nextAttack))
        {
            nextAttack = now + 2000;
            uint8_t shots = ++attacks % 3 == 0 ? 3 : 1;
            coop.Attack(shots);
            LOG_INFO("Attack (%u)", shots);
        }
        if (hitTarget && coop.GetFriendState() == coop::FriendState::Joined && coop::IsDue(now, nextHit))
        {
            nextHit = now + 2000;
            coop.ReportHit(hitTarget, 10);
            LOG_INFO("Hit %08X for 10", hitTarget);
        }
        if (hurt && coop.GetRole() == coop::Role::Host && coop::IsDue(now, nextHit))
        {
            nextHit = now + 3000;
            for (auto& [id, remote] : coop.Remotes())
            {
                coop.DamagePlayer(id, 5);
                LOG_INFO("Hurt %s for 5 (health was %.1f)", remote.name.c_str(), remote.health);
            }
        }
        if (float damage = coop.TakeDamage(); damage > 0 && health > 0)
        {
            health = health > damage ? health - damage : 0;
            LOG_INFO("Took %.1f damage, health %.1f / 100%s", damage, health, health > 0 ? "" : ": down");
        }
        coop.SetMyHealth(health, 100);
        for (const coop::HitReport& hit : coop.TakeHits())
            LOG_INFO("Player %u hit %08X for %.1f", hit.playerId, hit.targetRef, hit.damage);
        for (const net::PlayerCommand& c : coop.TakePlayerCommands())
            LOG_INFO("Scripted: the host's command %u (%08X %08X)", static_cast<unsigned>(c.kind), c.form, c.ref);
        if (coop.GetRole() == coop::Role::Friend &&
            (coop.GetFriendState() == coop::FriendState::Rejected ||
             coop.GetFriendState() == coop::FriendState::Disconnected))
            break;
        Sleep(16);
    }
    recorder.Close();
    coop.Stop();
    return 0;
}
