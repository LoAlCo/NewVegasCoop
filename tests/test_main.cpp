// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/coop.h"
#include "coop/attack_versions.h"
#include "coop/balance.h"
#include "coop/game_mode.h"
#include "coop/frame_parts.h"
#include "coop/threat.h"
#include "coop/reinforcements.h"
#include "coop/voice.h"
#include "coop/progress.h"
#include "coop/quests.h"
#include "coop/sha256.h"
#include "coop/shot_counter.h"
#include "coop/clock.h"
#include "coop/steer.h"
#include "coop/talk_log.h"
#include "coop/item_stacks.h"
#include "dev/inspect_text.h"
#include "dev/lab_channel.h"
#include "dll_names.h"
#include "net/fake_lag.h"
#include "net/protocol.h"
#include "net/steam_session.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <set>
#include <deque>
#include <map>
#include <tuple>
#include <memory>
#include <vector>
#include <windows.h>

namespace
{
    int g_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("  FAILED line %d: %s\n", __LINE__, #cond); ++g_failures; } } while (0)

    struct Test { const char* name; std::function<void()> run; };
    std::vector<Test>& Tests() { static std::vector<Test> t; return t; }
    struct Register { Register(const char* n, std::function<void()> f) { Tests().push_back({n, f}); } };
#define TEST(name) static void name(); static Register reg_##name(#name, name); static void name()

    using namespace net;

    bool RunUntil(std::vector<coop::Coop*> sides, const std::function<bool()>& done, int maxMs = 3000)
    {
        for (int i = 0; i < maxMs / 10; ++i)
        {
            for (auto* s : sides)
                s->Update();
            if (done())
                return true;
            Sleep(10);
        }
        return false;
    }

    constexpr uint16_t kTestPort = 27991;
}

TEST(hello_round_trip)
{
    Bytes b = Encode(Hello{kProtocolVersion, "Courier"});
    Reader r(b);
    MsgType t;
    Hello h;
    CHECK(r.Type(t) && t == MsgType::Hello);
    CHECK(Decode(r, h));
    CHECK(h.version == kProtocolVersion && h.name == "Courier");
}

TEST(truncated_messages_are_rejected)
{
    Bytes full = Encode(Hello{kProtocolVersion, "Courier"});
    for (size_t len = 1; len < full.size(); ++len)
    {
        Reader r(full.data(), len);
        MsgType t;
        Hello h;
        CHECK(r.Type(t) && !Decode(r, h));
    }
}

TEST(trailing_bytes_are_rejected)
{
    Bytes b = Encode(Welcome{1, "Host"});
    b.push_back(0);
    Reader r(b);
    MsgType t;
    Welcome w;
    CHECK(r.Type(t) && !Decode(r, w));
}

TEST(discord_overlay_is_recognised)
{
    auto blocked = [](const wchar_t* s) { return IsBlockedDll(s, wcslen(s)); };
    CHECK(blocked(L"C:\\Users\\Alex\\AppData\\Local\\Discord\\discord_hook\\DiscordHook.dll"));
    CHECK(blocked(L"discordhook.dll"));
    CHECK(blocked(L"D:/x/DISCORDHOOK.DLL"));
    CHECK(!blocked(L"NotDiscordHook.dll"));
    CHECK(!blocked(L"DiscordHook.dll.bak"));
    CHECK(!blocked(L"d3d9.dll"));
    CHECK(!blocked(L""));
    CHECK(!IsBlockedDll(nullptr, 15));
    const wchar_t padded[] = L"DiscordHook.dllXYZ";
    CHECK(IsBlockedDll(padded, 15));
}

TEST(timers_work_on_pcs_on_for_weeks)
{
    uint32_t now = 30u * 24 * 3600 * 1000;
    CHECK(coop::IsDue(now, 0));
    CHECK(coop::IsDue(now, now));
    CHECK(!coop::IsDue(now, now + 500));
    CHECK(coop::IsDue(now + 500, now + 500));
    CHECK(coop::IsDue(5, 0xFFFFFFF0u));
    CHECK(!coop::IsDue(0xFFFFFFF0u, 5));
    CHECK(!coop::IsDue(100, 200));
}

TEST(long_or_odd_names_are_rejected)
{
    Writer w(MsgType::Hello);
    w.U32(kProtocolVersion);
    w.U16(1000);
    Bytes b = w.Take();
    Reader r(b);
    MsgType t;
    Hello h;
    CHECK(r.Type(t) && !Decode(r, h));

    Bytes ctrl = Encode(Hello{kProtocolVersion, std::string("bad\nname")});
    Reader r2(ctrl);
    CHECK(r2.Type(t) && !Decode(r2, h));

    Bytes empty = Encode(Hello{kProtocolVersion, ""});
    Reader r3(empty);
    CHECK(r3.Type(t) && !Decode(r3, h));
}

TEST(sent_names_are_cut_to_the_limit)
{
    Bytes b = Encode(Hello{kProtocolVersion, std::string(100, 'a')});
    Reader r(b);
    MsgType t;
    Hello h;
    CHECK(r.Type(t) && Decode(r, h) && h.name.size() == kMaxNameLength);
}

TEST(friend_joins_host)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(buddy.MyPlayerId() == 1);
    CHECK(host.Players().size() == 1 && host.Players().begin()->second.name == "Buddy");

    buddy.Stop();
    CHECK(RunUntil({&host}, [&] { return host.Players().empty(); }));
}

TEST(second_host_on_same_port_fails)
{
    coop::Coop a, b;
    CHECK(a.Host(kTestPort, "A"));
    CHECK(!b.Host(kTestPort, "B"));
}

TEST(wrong_version_is_rejected)
{
    coop::Coop host;
    CHECK(host.Host(kTestPort, "Host"));
    Session raw;
    CHECK(raw.StartClient("127.0.0.1", kTestPort));

    bool rejected = false, disconnected = false;
    for (int i = 0; i < 300 && !disconnected; ++i)
    {
        host.Update();
        Event ev;
        while (raw.Poll(ev))
        {
            if (ev.kind == Event::Kind::Connected)
                raw.Send(ev.peer, Encode(Hello{kProtocolVersion + 1, "Old"}));
            else if (ev.kind == Event::Kind::Received)
            {
                Reader r(ev.data);
                MsgType t;
                rejected |= r.Type(t) && t == MsgType::Reject;
            }
            else
                disconnected = true;
        }
        Sleep(10);
    }
    CHECK(rejected && disconnected);
    CHECK(host.Players().empty());
}

TEST(garbage_gets_kicked)
{
    coop::Coop host;
    CHECK(host.Host(kTestPort, "Host"));
    Session raw;
    CHECK(raw.StartClient("127.0.0.1", kTestPort));

    bool disconnected = false;
    for (int i = 0; i < 300 && !disconnected; ++i)
    {
        host.Update();
        Event ev;
        while (raw.Poll(ev))
        {
            if (ev.kind == Event::Kind::Connected)
                raw.Send(ev.peer, Bytes{0xFF, 0x01, 0x02});
            disconnected |= ev.kind == Event::Kind::Disconnected;
        }
        Sleep(10);
    }
    CHECK(disconnected);
}

TEST(a_host_cant_announce_endless_players)
{
    Session raw;
    CHECK(raw.StartHost(kTestPort, 4));
    coop::Coop a;
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    bool welcomed = false;
    for (int i = 0; i < 300 && a.Remotes().size() < coop::kMaxFriends; ++i)
    {
        a.Update();
        Event ev;
        while (raw.Poll(ev))
            if (ev.kind == Event::Kind::Received && !welcomed)
            {
                welcomed = true;
                raw.Send(ev.peer, Encode(Welcome{1, "Evil"}));
                for (uint8_t id = 2; id < 22; ++id)
                    raw.Send(ev.peer, Encode(PlayerJoined{id, "P" + std::to_string(id)}));
            }
        Sleep(10);
    }
    for (int i = 0; i < 30; ++i)
    {
        a.Update();
        Sleep(10);
    }
    CHECK(welcomed && a.Remotes().size() == coop::kMaxFriends);
    a.Stop();
    raw.Stop();
}

TEST(player_state_round_trip)
{
    PlayerState in{3, 123456, 0xDA726, 1.5f, -20000.25f, 300.f, 3.1f, coop::Pose::kSneaking, 0x601, -0.7f};
    Bytes b = Encode(in);
    Reader r(b);
    MsgType type;
    PlayerState out;
    CHECK(r.Type(type) && type == MsgType::PlayerState && Decode(r, out));
    CHECK(out.playerId == 3 && out.timeMs == 123456 && out.space == 0xDA726);
    CHECK(out.x == 1.5f && out.y == -20000.25f && out.z == 300.f && out.heading == 3.1f);
    CHECK(out.flags == coop::Pose::kSneaking && out.moves == 0x601 && out.pitch == -0.7f);
}

TEST(looking_too_far_up_or_down_is_rejected)
{
    for (float pitch : {2.0f, -1.7f, std::nanf("")})
    {
        Bytes b = Encode(PlayerState{1, 0, 0, 0, 0, 0, 0, 0, 0, pitch});
        Reader r(b);
        MsgType type;
        PlayerState out;
        CHECK(r.Type(type) && !Decode(r, out));
    }
}

TEST(smoother_blends_where_they_look)
{
    coop::Smoother s;
    s.Add(0, coop::Pose{1, 0, 0, 0, 0, 0, 0, 0.2f}, 1000);
    s.Add(100, coop::Pose{1, 0, 0, 0, 0, 0, 0, 0.6f}, 1100);
    coop::Pose out;
    CHECK(s.Sample(1050 + coop::Smoother::kDelayMs, out) && out.pitch > 0.2f && out.pitch < 0.6f);
    CHECK(s.Sample(5000, out) && out.pitch == 0.6f);
}

TEST(movement_flags_keep_jumping)
{
    Bytes b = Encode(PlayerState{1, 0, 0, 0, 0, 0, 0, 0, 0xF201});
    Reader r(b);
    MsgType type;
    PlayerState out;
    CHECK(r.Type(type) && Decode(r, out) && out.moves == 0xF201);
}

TEST(unknown_player_flags_are_rejected)
{
    Bytes b = Encode(PlayerState{1, 0, 0, 0, 0, 0, 0, 0x80});
    Reader r(b);
    MsgType type;
    PlayerState out;
    CHECK(r.Type(type) && !Decode(r, out));
}

TEST(smoother_keeps_flags)
{
    coop::Smoother s;
    coop::Pose p;
    s.Add(0, {7, 0, 0, 0, 0, coop::Pose::kWeaponOut, 0x201}, 1000);
    s.Add(100, {7, 100, 0, 0, 0, 0}, 1100);
    CHECK(s.Sample(1050 + coop::Smoother::kDelayMs, p) && p.flags == coop::Pose::kWeaponOut && p.moves == 0x201);
    CHECK(s.Sample(5000, p) && p.flags == 0);
}

TEST(bad_numbers_are_rejected)
{
    for (float bad : {NAN, INFINITY, 1e9f})
    {
        Bytes b = Encode(PlayerState{1, 0, 0, bad, 0, 0, 0});
        Reader r(b);
        MsgType type;
        PlayerState out;
        CHECK(r.Type(type) && !Decode(r, out));
    }
}

TEST(smoother_blends_between_updates)
{
    coop::Smoother s;
    coop::Pose p;
    CHECK(!s.Sample(0, p));
    s.Add(0, {7, 0, 0, 0, 0}, 1000);
    s.Add(100, {7, 100, 0, 0, 0}, 1100);
    CHECK(s.Sample(1050 + coop::Smoother::kDelayMs, p) && p.x == 50 && p.space == 7);
    CHECK(s.Sample(5000, p) && p.x == 100);
    CHECK(s.Sample(0, p) && p.x == 0);
}

TEST(smoother_ignores_old_updates_and_jumps_between_places)
{
    coop::Smoother s;
    coop::Pose p;
    s.Add(100, {1, 0, 0, 0, 0}, 100);
    s.Add(50, {1, 999, 0, 0, 0}, 110);
    s.Add(200, {2, 100, 0, 0, 0}, 200);
    CHECK(s.Sample(150 + coop::Smoother::kDelayMs, p) && p.x == 0 && p.space == 1);
    CHECK(s.Sample(200 + coop::Smoother::kDelayMs, p) && p.x == 100 && p.space == 2);
}

TEST(smoother_turns_the_short_way)
{
    coop::Smoother s;
    coop::Pose p;
    s.Add(0, {0, 0, 0, 0, 6.2f}, 0);
    s.Add(100, {0, 0, 0, 0, 0.2f}, 100);
    CHECK(s.Sample(50 + coop::Smoother::kDelayMs, p) && (p.heading > 6.2f || p.heading < 0.2f));
}

TEST(gear_round_trip_and_limits)
{
    PlayerGear in{2, {0x1234, 0xABCDEF01}};
    Bytes b = Encode(in);
    Reader r(b);
    MsgType type;
    PlayerGear out;
    CHECK(r.Type(type) && type == MsgType::PlayerGear && Decode(r, out));
    CHECK(out.playerId == 2 && out.items == in.items);

    Bytes tooMany = {static_cast<uint8_t>(MsgType::PlayerGear), 1, static_cast<uint8_t>(kMaxGearItems + 1)};
    tooMany.resize(tooMany.size() + 4 * (kMaxGearItems + 1));
    Reader r2(tooMany);
    CHECK(r2.Type(type) && !Decode(r2, out));
}

TEST(players_see_each_other_gear)
{
    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    host.SetMyGear({30, 10, 10});
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    a.SetMyGear({5});
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    auto gear = [](coop::Coop& c, uint8_t id) {
        auto it = c.Remotes().find(id);
        return it == c.Remotes().end() ? std::vector<uint32_t>{} : it->second.gear;
    };
    CHECK(RunUntil({&host, &a, &b}, [&] {
        return gear(b, 0) == std::vector<uint32_t>{10, 30} && gear(b, a.MyPlayerId()) == std::vector<uint32_t>{5};
    }));
    host.SetMyGear({});
    CHECK(RunUntil({&host, &a, &b}, [&] { return gear(a, 0).empty() && gear(b, 0).empty(); }));
    b.Stop();
    a.Stop();
    host.Stop();
}

TEST(friends_hits_reach_the_host_checked)
{
    Hit in{3, 0x1234, 25.5f};
    Bytes b = Encode(in);
    Reader r(b);
    MsgType type;
    Hit out;
    CHECK(r.Type(type) && type == MsgType::Hit && Decode(r, out));
    CHECK(out.playerId == 3 && out.targetRef == 0x1234 && out.damage == 25.5f);

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    a.ReportHit(0, 10);
    a.ReportHit(0x55, -5);
    a.ReportHit(0x55, 1e6f);
    a.ReportHit(0x77, 12.5f);
    std::vector<coop::HitReport> hits;
    CHECK(RunUntil({&host, &a}, [&] {
        for (auto& h : host.TakeHits())
            hits.push_back(h);
        return !hits.empty();
    }));
    CHECK(hits.size() == 1 && hits[0].playerId == a.MyPlayerId() && hits[0].targetRef == 0x77 && hits[0].damage == 12.5f);
    host.ReportHit(0x77, 5);
    a.Stop();
    host.Stop();
}

TEST(health_limits_and_damage_reach_only_that_friend)
{
    PlayerHealth h{1, 50, 100};
    Bytes b = Encode(h);
    Reader r(b);
    MsgType type;
    PlayerHealth out;
    CHECK(r.Type(type) && type == MsgType::PlayerHealth && Decode(r, out) && out.health == 50 && out.maxHealth == 100);
    Bytes noMax = Encode(PlayerHealth{1, 50, 0});
    Reader r2(noMax);
    CHECK(r2.Type(type) && !Decode(r2, out));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    host.SetMyHealth(80, 100);
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(b2.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b2}, [&] { return b2.GetFriendState() == coop::FriendState::Joined; }));
    a.SetMyHealth(40, 120);
    auto healthOf = [](coop::Coop& c, uint8_t id) {
        auto it = c.Remotes().find(id);
        return it == c.Remotes().end() ? -1.f : it->second.health;
    };
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return healthOf(a, 0) == 80 && healthOf(b2, 0) == 80 && healthOf(b2, a.MyPlayerId()) == 40 &&
            healthOf(host, a.MyPlayerId()) == 40;
    }));

    host.DamagePlayer(a.MyPlayerId(), 15);
    float taken = 0;
    CHECK(RunUntil({&host, &a, &b2}, [&] { taken += a.TakeDamage(); return taken > 0; }));
    CHECK(taken == 15 && b2.TakeDamage() == 0);
    b2.Stop();
    a.Stop();
    host.Stop();
}

TEST(downed_players_are_revived_only_up_close)
{
    Bytes bad = Encode(GameOver{static_cast<GameOverReason>(7), 0});
    Reader r(bad);
    MsgType type;
    GameOver over;
    CHECK(r.Type(type) && type == MsgType::GameOver && !Decode(r, over));

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    uint8_t id = a.MyPlayerId();
    host.SetMyHealth(80, 100);
    host.SetMyPose({5, 1000, 0, 0, 0});
    a.SetMyPose({5, 0, 0, 0, 0});
    a.SetMyHealth(0, 100);
    coop::Pose p;
    CHECK(RunUntil({&host, &a}, [&] {
        auto it = host.Remotes().find(id);
        return it != host.Remotes().end() && it->second.Downed() && it->second.motion.Sample(GetTickCount(), p) &&
            a.Remotes().count(0) && !a.Remotes().at(0).Downed();
    }));

    host.Revive(id);
    a.Revive(0);
    RunUntil({&host, &a}, [&] { return false; }, 200);
    CHECK(!a.TakeRevived() && !host.TakeRevived());

    host.SetMyPose({5, 100, 0, 0, 0});
    host.Revive(id);
    CHECK(RunUntil({&host, &a}, [&] { return a.TakeRevived(); }));
    a.SetMyHealth(30, 100);
    net::GameOver out;
    CHECK(!host.TakeGameOver(out) && !a.TakeGameOver(out));
    a.Stop();
    host.Stop();
}

TEST(a_mode_change_message_carries_only_the_two_switches)
{
    MsgType type;
    net::ModeChange m;
    Bytes ok = Encode(net::ModeChange{coop::SaveMode{coop::GameMode::Balanced, false}});
    Reader r1(ok);
    CHECK(r1.Type(type) && type == MsgType::Mode && Decode(r1, m) && m.mode.mode == coop::GameMode::Balanced &&
        !m.mode.friendlyFire);
    auto bad = [&](uint8_t mode, uint8_t ff, bool extra) {
        net::Writer w(MsgType::Mode);
        w.U8(mode);
        w.U8(ff);
        if (extra)
            w.U8(0);
        Bytes b = w.Take();
        Reader r(b);
        net::ModeChange out;
        return r.Type(type) && !Decode(r, out);
    };
    CHECK(bad(0, 0, false));
    CHECK(bad(3, 0, false));
    CHECK(bad(1, 2, false));
    CHECK(bad(1, 0, true));
}

TEST(downed_players_are_dragged_up_close_and_dont_bleed_out_meanwhile)
{
    MsgType type;
    net::Drag m;
    Bytes self = Encode(net::Drag{1, 1, true});
    Reader r1(self);
    CHECK(r1.Type(type) && type == MsgType::Drag && !Decode(r1, m));
    Bytes ok = Encode(net::Drag{2, 3, true});
    Reader r2(ok);
    CHECK(r2.Type(type) && Decode(r2, m) && m.dragger == 2 && m.target == 3 && m.on);

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    uint8_t id = a.MyPlayerId();
    host.SetMyHealth(80, 100);
    host.SetMyPose({5, 1000, 0, 0, 0});
    a.SetMyPose({5, 0, 0, 0, 0});
    a.SetMyHealth(0, 100);
    coop::Pose p;
    CHECK(RunUntil({&host, &a}, [&] {
        auto it = host.Remotes().find(id);
        return it != host.Remotes().end() && it->second.Downed() && it->second.motion.Sample(GetTickCount(), p) &&
            a.Remotes().count(0) && !a.Remotes().at(0).Downed();
    }));

    host.Drag(id, true);
    RunUntil({&host, &a}, [&] { return false; }, 200);
    CHECK(host.DraggedBy(id) == -1 && a.DraggedBy(id) == -1);

    host.SetBleedOutMs(300);
    host.SetMyPose({5, 100, 0, 0, 0});
    host.Drag(id, true);
    CHECK(host.DraggedBy(id) == 0 && host.Dragging(0) == id);
    CHECK(RunUntil({&host, &a}, [&] { return a.DraggedBy(id) == 0; }));
    a.Drag(0, true);
    RunUntil({&host, &a}, [&] { return false; }, 600);
    net::GameOver out;
    CHECK(!host.TakeGameOver(out) && !a.TakeGameOver(out) && host.DraggedBy(0) == -1);

    host.Drag(id, false);
    CHECK(RunUntil({&host, &a}, [&] { return a.DraggedBy(id) == -1; }));
    CHECK(RunUntil({&host, &a}, [&] { return a.TakeGameOver(out); }));
    CHECK(out.reason == GameOverReason::BledOut && out.playerId == id);
    a.Stop();
    host.Stop();
}

TEST(a_drag_ends_when_they_are_revived)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    uint8_t id = a.MyPlayerId();
    a.SetMyHealth(90, 100);
    a.SetMyPose({5, 50, 0, 0, 0});
    host.SetMyPose({5, 0, 0, 0, 0});
    host.SetMyHealth(0, 100);
    coop::Pose p;
    CHECK(RunUntil({&host, &a}, [&] {
        auto it = host.Remotes().find(id);
        return it != host.Remotes().end() && it->second.motion.Sample(GetTickCount(), p) && it->second.maxHealth > 0 &&
            a.Remotes().count(0) && a.Remotes().at(0).Downed();
    }));
    a.Drag(0, true);
    CHECK(RunUntil({&host, &a}, [&] { return host.DraggedBy(0) == id && a.Dragging(id) == 0; }));
    a.Revive(0);
    CHECK(RunUntil({&host, &a}, [&] { return host.TakeRevived(); }));
    CHECK(RunUntil({&host, &a}, [&] { return host.DraggedBy(0) == -1 && a.DraggedBy(0) == -1; }));
    a.Stop();
    host.Stop();
}

TEST(a_revived_host_is_up_straight_away)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    host.SetMyPose({5, 0, 0, 0, 0});
    a.SetMyPose({5, 100, 0, 0, 0});
    a.SetMyHealth(50, 100);
    CHECK(RunUntil({&host, &a}, [&] {
        return host.Remotes().count(a.MyPlayerId()) && host.Remotes().at(a.MyPlayerId()).maxHealth > 0;
    }));
    host.SetMyHealth(0, 100);
    CHECK(RunUntil({&host, &a}, [&] { return a.Remotes().count(0) && a.Remotes().at(0).Downed(); }));
    a.Revive(0);
    CHECK(RunUntil({&host, &a}, [&] { return host.TakeRevived(); }));
    host.SetBleedOutMs(300);
    RunUntil({&host, &a}, [&] { return false; }, 500);
    net::GameOver out;
    CHECK(!host.TakeGameOver(out) && !a.TakeGameOver(out));
    CHECK(!a.Remotes().at(0).Downed());
    a.Stop();
    host.Stop();
}

TEST(game_over_when_everyone_is_down_or_someone_bleeds_out)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    a.SetMyHealth(50, 100);
    CHECK(RunUntil({&host, &a}, [&] { return host.Remotes().count(a.MyPlayerId()) && host.Remotes().at(a.MyPlayerId()).maxHealth > 0; }));
    host.SetMyHealth(0, 100);
    RunUntil({&host, &a}, [&] { return false; }, 300);
    net::GameOver out;
    CHECK(!host.TakeGameOver(out));

    a.SetMyHealth(-5, 100);
    CHECK(RunUntil({&host, &a}, [&] { return a.TakeGameOver(out); }));
    CHECK(out.reason == GameOverReason::AllDown && host.TakeGameOver(out));

    host.SetMyHealth(100, 100);
    a.SetMyHealth(100, 100);
    RunUntil({&host, &a}, [&] { return false; }, 300);
    host.SetBleedOutMs(300);
    a.SetMyHealth(0, 100);
    CHECK(RunUntil({&host, &a}, [&] { return a.TakeGameOver(out); }));
    CHECK(out.reason == GameOverReason::BledOut && out.playerId == a.MyPlayerId());
    a.Stop();
    host.Stop();
}

TEST(attacks_reach_everyone_else)
{
    MsgType t;
    net::Attack m;
    Bytes none = Encode(net::Attack{1, 0});
    Reader r1(none);
    CHECK(r1.Type(t) && t == MsgType::Attack && !Decode(r1, m));
    Bytes many = Encode(net::Attack{1, static_cast<uint8_t>(kMaxShots + 1)});
    Reader r2(many);
    CHECK(r2.Type(t) && !Decode(r2, m));
    Bytes ok = Encode(net::Attack{2, 3});
    Reader r3(ok);
    CHECK(r3.Type(t) && Decode(r3, m) && m.playerId == 2 && m.shots == 3);

    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] { return a.Remotes().size() == 2 && b.Remotes().size() == 2; }));
    a.Attack(0);
    a.Attack(200);
    host.Attack(1);
    std::vector<coop::AttackReport> atHost, atA, atB;
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& x : host.TakeAttacks()) atHost.push_back(x);
        for (auto& x : a.TakeAttacks()) atA.push_back(x);
        for (auto& x : b.TakeAttacks()) atB.push_back(x);
        return atHost.size() >= 1 && atA.size() >= 1 && atB.size() >= 2;
    }));
    CHECK(atHost.size() == 1 && atHost[0].playerId == a.MyPlayerId() && atHost[0].shots == kMaxShots);
    CHECK(atA.size() == 1 && atA[0].playerId == coop::kHostPlayerId && atA[0].shots == 1);
    bool fromA = false, fromHost = false;
    for (auto& x : atB)
    {
        fromA = fromA || (x.playerId == a.MyPlayerId() && x.shots == kMaxShots);
        fromHost = fromHost || (x.playerId == coop::kHostPlayerId && x.shots == 1);
    }
    CHECK(atB.size() == 2 && fromA && fromHost);
    b.Stop();
    a.Stop();
    host.Stop();
}

TEST(explosions_are_shown_where_they_went_off)
{
    MsgType t;
    net::ExplosionAt e;
    Bytes ok = Encode(net::ExplosionAt{0x179DA, 0xDA726, -70868.f, 2014.f, 8366.f});
    Reader r1(ok);
    CHECK(r1.Type(t) && t == MsgType::ExplosionAt && Decode(r1, e) && e.form == 0x179DA && e.space == 0xDA726 &&
        e.x == -70868.f && e.y == 2014.f && e.z == 8366.f);
    for (net::ExplosionAt bad : {net::ExplosionAt{0, 0xDA726}, net::ExplosionAt{0xFF001343, 0xDA726},
             net::ExplosionAt{0x179DA, 0}, net::ExplosionAt{0x179DA, 0xDA726, 1e9f}})
    {
        Bytes b = Encode(bad);
        Reader r(b);
        CHECK(r.Type(t) && !Decode(r, e));
    }
    Bytes longer = Encode(net::ExplosionAt{0x179DA, 0xDA726});
    longer.push_back(0);
    Reader r2(longer);
    CHECK(r2.Type(t) && !Decode(r2, e));

    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    std::vector<coop::Coop*> all{&host, &a, &b};
    CHECK(RunUntil(all, [&] { return a.Remotes().size() == 2 && b.Remotes().size() == 2; }));
    a.ShareExplosion({0x179DA, 0xDA726, 1, 2, 3});
    host.ShareExplosion({0x5F9CD, 0xDA726, 4, 5, 6});
    std::vector<net::ExplosionAt> atHost, atA, atB;
    auto take = [&] {
        for (auto& x : host.TakeExplosions()) atHost.push_back(x);
        for (auto& x : a.TakeExplosions()) atA.push_back(x);
        for (auto& x : b.TakeExplosions()) atB.push_back(x);
    };
    CHECK(RunUntil(all, [&] { take(); return atHost.size() >= 1 && atA.size() >= 1 && atB.size() >= 2; }));
    RunUntil(all, [&] { take(); return false; }, 100);
    CHECK(atHost.size() == 1 && atHost[0].form == 0x179DA && atHost[0].x == 1);
    CHECK(atA.size() == 1 && atA[0].form == 0x5F9CD && atA[0].z == 6);
    CHECK(atB.size() == 2);

    atHost.clear();
    atB.clear();
    for (int i = 0; i < 40; ++i)
        a.ShareExplosion({0x179DA, 0xDA726, float(i), 0, 0});
    RunUntil(all, [&] { take(); return false; }, 300);
    CHECK(atHost.size() >= coop::kMaxExplosionsPerSecond - 1 && atHost.size() <= 2 * coop::kMaxExplosionsPerSecond);
    CHECK(atB.size() == atHost.size());
    b.Stop();
    a.Stop();
    host.Stop();
}

TEST(npc_states_round_trip_and_checks)
{
    auto decodes = [](const NpcStates& in, NpcStates& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::NpcStates && Decode(r, out);
    };
    NpcStates in{1234, 0xDA726, {{0x104C79, 1, 2, 3, 0.5f, coop::Pose::kWeaponOut, 0x0201, 80, 100, 0x12345, 0x8F216},
        {0x1234, 4, 5, 6, 0, kNpcDead, 0, 0, 50, 0, 0}}};
    NpcStates out;
    CHECK(decodes(in, out) && out.timeMs == 1234 && out.space == 0xDA726 && out.npcs.size() == 2);
    NpcStates gore{1, 0xDA726, {{0x104C79, 1, 2, 3, 0, 0, 0, 80, 100}, {0x1234, 4, 5, 6, 0, kNpcDead, 0, 0, 50}}};
    gore.npcs[0].limbs = 0x0002;
    gore.npcs[1].limbs = 0x0402;
    NpcStates goreOut;
    CHECK(decodes(gore, goreOut) && goreOut.npcs[0].limbs == 0 && goreOut.npcs[1].limbs == 0x0402);
    gore.npcs[1].limbs = 0x8000;
    CHECK(!decodes(gore, goreOut));
    CHECK(out.npcs[0].ref == 0x104C79 && out.npcs[0].z == 3 && out.npcs[0].flags == coop::Pose::kWeaponOut &&
        out.npcs[0].moves == 0x0201 && out.npcs[0].health == 80 && out.npcs[0].maxHealth == 100 &&
        out.npcs[0].idle == 0x12345 && out.npcs[0].weapon == 0x8F216 && out.npcs[1].flags == kNpcDead && out.npcs[1].maxHealth == 50);

    for (uint32_t bad : {0u, 0x14u, 0xFF000801u})
    {
        NpcStates m = in;
        m.npcs[1].ref = bad;
        CHECK(!decodes(m, out));
    }
    NpcStates twice = in;
    twice.npcs[1].ref = twice.npcs[0].ref;
    CHECK(!decodes(twice, out));
    NpcStates odd = in;
    odd.npcs[0].flags = 0x10;
    CHECK(!decodes(odd, out));
    for (int i = 0; i < 5; ++i)
    {
        NpcStates m = in;
        if (i == 4) m.npcs[0].weapon = 0xFF000124;
        if (i == 0) m.npcs[0].maxHealth = 0;
        if (i == 1) m.npcs[0].maxHealth = kMaxHealth * 2;
        if (i == 2) m.npcs[0].health = -kMaxHealth * 2;
        if (i == 3) m.npcs[0].idle = 0xFF000123;
        CHECK(!decodes(m, out));
    }
    NpcStates nowhere = in;
    nowhere.space = 0;
    CHECK(!decodes(nowhere, out));

    NpcStates many{1, 2, {}};
    for (uint32_t i = 0; i < kMaxNpcsPerMessage + 5; ++i)
        many.npcs.push_back({0x1000 + i, 0, 0, 0, 0, 0, 0, 10, 10, 0});
    CHECK(decodes(many, out) && out.npcs.size() == kMaxNpcsPerMessage);
    Bytes b = Encode(many);
    b[9] = static_cast<uint8_t>(kMaxNpcsPerMessage + 1);
    b.insert(b.end(), 39, 0);
    Reader r(b);
    MsgType t;
    CHECK(r.Type(t) && !Decode(r, out));
}

TEST(friends_see_the_hosts_npcs_and_forget_the_ones_it_stops_sharing)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    std::vector<NpcState> npcs;
    for (uint32_t i = 0; i < coop::kMaxNpcs + 3; ++i)
        npcs.push_back({0x5000 + i, static_cast<float>(i), 0, 0, 0, 0, 0, 10, 10, 0});
    host.SetNpcs(0x777, npcs);
    CHECK(RunUntil({&host, &a}, [&] { return a.Npcs().size() == coop::kMaxNpcs; }));
    CHECK(a.Npcs().count(0x5000) && !a.Npcs().count(0x5000 + coop::kMaxNpcs));
    coop::Pose p;
    CHECK(a.Npcs().at(0x5001).motion.Sample(GetTickCount() + 1000, p) && p.space == 0x777 && p.x == 1);

    npcs.resize(1);
    npcs[0].x = 500;
    host.SetNpcs(0x777, npcs);
    CHECK(RunUntil({&host, &a}, [&] {
        coop::Pose q;
        return a.Npcs().size() == 1 && a.Npcs().at(0x5000).motion.Sample(GetTickCount() + 1000, q) && q.x == 500;
    }, coop::kNpcTimeoutMs + 2000));
    a.Stop();
    host.Stop();
}

TEST(npcs_made_while_playing_need_a_base_and_all_npcs_carry_gear)
{
    auto decodes = [](const NpcStates& in, NpcStates& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::NpcStates && Decode(r, out);
    };
    NpcStates in{1, 0xDA726, {{0xFF000801, 1, 2, 3, 0, 0, 0, 50, 50, 0, 0, 0x101440}}};
    NpcStates out;
    CHECK(decodes(in, out) && out.npcs[0].ref == 0xFF000801 && out.npcs[0].base == 0x101440);
    for (uint32_t base : {0u, 0xFF000900u})
    {
        NpcStates m = in;
        m.npcs[0].base = base;
        CHECK(!decodes(m, out));
    }

    auto gearDecodes = [](const NpcGear& in, NpcGear& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::NpcGear && Decode(r, out);
    };
    NpcGear gear{0xFF000801, {{0x1234, 35}, {0x5678, kNoCondition}}};
    NpcGear got;
    CHECK(gearDecodes(gear, got) && got.ref == 0xFF000801 && got.items == gear.items);
    gear.ref = 0x104C80;
    CHECK(gearDecodes(gear, got) && got.ref == 0x104C80 && got.items == gear.items);
    for (int i = 0; i < 5; ++i)
    {
        NpcGear m = gear;
        if (i == 0) m.ref = 0;
        if (i == 1) m.ref = 0x14;
        if (i == 2) m.items[1].item = 0;
        if (i == 3) m.items[1].item = 0xFF000123;
        if (i == 4) m.items[0].health = 101;
        CHECK(!gearDecodes(m, got));
    }
    NpcGear lots{0xFF000801, {}};
    for (uint32_t i = 0; i < kMaxGearItems + 5; ++i)
        lots.items.push_back({0x1000 + i, 100});
    CHECK(gearDecodes(lots, got) && got.items.size() == kMaxGearItems);

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    host.SetNpcGear(0xFF000801, {{0x5678, 50}, {0x1234, 35}, {0x1234, 35}, {0xFF000002, 100}, {0x9999, 200}});
    std::vector<NpcState> both = in.npcs;
    both.push_back({0x104C80, 4, 5, 6, 0, 0, 0, 65, 65, 0, 0, 0});
    both.push_back({0x104C81, 7, 8, 9, 0, 0, 0, 45, 45, 0, 0, 0});
    host.SetNpcGear(0x104C80, {{0x153A0E, 100}, {0x33598, 40}});
    host.SetNpcGear(0x104C81, {});
    host.SetNpcs(0xDA726, both);
    CHECK(RunUntil({&host, &a}, [&] {
        auto it = a.Npcs().find(0xFF000801), placed = a.Npcs().find(0x104C80), bare = a.Npcs().find(0x104C81);
        return it != a.Npcs().end() && it->second.gearChanges > 0 && placed != a.Npcs().end() &&
            placed->second.gearChanges > 0 && bare != a.Npcs().end() && bare->second.gearChanges > 0;
    }, coop::kNpcGearResendMs + 2000));
    const coop::RemoteNpc& npc = a.Npcs().at(0xFF000801);
    CHECK(npc.base == 0x101440 && npc.gear == std::vector<WornItem>({{0x1234, 35}, {0x5678, 50}}));
    CHECK(a.Npcs().at(0x104C80).gear == std::vector<WornItem>({{0x33598, 40}, {0x153A0E, 100}}));
    CHECK(a.Npcs().at(0x104C81).gear.empty());
    a.SetNpcGear(0xFF000801, {{1, 100}});
    a.Stop();
    host.Stop();
}

TEST(npc_gun_shots_are_counted_once_each)
{
    coop::ShotCounter single;
    uint32_t shots = single.Look(true, true, 0, false);
    shots += single.Look(false, false, 0, true);
    shots += single.Look(true, true, 0, false);
    CHECK(shots == 2);

    coop::ShotCounter semi;
    shots = semi.Look(true, true, 1, false);
    shots += semi.Look(false, true, 0, false);
    shots += semi.Look(true, true, 0, false);
    shots += semi.Look(false, true, 1, false);
    shots += semi.Look(false, false, 0, false);
    CHECK(shots == 2);

    coop::ShotCounter automatic;
    shots = automatic.Look(true, true, 1, false);
    shots += automatic.Look(false, true, 3, false);
    shots += automatic.Look(false, true, 2, false);
    shots += automatic.Look(false, false, 0, false);
    CHECK(shots == 6);

    coop::ShotCounter stale;
    shots = stale.Look(true, true, 0, false);
    shots += stale.Look(false, false, 0, false);
    shots += stale.Look(false, true, 3, false);
    CHECK(shots == 4);
}

TEST(attack_up_and_down_versions_only_with_the_weapon_layer_playing)
{
    struct Actor
    {
        bool levelFills = true;
        bool upEmpties = false;
        bool layer = false;
        std::vector<int> played;
        int Attack()
        {
            return coop::PlayAttackVersions(
                [this](int version) {
                    played.push_back(version);
                    if (version == 0 && levelFills)
                        layer = true;
                    if (version == 1 && upEmpties)
                        layer = false;
                },
                [this] { return layer; });
        }
    };
    Actor normal;
    CHECK(normal.Attack() == 0 && normal.played == std::vector<int>({0, 1, 2}));

    Actor revived;
    revived.levelFills = false;
    CHECK(revived.Attack() == 2 && revived.played == std::vector<int>({0}));

    Actor emptied;
    emptied.upEmpties = true;
    CHECK(emptied.Attack() == 1 && emptied.played == std::vector<int>({0, 1}));

    Actor already;
    already.levelFills = false;
    already.layer = true;
    CHECK(already.Attack() == 0 && already.played == std::vector<int>({0, 1, 2}));
}

TEST(npc_attacks_round_trip_and_checks)
{
    auto decodes = [](const NpcAttacks& in, NpcAttacks& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::NpcAttacks && Decode(r, out);
    };
    NpcAttacks in{{{0x104C79, 3, false}, {0x1234, 0, true}}};
    NpcAttacks out;
    CHECK(decodes(in, out) && out.attacks.size() == 2 && out.attacks[0].ref == 0x104C79 && out.attacks[0].shots == 3 &&
        !out.attacks[0].reload && out.attacks[1].shots == 0 && out.attacks[1].reload);

    CHECK(!decodes(NpcAttacks{}, out));
    for (int i = 0; i < 5; ++i)
    {
        NpcAttacks m = in;
        if (i == 0) m.attacks[1].reload = false;
        if (i == 1) m.attacks[1].shots = kMaxShots + 1;
        if (i == 2) m.attacks[1].ref = m.attacks[0].ref;
        if (i == 3) m.attacks[1].ref = 0x14;
        if (i == 4) m.attacks[1].ref = 0;
        CHECK(!decodes(m, out));
    }
    NpcAttacks made{{{0xFF000801, 1, false}}};
    CHECK(decodes(made, out) && out.attacks[0].ref == 0xFF000801);
    NpcAttacks many;
    for (uint32_t i = 0; i < kMaxNpcsPerMessage + 5; ++i)
        many.attacks.push_back({0x1000 + i, 1, false});
    CHECK(decodes(many, out) && out.attacks.size() == kMaxNpcsPerMessage);
    CHECK(decodes(NpcAttacks{{{0x1234, 0, false, true}}}, out) && out.attacks[0].release && !out.attacks[0].reload &&
        out.attacks[0].shots == 0);
    Bytes b = Encode(NpcAttacks{{{0x1234, 1, true}}});
    b.back() = 4;
    Reader r(b);
    MsgType t;
    CHECK(r.Type(t) && !Decode(r, out));
}

TEST(a_throw_leaving_the_hand_is_its_own_attack)
{
    MsgType t;
    Attack a;
    Bytes start = Encode(Attack{2, 1, false});
    Reader rs(start);
    CHECK(rs.Type(t) && Decode(rs, a) && a.shots == 1 && !a.release);
    Bytes release = Encode(Attack{2, 5, true});
    Reader rr(release);
    CHECK(rr.Type(t) && Decode(rr, a) && a.release && a.shots == 0 && a.playerId == 2);
    Bytes none = Encode(Attack{2, 1, false});
    none[2] = 0;
    Reader rn(none);
    CHECK(rn.Type(t) && !Decode(rn, a));
    Bytes odd = Encode(Attack{2, 1, false});
    odd[3] = 2;
    Reader ro(odd);
    CHECK(ro.Type(t) && !Decode(ro, a));

    coop::Coop host, a1, a2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a1.Join("127.0.0.1", kTestPort, "A"));
    CHECK(a2.Join("127.0.0.1", kTestPort, "B"));
    std::vector<coop::Coop*> all{&host, &a1, &a2};
    CHECK(RunUntil(all, [&] { return a1.Remotes().size() == 2 && a2.Remotes().size() == 2; }));
    a1.Attack(1);
    a1.Attack(0, true);
    std::vector<coop::AttackReport> atHost, atB;
    CHECK(RunUntil(all, [&] {
        for (auto& x : host.TakeAttacks()) atHost.push_back(x);
        for (auto& x : a2.TakeAttacks()) atB.push_back(x);
        return atHost.size() == 2 && atB.size() == 2;
    }));
    CHECK(atHost[0].shots == 1 && !atHost[0].release && atHost[1].release && atHost[1].playerId == a1.MyPlayerId());
    CHECK(atB[1].release && atB[1].playerId == a1.MyPlayerId());
    a2.Stop();
    a1.Stop();
    host.Stop();
}

TEST(friends_see_the_hosts_npcs_health_death_idles_and_attacks)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    std::vector<NpcState> npcs{{0x5000, 0, 0, 0, 0, 0, 0, 70, 100, 0x4444, 0x8F216}, {0x5001, 0, 0, 0, 0, kNpcDead, 0, 0, 30, 0},
        {0x5002, 0, 0, 0, 0, kNpcDown | kNpcSitting | coop::Pose::kWeaponOut, 0, 0, 30, 0}};
    host.SetNpcs(0x777, npcs);
    CHECK(RunUntil({&host, &a}, [&] { return a.Npcs().size() == 3; }));
    const coop::RemoteNpc& alive = a.Npcs().at(0x5000);
    CHECK(!alive.dead && alive.health == 70 && alive.maxHealth == 100 && alive.idle == 0x4444 && alive.weapon == 0x8F216);
    CHECK(a.Npcs().at(0x5001).dead);
    const coop::RemoteNpc& down = a.Npcs().at(0x5002);
    CHECK(down.down && down.sitting && !down.dead && !alive.down && !alive.sitting);
    coop::Pose p;
    CHECK(a.Npcs().at(0x5001).motion.Sample(GetTickCount() + 1000, p) && p.flags == 0);
    CHECK(down.motion.Sample(GetTickCount() + 1000, p) && p.flags == coop::Pose::kWeaponOut);

    npcs[0].health = 20;
    host.SetNpcs(0x777, npcs);
    CHECK(RunUntil({&host, &a}, [&] { return a.Npcs().at(0x5000).health == 20; }));

    host.NpcAttacked(0x5000, 2);
    host.NpcAttacked(0x5000, 1, true);
    host.NpcAttacked(0x9999, 1);
    a.NpcAttacked(0x5000, 1);
    std::vector<NpcAttack> got;
    CHECK(RunUntil({&host, &a}, [&] {
        for (const NpcAttack& x : a.TakeNpcAttacks())
            got.push_back(x);
        return !got.empty();
    }));
    CHECK(got.size() == 1 && got[0].ref == 0x5000 && got[0].shots == 3 && got[0].reload);
    host.NpcAttacked(0x5000, 200);
    got.clear();
    CHECK(RunUntil({&host, &a}, [&] {
        for (const NpcAttack& x : a.TakeNpcAttacks())
            got.push_back(x);
        return !got.empty();
    }));
    CHECK(got.size() == 1 && got[0].shots == kMaxShots);
    a.Stop();
    host.Stop();
}

TEST(lab_channel_answers_tools_line_by_line)
{
    const wchar_t* name = L"\\\\.\\pipe\\NVCoopLabTest";
    dev::LabChannel lab;
    CHECK(lab.Start(name));
    dev::LabChannel impostor;
    CHECK(!impostor.Start(name));

    std::atomic<bool> done = false;
    std::thread game([&] {
        while (!done)
        {
            uint32_t id;
            std::string line;
            if (lab.TakeCommand(id, line))
                lab.Reply(id, "you said: " + line + "\nand more");
            Sleep(2);
        }
    });
    HANDLE tool = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(tool != INVALID_HANDLE_VALUE);
    auto ask = [&](const std::string& line) {
        std::string data = line + "\n", answer;
        DWORD n = 0;
        WriteFile(tool, data.data(), static_cast<DWORD>(data.size()), &n, nullptr);
        char c;
        while (ReadFile(tool, &c, 1, &n, nullptr) && n == 1 && c != '\n')
            answer += c;
        return answer;
    };
    CHECK(ask("status") == "you said: status | and more");
    CHECK(ask(std::string(1500, 'x')) == "refused: line too long");
    CHECK(ask("bad\x01line") == "refused: control characters");
    CHECK(ask("after") == "you said: after | and more");
    CloseHandle(tool);
    done = true;
    game.join();
    lab.Stop();
    CHECK(!lab.Running());
}

TEST(fake_lag_holds_messages_back_in_order)
{
    using Q = LagQueue<int>;
    int v = 0;
    Q off(FakeLag{});
    off.Push(1000, Q::Kind::Unreliable, 7);
    CHECK(off.Pop(1000, v) && v == 7);

    Q slow(FakeLag{100, 0, 0});
    slow.Push(1000, Q::Kind::Reliable, 1);
    CHECK(!slow.Pop(1049, v));
    CHECK(slow.Pop(1050, v) && v == 1);

    Q jittery(FakeLag{100, 80, 0}, 42);
    for (int i = 0; i < 50; ++i)
        jittery.Push(1000 + i, i % 2 ? Q::Kind::Unreliable : Q::Kind::Reliable, i);
    jittery.Push(1050, Q::Kind::Barrier, 999);
    int lastReliable = -1, lastUnreliable = -1, count = 0;
    bool inOrder = true, barrierLast = false;
    for (uint64_t now = 1000; now < 2000; ++now)
        while (jittery.Pop(now, v))
        {
            ++count;
            barrierLast = v == 999;
            if (v == 999)
                continue;
            int& last = v % 2 ? lastUnreliable : lastReliable;
            inOrder = inOrder && v > last;
            last = v;
        }
    CHECK(count == 51 && inOrder && barrierLast);

    Q lossy(FakeLag{100, 0, 100});
    lossy.Push(1000, Q::Kind::Unreliable, 1);
    lossy.Push(1000, Q::Kind::Reliable, 2);
    CHECK(lossy.Size() == 1);
    CHECK(!lossy.Pop(1149, v) && lossy.Pop(1150, v) && v == 2);
}

TEST(friends_with_bad_internet_still_join_and_play)
{
    coop::Coop host, a;
    a.GetSession().SetFakeLag({150, 50, 10});
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    host.Attack(2);
    a.Attack(3);
    std::vector<coop::AttackReport> atHost, atA;
    CHECK(RunUntil({&host, &a}, [&] {
        for (auto& x : host.TakeAttacks()) atHost.push_back(x);
        for (auto& x : a.TakeAttacks()) atA.push_back(x);
        return !atHost.empty() && !atA.empty();
    }));
    CHECK(atHost.size() == 1 && atHost[0].shots == 3 && atA.size() == 1 && atA[0].shots == 2);
    a.Stop();
    host.Stop();
}

TEST(players_see_each_other_move)
{
    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] { return b.Remotes().size() == 2 && a.Remotes().size() == 2; }));
    CHECK(b.Remotes().count(0) && b.Remotes().at(0).name == "Host");

    host.SetMyPose({5, 10, 20, 30, 1});
    a.SetMyPose({5, -1, -2, -3, 2});
    coop::Pose p;
    auto at = [&](coop::Coop& c, uint8_t id, float x) {
        auto it = c.Remotes().find(id);
        return it != c.Remotes().end() && it->second.motion.Sample(GetTickCount(), p) && p.x == x;
    };
    CHECK(RunUntil({&host, &a, &b}, [&] { return at(b, 0, 10) && at(b, a.MyPlayerId(), -1) && at(host, a.MyPlayerId(), -1); }));

    a.Stop();
    CHECK(RunUntil({&host, &b}, [&] { return b.Remotes().size() == 1; }));
    b.Stop();
    host.Stop();
}

TEST(doors_and_fast_travel_pull_everyone)
{
    MsgType t;
    net::Travel m;
    Bytes nowhere = Encode(net::Travel{1, 0, 1, 2, 3, 0});
    Reader r1(nowhere);
    CHECK(r1.Type(t) && t == MsgType::Travel && !Decode(r1, m));
    Bytes ok = Encode(net::Travel{2, 0x1234, 1, 2, 3, 0.5f});
    Reader r2(ok);
    CHECK(r2.Type(t) && Decode(r2, m) && m.playerId == 2 && m.space == 0x1234 && m.z == 3 && m.heading == 0.5f);

    coop::Coop host, a, b;
    host.SetTravelCooldownMs(1000);
    CHECK(host.Host(kTestPort, "Host"));
    host.SetMyPose({5, 0, 0, 0, 0});
    a.SetMyPose({5, 100, 0, 0, 0});
    b.SetMyPose({12, 0, 0, 0, 0});
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    std::vector<coop::Coop*> all{&host, &a, &b};
    coop::Pose p;
    uint8_t leader = 99;
    CHECK(RunUntil(all, [&] { return a.Remotes().size() == 2 && b.Remotes().size() == 2 && b.TakeTravel(p, leader); }));
    CHECK(p.space == 5 && p.x == 0 && leader == coop::kHostPlayerId);
    b.SetMyPose({5, 0, 0, 0, 0});
    RunUntil(all, [] { return false; }, 200);
    CHECK(!host.TakeTravel(p, leader) && !a.TakeTravel(p, leader) && !b.TakeTravel(p, leader));

    uint8_t idA = a.MyPlayerId();
    a.SetMyPose({7, 50, 50, 0, 0});
    bool hostGoes = false, bGoes = false;
    CHECK(RunUntil(all, [&] {
        if (host.TakeTravel(p, leader)) hostGoes = p.space == 7 && leader == idA;
        if (b.TakeTravel(p, leader)) bGoes = p.space == 7 && p.x == 50 && leader == idA;
        return hostGoes && bGoes;
    }));
    CHECK(!a.TakeTravel(p, leader));

    b.SetMyPose({9, 0, 0, 0, 0});
    CHECK(RunUntil(all, [&] { return b.TakeTravel(p, leader) && p.space == 7 && leader == idA; }));
    host.SetMyPose({7, 60, 50, 0, 0});
    b.SetMyPose({7, 40, 50, 0, 0});
    RunUntil(all, [] { return false; }, 1100);
    CHECK(!host.TakeTravel(p, leader) && !a.TakeTravel(p, leader) && !b.TakeTravel(p, leader));

    host.SetMyPose({7, 20000, 0, 0, 0});
    bool aGoes = false;
    bGoes = false;
    CHECK(RunUntil(all, [&] {
        if (a.TakeTravel(p, leader)) aGoes = p.x == 20000 && leader == coop::kHostPlayerId;
        if (b.TakeTravel(p, leader)) bGoes = p.x == 20000 && leader == coop::kHostPlayerId;
        return aGoes && bGoes;
    }));
    CHECK(!host.TakeTravel(p, leader));

    a.SetMyPose({7, 20000, 0, 0, 0});
    host.SetTravelCooldownMs(0);
    a.ForgetMyPlace();
    RunUntil(all, [] { return false; }, 100);
    a.SetMyPose({3, 0, 0, 0, 0});
    CHECK(RunUntil(all, [&] { return a.TakeTravel(p, leader) && p.space == 7 && p.x == 20000; }));
    CHECK(!host.TakeTravel(p, leader) && !b.TakeTravel(p, leader));

    host.ForgetMyPlace();
    RunUntil(all, [] { return false; }, 100);
    host.SetMyPose({4, 10, 0, 0, 0});
    aGoes = bGoes = false;
    CHECK(RunUntil(all, [&] {
        if (a.TakeTravel(p, leader)) aGoes = p.space == 4;
        if (b.TakeTravel(p, leader)) bGoes = p.space == 4;
        return aGoes && bGoes;
    }));
    b.Stop();
    a.Stop();
    host.Stop();
}

TEST(regroup_message_has_no_contents)
{
    Bytes ok = EncodeRegroup();
    Reader r(ok);
    MsgType t;
    CHECK(r.Type(t) && t == MsgType::Regroup && r.AtEnd());
}

namespace
{
    std::string FakeAnimName(uint16_t id) { return (id & 0xFF) == 3 ? "Forward" : "Idle"; }
}

TEST(inspector_names_movement_flags)
{
    CHECK(dev::MoveFlagNames(0) == "none");
    CHECK(dev::MoveFlagNames(0x201) == "Forward Running");
    CHECK(dev::MoveFlagNames(0x10080) == "0x10080");
}

TEST(inspector_ai_bits_round_trip)
{
    dev::AiBits a{true, false, true, false, 3, 14, -1, 41};
    dev::AiBits b = dev::UnpackAi(dev::PackAi(a));
    CHECK(b.weaponOut && !b.sneaking && b.inCombat && !b.restrained);
    CHECK(b.knocked == 3 && b.sitting == 14 && b.package == -1 && b.procedure == 41);
}

TEST(inspector_logs_changes_but_not_position)
{
    dev::ActorState a;
    a.mover = "NPC";
    a.animsKnown = true;
    dev::ActorState b = a;
    b.x = 500;
    CHECK(dev::Changes(a, b, FakeAnimName).empty());
    b.flags = 0x201;
    b.anims[0] = 0x0D03;
    auto lines = dev::Changes(a, b, FakeAnimName);
    CHECK(lines.size() == 2);
    CHECK(lines.size() == 2 && lines[0].find("Forward Running") != std::string::npos);
    CHECK(lines.size() == 2 && lines[1].find("layer 0: Forward (0x0D03)") != std::string::npos);
}

TEST(inspector_measures_speed)
{
    dev::ActorState a, b;
    b.x = 300;
    b.y = 400;
    CHECK(dev::Motion(a, b, 500).find("speed 1000 units/s") != std::string::npos);
    b.space = 7;
    CHECK(dev::Motion(a, b, 500).find("speed") == std::string::npos);
}

TEST(puppet_runs_forward_with_its_player)
{
    coop::Steer steer;
    uint32_t f = steer.Flags({0, 0, 0, 0, 0, 340, 0, 0x201});
    CHECK(f == (0x200 | 0x1));
}

TEST(puppet_moves_at_its_players_own_gait)
{
    coop::Steer walk;
    CHECK(walk.Flags({0, 0, 0, 70, 0, 90, 0, 0x101}) == (0x100 | 0x1));
    coop::Steer behind;
    CHECK(behind.Flags({0, 0, 0, 150, 0, 90, 0, 0x101}) == (0x200 | 0x1));
    coop::Steer stop;
    CHECK(stop.Flags({0, 0, 0, 0, 0, 340, 0, 0x201}) & 0x1);
    CHECK(!(stop.Flags({0, 0, 0, 10, 0, 0, 0, 0x200}) & 0xF));
    CHECK(stop.Flags({0, 0, 0, 60, 0, 0, 0, 0x200}) == (0x100 | 0x1));
    CHECK(stop.Flags({0, 0, 0, 20, 0, 0, 0, 0x200}) == (0x100 | 0x1));
    CHECK(!(stop.Flags({0, 0, 0, 10, 0, 0, 0, 0x200}) & 0xF));
    coop::Steer wall;
    CHECK(wall.Flags({0, 0, 0, 0, 0, 0, 0, 0x209}) == (0x200 | 0x1 | 0x8));
}

TEST(puppet_strafes_and_backs_up_facing_their_way)
{
    coop::Steer steer;
    CHECK(steer.Flags({0, 0, 0, 0, 90, 0, 0, 0}) == (0x100 | 0x8));
    coop::Steer back;
    CHECK(back.Flags({0, 0, 0, 0, -340, 0, 1.5707963f, 0}) == (0x200 | 0x2));
    coop::Steer diagonal;
    CHECK(diagonal.Flags({0, 0, 0, 0, 240, 240, 0, 0}) == (0x200 | 0x1 | 0x8));
}

TEST(puppet_walks_back_onto_its_spot)
{
    coop::Steer steer;
    CHECK(steer.Flags({0, 0, 0, 60, 0, 0, 0, 0}) == (0x100 | 0x1));
    coop::Steer still;
    CHECK(still.Flags({0, 0, 0, 5, 0, 0, 0, 0x20}) == (0x100 | 0x20));
}

TEST(puppet_does_not_flicker_between_walk_and_run)
{
    coop::Steer steer;
    CHECK(steer.Flags({0, 0, 0, 0, 0, 230, 0, 0}) & 0x200);
    CHECK(steer.Flags({0, 0, 0, 0, 0, 200, 0, 0}) & 0x200);
    CHECK(steer.Flags({0, 0, 0, 0, 0, 170, 0, 0}) & 0x100);
    CHECK(steer.Flags({0, 0, 0, 0, 0, 200, 0, 0}) & 0x100);
    CHECK(steer.Flags({0, 0, 0, 0, 0, 30, 0, 0}) & 0x1);
    CHECK(!(steer.Flags({0, 0, 0, 0, 0, 10, 0, 0}) & 0xF));
    CHECK(!(steer.Flags({0, 0, 0, 0, 0, 30, 0, 0}) & 0xF));
}

TEST(puppet_copies_sneaking_but_not_their_keys)
{
    coop::Steer steer;
    uint32_t f = steer.Flags({0, 0, 0, 0, 0, 90, 0, 0x400 | 0x8 | 0x40});
    CHECK(f == (0x400 | 0x100 | 0x1));
}

TEST(puppet_jump_flag_is_not_copied_to_its_legs)
{
    coop::Steer steer;
    CHECK(!(steer.Flags({0, 0, 0, 0, 0, 0, 0, 0x1000}) & 0x1000));
}

int main()
{
    for (auto& t : Tests())
    {
        int before = g_failures;
        t.run();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nAll tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}

TEST(door_messages_round_trip_and_checks)
{
    auto decodes = [](const DoorStates& in, DoorStates& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::DoorStates && Decode(r, out);
    };
    DoorStates in{{{0x1062AC, kDoorOpen, 0}, {0x109040, kDoorLocked, 255}}};
    DoorStates out;
    CHECK(decodes(in, out) && out.doors.size() == 2 && out.doors[0] == in.doors[0] && out.doors[1] == in.doors[1]);

    CHECK(!decodes(DoorStates{}, out));
    for (int i = 0; i < 4; ++i)
    {
        DoorStates m = in;
        if (i == 0) m.doors[1].ref = m.doors[0].ref;
        if (i == 1) m.doors[1].ref = 0;
        if (i == 2) m.doors[1].ref = 0xFF000801;
        if (i == 3) m.doors[1].flags = 4;
        CHECK(!decodes(m, out));
    }
    DoorStates many;
    for (uint32_t i = 0; i < kMaxDoorsPerMessage + 5; ++i)
        many.doors.push_back({0x1000 + i, 0, 0});
    CHECK(decodes(many, out) && out.doors.size() == kMaxDoorsPerMessage);

    Bytes b = EncodeDoorUse({0x1062AC, kDoorOpen, 0});
    Reader r(b);
    MsgType t;
    DoorState use;
    CHECK(r.Type(t) && t == MsgType::DoorUse && DecodeDoorUse(r, use) && use.ref == 0x1062AC && use.flags == kDoorOpen);
    b.push_back(0);
    Reader extra(b);
    CHECK(extra.Type(t) && !DecodeDoorUse(extra, use));
}

TEST(friend_doors_follow_the_host_and_send_own_uses)
{
    using coop::LocalDoor;
    coop::FriendDoors doors;
    std::vector<DoorState> uses;
    const DoorState closed{0x1062AC, 0, 0}, open{0x1062AC, kDoorOpen, 0};
    uint32_t now = 1000;

    doors.Heard(open);
    auto fixes = doors.Observe(now, {LocalDoor{closed, true}}, uses);
    CHECK(fixes.size() == 1 && fixes[0] == open && uses.empty());
    fixes = doors.Observe(now += 250, {LocalDoor{open, true}}, uses);
    CHECK(fixes.empty() && uses.empty());

    fixes = doors.Observe(now += 250, {LocalDoor{closed, true}}, uses);
    CHECK(fixes.empty() && uses.size() == 1 && uses[0] == closed);
    uses.clear();
    fixes = doors.Observe(now += 250, {LocalDoor{closed, true}}, uses);
    CHECK(fixes.empty() && uses.empty());
    doors.Heard(closed);
    fixes = doors.Observe(now += 250, {LocalDoor{closed, true}}, uses);
    CHECK(fixes.empty() && uses.empty());

    doors.Observe(now += 250, {LocalDoor{open, true}}, uses);
    CHECK(uses.size() == 1);
    uses.clear();
    fixes = doors.Observe(now += coop::kDoorUseWaitMs, {LocalDoor{open, true}}, uses);
    CHECK(fixes.size() == 1 && fixes[0] == closed && uses.empty());

    doors.Observe(now += 250, {LocalDoor{closed, false}}, uses);
    fixes = doors.Observe(now += 250, {LocalDoor{open, false}}, uses);
    CHECK(fixes.size() == 1 && fixes[0] == closed && uses.empty());

    doors.Observe(now += 250, {}, uses);
    CHECK(doors.Known() == 0);
}

TEST(doors_reach_friends_and_door_uses_reach_the_host)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));

    const DoorState closed{0x1062AC, 0, 0}, open{0x1062AC, kDoorOpen, 0}, unlocked{0x15EFB7, 0, 25};
    host.SetDoors({open, unlocked});
    std::vector<DoorState> fixes;
    CHECK(RunUntil({&host, &buddy}, [&] {
        fixes = buddy.ObserveDoors(GetTickCount(), {{closed, true}, {unlocked, true}});
        return !fixes.empty();
    }));
    CHECK(fixes.size() == 1 && fixes[0] == open);

    const DoorState locked{0x15EFB7, kDoorLocked, 25};
    buddy.ObserveDoors(GetTickCount(), {{open, true}, {unlocked, true}});
    buddy.ObserveDoors(GetTickCount(), {{open, true}, {locked, true}});
    std::vector<DoorState> asked;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (const DoorState& d : host.TakeDoorUses())
            asked.push_back(d);
        return !asked.empty();
    }));
    CHECK(asked.size() == 1 && asked[0] == locked);
    buddy.Stop();
    host.Stop();
}

TEST(container_messages_round_trip_and_checks)
{
    ContainerContents in{0x1062A9, 7, {{0x103B1B, 5}, {0x15169, 2}}};
    Bytes b = Encode(in);
    Reader r(b);
    MsgType t;
    ContainerContents out;
    CHECK(r.Type(t) && t == MsgType::ContainerContents && Decode(r, out) && out.ref == in.ref && out.seen == 7 &&
        out.items == in.items);
    auto decodes = [](const ContainerContents& m) {
        Bytes b = Encode(m);
        Reader r(b);
        MsgType t;
        ContainerContents out;
        return r.Type(t) && Decode(r, out);
    };
    CHECK((decodes(ContainerContents{0x1062A9, 0, {}})));
    for (int i = 0; i < 5; ++i)
    {
        ContainerContents m = in;
        if (i == 0) m.ref = 0;
        if (i == 1) m.items[1].item = m.items[0].item;
        if (i == 2) m.items[1].count = 0;
        if (i == 3) m.items[1].count = -2;
        if (i == 4) m.items[1].item = 0;
        CHECK(!decodes(m));
    }

    auto change = [](const ContainerChange& m, ContainerChange& out) {
        Bytes b = Encode(m);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::ContainerChange && Decode(r, out);
    };
    ContainerChange c;
    CHECK((change({0x1062A9, 3, {0x15169, -2}}, c) && c.ref == 0x1062A9 && c.seq == 3 && c.change.count == -2));
    CHECK((!change({0x1062A9, 3, {0x15169, 0}}, c)));
    CHECK((!change({0x1062A9, 3, {0x15169, kMaxItemCount + 1}}, c)));
    CHECK((!change({0x14, 3, {0x15169, 1}}, c)));

    Bytes v = EncodeContainerView(0);
    Reader rv(v);
    uint32_t ref = 1;
    CHECK(rv.Type(t) && t == MsgType::ContainerView && DecodeContainerView(rv, ref) && ref == 0);
    Bytes f = EncodeItemRefund({0x15169, -1});
    Reader rf(f);
    ItemCount refund;
    CHECK(rf.Type(t) && t == MsgType::ItemRefund && DecodeItemRefund(rf, refund) && refund.count == -1);
    Bytes z = EncodeItemRefund({0x15169, 0});
    Reader rz(z);
    CHECK(rz.Type(t) && !DecodeItemRefund(rz, refund));
}

TEST(friend_container_waits_for_the_host_to_see_its_changes)
{
    coop::FriendContainer box;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> start{{0x103B1B, 5}, {0x15169, 2}};

    CHECK(box.View(0x1062A9) && !box.View(0x1062A9));
    CHECK(box.Look(start, changes).empty() && changes.empty());
    box.Heard({0x1062A9, 0, {{0x15169, 3}}});
    auto fixes = box.Look(start, changes);
    CHECK((changes.empty() && fixes == (std::vector<ItemCount>{{0x15169, 1}, {0x103B1B, -5}}) ||
        fixes == (std::vector<ItemCount>{{0x103B1B, -5}, {0x15169, 1}})));
    const std::vector<ItemCount> matched{{0x15169, 3}};
    box.Settle(matched);

    const std::vector<ItemCount> took{{0x15169, 1}};
    CHECK(box.Look(took, changes).empty());
    CHECK((changes.size() == 1 && changes[0].seq == 1 && changes[0].change == ItemCount{0x15169, -2}));
    changes.clear();
    box.Heard({0x1062A9, 0, {{0x15169, 3}}});
    CHECK(box.Look(took, changes).empty() && changes.empty());
    box.Heard({0x1062A9, 1, {{0x15169, 1}}});
    CHECK(box.Look(took, changes).empty() && changes.empty());

    const std::vector<ItemCount> put{{0x15169, 2}};
    box.Look(put, changes);
    CHECK((changes.size() == 1 && changes[0].seq == 2 && changes[0].change == ItemCount{0x15169, 1}));
    box.Heard({0x1062A9, 2, {{0x15169, 1}}});
    fixes = box.Look(put, changes);
    CHECK((fixes == (std::vector<ItemCount>{{0x15169, -1}})));

    CHECK(box.View(0));
    box.Heard({0x1062A9, 2, {}});
    CHECK(box.Look(put, changes).size() == 0);
}

TEST(friend_container_is_matched_while_only_aimed_at)
{
    coop::FriendContainer box;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> ours{{0x103B1B, 5}};
    CHECK(box.View(0x1062A9, false));
    CHECK(box.Look(ours, changes).empty());
    box.Heard({0x1062A9, 0, {{0x15169, 3}}});
    auto fixes = box.Look(ours, changes);
    CHECK(fixes.size() == 2 && changes.empty());
    const std::vector<ItemCount> matched{{0x15169, 3}};
    box.Settle(matched);
    const std::vector<ItemCount> other{{0x15169, 2}};
    CHECK(box.Look(other, changes).empty() && changes.empty());
    CHECK(!box.View(0x1062A9, true));
    const std::vector<ItemCount> took{{0x15169, 1}};
    box.Look(took, changes);
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{0x15169, -1}));
}

TEST(friend_container_reports_what_was_taken_as_the_menu_shut)
{
    coop::FriendContainer body;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> full{{0x15169, 3}, {0x103B1B, 5}};
    CHECK(body.View(0x1062A9, false));
    CHECK(body.Look(full, changes).empty());
    CHECK(!body.View(0x1062A9, true));
    CHECK(body.Look(full, changes).empty() && changes.empty());

    // Take All: everything goes and the menu is shut before the next look.
    CHECK(!body.View(0x1062A9, false));
    CHECK(body.Look({}, changes).empty());
    CHECK(changes.size() == 2);
    int32_t took = 0;
    for (const ContainerChange& c : changes)
    {
        CHECK(c.ref == 0x1062A9 && c.change.count < 0);
        took -= c.change.count;
    }
    CHECK(took == 8);
    if (changes.empty())
        return;
    uint32_t lastSeq = changes.back().seq;
    changes.clear();

    // Only that one look counts: after it, changes while just aiming at the body aren't the player's.
    const std::vector<ItemCount> other{{0x15169, 1}};
    CHECK(body.Look(other, changes).empty() && changes.empty());
    CHECK(body.Look({}, changes).empty() && changes.empty());

    // The host's reply from before it heard about the loot can't put the items back; its next one matches.
    body.Heard({0x1062A9, lastSeq - 1, full});
    CHECK(body.Look({}, changes).empty() && changes.empty());
    body.Heard({0x1062A9, lastSeq, {}});
    CHECK(body.Look({}, changes).empty() && changes.empty());
}

TEST(friend_container_menu_opened_and_shut_between_looks_still_counts)
{
    coop::FriendContainer body;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> full{{0x15169, 3}};
    body.View(0x1062A9, false);
    body.Look(full, changes);
    body.View(0x1062A9, true);
    body.View(0x1062A9, false);
    body.Look({}, changes);
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{0x15169, -3}));
}

TEST(friend_container_closing_doesnt_carry_over_to_another_container)
{
    coop::FriendContainer box;
    std::vector<ContainerChange> changes;
    box.View(0x1062A9, true);
    box.Look({{0x15169, 3}}, changes);
    box.View(0x1062A9, false);
    CHECK(box.View(0x1062AA, false));
    box.Look({{0x15169, 3}}, changes);
    box.Look({}, changes);
    CHECK(changes.empty());
}

TEST(friend_container_counts_a_take_the_player_gained_with_no_menu_seen)
{
    const uint32_t gun = 0x15169, ammo = 0x103B1B;
    coop::FriendContainer body;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> full{{gun, 1, 80, 0}, {ammo, 5}};
    body.View(0x1062A9, false);
    body.Look(full, changes);

    // The player's copy can read a different condition: it's the same item leaving the body.
    const std::vector<ItemCount> noGun{{ammo, 5}};
    body.Look(noGun, changes, {{gun, 1, 79, 0}});
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{gun, -1, 80, 0}));
    changes.clear();

    // The game changing its copy of the body on its own isn't the player's doing, nor is an item gained elsewhere.
    body.Look({}, changes, {{gun, 3}});
    CHECK(changes.empty());
    body.Settle(noGun);

    // Only as many as the player really gained count.
    const std::vector<ItemCount> twoLeft{{ammo, 2}};
    body.Look(twoLeft, changes, {{ammo, 1}});
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{ammo, -1}));
    changes.clear();

    // Putting something in works the same way, and without the player losing it, it doesn't count.
    const std::vector<ItemCount> four{{ammo, 4}}, nine{{ammo, 9}};
    body.Look(four, changes, {{ammo, -2}});
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{ammo, 2}));
    changes.clear();
    body.Look(nine, changes, {{ammo, 5}});
    CHECK(changes.empty());
}

TEST(only_the_watched_containers_items_are_counted_on_the_player)
{
    const uint32_t gun = 0x15169, ammo = 0x103B1B, caps = 0xF;
    const std::vector<ItemCount> body{{gun, 1, 80, 0}, {gun, 2, 40, 0}, {ammo, 5}};

    // One count per item, whatever its stacks: it's what's on the body that's counted, and nothing else.
    CHECK((coop::FormsToCount({}, body) == std::vector<uint32_t>{gun, ammo} ||
        coop::FormsToCount({}, body) == std::vector<uint32_t>{ammo, gun}));
    CHECK(coop::FormsToCount({}, {}).empty());

    // What left the body since the last look is still counted, or a take of its last one would go unseen.
    const std::vector<ItemCount> noGun{{ammo, 5}};
    const coop::FormCounts before{{gun, 0}, {ammo, 10}};
    CHECK(coop::FormsToCount(before, noGun).size() == 2);

    coop::FormCounts now{{gun, 1}, {ammo, 10}};
    auto changes = coop::CountChanges(before, now);
    CHECK(changes.size() == 1 && changes[0].item == gun && changes[0].count == 1);
    CHECK(coop::CountChanges(now, now).empty());

    // Something the last look didn't know about isn't a change, and one it can't count now is down to none.
    now[caps] = 50;
    CHECK(coop::CountChanges(before, now).size() == 1);
    changes = coop::CountChanges(before, {{gun, 0}});
    CHECK(changes.size() == 1 && changes[0].item == ammo && changes[0].count == -10);

    // Only what's on the container now is carried over to the next look.
    auto kept = coop::CountsToKeep(now, noGun);
    CHECK(kept.size() == 1 && kept.at(ammo) == 10);
    CHECK(coop::CountsToKeep({}, noGun).at(ammo) == 0);
}

TEST(a_take_seen_through_counts_on_the_player_reaches_the_host)
{
    const uint32_t gun = 0x15169, ammo = 0x103B1B;
    coop::FriendContainer body;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> full{{gun, 1, 80, 0}, {ammo, 5}};
    body.View(0x1062A9, false);
    coop::FormCounts mine = coop::CountsToKeep({{gun, 3}, {ammo, 20}}, full);
    body.Look(full, changes, coop::CountChanges({}, mine));
    CHECK(changes.empty());

    const std::vector<ItemCount> left{{ammo, 5}};
    const coop::FormCounts now{{gun, 4}, {ammo, 20}};
    body.Look(left, changes, coop::CountChanges(mine, now));
    CHECK((changes.size() == 1 && changes[0].change == ItemCount{gun, -1, 80, 0}));
    changes.clear();

    // The player gaining something the body has, with the body unchanged, sends nothing.
    mine = coop::CountsToKeep(now, left);
    body.Look(left, changes, coop::CountChanges(mine, {{ammo, 30}}));
    CHECK(changes.empty());
}

TEST(friend_container_keeps_the_hosts_old_list_from_undoing_a_gained_take)
{
    const uint32_t gun = 0x15169;
    coop::FriendContainer body;
    std::vector<ContainerChange> changes;
    const std::vector<ItemCount> two{{gun, 2}};
    body.View(0x1062A9, false);
    body.Look(two, changes);
    body.Look({}, changes, two);
    CHECK(changes.size() == 1);
    if (changes.empty())
        return;
    uint32_t seq = changes[0].seq;
    body.Heard({0x1062A9, seq - 1, two});
    CHECK(body.Look({}, changes).empty());
    body.Heard({0x1062A9, seq, {}});
    CHECK(body.Look({}, changes).empty());
}

TEST(host_plans_a_take_from_what_the_container_really_has)
{
    const uint32_t gun = 0x15169, ammo = 0x103B1B;
    const std::vector<ItemCount> body{{ammo, 10}, {gun, 1, 40, 0}, {gun, 2, 80, 0}};

    auto exact = coop::PlanContainerTake(body, {gun, -1, 80, 0});
    CHECK((exact.taken == 1 && exact.missing == 0 && exact.removes == (std::vector<ItemCount>{{gun, -1, 80, 0}})));

    auto partial = coop::PlanContainerTake(body, {ammo, -25});
    CHECK((partial.taken == 10 && partial.missing == 15 && partial.removes == (std::vector<ItemCount>{{ammo, -10}})));

    // Their copy read a different condition: the body still loses one, and nobody is told it was gone.
    auto otherKind = coop::PlanContainerTake(body, {gun, -1, 41, 0});
    CHECK((otherKind.taken == 1 && otherKind.missing == 0 &&
        otherKind.removes == (std::vector<ItemCount>{{gun, -1, 40, 0}})));

    auto spills = coop::PlanContainerTake(body, {gun, -3, 80, 0});
    CHECK((spills.taken == 3 && spills.missing == 0 &&
        spills.removes == (std::vector<ItemCount>{{gun, -2, 80, 0}, {gun, -1, 40, 0}})));

    auto none = coop::PlanContainerTake(body, {0x999, -2});
    CHECK(none.taken == 0 && none.missing == 2 && none.removes.empty());

    // Two players taking the same thing: the second finds it gone and gets the refund.
    std::vector<ItemCount> left{{gun, 1, 80, 0}};
    auto first = coop::PlanContainerTake(left, {gun, -1, 80, 0});
    CHECK(first.taken == 1 && first.missing == 0);
    left.clear();
    auto second = coop::PlanContainerTake(left, {gun, -1, 80, 0});
    CHECK(second.taken == 0 && second.missing == 1);

    CHECK(coop::TotalOf(body, gun) == 3 && coop::TotalOf(body, ammo) == 10 && coop::TotalOf(body, 0x999) == 0);
}

TEST(take_all_reaches_the_host_and_the_body_stays_empty)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK((RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; })));

    const uint32_t body = 0x1062A9;
    const std::vector<ItemCount> hostHas{{0x15169, 3}};
    auto nothingPutBack = [&] {
        RunUntil({&host, &buddy}, [&] { CHECK(buddy.LookAtContainer({}).empty()); return false; }, 150);
    };
    buddy.ViewContainer(body, false);
    CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers() == std::vector<uint32_t>{body}; })));
    host.SetContainerContents(body, hostHas);
    RunUntil({&host, &buddy}, [&] { CHECK(buddy.LookAtContainer(hostHas).empty()); return false; }, 100);
    buddy.ViewContainer(body, true);
    buddy.LookAtContainer(hostHas);

    buddy.ViewContainer(body, false);
    buddy.LookAtContainer({});
    std::vector<coop::Coop::ItemChangeFrom> asked;
    CHECK((RunUntil({&host, &buddy}, [&] {
        for (auto& c : host.TakeContainerChanges())
            asked.push_back(c);
        return !asked.empty();
    })));
    CHECK((asked.size() == 1 && asked[0].change.ref == body && asked[0].change.change == ItemCount{0x15169, -3}));
    if (asked.empty())
    {
        buddy.Stop();
        host.Stop();
        return;
    }
    host.ContainerChangeDone(asked[0].playerId, asked[0].change.seq, {0x15169, 0});
    host.SetContainerContents(body, {});
    nothingPutBack();

    buddy.ViewContainer(0);
    CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers().empty(); })));
    buddy.ViewContainer(body, false);
    CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers() == std::vector<uint32_t>{body}; })));
    host.SetContainerContents(body, {});
    nothingPutBack();
    CHECK(buddy.TakeItemRefunds().empty());
    buddy.Stop();
    host.Stop();
}

TEST(container_changes_reach_the_host_and_contents_reach_the_friend)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK((RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; })));

    const uint32_t box = 0x1062A9;
    buddy.ViewContainer(box);
    CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers() == std::vector<uint32_t>{box}; })));
    const std::vector<ItemCount> hostHas{{0x15169, 3}};
    host.SetContainerContents(box, hostHas);
    std::vector<ItemCount> fixes;
    buddy.LookAtContainer({{0x15169, 1}});
    CHECK((RunUntil({&host, &buddy}, [&] { fixes = buddy.LookAtContainer({{0x15169, 1}}); return !fixes.empty(); })));
    CHECK((fixes == (std::vector<ItemCount>{{0x15169, 2}})));
    buddy.ContainerSettled(hostHas);

    buddy.LookAtContainer({});
    std::vector<coop::Coop::ItemChangeFrom> asked;
    CHECK((RunUntil({&host, &buddy}, [&] {
        for (auto& c : host.TakeContainerChanges())
            asked.push_back(c);
        return !asked.empty();
    })));
    CHECK((asked.size() == 1 && asked[0].playerId == 1 && asked[0].change.change == ItemCount{0x15169, -3}));
    host.ContainerChangeDone(asked[0].playerId, asked[0].change.seq, {0x15169, -1});
    std::vector<ItemCount> refunds;
    CHECK((RunUntil({&host, &buddy}, [&] { refunds = buddy.TakeItemRefunds(); return !refunds.empty(); })));
    CHECK((refunds.size() == 1 && refunds[0] == ItemCount{0x15169, -1}));

    buddy.ViewContainer(0);
    CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers().empty(); })));
    buddy.Stop();
    host.Stop();
}

TEST(a_friend_joining_again_sees_the_hosts_box_contents)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    const uint32_t box = 0x1062A9;
    const std::vector<ItemCount> hostHas{{0x15169, 3}};
    for (int round = 0; round < 2; ++round)
    {
        CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
        CHECK((RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; })));
        buddy.ViewContainer(box);
        CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers() == std::vector<uint32_t>{box}; })));
        host.SetContainerContents(box, hostHas);
        std::vector<ItemCount> fixes;
        buddy.LookAtContainer({{0x15169, 1}});
        CHECK((RunUntil({&host, &buddy}, [&] { fixes = buddy.LookAtContainer({{0x15169, 1}}); return !fixes.empty(); })));
        CHECK((fixes == (std::vector<ItemCount>{{0x15169, 2}})));
        buddy.ContainerSettled(hostHas);
        buddy.LookAtContainer({});
        std::vector<coop::Coop::ItemChangeFrom> asked;
        CHECK((RunUntil({&host, &buddy}, [&] {
            for (auto& c : host.TakeContainerChanges())
                asked.push_back(c);
            return !asked.empty();
        })));
        host.ContainerChangeDone(asked[0].playerId, asked[0].change.seq, asked[0].change.change);
        buddy.Stop();
        CHECK((RunUntil({&host, &buddy}, [&] { return host.ViewedContainers().empty(); })));
    }
    host.Stop();
}

TEST(item_messages_round_trip_and_checks)
{
    auto decodes = [](const ItemStates& in, ItemStates& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::ItemStates && Decode(r, out);
    };
    ItemStates in{{{0x171CAA, 0x171CE0, 1, true, -73107, 1302, 8891}, {0xFF00131C, 0x15169, 2, false, 1, 2, 3}}};
    in.items[0].health = 45;
    in.items[0].mods = 5;
    ItemStates out;
    CHECK(decodes(in, out) && out.items.size() == 2 && out.items[1].ref == 0xFF00131C && out.items[1].count == 2 &&
        !out.items[1].present && out.items[0].x == -73107.0f && out.items[0].health == 45 && out.items[0].mods == 5 &&
        out.items[1].health == kNoCondition && out.items[1].mods == 0);
    CHECK(!decodes(ItemStates{}, out));
    for (int i = 0; i < 7; ++i)
    {
        ItemStates m = in;
        if (i == 0) m.items[1].ref = m.items[0].ref;
        if (i == 1) m.items[1].ref = 0;
        if (i == 2) m.items[1].base = 0;
        if (i == 3) m.items[1].count = 0;
        if (i == 4) m.items[1].count = kMaxItemCount + 1;
        if (i == 5) m.items[0].health = 101;
        if (i == 6) m.items[0].mods = 8;
        CHECK(!decodes(m, out));
    }
    Bytes b = Encode(ItemDrop{0x15169, 2, 1, 2, 3});
    Reader r(b);
    MsgType t;
    ItemDrop drop;
    CHECK(r.Type(t) && t == MsgType::ItemDrop && Decode(r, drop) && drop.base == 0x15169 && drop.count == 2 &&
        drop.z == 3 && drop.health == kNoCondition);
    Bytes gb = Encode(ItemDrop{0x4332, 1, 1, 2, 3, 30, 3});
    Reader gr(gb);
    CHECK(gr.Type(t) && Decode(gr, drop) && drop.health == 30 && drop.mods == 3);
    Bytes bad = Encode(ItemDrop{0x4332, 1, 1, 2, 3, 200});
    Reader br(bad);
    CHECK(br.Type(t) && !Decode(br, drop));
    Bytes badMods = Encode(ItemDrop{0x4332, 1, 1, 2, 3, 30, 9});
    Reader bm(badMods);
    CHECK(bm.Type(t) && !Decode(bm, drop));
    Bytes tb = Encode(ItemTake{0x171CAA, 0x171CE0, 1});
    Reader tr(tb);
    ItemTake take;
    CHECK(tr.Type(t) && t == MsgType::ItemTake && Decode(tr, take) && take.ref == 0x171CAA &&
        take.health == kNoCondition && take.mods == 0);
    Bytes gt = Encode(ItemTake{0xFF001400, 0x4332, 1, 30, 7});
    Reader gtr(gt);
    CHECK(gtr.Type(t) && Decode(gtr, take) && take.health == 30 && take.mods == 7);
    Bytes badTake = Encode(ItemTake{0xFF001400, 0x4332, 1, 30, 8});
    Reader btr(badTake);
    CHECK(btr.Type(t) && !Decode(btr, take));
}

TEST(friend_items_follow_the_host_and_send_own_pickups_and_drops)
{
    using coop::LocalItem;
    coop::FriendItems items;
    const LocalItem can{0x171CAA, 0x171CE0, 1, true, 0, 0, 0, true};

    LocalItem leftover{0xFF000100, 0x15169, 1, true, 5, 5, 0, true};
    auto a = items.Look({can, leftover});
    CHECK(a.drops.empty() && a.remove == std::vector<uint32_t>{0xFF000100});

    items.Heard({0x171CAA, 0x171CE0, 1, false, 0, 0, 0});
    a = items.Look({can});
    CHECK(a.remove == std::vector<uint32_t>{0x171CAA});
    LocalItem hiddenCan = can;
    hiddenCan.present = false;
    a = items.Look({hiddenCan});
    CHECK(a.takes.empty());
    a = items.Look({});
    CHECK(a.takes.empty());

    LocalItem bottle{0x172217, 0x32EFE, 1, true, 10, 0, 0, true};
    items.Heard({0x172217, 0x32EFE, 1, true, 10, 0, 0});
    a = items.Look({bottle});
    CHECK(a.takes.empty() && a.remove.empty());
    bottle.present = false;
    a = items.Look({bottle});
    CHECK(a.takes.size() == 1 && a.takes[0].ref == 0x172217 && a.takes[0].base == 0x32EFE && a.remove.empty());
    a = items.Look({bottle});
    CHECK(a.takes.empty());

    LocalItem ammo{0x177D3C, 0x4240, 24, true, 0, 10, 0, true};
    items.Look({ammo});
    ammo.present = false;
    ammo.count = 1;
    a = items.Look({ammo});
    CHECK(a.takes.size() == 1 && a.takes[0].count == 24);

    items.Heard({0xFF00131C, 0x15169, 2, true, 100, 0, 0});
    a = items.Look({});
    CHECK(a.make.size() == 1 && a.make[0].ref == 0xFF00131C && a.make[0].count == 2);
    items.Made(0xFF00131C, 0xFF000200);
    LocalItem copy{0xFF000200, 0x15169, 2, true, 100, 0, 0, false};
    a = items.Look({copy});
    CHECK(a.make.empty() && a.remove.empty() && a.drops.empty() && a.move.empty());
    items.Heard({0xFF00131C, 0x15169, 2, true, 300, 0, 0});
    a = items.Look({copy});
    CHECK(a.move.size() == 1 && a.move[0].ref == 0xFF000200 && a.move[0].x == 300);
    items.Heard({0xFF00131C, 0x15169, 2, false, 300, 0, 0});
    a = items.Look({copy});
    CHECK(a.remove == std::vector<uint32_t>{0xFF000200} && items.Copies() == 0);

    LocalItem dropped{0xFF000300, 0x4240, 24, true, 7, 8, 9, true};
    a = items.Look({dropped});
    CHECK(a.drops.size() == 1 && a.drops[0].base == 0x4240 && a.drops[0].count == 24 && a.drops[0].x == 7 &&
        a.remove == std::vector<uint32_t>{0xFF000300});
    LocalItem distant{0xFF000400, 0x4240, 1, true, 900, 0, 0, false};
    a = items.Look({distant});
    CHECK(a.drops.empty() && a.remove == std::vector<uint32_t>{0xFF000400});

    LocalItem gun{0xFF000500, 0x4332, 1, true, 7, 8, 9, true};
    gun.health = 45;
    gun.mods = 2;
    a = items.Look({gun});
    CHECK(a.drops.size() == 1 && a.drops[0].health == 45 && a.drops[0].mods == 2);
    net::ItemState hostGun{0xFF001400, 0x4332, 1, true, 50, 0, 0};
    hostGun.health = 30;
    hostGun.mods = 7;
    items.Heard(hostGun);
    a = items.Look({});
    CHECK(a.make.size() == 1 && a.make[0].ref == 0xFF001400 && a.make[0].health == 30 && a.make[0].mods == 7);

    items.Made(0xFF001400, 0xFF000600);
    LocalItem gunCopy{0xFF000600, 0x4332, 1, true, 50, 0, 0, true};
    gunCopy.health = 30;
    gunCopy.mods = 7;
    a = items.Look({gunCopy});
    CHECK(a.takes.empty() && a.remove.empty());
    gunCopy.present = false;
    gunCopy.health = kNoCondition;
    gunCopy.mods = 0;
    a = items.Look({gunCopy});
    CHECK(a.takes.size() == 1 && a.takes[0].ref == 0xFF001400 && a.takes[0].health == 30 && a.takes[0].mods == 7);
    LocalItem placedGun{0x172300, 0x4332, 1, true, 60, 0, 0, true};
    placedGun.health = 55;
    placedGun.mods = 1;
    a = items.Look({placedGun});
    a = items.Look({});
    CHECK(a.takes.size() == 1 && a.takes[0].ref == 0x172300 && a.takes[0].health == 55 && a.takes[0].mods == 1);
}

TEST(item_pickups_and_drops_reach_the_host_and_items_reach_friends)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));

    net::ItemState stims{0xFF00131C, 0x15169, 2, true, 100, 0, 0};
    net::ItemState gun{0xFF001400, 0x4332, 1, true, 50, 0, 0};
    gun.health = 30;
    gun.mods = 4;
    host.SetWorldItems({stims, gun});
    coop::ItemActions a;
    buddy.LookAtItems({}, true);
    CHECK(RunUntil({&host, &buddy}, [&] { a = buddy.LookAtItems({}, true); return a.make.size() == 2; }));
    CHECK((a.make.size() == 2 && a.make[0].ref == 0xFF00131C && a.make[0].health == net::kNoCondition &&
        a.make[1].ref == 0xFF001400 && a.make[1].health == 30 && a.make[1].mods == 4));

    coop::LocalItem droppedGun{0xFF000310, 0x4332, 1, true, 7, 8, 9, true};
    droppedGun.health = 45;
    droppedGun.mods = 1;
    buddy.LookAtItems({{0xFF000300, 0x4240, 24, true, 7, 8, 9, true}, droppedGun}, true);
    std::vector<coop::Coop::ItemDropFrom> drops;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& d : host.TakeItemDrops())
            drops.push_back(d);
        return drops.size() >= 2;
    }));
    CHECK((drops.size() == 2 && drops[0].playerId == 1 && drops[0].drop.count == 24 &&
        drops[0].drop.health == net::kNoCondition && drops[1].drop.health == 45 && drops[1].drop.mods == 1));

    host.SetWorldItems({});
    bool gone = false;
    CHECK(RunUntil({&host, &buddy}, [&] {
        auto acts = buddy.LookAtItems({}, true);
        gone = buddy.LookAtItems({}, true).make.empty();
        return gone;
    }));
    buddy.Stop();
    host.Stop();
}

TEST(friend_items_real_pickups_and_quick_drops)
{
    using coop::LocalItem;
    coop::FriendItems items;
    items.Look({});

    items.Heard({0xFF00131C, 0x15169, 3, true, 100, 0, 0});
    auto a = items.Look({});
    CHECK(a.make.size() == 1);
    items.Made(0xFF00131C, 0xFF000200);
    LocalItem copy{0xFF000200, 0x15169, 3, true, 100, 0, 0, true};
    items.Look({copy});
    a = items.Look({});
    CHECK((a.takes.size() == 1 && a.takes[0].ref == 0xFF00131C && a.takes[0].count == 3 && a.takes[0].base == 0x15169));
    a = items.Look({});
    CHECK(a.takes.empty());

    LocalItem bottle{0x172217, 0x32EFE, 1, true, 10, 0, 0, true};
    items.Look({bottle});
    a = items.Look({}, false);
    CHECK(a.takes.empty());
    LocalItem distantBottle{0x172218, 0x32EFE, 1, true, 5000, 0, 0, false};
    items.Look({distantBottle});
    a = items.Look({});
    CHECK(a.takes.empty());

    LocalItem drop{0xFF000300, 0x4240, 5, false, 7, 8, 9, true};
    a = items.Look({drop});
    CHECK(a.drops.empty());
    drop.present = true;
    a = items.Look({drop});
    CHECK((a.drops.size() == 1 && a.drops[0].count == 5 && a.remove == std::vector<uint32_t>{0xFF000300}));
    drop.present = false;
    a = items.Look({drop});
    CHECK(a.drops.empty());
    a = items.Look({{0xFF000301, 0x4240, 1, true, 1, 1, 1, true}, {0xFF000302, 0x4241, 1, true, 2, 2, 2, true},
        {0xFF000303, 0x15169, 2, true, 3, 3, 3, true}});
    CHECK(a.drops.size() == 3);
    a = items.Look({{0xFF000304, 0xFF000050, 1, true, 1, 1, 1, true}});
    CHECK((a.drops.empty() && a.remove == std::vector<uint32_t>{0xFF000304}));
}

TEST(item_angles_and_moves_round_trip_and_checks)
{
    MsgType t;
    ItemStates states;
    Bytes sb = Encode(ItemStates{{{0x171CAA, 0x171CE0, 1, true, 1, 2, 3, 0.5f, -1.5f, 3.1f, true}}});
    Reader sr(sb);
    CHECK(sr.Type(t) && Decode(sr, states) && states.items.size() == 1 && states.items[0].held &&
        states.items[0].present && states.items[0].ry == -1.5f && states.items[0].rz == 3.1f);
    Bytes bent = Encode(ItemStates{{{0x171CAA, 0x171CE0, 1, true, 1, 2, 3, 0, 0, 100.f}}});
    Reader br(bent);
    CHECK(br.Type(t) && !Decode(br, states));

    ItemMoves moves;
    Bytes mb = Encode(ItemMoves{{{0x171CAA, 10, 20, 30, 0, 1.5f, -0.5f, true}, {0xFF00131C, 1, 2, 3}}});
    Reader mr(mb);
    CHECK(mr.Type(t) && t == MsgType::ItemMoves && Decode(mr, moves) && moves.moves.size() == 2 &&
        moves.moves[0].held && moves.moves[0].ry == 1.5f && moves.moves[1].ref == 0xFF00131C && !moves.moves[1].held);
    auto bad = [&](const ItemMoves& m) {
        Bytes b = Encode(m);
        Reader r(b);
        return r.Type(t) && !Decode(r, moves);
    };
    CHECK(bad(ItemMoves{}));
    CHECK(bad(ItemMoves{{{0x171CAA, 1, 2, 3}, {0x171CAA, 4, 5, 6}}}));
    CHECK(bad(ItemMoves{{{0, 1, 2, 3}}}));
    CHECK(bad(ItemMoves{{{0x14, 1, 2, 3}}}));
    CHECK(bad(ItemMoves{{{0x171CAA, 1, 2, 3, 0, 0, -9.f}}}));
    CHECK(bad(ItemMoves{{{0x171CAA, 1, 2, 3e8f}}}));
    std::vector<ItemMove> many(kMaxItemMovesPerMessage + 1);
    for (size_t i = 0; i < many.size(); ++i)
        many[i].ref = 0x100 + static_cast<uint32_t>(i);
    Bytes cut = Encode(ItemMoves{many});
    Reader cr(cut);
    CHECK(cr.Type(t) && Decode(cr, moves) && moves.moves.size() == kMaxItemMovesPerMessage);
    Bytes longer = Encode(ItemMoves{{{0x171CAA, 1, 2, 3}}});
    longer.push_back(0);
    Reader lr(longer);
    CHECK(lr.Type(t) && !Decode(lr, moves));
}

TEST(friend_items_send_our_own_moves_and_follow_the_hosts)
{
    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem can{0x171CAA, 0x171CE0, 1, true, 100, 0, 0, false};
    items.Heard({0x171CAA, 0x171CE0, 1, true, 100, 0, 0}, 1000);
    auto a = items.Look({can}, true, 1000);
    CHECK(a.moved.empty() && a.move.empty());

    can.x = 160;
    can.ry = 1.5f;
    a = items.Look({can}, true, 1500);
    CHECK((a.moved.size() == 1 && a.moved[0].ref == 0x171CAA && a.moved[0].x == 160 && a.moved[0].ry == 1.5f &&
        !a.moved[0].held && a.move.empty()));
    can.x = 170;
    a = items.Look({can}, true, 2000);
    CHECK(a.moved.size() == 1 && a.moved[0].x == 170 && a.move.empty());
    a = items.Look({can}, true, 2500);
    CHECK(a.moved.empty() && a.move.empty());
    items.Heard({0x171CAA, 0x171CE0, 1, true, 170, 0, 0, 0, 1.5f, 0}, 3000);
    a = items.Look({can}, true, 2000 + coop::kOwnItemMs);
    a = items.Look({can}, true, 2500 + coop::kOwnItemMs);
    CHECK(a.moved.empty() && a.move.empty());

    items.Heard({0x171CAA, 0x171CE0, 1, true, 170, 0, 0, 1.6f, 1.5f, 0}, 6000);
    a = items.Look({can}, true, 6000);
    CHECK((a.move.size() == 1 && a.move[0].rx == 1.6f && a.move[0].ry == 1.5f && a.moved.empty()));
    can.rx = 1.6f;
    can.following = true;
    items.Heard({0x171CAA, 0x171CE0, 1, true, 170, 0, 0, 1.7f, 1.5f, 0}, 6500);
    a = items.Look({can}, true, 6500);
    CHECK(a.move.empty() && a.moved.empty());
    can.x = 400;
    items.Heard({0x171CAA, 0x171CE0, 1, true, 400, 0, 0, 1.6f, 1.5f, 0}, 7000);
    a = items.Look({can}, true, 7000);
    CHECK(a.moved.empty());
    can.following = false;

    can.held = true;
    items.Heard({0x171CAA, 0x171CE0, 1, true, 400, 0, 0, 1.6f, 1.5f, 0}, 8000);
    a = items.Look({can}, true, 8000);
    CHECK(a.moved.size() == 1 && a.moved[0].held && a.moved[0].x == 400);
    a = items.Look({can}, true, 8500);
    CHECK(a.moved.empty());
    a = items.Look({can}, true, 8000 + coop::kHeldResendMs);
    CHECK(a.moved.size() == 1 && a.moved[0].held);
    can.x = 500;
    items.Heard({0x171CAA, 0x171CE0, 1, true, 400, 0, 0, 1.6f, 1.5f, 0}, 10500);
    a = items.Look({can}, true, 10500);
    CHECK(a.moved.size() == 1 && a.moved[0].x == 500);
    can.held = false;
    a = items.Look({can}, true, 11000);
    CHECK(a.moved.size() == 1 && !a.moved[0].held && a.move.empty());

    LocalItem bottle{0x172217, 0x32EFE, 1, true, 0, 0, 0, false};
    items.Heard({0x172217, 0x32EFE, 1, true, 0, 0, 50, 0, 0, 0, true}, 20000);
    a = items.Look({bottle}, true, 20000);
    CHECK(a.move.size() == 1 && a.move[0].held && a.move[0].z == 50);
    bottle.z = 50;
    bottle.following = true;
    a = items.Look({bottle}, true, 20500);
    CHECK(a.move.empty() && a.moved.empty());
    items.Heard({0x172217, 0x32EFE, 1, true, 0, 0, 60, 0, 0, 0, true}, 20800);
    a = items.Look({bottle}, true, 20800);
    CHECK(a.move.size() == 1 && a.move[0].held && a.move[0].z == 60);
    items.Heard({0x172217, 0x32EFE, 1, true, 0, 0, 50}, 21000);
    a = items.Look({bottle}, true, 21000);
    CHECK(a.move.size() == 1 && !a.move[0].held);
    a = items.Look({bottle}, true, 21500);
    CHECK(a.move.empty());

    bottle.x = 900;
    bottle.following = false;
    a = items.Look({bottle}, false, 22000);
    CHECK(a.moved.empty());

    LocalItem weed{0x178A81, 0xCD5AE, 1, true, 0, 0, 9000, false};
    items.Heard({0x178A81, 0xCD5AE, 1, true, 0, 0, 9448}, 30000);
    a = items.Look({weed}, true, 30000);
    CHECK(a.move.size() == 1);
    weed.z = 8400;
    weed.following = true;
    a = items.Look({weed}, true, 31000);
    CHECK(a.move.empty() && a.moved.empty());
    weed.following = false;
    a = items.Look({weed}, true, 30000 + coop::kHostItemStaleMs + 1);
    CHECK(a.move.empty() && a.moved.empty());
    items.Heard({0x178A81, 0xCD5AE, 1, true, 0, 0, 9448}, 40000);
    a = items.Look({weed}, true, 40000);
    CHECK(a.move.size() == 1 && a.move[0].z == 9448);
}

TEST(friend_movables_move_both_ways_but_are_never_picked_up)
{
    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem crate{0x10B8D0, 0x1093C0, 1, true, 10, 0, 0, true};
    crate.movable = true;
    items.Heard({0x10B8D0, 0x1093C0, 1, true, 10, 0, 0});
    auto a = items.Look({crate}, true, 1000);
    CHECK(a.moved.empty() && a.move.empty() && a.takes.empty());
    crate.x = 80;
    a = items.Look({crate}, true, 1500);
    CHECK(a.moved.size() == 1 && a.moved[0].ref == 0x10B8D0 && a.moved[0].x == 80);
    a = items.Look({}, true, 2000);
    CHECK(a.takes.empty());
    a = items.Look({crate}, true, 2500);
    LocalItem hidden = crate;
    hidden.present = false;
    a = items.Look({hidden}, true, 3000);
    CHECK(a.takes.empty());
    LocalItem weed{0x178A81, 0xCD5AE, 1, true, 500, 0, 0, false};
    weed.movable = true;
    items.Heard({0x178A81, 0xCD5AE, 1, true, 900, 0, 0, 0, 0, 1.0f});
    a = items.Look({weed}, true, 4000);
    CHECK(a.move.size() == 1 && a.move[0].ref == 0x178A81 && a.move[0].x == 900 && a.move[0].rz == 1.0f);

    LocalItem rolling{0x178A7E, 0xCD5AE, 1, true, 5000, 0, 0, false};
    rolling.movable = true;
    items.Heard({0x178A7E, 0xCD5AE, 1, true, 5000, 0, 0}, 5000);
    items.Look({rolling}, true, 5000);
    rolling.x = 5200;
    rolling.rz = 2.0f;
    a = items.Look({rolling}, true, 5500);
    CHECK(a.moved.empty());
    LocalItem kicked{0x178A7C, 0xCD5AE, 1, true, 50, 0, 0, true};
    kicked.movable = true;
    items.Heard({0x178A7C, 0xCD5AE, 1, true, 50, 0, 0}, 5000);
    items.Look({kicked}, true, 5000);
    kicked.x = 700;
    kicked.nearMe = false;
    a = items.Look({kicked}, true, 5500);
    CHECK(a.moved.size() == 1 && a.moved[0].ref == 0x178A7C && a.moved[0].x == 700);
}

TEST(friends_item_moves_reach_the_host_checked)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));

    host.SetWorldItems({{0x171CAA, 0x171CE0, 1, true, 100, 0, 0},
        {0x172217, 0x32EFE, 1, true, 0, 0, 50, 0, 0, 0, true}});
    coop::LocalItem can{0x171CAA, 0x171CE0, 1, true, 0, 0, 0, false};
    coop::LocalItem bottle{0x172217, 0x32EFE, 1, true, 0, 0, 50, false};
    CHECK(RunUntil({&host, &buddy}, [&] { return !buddy.LookAtItems({can, bottle}, true).move.empty(); }));
    can.x = 100;
    buddy.LookAtItems({can, bottle}, true);
    can.x = 300;
    bottle.x = 200;
    buddy.LookAtItems({can, bottle}, true);
    std::vector<coop::Coop::ItemMoveFrom> moves;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& m : host.TakeItemMoves())
            moves.push_back(m);
        return !moves.empty();
    }));
    RunUntil({&host, &buddy}, [&] {
        for (auto& m : host.TakeItemMoves())
            moves.push_back(m);
        return false;
    }, 200);
    CHECK((moves.size() == 1 && moves[0].playerId == buddy.MyPlayerId() && moves[0].move.ref == 0x171CAA &&
        moves[0].move.x == 300));

    can.x = 300 + 2 * coop::kMaxItemMoveReach;
    buddy.LookAtItems({can, bottle, {0x175000, 0x32EFE, 1, true, 10, 10, 10, false}}, true);
    buddy.LookAtItems({can, bottle, {0x175000, 0x32EFE, 1, true, 900, 10, 10, false}}, true);
    moves.clear();
    RunUntil({&host, &buddy}, [&] {
        for (auto& m : host.TakeItemMoves())
            moves.push_back(m);
        return false;
    }, 300);
    CHECK(moves.empty());
    buddy.Stop();
    host.Stop();
}

TEST(desyncs_are_what_lasts_past_the_fixes)
{
    using coop::DesyncKind;
    using coop::DesyncAspect;
    coop::DesyncTracker t;
    const coop::Disagreement dead{DesyncKind::Npc, 0x104C0C, DesyncAspect::Life, "dead here, alive in the host's game"};
    t.Saw(dead, 1000);
    t.Saw(dead, 3000);
    CHECK(t.Sweep(3000).empty());
    CHECK(t.Sweep(3000 + coop::kDesyncGoneMs + 1).empty() && t.Lasting().Total() == 0);

    for (uint32_t ms = 10000; ms <= 10000 + coop::kDesyncGraceMs; ms += 1000)
        t.Saw(dead, ms);
    auto events = t.Sweep(10000 + coop::kDesyncGraceMs);
    CHECK(events.size() == 1 && events[0].started && events[0].what.ref == 0x104C0C);
    CHECK(t.Lasting().npcs == 1 && t.Lasting().items == 0);
    t.Saw(dead, 16000);
    CHECK(t.Sweep(16000).empty());
    events = t.Sweep(16000 + coop::kDesyncGoneMs + 1);
    CHECK(events.size() == 1 && !events[0].started && events[0].lastedMs == 6000 && t.Lasting().Total() == 0);

    t.Saw({DesyncKind::Door, 0x1, DesyncAspect::Door, "should be open"}, 30000);
    t.Saw({DesyncKind::Item, 0x1, DesyncAspect::Place, "100 units off"}, 30000);
    t.Saw({DesyncKind::Item, 0x1, DesyncAspect::Place, "120 units off"}, 34000);
    t.Saw({DesyncKind::Item, 0x1, DesyncAspect::Place, "130 units off"}, 34000 + coop::kDesyncGoneMs + 1);
    CHECK(t.Sweep(34000 + coop::kDesyncGoneMs + 1).empty());
    for (uint32_t i = 0; i < coop::kMaxDesyncs + 10; ++i)
        t.Saw({DesyncKind::Npc, 0x1000 + i, DesyncAspect::Health, ""}, 50000);
    t.Sweep(50000);

    MsgType mt;
    DesyncReport rep;
    Bytes b = Encode(DesyncReport{2, 0, 1});
    Reader r(b);
    CHECK(r.Type(mt) && mt == MsgType::DesyncReport && Decode(r, rep) && rep.npcs == 2 && rep.doors == 1);
    Bytes many = Encode(DesyncReport{static_cast<uint16_t>(kMaxDesyncCount + 1), 0, 0});
    Reader rm(many);
    CHECK(rm.Type(mt) && !Decode(rm, rep));
}

TEST(friend_items_say_what_disagrees)
{
    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem can{0x171CAA, 0x171CE0, 1, true, 100, 0, 0, false};
    items.Heard({0x171CAA, 0x171CE0, 1, true, 400, 0, 0}, 1000);
    auto a = items.Look({can}, true, 1000);
    CHECK(a.differ.size() == 1 && a.differ[0].ref == 0x171CAA && a.differ[0].aspect == coop::DesyncAspect::Place);
    can.following = true;
    a = items.Look({can}, true, 1500);
    CHECK(a.differ.empty());
    LocalItem weed{0x178A81, 0xCD5AE, 1, true, 100, 0, 0, false};
    weed.movable = true;
    items.Heard({0x178A81, 0xCD5AE, 1, true, 900, 0, 0}, 2000);
    a = items.Look({weed}, true, 2000);
    CHECK(a.differ.empty());
    items.Heard({0x171CAA, 0x171CE0, 1, false, 400, 0, 0}, 3000);
    can.following = false;
    a = items.Look({can}, true, 3000);
    CHECK(a.differ.size() == 1 && a.differ[0].aspect == coop::DesyncAspect::There);
    LocalItem bottle{0x172217, 0x32EFE, 1, false, 10, 0, 0, false};
    items.Heard({0x172217, 0x32EFE, 1, true, 10, 0, 0}, 4000);
    a = items.Look({bottle}, true, 4000);
    CHECK(a.differ.size() == 1 && a.differ[0].ref == 0x172217 && a.differ[0].detail.find("gone here") == 0);
}

TEST(a_friends_lasting_desync_reaches_the_host)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    std::vector<std::string> atHost, atBuddy;
    CHECK(RunUntil({&host, &buddy}, [&] {
        buddy.SawDesync({coop::DesyncKind::Npc, 0x104C0C, coop::DesyncAspect::Life, "dead here, alive in the host's game"});
        for (auto& n : host.TakeDesyncNotices()) atHost.push_back(n);
        for (auto& n : buddy.TakeDesyncNotices()) atBuddy.push_back(n);
        return !atHost.empty() && !atBuddy.empty();
    }, coop::kDesyncGraceMs + 3000));
    CHECK(atBuddy.size() == 1 && atBuddy[0].find("dead here") != std::string::npos);
    CHECK(atHost.size() == 1 && atHost[0].find("Buddy") != std::string::npos);
    buddy.Stop();
    host.Stop();
}

TEST(companions_are_the_partys)
{
    MsgType t;
    net::CompanionStates cs;
    Bytes b = Encode(net::CompanionStates{{{0xE32A9, 0}, {0x92BD2, 2}, {0x104C0C, net::kCompanionWaiting}}});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::CompanionStates && Decode(r, cs) && cs.companions.size() == 3 &&
        cs.companions[1].leader == 2 && cs.companions[2].leader == net::kCompanionWaiting);
    Bytes none = Encode(net::CompanionStates{});
    Reader rn(none);
    CHECK(rn.Type(t) && Decode(rn, cs) && cs.companions.empty());
    auto bad = [&](const net::CompanionStates& m) {
        Bytes x = Encode(m);
        Reader rx(x);
        return rx.Type(t) && !Decode(rx, cs);
    };
    CHECK(bad(net::CompanionStates{{{0xE32A9, 0}, {0xE32A9, 1}}}));
    CHECK(bad(net::CompanionStates{{{0xE32A9, 9}}}));
    CHECK(bad(net::CompanionStates{{{0x14, 0}}}));
    net::CompanionOrder o;
    Bytes ob = Encode(net::CompanionOrder{0xE32A9, net::CompanionOrderKind::Wait});
    Reader ro(ob);
    CHECK(ro.Type(t) && t == MsgType::CompanionOrder && Decode(ro, o) && o.order == net::CompanionOrderKind::Wait);
    ob[ob.size() - 1] = 3;
    Reader ro2(ob);
    CHECK(ro2.Type(t) && !Decode(ro2, o));

    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    host.SetCompanions({{0xE32A9, coop::kHostPlayerId}});
    uint8_t leader = 9;
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.CompanionLeader(0xE32A9, leader); }));
    CHECK(leader == coop::kHostPlayerId && !buddy.CompanionLeader(0x92BD2, leader));
    buddy.OrderCompanion(0xE32A9, net::CompanionOrderKind::FollowMe);
    buddy.OrderCompanion(0x92BD2, net::CompanionOrderKind::FollowMe);
    std::vector<coop::Coop::CompanionOrderFrom> orders;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& x : host.TakeCompanionOrders()) orders.push_back(x);
        return !orders.empty();
    }));
    RunUntil({&host, &buddy}, [&] { for (auto& x : host.TakeCompanionOrders()) orders.push_back(x); return false; }, 200);
    CHECK((orders.size() == 1 && orders[0].playerId == buddy.MyPlayerId() && orders[0].order.ref == 0xE32A9 &&
        orders[0].order.order == net::CompanionOrderKind::FollowMe));
    host.SetCompanions({{0xE32A9, buddy.MyPlayerId()}});
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.CompanionLeader(0xE32A9, leader) && leader == buddy.MyPlayerId(); }));
    buddy.Stop();
    host.Stop();
}

TEST(friends_follow_what_quest_scripts_do_to_the_host)
{
    MsgType t;
    net::PlayerCommand c;
    net::PlayerCommand take{net::PlayerCommandKind::RemoveAllTypedItems, 0, 0x01004F12, {1, 0, 40}, 0};
    Bytes b = Encode(take);
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::PlayerCommand && Decode(r, c) && c.kind == net::PlayerCommandKind::RemoveAllTypedItems &&
        c.ref == 0x01004F12 && c.n[2] == 40);
    auto bad = [&](const net::PlayerCommand& m) {
        Bytes x = Encode(m);
        Reader rx(x);
        return rx.Type(t) && !Decode(rx, c);
    };
    CHECK(bad({net::PlayerCommandKind::AddItem, 0xFF000801, 0, {1}}));
    CHECK(bad({net::PlayerCommandKind::GiveAllItems, 0, 0x14}));
    CHECK(bad({net::PlayerCommandKind::AddItem, 0x15169, 0, {net::kMaxPlayerCommandNumber + 1}}));
    Bytes odd = Encode(net::PlayerCommand{net::PlayerCommandKind::AddPerk, 0x31DB7});
    odd[1] = static_cast<uint8_t>(net::PlayerCommandKind::Last) + 1;
    Reader ro(odd);
    CHECK(ro.Type(t) && !Decode(ro, c));

    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    host.SharePlayerCommand(take);
    host.SharePlayerCommand({net::PlayerCommandKind::EquipItem, 0x01004E0D, 0, {0, 1}});
    buddy.SharePlayerCommand({net::PlayerCommandKind::AddItem, 0x15169, 0, {5}});
    std::vector<net::PlayerCommand> got;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& x : buddy.TakePlayerCommands()) got.push_back(x);
        return got.size() >= 2;
    }));
    CHECK(got[0].kind == net::PlayerCommandKind::RemoveAllTypedItems && got[1].form == 0x01004E0D);
    CHECK(host.TakePlayerCommands().empty());
    buddy.Stop();
    host.Stop();
}

TEST(radios_play_the_hosts_songs)
{
    MsgType t;
    net::RadioLine l;
    std::vector<net::RadioPick> run = {{0x14DFB6, 0x14EB2F}, {0x14F114, 0x14F120}, {0x16ACEA, 0x16B812}};
    Bytes b = Encode(net::RadioLine{0x16B74C, 0x16B812, 3, 61000, 1, run});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::RadioLine && Decode(r, l) && l.station == 0x16B74C && l.info == 0x16B812 && l.seq == 3 &&
        l.elapsed == 61000 && l.part == 1 && l.run.size() == 3 && l.run[2].topic == 0x16ACEA && l.run[2].info == 0x16B812);
    auto refused = [&](const net::RadioLine& bad) {
        Bytes x = Encode(bad);
        Reader rx(x);
        return rx.Type(t) && !Decode(rx, l);
    };
    CHECK(refused(net::RadioLine{0x16B74C, 0x16B812, 3, 61000, 64, run}));
    CHECK(refused(net::RadioLine{0x16B74C, 0x16B812, 3, 3600000, 0, run}));
    CHECK(refused(net::RadioLine{0x16B74C, 0, 3, 0, 0, run}));
    CHECK(refused(net::RadioLine{0xFF000801, 0x16B812, 3, 0, 0, run}));
    CHECK(refused(net::RadioLine{0x16B74C, 0x16B812, 3, 0, 0, {{0x16ACEA, 0}}}));
    CHECK(refused(net::RadioLine{0x16B74C, 0x16B812, 3, 0, 0, {{0x16ACEA, 0xFF000900}}}));
    Bytes longRun = Encode(net::RadioLine{0x16B74C, 0x16B812, 3, 0, 0, std::vector<net::RadioPick>(40, run[0])});
    Reader rr(longRun);
    CHECK(rr.Type(t) && Decode(rr, l) && l.run.size() == net::kMaxRadioRun);
    Bytes lying = Encode(net::RadioLine{0x16B74C, 0x16B812, 3, 0, 0, {}});
    lying.back() = 200;
    Reader rly(lying);
    CHECK(rly.Type(t) && !Decode(rly, l));

    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    host.ShareRadioLine(net::RadioLine{0x16B74C, 0x16B812, 0, 0, 0, run}, 1000);
    host.ShareRadioLine(net::RadioLine{0x16B74C, 0x16B813, 0, 0, 1, run}, 2000);
    std::map<uint32_t, net::RadioLine> got;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& [s, x] : buddy.TakeRadioLines()) got[s] = x;
        return got.count(0x16B74C) && got[0x16B74C].info == 0x16B813;
    }));
    CHECK(got[0x16B74C].seq == 2);
    buddy.Stop();
    host.Stop();
}

TEST(chat_lines_are_cleaned_and_limited)
{
    using coop::CleanChat;
    CHECK(CleanChat("  hello   there  ") == "hello there");
    CHECK(CleanChat("a\tb\nc\x01" "d") == "a bcd");
    CHECK(CleanChat("<b>fish & chips</b>") == "(b)fish + chips(/b)");
    CHECK(CleanChat("caf\xE9") == "caf");
    CHECK(CleanChat("   ").empty() && CleanChat("").empty());
    CHECK(CleanChat(std::string(500, 'x')).size() == coop::kMaxChatLength);

    coop::ChatDraft d;
    using Done = coop::ChatDraft::Done;
    for (char c : std::string("hey"))
        CHECK(d.Type(static_cast<unsigned char>(c)) == Done::No);
    CHECK(d.Type('\b') == Done::No && d.Text() == "he");
    CHECK(d.Type(0x07) == Done::No && d.Type(0xE9) == Done::No && d.Text() == "he");
    CHECK(d.Type('\r') == Done::Send && d.Take() == "he" && d.Text().empty());
    d.Type('x');
    CHECK(d.Type(0x1B) == Done::Cancel && d.Text().empty());
    for (int i = 0; i < 300; ++i)
        d.Type('a');
    CHECK(d.Text().size() == coop::kMaxChatLength);
    CHECK(d.Type('\b') == Done::No && d.Text().size() == coop::kMaxChatLength - 1);

    using coop::MixSpeech;
    CHECK(MixSpeech(0, 100, 0, 0).volume == 1 && std::fabs(MixSpeech(0, 100, 0, 0).pan) < 0.01f);
    CHECK(MixSpeech(0, 5000, 0, 0).volume == 0);
    float half = (coop::kSpeechFullRange + coop::kSpeechSilentRange) / 2;
    CHECK(std::fabs(MixSpeech(0, half, 0, 0).volume - 0.5f) < 0.01f);
    CHECK(MixSpeech(300, 0, 0, 0).pan > 0.99f);
    CHECK(MixSpeech(-300, 0, 0, 0).pan < -0.99f);
    CHECK(MixSpeech(300, 0, 0, 1.5707964f).pan < 0.01f);
    CHECK(MixSpeech(0, 0, 0, 0).volume == 1 && MixSpeech(0, 0, 0, 0).pan == 0);

    coop::ChatLimiter limit;
    for (int i = 0; i < coop::kChatLinesPerWindow; ++i)
        CHECK(limit.Allow(1000 + i));
    CHECK(!limit.Allow(1100));
    CHECK(limit.Allow(1000 + coop::kChatWindowMs));
}

TEST(voice_effects_change_how_a_line_sounds)
{
    using coop::ApplyVoiceEffect;
    using coop::VoiceEffect;
    const uint32_t rate = 22050;
    auto tone = [&](float hz, size_t n) {
        std::vector<int16_t> s(n);
        for (size_t i = 0; i < n; ++i)
            s[i] = static_cast<int16_t>(10000 * std::sin(6.2831853f * hz * i / rate));
        return s;
    };
    auto loudness = [](const std::vector<int16_t>& s, size_t from, size_t to) {
        double sum = 0;
        for (size_t i = from; i < to && i < s.size(); ++i)
            sum += std::fabs(static_cast<double>(s[i]));
        return sum / static_cast<double>(to - from);
    };
    std::vector<int16_t> line = tone(1000, rate / 2);
    std::vector<int16_t> same = line;
    ApplyVoiceEffect(same, rate, VoiceEffect::None);
    CHECK(same == line);
    std::vector<int16_t> echo = line;
    ApplyVoiceEffect(echo, rate, VoiceEffect::Echo);
    CHECK(echo.size() > line.size() && loudness(echo, line.size(), echo.size()) > 100);
    std::vector<int16_t> hum = tone(60, rate / 2), mid = tone(1000, rate / 2);
    ApplyVoiceEffect(hum, rate, VoiceEffect::Radio);
    ApplyVoiceEffect(mid, rate, VoiceEffect::Radio);
    CHECK(loudness(hum, 2000, hum.size()) < loudness(mid, 2000, mid.size()) / 3);
    std::vector<int16_t> robot = line, megaphone = line, none;
    ApplyVoiceEffect(robot, rate, VoiceEffect::Robot);
    ApplyVoiceEffect(megaphone, rate, VoiceEffect::Megaphone);
    ApplyVoiceEffect(none, rate, VoiceEffect::Echo);
    CHECK(robot != line && megaphone != line && none.empty() && robot.size() == line.size());

    coop::VoiceStyle style;
    CHECK(style.Valid());
    style.effect = static_cast<uint8_t>(VoiceEffect::Count);
    CHECK(!style.Valid());
}

TEST(voices_are_mixed_from_where_players_are)
{
    using namespace coop;
    VoiceMixer mixer;
    std::vector<int16_t> voice(kVoiceRate / 10, 8000);
    std::vector<int16_t> out(2 * 240);
    mixer.Push(1, voice.data(), kVoiceStartSamples / 2);
    mixer.SetMix(1, 1, 0);
    mixer.Mix(out.data(), 240);
    CHECK(out[0] == 0 && out[479] == 0 && mixer.Talking(1));
    mixer.Push(1, voice.data(), voice.size());
    mixer.Mix(out.data(), 240);
    mixer.Mix(out.data(), 240);
    CHECK(out[478] > 4000 && out[479] > 4000);
    CHECK(mixer.Level(1) > 7000 && coop::MouthOpening(mixer.Level(1)) == 1);
    CHECK(coop::MouthOpening(0) == 0 && coop::MouthOpening(300) == 0);
    CHECK(coop::MouthOpening(2100) > 0.4f && coop::MouthOpening(2100) < 0.6f);
    mixer.SetMix(1, 1, 1);
    mixer.Mix(out.data(), 240);
    mixer.Mix(out.data(), 240);
    CHECK(std::abs(out[478]) < 100 && out[479] > 6000);
    mixer.SetMix(1, 0, 0);
    mixer.Mix(out.data(), 240);
    mixer.Mix(out.data(), 240);
    CHECK(std::abs(out[478]) < 50 && std::abs(out[479]) < 50);
    for (int i = 0; i < 20; ++i)
        mixer.Mix(out.data(), 240);
    CHECK(!mixer.Talking(1) && mixer.Level(1) == 0);
    std::vector<int16_t> lots(kVoiceRate * 2, 100);
    mixer.Push(2, lots.data(), lots.size());
    CHECK(mixer.Talking(2));
    mixer.SetMix(2, 1, 0);
    size_t played = 0;
    for (int i = 0; i < 100 && mixer.Talking(2); ++i, played += 240)
        mixer.Mix(out.data(), 240);
    CHECK(played <= kVoiceMaxSamples + 240);

    VoiceLimiter limit;
    int allowed = 0;
    for (int i = 0; i < 200; ++i)
        allowed += limit.Allow(5000, 100) ? 1 : 0;
    CHECK(allowed == static_cast<int>(VoiceLimiter::kPacketsPerSecond));
    CHECK(limit.Allow(6000, 100));
}

TEST(voice_goes_to_everyone_through_the_host)
{
    MsgType t;
    net::VoiceData m;
    Bytes b = Encode(net::VoiceData{3, {1, 2, 3}});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::VoiceData && Decode(r, m) && m.player == 3 && m.data == Bytes({1, 2, 3}));
    Bytes none = Encode(net::VoiceData{3, {}});
    Reader rn(none);
    CHECK(rn.Type(t) && !Decode(rn, m));
    Bytes lying = Encode(net::VoiceData{3, {1, 2, 3}});
    lying[2] = 0x10;
    Reader rl(lying);
    CHECK(rl.Type(t) && !Decode(rl, m));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    a.SendVoice(Bytes(300, 7));
    std::vector<coop::Coop::VoiceHeard> atHost, atBob, atAnn;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (auto& x : host.TakeVoice()) atHost.push_back(x);
        for (auto& x : b2.TakeVoice()) atBob.push_back(x);
        for (auto& x : a.TakeVoice()) atAnn.push_back(x);
        return !atHost.empty() && !atBob.empty();
    }));
    CHECK(atHost[0].data.size() == 300 && atBob[0].data == atHost[0].data && atBob[0].player == atHost[0].player);
    CHECK(atAnn.empty());
    b2.Stop();
    a.Stop();
    host.Stop();
}

TEST(the_mode_switches_are_sent_to_friends_and_changes_reach_them)
{
    using namespace coop;
    SaveMode balanced{GameMode::Balanced, true};
    CHECK(SaveModeButtons() == (std::vector<std::string>{"Neither", "Co-op balanced", "Friendly fire", "Both"}));
    CHECK(SaveModeFromButton(0) == ModeFromSwitches(false, false));
    CHECK(SaveModeFromButton(1) == (SaveMode{GameMode::Balanced, false}));
    CHECK(SaveModeFromButton(2) == (SaveMode{GameMode::Normal, true}));
    CHECK(SaveModeFromButton(3) == balanced);
    CHECK(SaveModeFromButton(4).mode == GameMode::Unset && SaveModeFromButton(-1).mode == GameMode::Unset);
    CHECK(Describe(balanced) == "Co-op balanced on, friendly fire on");
    CHECK(Describe(ModeFromSwitches(false, false)) == "Co-op balanced off, friendly fire off");

    Coop host, a, b2;
    host.SetSessionMode(balanced);
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == FriendState::Joined && b2.GetFriendState() == FriendState::Joined;
    }));
    CHECK(a.SessionMode() == balanced);
    a.SetSessionMode(SaveMode{});
    CHECK(a.SessionMode() == balanced);
    uint8_t bob = b2.MyPlayerId(), hostId = host.MyPlayerId();
    a.ReportPlayerHit(bob, 25);
    a.ReportPlayerHit(hostId, 10);
    float bobTook = 0, hostTook = 0;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        bobTook += b2.TakeDamage();
        hostTook += host.TakeDamage();
        return bobTook > 0 && hostTook > 0;
    }));
    CHECK(bobTook == 25 && hostTook == 10);
    SaveMode plain = ModeFromSwitches(false, false);
    host.SetSessionMode(plain);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return a.SessionMode() == plain && b2.SessionMode() == plain; }));
    b2.Stop();
    a.Stop();
    host.Stop();

    Coop host2, c;
    host2.SetSessionMode(SaveMode{GameMode::Normal, false});
    CHECK(host2.Host(kTestPort, "Host"));
    CHECK(c.Join("127.0.0.1", kTestPort, "Cat"));
    CHECK(RunUntil({&host2, &c}, [&] { return c.GetFriendState() == FriendState::Joined; }));
    c.ReportPlayerHit(host2.MyPlayerId(), 50);
    float took = 0;
    RunUntil({&host2, &c}, [&] { took += host2.TakeDamage(); return false; }, 500);
    CHECK(took == 0);
    c.Stop();
    host2.Stop();
}

TEST(co_op_balanced_numbers)
{
    const coop::Balance& b = coop::CoopBalance();
    for (int endurance = 1; endurance <= 10; ++endurance)
    {
        float game = 100.0f + endurance * 20, jsawyer = 50.0f + endurance * 10;
        CHECK(std::fabs(game / b.damageToPlayers - jsawyer) < 0.01f);
    }
    CHECK(b.damageByPlayers > 1);
    CHECK(b.stimpakPerSecond * b.stimpakSeconds == 20);
    CHECK(b.stimpakSeconds > 6);
    CHECK(b.superStimpakPerSecond * b.superStimpakSeconds < 60);
    CHECK(coop::DamageTaken(10, true) == 20 && coop::DamageTaken(10, false) == 10);
    CHECK(std::fabs(2 * b.headshotMult - 3) < 0.01f && b.limbMult < 1);
}

TEST(items_keep_their_condition_and_weapon_mods)
{
    using net::ItemCount;
    const uint32_t gun = 0x000E3778, ammo = 0x0004241C;
    std::vector<ItemCount> host = {{gun, 1, 47, 3}, {gun, 1, 100, 0}, {ammo, 30}};
    std::vector<ItemCount> ours = {{gun, 2, 100, 0}, {ammo, 30}};
    std::vector<ItemCount> fix = coop::ItemDiff(ours, host);
    CHECK(fix.size() == 2);
    bool removedNew = false, addedWorn = false;
    for (const ItemCount& f : fix)
    {
        removedNew |= f.item == gun && f.health == 100 && f.mods == 0 && f.count == -1;
        addedWorn |= f.item == gun && f.health == 47 && f.mods == 3 && f.count == 1;
    }
    CHECK(removedNew && addedWorn);
    CHECK(coop::ItemDiff(host, host).empty());

    MsgType t;
    net::ContainerContents m;
    Bytes b = Encode(net::ContainerContents{0x00104C10, 3, host});
    Reader r(b);
    CHECK(r.Type(t) && Decode(r, m) && m.items == host);
    auto refused = [&](std::vector<ItemCount> items) {
        Bytes x = Encode(net::ContainerContents{0x00104C10, 3, items});
        Reader rx(x);
        return rx.Type(t) && !Decode(rx, m);
    };
    CHECK(refused({{gun, 1, 150, 0}}));
    CHECK(refused({{gun, 1, 50, 9}}));
    CHECK(refused({{gun, 1, 47, 3}, {gun, 2, 47, 3}}));
    CHECK(!refused({{gun, 1, 47, 3}, {gun, 2, 47, 1}}));
}

TEST(chat_goes_to_everyone_through_the_host)
{
    MsgType t;
    net::ChatLine m;
    coop::VoiceStyle hers{1, 2, -4, 3, static_cast<uint8_t>(coop::VoiceEffect::Robot)};
    Bytes b = Encode(net::ChatLine{2, "hi", hers});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::ChatLine && Decode(r, m) && m.player == 2 && m.text == "hi" && m.style.voice == 1 &&
        m.style.variant == 2 && m.style.pitch == -4 && m.style.speed == 3 && m.style.effect == hers.effect);
    Bytes empty = Encode(net::ChatLine{2, ""});
    Reader re(empty);
    CHECK(re.Type(t) && !Decode(re, m));
    for (coop::VoiceStyle odd : {coop::VoiceStyle{7}, coop::VoiceStyle{0, 9}, coop::VoiceStyle{0, 0, 90},
             coop::VoiceStyle{0, 0, 0, -50}, coop::VoiceStyle{0, 0, 0, 0, 200}})
    {
        Bytes x = Encode(net::ChatLine{2, "hi", odd});
        Reader rx(x);
        CHECK(rx.Type(t) && !Decode(rx, m));
    }

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    a.SendChat("  hello <all>  ", coop::VoiceStyle{1, 0, 3});
    std::vector<coop::Coop::ChatHeard> atHost, atBob, atAnn;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (auto& x : host.TakeChat()) atHost.push_back(x);
        for (auto& x : b2.TakeChat()) atBob.push_back(x);
        for (auto& x : a.TakeChat()) atAnn.push_back(x);
        return !atHost.empty() && !atBob.empty();
    }));
    CHECK(atHost[0].name == "Ann" && atHost[0].text == "hello (all)");
    CHECK(atBob[0].name == "Ann" && atBob[0].text == "hello (all)" && atBob[0].player == atHost[0].player);
    CHECK(atHost[0].style.voice == 1 && atHost[0].style.pitch == 3 && atBob[0].style.voice == 1 &&
        atBob[0].style.pitch == 3);
    host.SendChat("welcome");
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (auto& x : a.TakeChat()) atAnn.push_back(x);
        return !atAnn.empty();
    }));
    CHECK(atAnn.size() == 1 && atAnn[0].name == "Host" && atAnn[0].text == "welcome");
    for (int i = 0; i < 20; ++i)
        b2.SendChat("spam");
    size_t heard = 0;
    RunUntil({&host, &a, &b2}, [&] { heard += host.TakeChat().size(); return false; }, 1500);
    CHECK(heard == static_cast<size_t>(coop::kChatLinesPerWindow));
    b2.Stop();
    a.Stop();
    host.Stop();
}

TEST(friend_items_drops_with_reused_ids_are_still_sent)
{
    using coop::LocalItem;
    coop::FriendItems items;
    items.Look({});

    LocalItem first{0xFF000300, 0x4240, 5, true, 7, 8, 9, true};
    auto a = items.Look({first});
    CHECK(a.drops.size() == 1);
    LocalItem gone = first;
    gone.present = false;
    items.Look({gone});
    LocalItem second{0xFF000300, 0x15169, 1, true, 7, 8, 9, true};
    a = items.Look({second});
    CHECK((a.drops.size() == 1 && a.drops[0].base == 0x15169));

    items.Heard({0xFF00131C, 0x15169, 1, true, 100, 0, 0});
    items.Look({});
    items.Made(0xFF00131C, 0xFF000400);
    LocalItem copy{0xFF000400, 0x15169, 1, true, 100, 0, 0, true};
    items.Look({copy});
    a = items.Look({});
    CHECK(a.takes.size() == 1);
    LocalItem reused{0xFF000400, 0x4240, 20, true, 5, 5, 5, true};
    a = items.Look({reused});
    CHECK((a.drops.size() == 1 && a.drops[0].base == 0x4240 && a.drops[0].count == 20));
    items.Heard({0xFF00131C, 0x15169, 1, false, 100, 0, 0});
    a = items.Look({});
    CHECK(a.remove.empty());
}

TEST(quest_messages_round_trip_and_checks)
{
    auto decodes = [](const QuestStates& in, QuestStates& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::QuestStates && Decode(r, out);
    };
    QuestState vcg02{0x10A214, 0x20, 5, {5}, {{3, 3}, {5, 1}, {10, 0}}};
    QuestState vmq01{0x842DD, 0x21, 10, {10}, {{10, 1}, {25, 1}}};
    QuestStates out;
    CHECK((decodes(QuestStates{{vcg02, vmq01}}, out) && out.quests.size() == 2 && out.quests[0] == vcg02 &&
        out.quests[1] == vmq01));
    CHECK(!decodes(QuestStates{}, out));
    for (int i = 0; i < 3; ++i)
    {
        QuestStates m{{vcg02, vmq01}};
        if (i == 0) m.quests[1].quest = m.quests[0].quest;
        if (i == 1) m.quests[1].quest = 0;
        if (i == 2) m.quests[1].objectives[0].status = 4;
        CHECK(!decodes(m, out));
    }
    QuestStates many;
    for (uint32_t i = 0; i < kMaxQuestsPerMessage + 3; ++i)
        many.quests.push_back({0x1000 + i, 0x21, 0, {}, {}});
    CHECK(decodes(many, out) && out.quests.size() == kMaxQuestsPerMessage);
}

TEST(quest_news_says_what_the_game_would_have_shown)
{
    QuestState before{0x10A214, 0x00, 0, {}, {{3, 0}, {5, 0}}};
    QuestState after{0x10A214, 0x21, 5, {5}, {{3, 1}, {5, 0}}};
    coop::QuestNews news = coop::CompareQuest(before, after);
    CHECK((news.added && !news.completed && news.shown == std::vector<uint32_t>{3} && news.finished.empty()));
    QuestState later{0x10A214, 0x22, 10, {5, 10}, {{3, 3}, {5, 1}}};
    news = coop::CompareQuest(after, later);
    CHECK((!news.added && news.completed && news.shown == std::vector<uint32_t>{5} &&
        news.finished == std::vector<uint32_t>{3}));
    news = coop::CompareQuest(later, later);
    CHECK(!news.added && !news.completed && !news.failed && news.shown.empty() && news.finished.empty());
}

TEST(quests_reach_friends_and_joins_are_counted)
{
    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(host.Joins() == 0);
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(host.Joins() == 1);
    std::vector<QuestState> sent;
    for (uint32_t i = 0; i < 40; ++i)
        sent.push_back({0x2000 + i, 0x21, 10, {10}, {{10, 1}}});
    host.SendQuestStates(sent);
    std::vector<QuestState> got;
    CHECK(RunUntil({&host, &buddy}, [&] {
        for (auto& q : buddy.TakeQuestStates())
            got.push_back(q);
        return got.size() >= sent.size();
    }));
    CHECK(got == sent);
    buddy.Stop();
    host.Stop();
}

namespace
{
    std::string HexOf(const coop::Sha256::Digest& d)
    {
        std::string s;
        char b[3];
        for (uint8_t v : d)
        {
            std::snprintf(b, sizeof(b), "%02x", v);
            s += b;
        }
        return s;
    }

    net::ModFile Mod(const std::string& name, net::ModKind kind, uint64_t size, uint8_t print = 0)
    {
        net::ModFile f;
        f.name = name;
        f.kind = kind;
        f.size = size;
        f.hashed = print != 0;
        f.hash.fill(print);
        return f;
    }

    coop::ModList SomeMods()
    {
        using net::ModKind;
        return {Mod("FalloutNV.esm", ModKind::Plugin, 100), Mod("DeadMoney.esm", ModKind::Plugin, 50),
            Mod("Cool Mod.esp", ModKind::Plugin, 10, 7), Mod("Cool Mod - Main.bsa", ModKind::Archive, 2000, 8),
            Mod("Fallout - Meshes.bsa", ModKind::Archive, 9000), Mod("jip_nvse.dll", ModKind::Dll, 300, 9)};
    }
}

TEST(sha256_matches_the_standard)
{
    CHECK(HexOf(coop::Sha256::Of("", 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(HexOf(coop::Sha256::Of("abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(HexOf(coop::Sha256::Of(two, std::strlen(two))) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    coop::Sha256 h;
    std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left)
    {
        size_t n = left < chunk.size() ? left : chunk.size();
        h.Add(chunk.data(), n);
        left -= n;
    }
    CHECK(HexOf(h.Finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(mod_lists_round_trip_and_bad_names_are_dropped)
{
    using net::ModKind;
    ModFiles m;
    m.last = true;
    m.files = SomeMods();
    m.files[0].size = 0x1'2345'6789ull;
    Bytes b = Encode(m);
    Reader r(b);
    MsgType t;
    ModFiles got;
    CHECK(r.Type(t) && t == MsgType::ModFiles && Decode(r, got) && got.last && got.files.size() == m.files.size());
    for (size_t i = 0; i < m.files.size(); ++i)
        CHECK(got.files[i].name == m.files[i].name && got.files[i].kind == m.files[i].kind &&
            got.files[i].size == m.files[i].size && got.files[i].hashed == m.files[i].hashed &&
            got.files[i].hash == m.files[i].hash);

    for (const char* bad : {"..\\evil.esp", "a/b.esp", "C:evil.esp", "CON.esp", "nul .esp", "evil.dll", "evil.esp ",
        ".esp", "x?.esp", "evil.esp.exe", "COM\xB9.esp", "lpt\xB2.esp", "COM0.esp", "CONIN$.esp", "conout$.esp"})
    {
        ModFiles one;
        one.files = {Mod(bad, ModKind::Plugin, 1)};
        Bytes e = Encode(one);
        Reader er(e);
        ModFiles out;
        CHECK(er.Type(t) && !Decode(er, out));
    }
    CHECK(net::IsPlainModFileName("Real Time Menus - Fixes.esm", ModKind::Plugin));
    CHECK(net::IsPlainModFileName("DeadMoney - Main.BSA", ModKind::Archive));
    CHECK(!net::IsPlainModFileName("DeadMoney - Main.bsa", ModKind::Plugin));
    CHECK(net::IsPlainModFileName("jip_nvse.dll", ModKind::Dll));
    CHECK(net::IsPlainModFileName("COM10.esp", ModKind::Plugin) && net::IsPlainModFileName("Commonwealth.esp",
        ModKind::Plugin) && net::IsPlainModFileName("LPTX.esp", ModKind::Plugin));

    {
        ModFiles big;
        big.files = {Mod("Big.esp", ModKind::Plugin, 1ull << 63)};
        Bytes e = Encode(big);
        Reader er(e);
        ModFiles out;
        CHECK(er.Type(t) && !Decode(er, out));
        big.files = {Mod("Big.esp", ModKind::Plugin, net::kMaxModFileSize)};
        Bytes ok = Encode(big);
        Reader okr(ok);
        CHECK(okr.Type(t) && Decode(okr, out) && out.files[0].size == net::kMaxModFileSize);
    }

    Bytes cut = Encode(m);
    cut.pop_back();
    Reader cr(cut);
    CHECK(cr.Type(t) && !Decode(cr, got));
}

TEST(tale_of_two_wastelands_files_count_by_name_or_size_and_are_never_sent)
{
    CHECK(coop::IsMadeFromOwnGame("TaleOfTwoWastelands.esm") && coop::IsMadeFromOwnGame("taleoftwowastelands - main.bsa"));
    CHECK(coop::IsMadeFromOwnGame("YUPTTW.esm") && coop::IsMadeFromOwnGame("ThePitt - Sounds.bsa"));
    CHECK(!coop::IsMadeFromOwnGame("Cool Mod.esp") && !coop::IsMadeFromOwnGame("Cool Mod - Main.bsa"));
    CHECK(!coop::IsMadeFromOwnGame("Fallout - Meshes.bsa"));
    CHECK(coop::IsMadeFromOwnGame("Fallout3.esm") && coop::IsMadeFromOwnGame("Fallout3 - Meshes.bsa"));
    using net::ModKind;
    auto file = [](const char* name, ModKind kind, uint64_t size) {
        net::ModFile f;
        f.name = name;
        f.kind = kind;
        f.size = size;
        return f;
    };
    coop::ModList host{file("FalloutNV.esm", ModKind::Plugin, 100), file("TaleOfTwoWastelands.esm", ModKind::Plugin, 500),
        file("TaleOfTwoWastelands - Main.bsa", ModKind::Archive, 690142)};
    coop::ModList mine = host;
    mine[2].size = 675327;
    CHECK(!coop::CompareMods(host, mine).Any() && coop::ModsFingerprint(host) == coop::ModsFingerprint(mine));
    mine[1].size = 499;
    CHECK(coop::CompareMods(host, mine).different.size() == 1);
    mine = host;
    mine.pop_back();
    CHECK(coop::CompareMods(host, mine).missing.size() == 1);
    CHECK(coop::ModsFingerprint(host) != coop::ModsFingerprint(mine));
}

TEST(mod_differences_are_found_and_described)
{
    using net::ModKind;
    coop::ModList host = SomeMods(), mine = SomeMods();
    CHECK(!coop::CompareMods(host, mine).Any());
    CHECK(coop::ModsFingerprint(host) == coop::ModsFingerprint(mine));
    mine[2].name = "COOL MOD.ESP";
    CHECK(!coop::CompareMods(host, mine).Any());
    CHECK(coop::ModsFingerprint(host) == coop::ModsFingerprint(mine));

    mine[5].hash.fill(1);
    coop::ModDifference d = coop::CompareMods(host, mine);
    CHECK(d.Any() && !d.Blocks() && d.differentDlls == std::vector<std::string>{"jip_nvse.dll"});
    CHECK(coop::ModsFingerprint(host) == coop::ModsFingerprint(mine));
    CHECK(coop::DescribeForScreen(d, "Alex").find("Joined") == 0);

    mine = SomeMods();
    mine[2].hash.fill(1);
    mine.erase(mine.begin() + 3);
    mine.insert(mine.begin() + 3, Mod("Mine.esp", ModKind::Plugin, 5, 2));
    mine.pop_back();
    d = coop::CompareMods(host, mine);
    CHECK(d.Blocks() && !d.orderDiffers);
    CHECK(d.different == std::vector<std::string>{"Cool Mod.esp"});
    CHECK(d.missing == std::vector<std::string>{"Cool Mod - Main.bsa"});
    CHECK(d.extra == std::vector<std::string>{"Mine.esp"});
    CHECK(d.missingDlls == std::vector<std::string>{"jip_nvse.dll"});
    CHECK(coop::ModsFingerprint(host) != coop::ModsFingerprint(mine));
    std::string text = coop::DescribeForScreen(d, "Alex");
    CHECK(text.find("Alex's") != std::string::npos && text.find("Mine.esp") != std::string::npos &&
        text.find("install by hand: jip_nvse.dll") != std::string::npos);
    CHECK(coop::DescribeForLog(d).size() == 4);

    mine = SomeMods();
    mine[0].size = 101;
    CHECK(coop::CompareMods(host, mine).different == std::vector<std::string>{"FalloutNV.esm"});

    mine = SomeMods();
    std::swap(mine[1], mine[2]);
    d = coop::CompareMods(host, mine);
    CHECK(d.orderDiffers && d.missing.empty() && d.extra.empty() && d.Blocks());
    CHECK(d.orderAt == 2 && d.orderHost == "DeadMoney.esm" && d.orderMine == "Cool Mod.esp");
    CHECK(coop::DescribeForScreen(d, "Alex").find("from plugin 2: Alex's has DeadMoney.esm there, yours Cool Mod.esp") !=
        std::string::npos);
    CHECK(coop::DescribeForLog(d) == std::vector<std::string>{
        "the load order is different from plugin 2: the host has DeadMoney.esm there, we have Cool Mod.esp"});
    CHECK(coop::ModsFingerprint(host) != coop::ModsFingerprint(mine));

    CHECK(coop::ArchiveBelongsTo("DeadMoney - Main.bsa", "DeadMoney.esm"));
    CHECK(coop::ArchiveBelongsTo("cool mod.bsa", "Cool Mod.esp"));
    CHECK(!coop::ArchiveBelongsTo("Cool Mods - Main.bsa", "Cool Mod.esp"));
    CHECK(!coop::ArchiveBelongsTo("Cool Mod Main.bsa", "Cool Mod.esp"));
    CHECK(coop::IsGameArchive("Fallout - Voices1.bsa") && coop::IsGameArchive("Update.bsa"));
    CHECK(!coop::IsGameArchive("Cool Mod.bsa"));
}

TEST(the_mod_scan_finds_what_the_game_loads)
{
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    std::string data = std::string(temp) + "nvcoop_scan_test\\";
    std::string dlls = data + "NVSE\\Plugins\\";
    CreateDirectoryA(data.c_str(), nullptr);
    CreateDirectoryA((data + "NVSE").c_str(), nullptr);
    CreateDirectoryA(dlls.c_str(), nullptr);
    auto write = [](const std::string& path, const std::string& text)
    {
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "wb");
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    };
    write(data + "FalloutNV.esm", "game");
    write(data + "Cool Mod.esp", "abc");
    write(data + "Cool Mod - Main.bsa", "archive");
    write(data + "Fallout - Misc.bsa", "game archive");
    write(data + "Unused.bsa", "not loaded");
    write(data + "Fallout - Invalidation.bsa", "a mod manager's");
    write(data + "Off.esp", "not active");
    write(dlls + "jip_nvse.dll", "jip");
    write(dlls + "NVCoop.dll", "ours");
    std::string cache = data + "hashes.txt";
    DeleteFileA(cache.c_str());

    coop::ModList mods = coop::ScanMods(data, {"FalloutNV.esm", "Cool Mod.esp"}, cache, "nvcoop.dll");
    CHECK(mods.size() == 5);
    if (mods.size() == 5)
    {
        CHECK(mods[0].name == "FalloutNV.esm" && !mods[0].hashed && mods[0].size == 4);
        CHECK(mods[1].name == "Cool Mod.esp" && mods[1].hashed && HexOf(mods[1].hash) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(mods[2].name == "Cool Mod - Main.bsa" && mods[2].hashed);
        CHECK(mods[3].name == "Fallout - Misc.bsa" && !mods[3].hashed);
        CHECK(mods[4].name == "jip_nvse.dll" && mods[4].kind == net::ModKind::Dll && mods[4].hashed);
    }
    CHECK(GetFileAttributesA(cache.c_str()) != INVALID_FILE_ATTRIBUTES);
    coop::ModList again = coop::ScanMods(data, {"FalloutNV.esm", "Cool Mod.esp"}, cache, "nvcoop.dll");
    CHECK(coop::ModsFingerprint(again) == coop::ModsFingerprint(mods) && !coop::CompareMods(mods, again).Any());

    write(data + "TaleOfTwoWastelands.esm", "ttw");
    write(data + "TaleOfTwoWastelands - Main.bsa", "fallout 3's");
    coop::ModList ttw = coop::ScanMods(data, {"FalloutNV.esm", "TaleOfTwoWastelands.esm"}, cache, "nvcoop.dll");
    CHECK(ttw.size() == 5);
    if (ttw.size() == 5)
    {
        CHECK(ttw[1].name == "TaleOfTwoWastelands.esm" && !ttw[1].hashed && ttw[1].size == 3);
        CHECK(ttw[3].name == "TaleOfTwoWastelands - Main.bsa" && ttw[3].kind == net::ModKind::Archive && !ttw[3].hashed);
    }

    for (const char* f : {"FalloutNV.esm", "Cool Mod.esp", "Cool Mod - Main.bsa", "Fallout - Misc.bsa", "Unused.bsa",
        "Off.esp", "hashes.txt", "TaleOfTwoWastelands.esm", "TaleOfTwoWastelands - Main.bsa", "Fallout - Invalidation.bsa"})
        DeleteFileA((data + f).c_str());
    DeleteFileA((dlls + "jip_nvse.dll").c_str());
    DeleteFileA((dlls + "NVCoop.dll").c_str());
    RemoveDirectoryA(dlls.c_str());
    RemoveDirectoryA((data + "NVSE").c_str());
    RemoveDirectoryA(data.c_str());
}

TEST(a_friend_with_other_mods_is_kept_out_and_shown_what_differs)
{
    coop::ModList theirs = SomeMods();
    theirs[2].hash.fill(1);

    Bytes b = Encode(Hello{kProtocolVersion, "Courier", true, coop::ModsFingerprint(theirs)});
    Reader r(b);
    MsgType t;
    Hello h;
    CHECK(r.Type(t) && Decode(r, h) && h.hasMods && h.mods == coop::ModsFingerprint(theirs));

    coop::Coop host, buddy;
    host.SetMods(SomeMods());
    buddy.SetMods(theirs);
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    std::string report;
    CHECK(RunUntil({&host, &buddy}, [&] { report = buddy.TakeModQuestion(); return !report.empty(); }));
    CHECK(buddy.GetFriendState() == coop::FriendState::FixingMods);
    CHECK(report.find("can't join") != std::string::npos && report.find("Cool Mod.esp") != std::string::npos);
    buddy.Stop();
    host.Stop();

    coop::ModList otherDll = SomeMods();
    otherDll[5].hash.fill(1);
    coop::Coop host2, buddy2;
    host2.SetMods(SomeMods());
    buddy2.SetMods(otherDll);
    CHECK(host2.Host(kTestPort, "Host"));
    CHECK(buddy2.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host2, &buddy2}, [&] { return buddy2.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(buddy2.TakeModReport().find("jip_nvse.dll") != std::string::npos);
    buddy2.Stop();
    host2.Stop();
}

namespace
{
    std::string TempFolder(const char* name)
    {
        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        return std::string(temp) + name + "\\";
    }

    void WriteFile(const std::string& path, const std::string& text)
    {
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "wb");
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }

    std::string ReadFile(const std::string& path)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
            return "(none)";
        std::string text;
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0)
            text.append(b, n);
        std::fclose(f);
        return text;
    }

    void RemoveTree(const std::string& folder)
    {
        WIN32_FIND_DATAA found;
        HANDLE h = FindFirstFileA((folder + "*").c_str(), &found);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                std::string name = found.cFileName;
                if (name == "." || name == "..")
                    continue;
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RemoveTree(folder + name + "\\");
                else
                    DeleteFileA((folder + name).c_str());
            } while (FindNextFileA(h, &found));
            FindClose(h);
        }
        RemoveDirectoryA(folder.c_str());
    }
}

namespace
{
    struct FakeSteamNet;
    class FakeSteam : public net::SteamApi
    {
    public:
        FakeSteam(FakeSteamNet& net, net::SteamId id);
        ~FakeSteam() override;
        net::SteamId Me() override { return m_id; }
        bool Send(net::SteamId to, const uint8_t* data, uint32_t size, bool, int channel) override;
        bool Read(Bytes& out, net::SteamId& from, int channel) override
        {
            auto& in = m_inbox[channel & 1];
            if (in.empty())
                return false;
            from = in.front().first;
            out = std::move(in.front().second);
            in.pop_front();
            return true;
        }
        void Accept(net::SteamId who) override
        {
            m_accepted.insert(who);
            for (size_t i = 0; i < m_held.size();)
                if (std::get<0>(m_held[i]) == who)
                {
                    m_inbox[std::get<1>(m_held[i])].push_back({who, std::move(std::get<2>(m_held[i]))});
                    m_held.erase(m_held.begin() + i);
                }
                else
                    ++i;
        }
        bool IsFriend(net::SteamId who) override { return friends.count(who) != 0; }
        bool TakeSessionRequest(net::SteamId& who) override
        {
            if (m_requests.empty())
                return false;
            who = m_requests.front();
            m_requests.pop_front();
            return true;
        }
        bool TakeConnectFail(net::SteamId&, uint8_t&) override { return false; }

        void Arrive(net::SteamId from, int channel, Bytes data)
        {
            if (m_accepted.count(from))
                return m_inbox[channel].push_back({from, std::move(data)});
            bool asked = false;
            for (auto& h : m_held)
                asked |= std::get<0>(h) == from;
            m_held.emplace_back(from, channel, std::move(data));
            if (!asked)
                m_requests.push_back(from);
        }

        std::set<net::SteamId> friends;
        std::set<net::SteamId> m_accepted;

    private:
        FakeSteamNet& m_net;
        net::SteamId m_id;
        std::deque<std::pair<net::SteamId, Bytes>> m_inbox[2];
        std::vector<std::tuple<net::SteamId, int, Bytes>> m_held;
        std::deque<net::SteamId> m_requests;
    };

    struct FakeSteamNet
    {
        std::map<net::SteamId, FakeSteam*> users;
    };

    FakeSteam::FakeSteam(FakeSteamNet& net, net::SteamId id) : m_net(net), m_id(id) { net.users[id] = this; }
    FakeSteam::~FakeSteam() { m_net.users.erase(m_id); }

    bool FakeSteam::Send(net::SteamId to, const uint8_t* data, uint32_t size, bool, int channel)
    {
        m_accepted.insert(to);
        auto it = m_net.users.find(to);
        if (it != m_net.users.end())
            it->second->Arrive(m_id, channel & 1, Bytes(data, data + size));
        return true;
    }

    bool PumpUntil(std::vector<std::pair<net::SteamSession*, std::vector<net::Event>*>> sessions,
        const std::function<bool()>& done, uint32_t limitMs = 3000)
    {
        for (uint32_t start = GetTickCount(); GetTickCount() - start < limitMs;)
        {
            for (auto& [s, got] : sessions)
            {
                net::Event e;
                while (s->Poll(e))
                    got->push_back(std::move(e));
            }
            if (done())
                return true;
            Sleep(5);
        }
        return false;
    }

    bool Has(const std::vector<net::Event>& events, net::Event::Kind kind, net::PeerId peer)
    {
        for (const net::Event& e : events)
            if (e.kind == kind && e.peer == peer)
                return true;
        return false;
    }
}

TEST(steam_friends_connect_and_strangers_dont)
{
    FakeSteamNet net;
    FakeSteam hostSteam(net, 100), buddySteam(net, 200), strangerSteam(net, 300);
    hostSteam.friends = {200};
    net::SteamSession host(hostSteam), buddy(buddySteam), stranger(strangerSteam);
    CHECK(host.StartHost(4));
    CHECK(buddy.StartClient(100));
    stranger.SetTimeouts(15000, 400);
    CHECK(stranger.StartClient(100));
    std::vector<net::Event> h, b, x;
    CHECK(PumpUntil({{&host, &h}, {&buddy, &b}, {&stranger, &x}}, [&] {
        return Has(b, net::Event::Kind::Connected, 1) && Has(x, net::Event::Kind::Disconnected, 1); }));
    CHECK(Has(h, net::Event::Kind::Connected, 1) && h[0].from == "steam:200");
    CHECK(!Has(x, net::Event::Kind::Connected, 1) && !Has(h, net::Event::Kind::Connected, 2));
    CHECK(!hostSteam.m_accepted.count(300));

    buddy.Send(1, Bytes{1, 2, 3});
    host.Send(1, Bytes(5000, 7), false);
    h.clear();
    b.clear();
    CHECK(PumpUntil({{&host, &h}, {&buddy, &b}}, [&] {
        return Has(h, net::Event::Kind::Received, 1) && Has(b, net::Event::Kind::Received, 1); }));
    CHECK(h.back().data == (Bytes{1, 2, 3}) && b.back().data.size() == 5000);

    host.Send(1, Bytes{9, 9});
    host.Disconnect(1);
    b.clear();
    CHECK(PumpUntil({{&host, &h}, {&buddy, &b}}, [&] { return Has(b, net::Event::Kind::Disconnected, 1); }));
    CHECK(b.size() == 2 && b[0].kind == net::Event::Kind::Received && b[0].data == (Bytes{9, 9}));

    CHECK(buddy.StartClient(100));
    h.clear();
    b.clear();
    CHECK(PumpUntil({{&host, &h}, {&buddy, &b}}, [&] { return Has(b, net::Event::Kind::Connected, 1); }));
    CHECK(Has(h, net::Event::Kind::Connected, 2));
}

TEST(a_silent_steam_friend_is_dropped)
{
    FakeSteamNet net;
    FakeSteam hostSteam(net, 100);
    hostSteam.friends = {200};
    net::SteamSession host(hostSteam);
    host.SetTimeouts(500, 20000);
    CHECK(host.StartHost(4));
    std::vector<net::Event> h, b;
    {
        FakeSteam buddySteam(net, 200);
        net::SteamSession buddy(buddySteam);
        CHECK(buddy.StartClient(100));
        CHECK(PumpUntil({{&host, &h}, {&buddy, &b}}, [&] { return Has(b, net::Event::Kind::Connected, 1); }));
        buddySteam.friends.clear();
        net.users.erase(200);
    }
    CHECK(PumpUntil({{&host, &h}}, [&] { return Has(h, net::Event::Kind::Disconnected, 1); }));
}

TEST(our_own_pause_isnt_their_silence_and_floods_wait)
{
    FakeSteamNet net;
    FakeSteam hostSteam(net, 100), buddySteam(net, 200);
    hostSteam.friends = {200};
    net::SteamSession host(hostSteam), buddy(buddySteam);
    host.SetTimeouts(1000, 20000);
    CHECK(host.StartHost(4));
    CHECK(buddy.StartClient(100));
    std::vector<net::Event> h, b;
    CHECK(PumpUntil({{&host, &h}, {&buddy, &b}}, [&] { return Has(b, net::Event::Kind::Connected, 1); }));

    Sleep(2500);
    h.clear();
    PumpUntil({{&host, &h}, {&buddy, &b}}, [] { return false; }, 300);
    CHECK(!Has(h, net::Event::Kind::Disconnected, 1));

    for (int i = 0; i < 1000; ++i)
        buddy.Send(1, Bytes{1, 2});
    int first = 0;
    net::Event e;
    while (host.Poll(e))
        first += e.kind == net::Event::Kind::Received;
    CHECK(first > 0 && first <= net::SteamSession::kMaxPacketsPerPoll);
    int total = first;
    for (int frame = 0; frame < 10; ++frame)
        while (host.Poll(e))
            total += e.kind == net::Event::Kind::Received;
    CHECK(total == 1000);
}

TEST(coop_runs_over_steam)
{
    FakeSteamNet net;
    FakeSteam hostSteam(net, 100), buddySteam(net, 200);
    hostSteam.friends = {200};
    auto hostNet = std::make_unique<net::SteamSession>(hostSteam);
    auto buddyNet = std::make_unique<net::SteamSession>(buddySteam);
    CHECK(hostNet->StartHost(coop::kMaxFriends + 2));
    CHECK(buddyNet->StartClient(100));
    coop::Coop host, buddy;
    host.SetMods(SomeMods());
    buddy.SetMods(SomeMods());
    host.HostOn(std::move(hostNet), "Host");
    buddy.JoinOn(std::move(buddyNet), "Buddy");
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(buddy.NameOf(0) == "Host");
    buddy.Stop();
    host.Stop();
}

TEST(only_who_the_host_lets_in_joins)
{
    coop::Coop host;
    host.SetMods(SomeMods());
    host.SetAutoApprove(false);
    CHECK(host.Host(kTestPort, "Host"));

    coop::Coop buddy;
    buddy.SetMods(SomeMods());
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    std::vector<coop::JoinAsk> asks;
    CHECK(RunUntil({&host, &buddy}, [&] { asks = host.TakeJoinAsks(); return !asks.empty(); }));
    CHECK(asks.size() == 1 && asks[0].name == "Buddy" && !asks[0].wantsFiles && host.StillAsking(asks[0].id));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.TakeAskingHost(); }));
    CHECK(buddy.GetFriendState() == coop::FriendState::WaitingForWelcome && host.Joins() == 0);
    host.AnswerJoin(asks[0].id, false);
    CHECK(!host.StillAsking(asks[0].id));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Rejected; }));
    CHECK(buddy.TakeRejectReason().find("said no") != std::string::npos);
    buddy.Stop();

    coop::Coop buddy2;
    buddy2.SetMods(SomeMods());
    CHECK(buddy2.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy2}, [&] { asks = host.TakeJoinAsks(); return !asks.empty(); }));
    host.AnswerJoin(asks[0].id, true);
    CHECK(RunUntil({&host, &buddy2}, [&] { return buddy2.GetFriendState() == coop::FriendState::Joined; }));
    buddy2.Stop();
    coop::Coop buddy3;
    buddy3.SetMods(SomeMods());
    CHECK(buddy3.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy3}, [&] { return buddy3.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(host.TakeJoinAsks().empty());
    buddy3.Stop();

    coop::ModList theirs = SomeMods();
    theirs[2].hash.fill(1);
    coop::Coop other;
    other.SetMods(theirs);
    CHECK(other.Join("127.0.0.1", kTestPort, "Other"));
    CHECK(RunUntil({&host, &other}, [&] { asks = host.TakeJoinAsks(); return !asks.empty(); }));
    CHECK(asks[0].name == "Other" && asks[0].wantsFiles);
    CHECK(other.TakeModQuestion().empty());
    host.AnswerJoin(asks[0].id, true);
    CHECK(RunUntil({&host, &other}, [&] { return !other.TakeModQuestion().empty(); }));
    other.Stop();
    host.Stop();
}

TEST(the_main_menu_only_compares_mods)
{
    Bytes b = Encode(Hello{kProtocolVersion, "Courier", true, coop::ModsFingerprint(SomeMods()), true});
    Reader r(b);
    MsgType t;
    Hello h;
    CHECK(r.Type(t) && Decode(r, h) && h.checkOnly && h.hasMods);

    Bytes mb = Encode(ModsMatch{"Alex", "Quicksave"});
    Reader mr(mb);
    ModsMatch match;
    CHECK(mr.Type(t) && t == MsgType::ModsMatch && Decode(mr, match) && match.hostName == "Alex" &&
        match.save == "Quicksave");
    Bytes nameless = Encode(ModsMatch{"", "Quicksave"});
    Reader nr(nameless);
    CHECK(nr.Type(t) && !Decode(nr, match));

    coop::Coop host, buddy;
    host.SetMods(SomeMods());
    host.SetCurrentSave("Save 3");
    buddy.SetMods(SomeMods());
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy", true));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.ModsMatched(); }));
    CHECK(buddy.GetRole() == coop::Role::None);
    CHECK(buddy.MatchedHost().hostName == "Host" && buddy.MatchedHost().save == "Save 3");
    CHECK(RunUntil({&host}, [&] { return host.Joins() == 0; }));
    buddy.Stop();
    host.SetCurrentSave("Save\" ; quit");
    coop::Coop sneaky;
    sneaky.SetMods(SomeMods());
    CHECK(sneaky.Join("127.0.0.1", kTestPort, "Buddy", true));
    CHECK(RunUntil({&host, &sneaky}, [&] { return sneaky.ModsMatched(); }));
    CHECK(sneaky.MatchedHost().save.empty());
    sneaky.Stop();

    coop::ModList theirs = SomeMods();
    theirs[2].hash.fill(1);
    coop::Coop buddy2;
    buddy2.SetMods(theirs);
    CHECK(buddy2.Join("127.0.0.1", kTestPort, "Buddy", true));
    std::string question;
    CHECK(RunUntil({&host, &buddy2}, [&] { question = buddy2.TakeModQuestion(); return !question.empty(); }));
    CHECK(question.find("Cool Mod.esp") != std::string::npos && !buddy2.ModsMatched());
    buddy2.Stop();
    host.Stop();
}

TEST(mod_file_pieces_round_trip_and_checks)
{
    Bytes b = Encode(ModFileAsk{3, 2ull * kModPieceSize});
    Reader r(b);
    MsgType t;
    ModFileAsk ask;
    CHECK(r.Type(t) && t == MsgType::ModFileAsk && Decode(r, ask) && ask.index == 3 && ask.offset == 2ull * kModPieceSize);
    Bytes odd = Encode(ModFileAsk{3, 100});
    Reader ro(odd);
    CHECK(ro.Type(t) && !Decode(ro, ask));

    ModPiece piece{2, kModPieceSize, true, Bytes(100, 7)};
    Bytes pb = Encode(piece);
    Reader pr(pb);
    ModPiece got;
    CHECK(pr.Type(t) && Decode(pr, got) && got.index == 2 && got.offset == kModPieceSize && got.ok &&
        got.data == piece.data);
    Bytes big = pb;
    big[12] = 0xFF;
    big[13] = 0xFF;
    Reader br(big);
    CHECK(br.Type(t) && !Decode(br, got));
}

TEST(a_download_is_checked_against_the_hosts_list)
{
    std::string folder = TempFolder("nvcoop_download_test");
    RemoveTree(folder);
    std::string content(kModPieceSize + 500, 'x');
    net::ModFile f = Mod("Cool Mod.esp", net::ModKind::Plugin, content.size());
    f.hashed = true;
    f.hash = coop::Sha256::Of(content.data(), content.size());
    net::ModFile empty = Mod("Empty.esp", net::ModKind::Plugin, 0);
    empty.hashed = true;
    empty.hash = coop::Sha256::Of("", 0);

    coop::ModDownload d;
    CHECK(d.Start({{4, f}, {5, empty}}, folder));
    std::vector<ModFileAsk> asks;
    d.Asks(asks);
    CHECK(asks.size() == 2 && asks[0].index == 4 && asks[1].offset == kModPieceSize);
    CHECK(d.Got({4, 0, true, Bytes(content.begin(), content.begin() + kModPieceSize)}));
    CHECK(!d.Done());
    CHECK(d.Got({4, kModPieceSize, true, Bytes(content.begin() + kModPieceSize, content.end())}));
    CHECK(d.Done() && d.Finished() == (std::vector<std::string>{"Cool Mod.esp", "Empty.esp"}));
    CHECK(ReadFile(folder + "Cool Mod.esp") == content && d.BytesDone() == content.size());

    coop::ModDownload bad;
    net::ModFile tiny = Mod("Small.esp", net::ModKind::Plugin, 3);
    tiny.hashed = true;
    tiny.hash = coop::Sha256::Of("abc", 3);
    CHECK(bad.Start({{1, tiny}}, folder));
    asks.clear();
    bad.Asks(asks);
    CHECK(!bad.Got({1, 0, true, Bytes{'a', 'b', 'd'}}) && bad.Failed());
    CHECK(GetFileAttributesA((folder + "Small.esp").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesA((folder + "Small.esp.part").c_str()) == INVALID_FILE_ATTRIBUTES);
    for (ModPiece wrong : {ModPiece{2, 0, true, Bytes{'a', 'b', 'c'}}, ModPiece{1, kModPieceSize, true, Bytes{'a'}},
        ModPiece{1, 0, true, Bytes{'a', 'b', 'c', 'd'}}, ModPiece{1, 0, false, {}}})
    {
        coop::ModDownload w;
        CHECK(w.Start({{1, tiny}}, folder));
        asks.clear();
        w.Asks(asks);
        CHECK(!w.Got(wrong) && w.Failed());
    }
    RemoveTree(folder);
}

TEST(a_friend_fetches_the_hosts_mods_and_matches_its_load_order)
{
    std::string root = TempFolder("nvcoop_fix_test");
    RemoveTree(root);
    std::string hostData = root + "host\\", data = root + "friend\\Data\\";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(hostData.c_str(), nullptr);
    CreateDirectoryA((root + "friend").c_str(), nullptr);
    CreateDirectoryA(data.c_str(), nullptr);
    std::string big(3 * kModPieceSize + 17, 'b');
    WriteFile(hostData + "FalloutNV.esm", "game");
    WriteFile(hostData + "Cool Mod.esp", "cool v2");
    WriteFile(hostData + "New Mod.esp", "new");
    WriteFile(hostData + "New Mod - Main.bsa", big);
    WriteFile(data + "FalloutNV.esm", "game");
    WriteFile(data + "Cool Mod.esp", "cool v1");
    WriteFile(data + "Cool Mod - Old.bsa", "old archive");
    WriteFile(data + "Extra.esp", "extra");
    std::string pluginsTxt = root + "friend\\plugins.txt";
    WriteFile(pluginsTxt, "FalloutNV.esm\nCool Mod.esp\nExtra.esp\n");

    coop::ModList hostMods = coop::ScanMods(hostData, {"FalloutNV.esm", "New Mod.esp", "Cool Mod.esp"},
        root + "host.txt", "NVCoop.dll");
    coop::ModList friendMods = coop::ScanMods(data, {"FalloutNV.esm", "Cool Mod.esp", "Extra.esp"},
        root + "friend.txt", "NVCoop.dll");
    coop::Coop host, buddy;
    host.SetMods(hostMods);
    host.SetModFolders({hostData, "", "", ""});
    buddy.SetMods(friendMods);
    coop::ModFolders folders{data, root + "friend\\NVCoop Downloads\\", root + "friend\\NVCoop Backup\\now\\", pluginsTxt};
    buddy.SetModFolders(folders);
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    std::string question;
    CHECK(RunUntil({&host, &buddy}, [&] { question = buddy.TakeModQuestion(); return !question.empty(); }));
    CHECK(buddy.GetFriendState() == coop::FriendState::FixingMods);
    CHECK(question.find("Get 3 files") != std::string::npos && question.find("trust") != std::string::npos);
    buddy.AnswerModFix(true);
    std::string result;
    CHECK(RunUntil({&host, &buddy}, [&] { result = buddy.TakeModFixResult(); return !result.empty(); }));
    CHECK(result.find("Restart the game") != std::string::npos);
    CHECK(buddy.GetRole() == coop::Role::None);

    CHECK(ReadFile(data + "Cool Mod.esp") == "cool v2" && ReadFile(data + "New Mod.esp") == "new");
    CHECK(ReadFile(data + "New Mod - Main.bsa") == big);
    CHECK(ReadFile(folders.backup + "Cool Mod.esp") == "cool v1");
    CHECK(ReadFile(folders.backup + "Cool Mod - Old.bsa") == "old archive");
    CHECK(ReadFile(data + "Extra.esp") == "extra");
    CHECK(ReadFile(folders.backup + "plugins.txt") == "FalloutNV.esm\nCool Mod.esp\nExtra.esp\n");
    std::string active = ReadFile(pluginsTxt);
    CHECK(active.find("FalloutNV.esm\r\nNew Mod.esp\r\nCool Mod.esp\r\n") != std::string::npos &&
        active.find("Extra.esp") == std::string::npos);
    coop::ModList after = coop::ScanMods(data, {"FalloutNV.esm", "New Mod.esp", "Cool Mod.esp"}, root + "friend.txt",
        "NVCoop.dll");
    CHECK(coop::ModsFingerprint(after) == coop::ModsFingerprint(hostMods));
    auto written = [&](const char* name)
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        GetFileAttributesExA((data + name).c_str(), GetFileExInfoStandard, &a);
        return (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    };
    CHECK(written("FalloutNV.esm") < written("New Mod.esp") && written("New Mod.esp") < written("Cool Mod.esp"));
    host.Stop();

    coop::Coop host2, buddy2;
    host2.SetMods(hostMods);
    host2.SetModFolders({hostData, "", "", ""});
    buddy2.SetMods(friendMods);
    buddy2.SetModFolders(folders);
    CHECK(host2.Host(kTestPort, "Host"));
    CHECK(buddy2.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host2, &buddy2}, [&] { return !buddy2.TakeModQuestion().empty(); }));
    buddy2.AnswerModFix(false);
    CHECK(buddy2.GetRole() == coop::Role::None);
    host2.Stop();

    coop::Coop host3, buddy3;
    host3.SetMods(hostMods);
    host3.SetModFolders({hostData, "", "", ""});
    buddy3.SetMods(friendMods);
    coop::ModFolders managed = folders;
    managed.manager = "Mod Organizer 2";
    buddy3.SetModFolders(managed);
    std::string activeBefore = ReadFile(pluginsTxt);
    CHECK(host3.Host(kTestPort, "Host"));
    CHECK(buddy3.Join("127.0.0.1", kTestPort, "Buddy"));
    std::string report;
    CHECK(RunUntil({&host3, &buddy3}, [&] { report = buddy3.TakeModReport(); return !report.empty(); }));
    CHECK(report.find("Mod Organizer 2 sets your mods: match the host's there") != std::string::npos);
    CHECK(buddy3.TakeModQuestion().empty() && buddy3.GetRole() == coop::Role::None);
    CHECK(ReadFile(pluginsTxt) == activeBefore);
    host3.Stop();
    RemoveTree(root);
}

TEST(ref_enable_messages_round_trip_and_checks)
{
    auto decodes = [](const std::vector<RefEnable>& in, std::vector<RefEnable>& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::RefEnables && Decode(r, out);
    };
    std::vector<RefEnable> in{{0x10A215, true}, {0x104C0F, false}}, out;
    CHECK(decodes(in, out) && out == in);
    CHECK(!decodes({}, out));
    CHECK(!decodes({{0, true}}, out));
    CHECK(!decodes({{0x14, true}}, out));
    CHECK(!decodes({{0xFF000801, true}}, out));
}

TEST(progress_watcher_shares_own_gains_only)
{
    coop::ProgressWatcher w;
    coop::Gain g = w.Look(1000, 50);
    CHECK(g.xp == 0 && g.karma == 0);
    g = w.Look(1025, 50);
    CHECK(g.xp == 25 && g.karma == 0);
    w.Expect(40, -10);
    g = w.Look(1065, 40);
    CHECK(g.xp == 0 && g.karma == 0);
    w.Expect(40, 0);
    g = w.Look(1110, 40);
    CHECK(g.xp == 5 && g.karma == 0);
    w.Expect(0, 50);
    g = w.Look(1110, 60);
    CHECK(g.karma == 0);
    g = w.Look(1110, 35);
    CHECK(g.karma == -25);
    g = w.Look(500, 0);
    CHECK(g.xp == 0 && g.karma == 0);
}

TEST(progress_and_reputation_messages_round_trip_and_checks)
{
    auto progress = [](const Progress& in, Progress& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::Progress && Decode(r, out);
    };
    Progress out;
    CHECK(progress({2, 150, -5}, out) && out.playerId == 2 && out.xp == 150 && out.karma == -5);
    CHECK(!progress({0, 0, 0}, out));
    CHECK(!progress({0, -1, 0}, out));
    CHECK(!progress({0, kMaxXpGain + 1, 0}, out));
    CHECK(!progress({0, 0, kMaxKarmaChange + 1}, out));
    auto reps = [](const std::vector<Reputation>& in, std::vector<Reputation>& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::Reputations && Decode(r, out);
    };
    std::vector<Reputation> in{{0x11E662, 25, 0}, {0x11E663, 0, 40}}, got;
    CHECK(reps(in, got) && got == in);
    CHECK(!reps({}, got));
    CHECK(!reps({{0, 1, 1}}, got));
    CHECK(!reps({{0xFF000001, 1, 1}}, got));
    CHECK(!reps({{0x11E662, -1, 0}}, got));
}

TEST(a_friends_gains_reach_the_host_and_other_friends)
{
    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b.GetFriendState() == coop::FriendState::Joined;
    }));
    a.ShareProgress(30, 2);
    std::vector<Progress> atHost, atB, atA;
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& p : host.TakeProgress()) atHost.push_back(p);
        for (auto& p : b.TakeProgress()) atB.push_back(p);
        for (auto& p : a.TakeProgress()) atA.push_back(p);
        return !atHost.empty() && !atB.empty();
    }));
    CHECK((atHost.size() == 1 && atHost[0].playerId == a.MyPlayerId() && atHost[0].xp == 30 && atHost[0].karma == 2));
    CHECK((atB.size() == 1 && atB[0].playerId == a.MyPlayerId() && atB[0].xp == 30));
    CHECK(atA.empty());
    host.ShareProgress(12, 0);
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& p : a.TakeProgress()) atA.push_back(p);
        return !atA.empty();
    }));
    CHECK(atA.size() == 1 && atA[0].xp == 12 && atA[0].playerId == 0);
    a.Stop();
    b.Stop();
    host.Stop();
}

TEST(talk_lock_lets_one_player_talk_at_a_time)
{
    coop::TalkLock lock;
    CHECK(lock.MayTalk(0) && lock.MayTalk(2));
    CHECK(lock.Report(2, true, 0x1234));
    CHECK(lock.Busy() && lock.Talker() == 2 && lock.Npc() == 0x1234);
    CHECK(!lock.Report(0, true, 0x5678));
    CHECK(!lock.MayTalk(0) && lock.MayTalk(2));
    CHECK(lock.Report(2, true, 0x9999));
    CHECK(lock.Npc() == 0x9999);
    CHECK(lock.Report(0, false, 0));
    CHECK(lock.Busy() && lock.Talker() == 2);
    CHECK(lock.Report(2, false, 0));
    CHECK(!lock.Busy() && lock.Npc() == 0 && lock.MayTalk(0));
    CHECK(lock.Report(0, true, 0x5678));
    lock.Left(3);
    CHECK(lock.Busy() && lock.Talker() == 0);
    lock.Left(0);
    CHECK(!lock.Busy());
}

TEST(dialogue_messages_round_trip_and_checks)
{
    auto talk = [](const TalkState& in, TalkState& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::TalkState && Decode(r, out);
    };
    TalkState t;
    CHECK(talk({2, true, 0x104C0C}, t) && t.playerId == 2 && t.talking && t.npc == 0x104C0C);
    CHECK(talk({1, true, 0}, t) && t.talking && t.npc == 0);
    CHECK(talk({1, false, 0x104C0C}, t) && !t.talking && t.npc == 0);
    CHECK(!talk({1, true, 0x14}, t));
    {
        Bytes b{static_cast<uint8_t>(MsgType::TalkState), 1, 2, 0, 0, 0, 0};
        Reader r(b);
        MsgType type;
        CHECK(r.Type(type) && !Decode(r, t));
    }
    auto line = [](const DialogueLine& in, DialogueLine& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType type;
        return r.Type(type) && type == MsgType::DialogueLine && Decode(r, out);
    };
    DialogueLine l;
    CHECK(line({0x10F1A2, true, 0xFF000801}, l) && l.info == 0x10F1A2 && l.onEnd && l.speaker == 0xFF000801);
    CHECK(!line({0, false, 0x104C0C}, l));
    CHECK(!line({0xFF000123, false, 0x104C0C}, l));
    CHECK(!line({0x10F1A2, false, 0}, l));
    CHECK(!line({0x10F1A2, false, 0x14}, l));
}

TEST(one_conversation_at_a_time_and_only_the_talkers_lines_reach_the_host)
{
    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b.GetFriendState() == coop::FriendState::Joined;
    }));
    a.SetMyConversation(true, 0x104C0C);
    CHECK(RunUntil({&host, &a, &b}, [&] { return b.Talk().Busy() && a.Talk().Busy(); }));
    CHECK(b.Talk().Talker() == a.MyPlayerId() && b.Talk().Npc() == 0x104C0C);
    CHECK(a.MayTalk() && !b.MayTalk() && !host.MayTalk());
    host.SetMyConversation(true, 0x111111);
    CHECK(host.Talk().Talker() == a.MyPlayerId());
    b.SetMyConversation(true, 0x222222);
    b.SendDialogueLine({0x10F1A2, false, 0x222222});
    a.SendDialogueLine({0x10F1A3, false, 0x104C0C});
    std::vector<coop::Coop::DialogueLineFrom> lines;
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& l : host.TakeDialogueLines()) lines.push_back(l);
        return !lines.empty();
    }));
    CHECK(lines.size() == 1 && lines[0].playerId == a.MyPlayerId() && lines[0].line.info == 0x10F1A3);
    CHECK(host.Talk().Talker() == a.MyPlayerId() && host.Talk().Npc() == 0x104C0C);
    a.SetMyConversation(false, 0);
    CHECK(RunUntil({&host, &a, &b}, [&] { return !b.Talk().Busy() && !host.Talk().Busy(); }));
    b.SetMyConversation(true, 0x222222);
    CHECK(RunUntil({&host, &a, &b}, [&] { return a.Talk().Busy() && a.Talk().Talker() == b.MyPlayerId(); }));
    host.ShareProgressFor(b.MyPlayerId(), 20, 0);
    std::vector<Progress> atA, atB;
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& p : a.TakeProgress()) atA.push_back(p);
        for (auto& p : b.TakeProgress()) atB.push_back(p);
        return !atA.empty();
    }));
    CHECK(atA.size() == 1 && atA[0].xp == 20 && atA[0].playerId == b.MyPlayerId());
    b.Stop();
    CHECK(RunUntil({&host, &a}, [&] { return !a.Talk().Busy() && !host.Talk().Busy(); }));
    CHECK(atB.empty());
    a.Stop();
    host.Stop();
}

TEST(subtitles_reach_everyone_else_and_only_from_the_talker)
{
    auto sub = [](const Subtitle& in, Subtitle& out) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        return r.Type(t) && t == MsgType::Subtitle && Decode(r, out);
    };
    Subtitle s;
    auto said = [](uint32_t speaker, uint32_t topic, uint32_t info, uint8_t response, std::string text) {
        Subtitle m;
        m.speaker = speaker;
        m.topic = topic;
        m.info = info;
        m.response = response;
        m.text = std::move(text);
        return m;
    };
    CHECK(sub(said(0x104E85, 0x10F1A0, 0x10F1A2, 2, "Well hey there, neighbor."), s) && s.speaker == 0x104E85 &&
        s.text == "Well hey there, neighbor." && s.topic == 0x10F1A0 && s.info == 0x10F1A2 && s.response == 2 &&
        !s.stop);
    CHECK(sub(said(0x104E85, 0, 0, 0, "Text only"), s) && s.info == 0);
    CHECK(!sub(said(0x104E85, 0, 0x10F1A2, 0, "A line without its topic"), s));
    CHECK(!sub(said(0x104E85, 0x10F1A0, 0xFF000001, 0, "Made while playing"), s));
    CHECK(!sub(said(0x104E85, 0, 0, 0, ""), s));
    CHECK(!sub(said(0x14, 0, 0, 0, "Hi"), s));
    CHECK(!sub(said(0x104E85, 0, 0, 0, "two\nlines"), s));
    CHECK(sub(said(0x104E85, 0, 0, 0, std::string(kMaxSubtitleLength + 50, 'a')), s) &&
        s.text.size() == kMaxSubtitleLength);
    Subtitle stop;
    stop.speaker = 0x104E85;
    stop.stop = true;
    stop.text = "ignored";
    CHECK(sub(stop, s) && s.stop && s.text.empty() && s.speaker == 0x104E85);
    Subtitle reply = said(0x104E85, 0x10F1A0, 0x10F1A2, 1, "What happened here?");
    reply.reply = true;
    CHECK(sub(reply, s) && s.reply && !s.stop && s.text == "What happened here?" && s.info == 0x10F1A2);
    CHECK(sub(said(0x104E85, 0, 0, 0, "Not a reply"), s) && !s.reply);

    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b.GetFriendState() == coop::FriendState::Joined;
    }));
    b.ShareSubtitle(said(0x222222, 0, 0, 0, "Not me"));
    a.SetMyConversation(true, 0x104E85);
    CHECK(RunUntil({&host, &a, &b}, [&] { return host.Talk().Busy(); }));
    a.ShareSubtitle(said(0x104E85, 0x10F1A0, 0x10F1A2, 0, "Well hey\nthere"));
    std::vector<Subtitle> atHost, atB, atA;
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& x : host.TakeSubtitles()) atHost.push_back(x);
        for (auto& x : b.TakeSubtitles()) atB.push_back(x);
        for (auto& x : a.TakeSubtitles()) atA.push_back(x);
        return !atHost.empty() && !atB.empty();
    }));
    CHECK(atHost.size() == 1 && atHost[0].text == "Well hey there" && atHost[0].playerId == a.MyPlayerId());
    CHECK(atB.size() == 1 && atB[0].speaker == 0x104E85 && atB[0].info == 0x10F1A2);
    Subtitle stopped;
    stopped.speaker = 0x104E85;
    stopped.stop = true;
    a.ShareSubtitle(stopped);
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& x : b.TakeSubtitles()) atB.push_back(x);
        return atB.size() == 2;
    }));
    CHECK(atB[1].stop && atB[1].speaker == 0x104E85);
    host.TakeSubtitles();
    CHECK(atA.empty());
    a.SetMyConversation(false, 0);
    host.SetMyConversation(true, 0x104C0F);
    host.ShareSubtitle(said(0x104C0F, 0, 0, 0, "Welcome back."));
    CHECK(RunUntil({&host, &a, &b}, [&] {
        for (auto& x : a.TakeSubtitles()) atA.push_back(x);
        return !atA.empty();
    }));
    CHECK(atA.size() == 1 && atA[0].text == "Welcome back." && atA[0].playerId == 0);
    a.Stop();
    b.Stop();
    host.Stop();
}

TEST(nearby_conversations_log_keeps_the_newest_lines_for_a_while)
{
    coop::TalkLog log;
    log.Configure(4, 10000);
    CHECK(log.Update(1000).empty());
    log.Add("Doc Mitchell: Easy now.", 1000);
    log.Add("Ana: Who are you?", 2000);
    auto shown = log.Update(2000);
    CHECK(shown.size() == 2 && shown[0].text == "Doc Mitchell: Easy now." && shown[1].text == "Ana: Who are you?");
    CHECK(shown[0].alpha == 1 && shown[0].id && shown[1].id && shown[0].id != shown[1].id);
    uint32_t ana = shown[1].id;
    shown = log.Update(11000 - coop::TalkLog::kFadeMs / 2);
    CHECK(shown.size() == 2 && shown[0].alpha > 0.4f && shown[0].alpha < 0.6f && shown[1].alpha == 1);
    shown = log.Update(11000);
    CHECK(shown.size() == 1 && shown[0].id == ana);
    CHECK(log.Update(12000).empty());
    for (int i = 0; i < 12; ++i)
        log.Add(std::to_string(i), 20000);
    shown = log.Update(20000);
    CHECK(shown.size() == 4 && shown[0].text == "8" && shown[3].text == "11");
    log.Configure(2, 10000);
    shown = log.Update(20000);
    CHECK(shown.size() == 2 && shown[0].text == "10");
    log.Configure(50, 10);
    for (int i = 0; i < 12; ++i)
        log.Add(std::to_string(i), 20000);
    CHECK(log.Update(20000).size() == coop::TalkLog::kMostLines);
    CHECK(log.Update(20000 + coop::TalkLog::kFadeMs).empty());
    log.Clear();
    log.Configure(4, 10000);
    log.Add("Wraps", 0xFFFFF000u);
    shown = log.Update(0x00000100u);
    CHECK(shown.size() == 1 && shown[0].text == "Wraps");
    CHECK(coop::TalkLine("Easy Pete", "Who are you?") == "EASY PETE: Who are you?");
    CHECK(coop::TalkLine("", "Just text") == "Just text");
}

TEST(only_the_games_own_quests_are_the_hosts_to_run)
{
    CHECK(coop::IsGameFile("FalloutNV.esm") && coop::IsGameFile("falloutnv.ESM") && coop::IsGameFile("DeadMoney.esm"));
    CHECK(!coop::IsGameFile("The Mod Configuration Menu.esp") && !coop::IsGameFile("Real Time Menus - Fixes.esm"));
    std::vector<std::string> order{"FalloutNV.esm", "DeadMoney.esm", "Real Time Menus - Fixes.esm",
        "The Mod Configuration Menu.esp"};
    CHECK(coop::FromGameFile(0x0010F1A2, order) && coop::FromGameFile(0x01000ADE, order));
    CHECK(!coop::FromGameFile(0x02000800, order) && !coop::FromGameFile(0x03000ADE, order));
    CHECK(!coop::FromGameFile(0x07000ADE, order));
}

TEST(the_host_runs_the_big_story_mods_quests_on_its_own)
{
    std::vector<std::string> ttw{"FalloutNV.esm", "DeadMoney.esm", "TaleofTwoWastelands.esm", "YUPTTW.esm"};
    std::vector<std::string> mods = coop::WithStoryMods({}, ttw);
    CHECK(mods.size() == 1 && mods[0] == "TaleOfTwoWastelands.esm");
    CHECK(coop::HostRunsQuestsOf(0x02000ADE, ttw, mods) && !coop::HostRunsQuestsOf(0x03000ADE, ttw, mods));
    CHECK(coop::WithStoryMods({"taleoftwowastelands.ESM"}, ttw).size() == 1);
    std::vector<std::string> both{"FalloutNV.esm", "TaleOfTwoWastelands.esm", "NewCalifornia.esm"};
    CHECK(coop::WithStoryMods({"Quest Mod.esp"}, both).size() == 3);
    CHECK(coop::WithStoryMods({"Quest Mod.esp"}, both)[0] == "Quest Mod.esp");
    std::vector<std::string> plain{"FalloutNV.esm", "Quest Mod.esp"};
    CHECK(coop::WithStoryMods({}, plain).empty());
}

TEST(the_host_runs_the_quests_of_the_mods_it_lists)
{
    std::vector<std::string> order{"FalloutNV.esm", "Quest Mod.esp", "The Mod Configuration Menu.esp"};
    std::vector<std::string> listed{"quest mod.ESP"};
    CHECK(coop::HostRunsQuestsOf(0x0010F1A2, order, listed) && coop::HostRunsQuestsOf(0x01000ADE, order, listed));
    CHECK(!coop::HostRunsQuestsOf(0x02000ADE, order, listed));
    CHECK(!coop::HostRunsQuestsOf(0x01000ADE, order, {}));
    CHECK(!coop::HostRunsQuestsOf(0xFF000ADE, order, listed));

    std::vector<std::string> dlc{"FalloutNV.esm", "DeadMoney.esm"};
    CHECK(coop::IsPerPlayerQuest(0x01006E93, dlc) && !coop::HostRunsQuestsOf(0x01006E93, dlc, {}));
    CHECK(coop::IsPerPlayerQuest(0x0100E882, dlc) && !coop::HostRunsQuestsOf(0x0100E882, dlc, {}));
    CHECK(!coop::IsPerPlayerQuest(0x01000FBF, dlc) && coop::HostRunsQuestsOf(0x01000FBF, dlc, {}));
    CHECK(!coop::IsPerPlayerQuest(0x00006E93, dlc));

    Bytes b = Encode(Welcome{1, "Host", {"Quest Mod.esp", "Other.esm"}});
    Reader r(b);
    MsgType t;
    Welcome w;
    CHECK(r.Type(t) && Decode(r, w) && w.questMods == (std::vector<std::string>{"Quest Mod.esp", "Other.esm"}));
    Bytes bad = Encode(Welcome{1, "Host", {"..\\evil.esp"}});
    Reader br(bad);
    CHECK(br.Type(t) && !Decode(br, w));

    coop::Coop host, buddy;
    host.SetQuestMods({"Quest Mod.esp", "not a plugin.txt"});
    CHECK(host.QuestMods() == std::vector<std::string>{"Quest Mod.esp"});
    buddy.SetQuestMods({"Mine.esp"});
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK(RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; }));
    CHECK(buddy.QuestMods() == std::vector<std::string>{"Quest Mod.esp"});
    buddy.Stop();
    host.Stop();
}

TEST(waiting_needs_everyone_to_agree)
{
    coop::WaitVote v;
    CHECK(!v.Start(1, 0, false, {0, 1, 2}, 0) && !v.Start(1, 25, false, {0, 1, 2}, 0));
    CHECK(v.Start(1, 8, true, {0, 1, 2}, 1000) && v.IsOpen() && v.Hours() == 8 && v.Sleep());
    CHECK(!v.Asked(1) && v.Asked(0) && v.Asked(2));
    CHECK(!v.Start(2, 4, false, {0, 1, 2}, 1000));
    v.Answer(0, true);
    CHECK(v.Check(2000) == coop::WaitVote::Outcome::Open);
    v.Answer(5, true);
    v.Answer(2, true);
    CHECK(v.Check(2000) == coop::WaitVote::Outcome::Passed && !v.IsOpen());
    CHECK(v.Start(0, 2, false, {0, 1, 2}, 0));
    v.Answer(1, true);
    v.Answer(2, false);
    CHECK(v.Check(10) == coop::WaitVote::Outcome::Refused && v.RefusedBy() == 2);
    CHECK(v.Start(0, 2, false, {0, 1}, 0));
    CHECK(v.Check(coop::WaitVote::kAnswerMs - 1) == coop::WaitVote::Outcome::Open);
    CHECK(v.Check(coop::WaitVote::kAnswerMs) == coop::WaitVote::Outcome::Refused && v.RefusedBy() == net::kNobody);
    CHECK(v.Start(1, 2, false, {0, 1}, 0));
    v.Answer(0, false);
    CHECK(v.Check(10) == coop::WaitVote::Outcome::Refused && v.RefusedBy() == 0);
    CHECK(v.Start(0, 2, false, {0, 1, 2}, 0));
    v.Answer(1, true);
    v.Left(2);
    CHECK(v.Check(10) == coop::WaitVote::Outcome::Passed);
    CHECK(v.Start(1, 2, false, {0, 1}, 0));
    v.Left(1);
    CHECK(!v.IsOpen() && v.Check(10) == coop::WaitVote::Outcome::Open);
    CHECK(v.Start(1, 2, false, {0, 1}, 0));
    v.Cancel(0);
    CHECK(v.IsOpen());
    v.Cancel(1);
    CHECK(!v.IsOpen());
}

TEST(bad_wait_and_clock_messages_are_rejected)
{
    auto ask = [](uint8_t hours, uint8_t sleep) {
        Writer w(MsgType::WaitAsk);
        w.U8(hours);
        w.U8(sleep);
        Bytes b = w.Take();
        Reader r(b);
        MsgType t;
        WaitAsk m;
        return r.Type(t) && Decode(r, m);
    };
    CHECK(ask(0, 0) && ask(24, 1) && !ask(25, 0) && !ask(4, 2));
    auto time = [](GameTime in) {
        Bytes b = Encode(in);
        Reader r(b);
        MsgType t;
        GameTime out;
        return r.Type(t) && t == MsgType::GameTime && Decode(r, out) && out.hour == in.hour && out.day == in.day;
    };
    CHECK(time(GameTime{6.36f, 8.65f, 19, 9, 2281}));
    CHECK(!time(GameTime{6.36f, 25.f, 19, 9, 2281}) && !time(GameTime{6.36f, 8.f, 0, 9, 2281}) &&
        !time(GameTime{6.36f, 8.f, 19, 12, 2281}) && !time(GameTime{-1.f, 8.f, 19, 9, 2281}));
    Bytes b = Encode(WaitResult{1, true, 0, false, 0});
    Reader r(b);
    MsgType t;
    WaitResult m;
    CHECK(r.Type(t) && !Decode(r, m));
}

TEST(a_wait_vote_reaches_everyone_and_everyone_waits)
{
    coop::Coop host, a, b;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    CHECK(RunUntil({&host, &a, &b}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b.GetFriendState() == coop::FriendState::Joined;
    }));
    using Kind = coop::Coop::WaitEvent::Kind;
    std::vector<coop::Coop::WaitEvent> atHost, atA, atB;
    auto take = [&] {
        for (auto& e : host.TakeWaitEvents()) atHost.push_back(e);
        for (auto& e : a.TakeWaitEvents()) atA.push_back(e);
        for (auto& e : b.TakeWaitEvents()) atB.push_back(e);
    };
    a.AskToWait(6, false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return !atHost.empty() && !atB.empty(); }));
    CHECK(atHost[0].kind == Kind::Asked && atHost[0].requester == a.MyPlayerId() && atHost[0].hours == 6);
    CHECK(atB[0].kind == Kind::Asked && atA.empty());
    host.AnswerWait(true);
    b.AnswerWait(true);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return atA.size() == 1 && atB.size() == 2 && atHost.size() == 2; }));
    CHECK(atA[0].kind == Kind::Start && atA[0].hours == 6 && !atA[0].sleep);
    CHECK(atB[1].kind == Kind::Start && atHost[1].kind == Kind::Start);
    atHost.clear(), atA.clear(), atB.clear();
    host.AskToWait(8, true);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return !atA.empty() && !atB.empty(); }));
    CHECK(atA[0].kind == Kind::Asked && atA[0].sleep && atHost.empty());
    b.AnswerWait(false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return atHost.size() == 1 && atA.size() == 2; }));
    CHECK(atHost[0].kind == Kind::Refused && atHost[0].refusedBy == b.MyPlayerId());
    CHECK(atA[1].kind == Kind::Refused);
    atHost.clear(), atA.clear(), atB.clear();
    a.AskToWait(2, false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return !atB.empty(); }));
    a.AskToWait(0, false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return atB.size() == 2; }));
    CHECK(atB[1].kind == Kind::Refused && atB[1].refusedBy == kNobody);
    atHost.clear(), atA.clear(), atB.clear();
    a.AskToWait(3, false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return !atHost.empty() && !atB.empty(); }));
    b.AnswerWait(true);
    host.AnswerWait(false);
    CHECK(RunUntil({&host, &a, &b}, [&] { take(); return atA.size() == 1 && atB.size() == 2; }));
    CHECK(atA[0].kind == Kind::Refused && atA[0].refusedBy == 0 && atB[1].kind == Kind::Refused);
    host.SetMyGameTime(GameTime{6.5f, 12.f, 20, 9, 2281});
    GameTime heard;
    CHECK(RunUntil({&host, &a, &b}, [&] { return a.TakeGameTime(heard); }));
    CHECK(heard.hour == 12.f && heard.day == 20);
    a.Stop();
    b.Stop();
    host.Stop();
}

TEST(linked_save_names_are_plain_and_stay_in_the_save_folder)
{
    CHECK(coop::SaveNameOf("Quicksave.fos") == "Quicksave" && coop::SaveNameOf("Save 12 - Alex, Goodsprings") ==
        "Save 12 - Alex, Goodsprings");
    CHECK(coop::SaveNameOf("..\\..\\Windows\\x.fos").empty() && coop::SaveNameOf("a/b").empty() &&
        coop::SaveNameOf("C:x").empty() && coop::SaveNameOf("").empty() && coop::SaveNameOf(".fos").empty());
    CHECK(coop::SaveNameOf("x\"y").empty() && coop::SaveNameOf(" lead").empty() && coop::SaveNameOf("trail.").empty());
    CHECK(coop::SaveNameOf(std::string(coop::kMaxSaveName + 1, 'a')).empty());
    CHECK(coop::LinkedSaveName("Alex", "Quicksave.fos") == "Co-op - Alex - Quicksave");
    CHECK(coop::LinkedSaveName("A/l\\e:x\"", "Autosave") == "Co-op - Alex - Autosave");
    CHECK(coop::LinkedSaveName("***", "Autosave") == "Co-op - Host - Autosave");
    CHECK(coop::LinkedSaveName("Alex", "bad|name").empty());
    CHECK(coop::SaveForFriend("Alex-Laptop", "2026-10-15 23-27-45") == "Co-op Alex-Laptop 2026-10-15 23-27-45");
    CHECK(coop::SaveForFriend("<>", "2026-10-15 23-27-45") == "Co-op Friend 2026-10-15 23-27-45");
    CHECK(coop::HostSaveOf("Alex", "Co-op - Alex - quicksave.fos") == "quicksave");
    CHECK(coop::HostSaveOf("Alex", "Co-op - Bob - quicksave").empty() && coop::HostSaveOf("Alex", "quicksave").empty());
    CHECK(coop::HostSaveOf("Alex", "Co-op - Alex - ").empty());
    CHECK(coop::IsAutosave("Autosave") && coop::IsAutosave("autosave2") && !coop::IsAutosave("quicksave"));
}

TEST(friends_hear_the_hosts_saves_and_loads)
{
    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Alex"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    host.ShareSave("Quicksave.fos", false);
    host.ShareSave("bad|name.fos", false);
    host.ShareSave("Save 3.fos", true);
    std::vector<SaveOrder> heard;
    CHECK(RunUntil({&host, &a}, [&] {
        for (auto& o : a.TakeSaveOrders()) heard.push_back(o);
        return heard.size() >= 2;
    }));
    CHECK(heard.size() == 2 && heard[0].name == "Quicksave" && !heard[0].load && heard[1].name == "Save 3" &&
        heard[1].load);
    CHECK(a.NameOf(0) == "Alex");
    a.Stop();
    host.Stop();
}

TEST(whoever_is_still_on_the_way_isnt_frozen)
{
    coop::Coop host, a, b;
    host.SetTravelCooldownMs(100);
    CHECK(host.Host(kTestPort, "Host"));
    host.SetMyPose({5, 0, 0, 0, 0});
    a.SetMyPose({5, 100, 0, 0, 0});
    b.SetMyPose({5, 200, 0, 0, 0});
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(b.Join("127.0.0.1", kTestPort, "B"));
    std::vector<coop::Coop*> all{&host, &a, &b};
    CHECK(RunUntil(all, [&] { return a.Remotes().size() == 2 && b.Remotes().size() == 2; }));
    RunUntil(all, [] { return false; }, 200);

    host.SetMyPose({7, 0, 0, 0, 0});
    coop::Pose p;
    uint8_t leader;
    bool aGoes = false, bGoes = false;
    CHECK(RunUntil(all, [&] {
        aGoes |= a.TakeTravel(p, leader);
        bGoes |= b.TakeTravel(p, leader);
        return aGoes && bGoes;
    }));
    RunUntil(all, [] { return false; }, 200);
    CHECK(host.Frozen() && !a.Frozen() && !b.Frozen());

    a.SetMyPose({7, 10, 0, 0, 0});
    CHECK(RunUntil(all, [&] { return a.Frozen(); }));
    CHECK(host.Frozen() && !b.Frozen());
    b.SetMyPose({7, 20, 0, 0, 0});
    CHECK(RunUntil(all, [&] { return !host.Frozen() && !a.Frozen() && !b.Frozen(); }));

    RunUntil(all, [] { return false; }, 200);
    a.SetMyPose({9, 0, 0, 0, 0});
    bool hostGoes = false;
    bGoes = false;
    CHECK(RunUntil(all, [&] {
        hostGoes |= host.TakeTravel(p, leader);
        bGoes |= b.TakeTravel(p, leader);
        return hostGoes && bGoes;
    }));
    CHECK(RunUntil(all, [&] { return a.Frozen(); }));
    CHECK(!host.Frozen() && !b.Frozen());
    b.SetMyPose({9, 5, 0, 0, 0});
    RunUntil(all, [] { return false; }, 200);
    CHECK(a.Frozen() && host.FrozenFor() == coop::kHostPlayerId);
    host.SetMyPose({9, 10, 0, 0, 0});
    CHECK(RunUntil(all, [&] { return !host.Frozen() && !a.Frozen() && !b.Frozen(); }));
    b.Stop();
    a.Stop();
    host.Stop();
}

TEST(the_world_waits_until_everyone_has_loaded)
{
    coop::LoadGate g;
    CHECK(!g.Waiting(0));
    g.Expect({1, 2}, 1000);
    CHECK(g.Waiting(1000) && (g.WaitingFor() == 1 || g.WaitingFor() == 2));
    g.Arrived(1);
    g.Arrived(7);
    CHECK(g.Waiting(2000) && g.WaitingFor() == 2);
    g.Left(2);
    CHECK(!g.Waiting(2000));
    g.Expect({3}, 0);
    std::vector<uint8_t> late;
    CHECK(g.Waiting(coop::LoadGate::kMaxWaitMs - 1, &late) && late.empty());
    CHECK(!g.Waiting(coop::LoadGate::kMaxWaitMs, &late) && late.size() == 1 && late[0] == 3);

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Alex"));
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    host.ShareSave("Quicksave.fos", true);
    CHECK(host.Frozen() && host.FrozenFor() == a.MyPlayerId());
    CHECK(RunUntil({&host, &a}, [&] { return a.Frozen(); }));
    a.LoadedHostSave();
    CHECK(RunUntil({&host, &a}, [&] { return !host.Frozen() && !a.Frozen(); }));
    host.ShareSave("Quicksave.fos", true);
    CHECK(host.Frozen());
    a.Stop();
    CHECK(RunUntil({&host}, [&] { return !host.Frozen(); }));
    host.Stop();
}

TEST(frame_parts_name_the_costly_parts)
{
    coop::FrameParts parts;
    const char* const names[] = {"network", "NPCs", "radio"};
    for (int i = 0; i < 100; ++i)
    {
        parts.Add(0, 0.01);
        parts.Add(1, 0.10);
    }
    parts.Add(2, 1.5);
    parts.Add(7, 50);
    parts.Add(-1, 50);
    CHECK(parts.Report(names, 3, 100, 0.02, 1.0) == "NPCs 0.10 (most 0.10), radio 0.01 (most 1.50)");
    CHECK(parts.Report(names, 3, 100, 0.02, 1.0).empty());
}

TEST(enemies_go_for_the_biggest_threat)
{
    coop::ThreatTable t;
    const uint32_t enemy = 0xFF001346;
    using P = coop::ThreatTable::Player;
    std::vector<P> both = {{0, 3000, false}, {1, 3000, false}};
    t.Hurt(enemy, 0, 10, 1000);
    CHECK(t.Pick(enemy, both, 1000) == 0);
    t.Hurt(enemy, 1, 30, 2000);
    CHECK(t.Pick(enemy, both, 2000) == 1);
    t.Hurt(enemy, 0, 25, 2100);
    CHECK(t.Pick(enemy, both, 2100) == 1);
    t.Hurt(enemy, 0, 30, 2200);
    CHECK(t.Pick(enemy, both, 2200) == 0);
    std::vector<P> hostDown = {{0, 3000, true}, {1, 3000, false}};
    CHECK(t.Pick(enemy, hostDown, 2300) == 1);
    std::vector<P> allDown = {{0, 100, true}, {1, 100, true}};
    CHECK(t.Pick(enemy, allDown, 2300) == -1);
    std::vector<P> friendNear = {{0, 3000, false}, {1, 200, false}};
    CHECK(t.Pick(enemy, friendNear, 12000) == 1);
    CHECK(t.Stale(12000).empty());
    CHECK(t.Stale(23000).size() == 1);
    CHECK(t.Pick(0x1234, both, 1000) == -1);
}

TEST(supplies_are_scarcer)
{
    CHECK(coop::ScarceChanceNone(0, 0.5f) == 50);
    CHECK(coop::ScarceChanceNone(50, 0.5f) == 75);
    CHECK(coop::ScarceChanceNone(80, 0.5f) == 90);
    CHECK(coop::ScarceChanceNone(100, 0.5f) == 100);
    CHECK(coop::ScarceChanceNone(30, 1.0f) == 30);
}

TEST(bigger_groups_meet_more_enemies)
{
    coop::Reinforcements r;
    auto add = [&r](int players, int enemies, int alive) {
        int extra = 0;
        for (int i = 0; i < enemies; ++i)
        {
            r.EnemyJoined(players);
            extra += r.Take(alive);
        }
        return extra;
    };
    CHECK(add(1, 8, 0) == 0);
    CHECK(add(2, 8, 0) == 4);
    CHECK(add(3, 8, 0) == 8);
    CHECK(add(4, 8, 0) == 8);
    CHECK(add(4, 8, coop::Reinforcements::kMaxAlive) == 0);
    CHECK(r.Take(0) && !r.Take(0));
    r.EnemyJoined(4);
    r.EnemyJoined(4);
    CHECK(r.Take(0) && r.Take(0) && r.Take(0) && !r.Take(0));
    r.Reset();
    for (int i = 0; i < 6; ++i)
        r.EnemyJoined(2);
    int made = 0;
    for (int second = 0; second < 10; ++second)
        if (r.Owed())
            made += r.Take(made);
    CHECK(made == 3 && !r.Owed());
}

TEST(co_op_balanced_gives_fewer_skill_points)
{
    const coop::Balance& b = coop::CoopBalance();
    CHECK(coop::FewerSkillPoints(13, b) == 10);
    CHECK(coop::FewerSkillPoints(15, b) == 12);
    CHECK(coop::FewerSkillPoints(1, b) == 0);
}

TEST(co_op_balanced_ammo_costs_more_and_comes_in_smaller_finds)
{
    const coop::Balance& b = coop::CoopBalance();
    CHECK(coop::AmmoValue(1, b) == 2 && coop::AmmoValue(0, b) == 0 && coop::AmmoValue(7, b) == 14);
    CHECK(coop::AmmoFound(20, b) == 10);
    CHECK(coop::AmmoFound(3, b) == 2);
    CHECK(coop::AmmoFound(1, b) == 1);
    CHECK(coop::AmmoFound(0, b) == 0);
}

TEST(players_looks_reach_everyone)
{
    net::PlayerLooks hers;
    hers.female = true;
    hers.race = 0x19;
    hers.hair = 0x1234;
    hers.eyes = 0x4321;
    hers.hairLength = 0.4f;
    hers.hairColor = 0x00336699;
    for (size_t i = 0; i < net::kFaceValues; ++i)
        hers.face[i] = static_cast<float>(i % 7) - 3.0f;
    hers.headParts = {0x1111, 0x2222};
    hers.name = "Veronica";
    MsgType t;
    net::PlayerLooks back;
    Bytes b = Encode(hers);
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::PlayerLooks && Decode(r, back) && back == hers);
    auto rejected = [&](net::PlayerLooks odd) {
        Bytes x = Encode(odd);
        Reader rx(x);
        net::PlayerLooks m;
        return rx.Type(t) && !Decode(rx, m);
    };
    net::PlayerLooks odd = hers;
    odd.face[5] = 1e6f;
    CHECK(rejected(odd));
    odd = hers;
    odd.hairLength = -1;
    CHECK(rejected(odd));
    odd = hers;
    odd.hair = 0xFF000801;
    CHECK(rejected(odd));
    odd = hers;
    odd.headParts = {0};
    CHECK(rejected(odd));
    odd = hers;
    odd.name = std::string("Bad") + char(10) + "name";
    CHECK(rejected(odd));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    net::PlayerLooks hostLooks;
    hostLooks.race = 0x42;
    host.SetMyLooks(hostLooks);
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    a.SetMyLooks(hers);
    CHECK(RunUntil({&host, &a}, [&] { return host.Looks().size() == 1 && a.Looks().size() == 1; }));
    uint8_t ann = host.Looks().begin()->first;
    CHECK(host.Looks().at(ann) == hers && a.Looks().at(coop::kHostPlayerId).race == 0x42);
    CHECK(host.NameOf(ann) == "Veronica");
    CHECK(a.NameOf(coop::kHostPlayerId) == "Host");
    net::PlayerLooks his;
    his.race = 0x77;
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    b2.SetMyLooks(his);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return b2.Looks().size() == 2 && a.Looks().size() == 2; }));
    CHECK(b2.Looks().at(ann) == hers && b2.Looks().at(coop::kHostPlayerId).race == 0x42);
    uint32_t changes = b2.LooksChanges();
    hers.hairLength = 0.9f;
    a.SetMyLooks(hers);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return b2.LooksChanges() > changes; }));
    CHECK(b2.Looks().at(ann).hairLength == 0.9f);
}

TEST(a_friend_who_then_hosts_sends_only_its_own_looks)
{
    coop::Coop oldHost, me, other;
    net::PlayerLooks hostLooks;
    hostLooks.race = 0x42;
    net::PlayerLooks mine;
    mine.race = 0x19;
    CHECK(oldHost.Host(kTestPort, "Old"));
    oldHost.SetMyLooks(hostLooks);
    CHECK(me.Join("127.0.0.1", kTestPort, "Me"));
    me.SetMyLooks(mine);
    CHECK(RunUntil({&oldHost, &me}, [&] { return me.Looks().count(coop::kHostPlayerId) == 1; }));
    me.Stop();
    oldHost.Stop();
    CHECK(me.Looks().empty());
    CHECK(me.Host(kTestPort, "Me"));
    CHECK(other.Join("127.0.0.1", kTestPort, "Other"));
    CHECK(RunUntil({&me, &other}, [&] { return other.Looks().count(coop::kHostPlayerId) == 1; }));
    for (int i = 0; i < 30; ++i)
    {
        me.Update();
        other.Update();
        Sleep(10);
    }
    CHECK(other.Looks().at(coop::kHostPlayerId).race == 0x19);
    other.Stop();
    me.Stop();
}

TEST(a_friend_not_yet_let_in_takes_no_save_orders)
{
    Session raw;
    CHECK(raw.StartHost(kTestPort, 4));
    coop::Coop a;
    CHECK(a.Join("127.0.0.1", kTestPort, "A"));
    bool answered = false;
    std::vector<SaveOrder> heard;
    for (int i = 0; i < 300 && heard.empty(); ++i)
    {
        a.Update();
        for (auto& o : a.TakeSaveOrders())
            heard.push_back(o);
        Event ev;
        while (raw.Poll(ev))
            if (ev.kind == Event::Kind::Received && !answered)
            {
                answered = true;
                raw.Send(ev.peer, Encode(SaveOrder{true, "Before"}));
                raw.Send(ev.peer, Encode(Welcome{1, "Host"}));
                raw.Send(ev.peer, Encode(SaveOrder{true, "After"}));
            }
        Sleep(10);
    }
    CHECK(answered && heard.size() == 1 && heard[0].name == "After" && heard[0].load);
    a.Stop();
    raw.Stop();
}

TEST(the_hosts_xp_comes_with_the_welcome)
{
    MsgType t;
    net::Welcome w;
    Bytes b = Encode(net::Welcome{3, "Host", {}, {}, 12345});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::Welcome && Decode(r, w) && w.hostXp == 12345 && w.playerId == 3);
    Bytes odd = Encode(net::Welcome{3, "Host", {}, {}, net::kMaxXp + 1});
    Reader ro(odd);
    CHECK(ro.Type(t) && !Decode(ro, w));

    coop::Coop host, a;
    CHECK(host.Host(kTestPort, "Host"));
    host.SetMyXp(5400);
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(RunUntil({&host, &a}, [&] { return a.GetFriendState() == coop::FriendState::Joined; }));
    int32_t xp = 0;
    CHECK(a.TakeHostXp(xp) && xp == 5400);
    CHECK(!a.TakeHostXp(xp));
    int32_t none = 0;
    CHECK(!host.TakeHostXp(none));
}

TEST(goo_and_ash_go_with_a_death)
{
    MsgType t;
    net::NpcStates states;
    states.timeMs = 5;
    states.space = 0xDA726;
    net::NpcState dead;
    dead.ref = 0x0010706E;
    dead.flags = net::kNpcDead;
    dead.maxHealth = 50;
    dead.limbs = 0x0004;
    dead.pile = net::kPileGoo;
    states.npcs.push_back(dead);
    Bytes b = Encode(states);
    Reader r(b);
    net::NpcStates back;
    CHECK(r.Type(t) && Decode(r, back) && back.npcs.size() == 1 && back.npcs[0].pile == net::kPileGoo &&
        back.npcs[0].limbs == 0x0004);
    for (uint8_t kind : {uint8_t(0), uint8_t(net::kPileLast + 1)})
    {
        Bytes p = Encode(net::NpcPile{0x0010706E, kind});
        Reader rp(p);
        net::NpcPile m;
        CHECK(rp.Type(t) && !Decode(rp, m));
    }
    Bytes ok = Encode(net::NpcPile{0x0010706E, net::kPileAsh});
    Reader ro(ok);
    net::NpcPile m;
    CHECK(ro.Type(t) && t == MsgType::NpcPile && Decode(ro, m) && m.kind == net::kPileAsh);
}

TEST(mines_disarmed_or_gone_reach_everyone)
{
    MsgType t;
    net::MineChange m;
    for (net::MineChange bad : {net::MineChange{0x0010873F, 0}, net::MineChange{0x0010873F, 4},
             net::MineChange{0xFF001378, net::kMineGone}, net::MineChange{0, net::kMineOff}})
    {
        Bytes b = Encode(bad);
        Reader r(b);
        CHECK(r.Type(t) && !Decode(r, m));
    }
    Bytes ok = Encode(net::MineChange{0x0010873F, net::kMineOff});
    Reader ro(ok);
    CHECK(ro.Type(t) && t == MsgType::MineChange && Decode(ro, m) && m.ref == 0x0010873F && m.state == net::kMineOff);

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    a.ReportMine(0x0010873F, net::kMineOff);
    a.ReportMine(0xFF000123, net::kMineGone);
    std::vector<coop::Coop::MineFrom> atHost, atBob, atAnn;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (auto& x : host.TakeMines()) atHost.push_back(x);
        for (auto& x : b2.TakeMines()) atBob.push_back(x);
        for (auto& x : a.TakeMines()) atAnn.push_back(x);
        return !atHost.empty() && !atBob.empty();
    }));
    CHECK(atHost.size() == 1 && atHost[0].change.ref == 0x0010873F && atHost[0].change.state == net::kMineOff &&
        atHost[0].playerId != coop::kHostPlayerId);
    CHECK(atBob.size() == 1 && atBob[0].change.ref == 0x0010873F && atBob[0].change.state == net::kMineOff);
    host.ReportMine(0x0010873F, net::kMineBlown);
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (auto& x : a.TakeMines()) atAnn.push_back(x);
        for (auto& x : b2.TakeMines()) atBob.push_back(x);
        return !atAnn.empty() && atBob.size() == 2;
    }));
    CHECK(atAnn.size() == 1 && atAnn[0].change.state == net::kMineBlown);
    CHECK(atBob[1].change.state == net::kMineBlown && atBob[1].playerId == coop::kHostPlayerId);
    b2.Stop();
    a.Stop();
    host.Stop();
}

TEST(friend_items_bring_back_what_our_save_lost)
{
    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem bottle{0x10A202, 0x10A1F6, 1, false, 10, 0, 0, false};
    items.Heard({0x10A202, 0x10A1F6, 1, true, 10, 0, 0}, 1000);
    auto a = items.Look({bottle}, true, 1000);
    CHECK(a.enable.empty() && a.differ.size() == 1);
    items.Heard({0x10A202, 0x10A1F6, 1, true, 10, 0, 0}, 1200);
    a = items.Look({bottle}, true, 1200);
    CHECK(a.enable == std::vector<uint32_t>{0x10A202} && a.make.empty() && a.takes.empty());
    a = items.Look({bottle}, true, 1500);
    CHECK(a.enable.empty() && a.differ.size() == 1);

    LocalItem maize{0x176357, 0xE9AD1, 1, false, 500, 0, 0, false};
    maize.deleted = true;
    items.Heard({0x176357, 0xE9AD1, 1, true, 500, 0, 0}, 1900);
    items.Look({maize}, true, 1900);
    items.Heard({0x176357, 0xE9AD1, 1, true, 500, 0, 0}, 2000);
    a = items.Look({maize}, true, 2000);
    CHECK(a.make.size() == 1 && a.make[0].ref == 0x176357 && a.enable.empty());
    items.Made(0x176357, 0xFF000300);
    LocalItem copy{0xFF000300, 0xE9AD1, 1, true, 500, 0, 0, false};
    a = items.Look({maize, copy}, true, 2500);
    CHECK(a.make.empty() && a.remove.empty() && a.differ.empty());
    items.Heard({0x176357, 0xE9AD1, 1, false, 500, 0, 0}, 3000);
    a = items.Look({maize, copy}, true, 3000);
    CHECK(a.remove == std::vector<uint32_t>{0xFF000300} && a.takes.empty());
    items.Heard({0x176357, 0xE9AD1, 1, true, 500, 0, 0}, 3900);
    items.Look({maize}, true, 3900);
    items.Heard({0x176357, 0xE9AD1, 1, true, 500, 0, 0}, 4000);
    a = items.Look({maize}, true, 4000);
    CHECK(a.make.size() == 1 && a.make[0].ref == 0x176357);

    items.Made(0x176357, 0xFF000301);
    LocalItem copy2{0xFF000301, 0xE9AD1, 1, true, 500, 0, 0, true};
    items.Look({maize, copy2}, true, 4500);
    copy2.present = false;
    copy2.deleted = true;
    a = items.Look({maize, copy2}, true, 5000);
    CHECK(a.takes.size() == 1 && a.takes[0].ref == 0x176357 && a.make.empty());
    a = items.Look({maize}, true, 5500);
    CHECK(a.make.empty());
}

TEST(friend_items_match_the_hosts_enable_parents)
{
    MsgType t;
    net::ItemStates s;
    net::ItemState bottle{0x10A202, 0x10A1F6, 1, false, 10, 0, 0};
    bottle.parent = 0x168A92;
    bottle.parentOn = false;
    net::ItemState lit = bottle;
    lit.ref = 0x10A203;
    lit.parentOn = true;
    net::ItemState plain{0x171CAA, 0x171CE0, 1, true, 0, 0, 0};
    s.items = {bottle, lit, plain};
    Bytes b = Encode(s);
    Reader r(b);
    net::ItemStates back;
    CHECK(r.Type(t) && Decode(r, back) && back.items.size() == 3 && back.items[0].parent == 0x168A92 &&
        !back.items[0].parentOn && back.items[1].parentOn && back.items[2].parent == 0 && !back.items[2].parentOn);
    net::ItemState odd = bottle;
    odd.parent = 0xFF000010;
    s.items = {odd};
    Bytes ob = Encode(s);
    Reader orr(ob);
    CHECK(orr.Type(t) && !Decode(orr, back));

    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem ours{0x10A202, 0x10A1F6, 1, true, 10, 0, 0, false};
    ours.parent = 0x168A92;
    ours.parentOn = true;
    items.Heard(bottle, 1000);
    auto a = items.Look({ours}, true, 1000);
    CHECK(a.parents.empty());
    a = items.Look({ours}, true, 1500);
    CHECK(a.parents.empty());
    items.Heard(bottle, 4000);
    a = items.Look({ours}, true, 4000);
    CHECK(a.parents.size() == 1 && a.parents[0].ref == 0x168A92 && !a.parents[0].on);
    a = items.Look({ours}, true, 4500);
    CHECK(a.parents.empty());
    coop::FriendItems later;
    later.Heard(bottle, 1000);
    later.Look({ours}, true, 1000);
    ours.parentOn = true;
    net::ItemState nowOn = bottle;
    nowOn.parentOn = true;
    later.Heard(nowOn, 4000);
    a = later.Look({ours}, true, 4000);
    CHECK(a.parents.empty());
}

TEST(friend_items_dont_bring_back_what_the_host_just_took)
{
    using coop::LocalItem;
    coop::FriendItems items;
    LocalItem caps{0xE436D, 0xF, 1, true, 525, -1071, 78, false};
    items.Heard({0xE436D, 0xF, 1, true, 525, -1071, 78}, 1000);
    items.Look({caps}, true, 1000);
    caps.present = false;
    auto a = items.Look({caps}, true, 1500);
    CHECK(a.enable.empty() && a.make.empty() && a.takes.empty());
    a = items.Look({caps}, true, 2000);
    CHECK(a.enable.empty());
    items.Heard({0xE436D, 0xF, 1, false, 525, -1071, 78}, 3000);
    a = items.Look({caps}, true, 3000);
    CHECK(a.enable.empty() && a.differ.empty());
}

TEST(a_friends_own_reputation_changes_reach_the_host)
{
    coop::ReputationWatcher w;
    float fame = 0, infamy = 0;
    CHECK(!w.Look(0x104C22, 15, 0, fame, infamy));
    w.Heard(0x104C22, 0, 0);
    CHECK(!w.Look(0x104C22, 0, 0, fame, infamy));
    CHECK(w.Look(0x104C22, 15, 0, fame, infamy) && fame == 15 && infamy == 0);
    CHECK(!w.Look(0x104C22, 15, 0, fame, infamy));
    w.Heard(0x104C22, 15, 0);
    CHECK(!w.Look(0x104C22, 15, 0, fame, infamy));
    w.Forget();
    CHECK(!w.Look(0x104C22, 30, 0, fame, infamy));

    MsgType t;
    std::vector<net::ReputationGain> got;
    Bytes b = net::EncodeGains({{0x104C22, -5, 15}});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::ReputationGain && net::DecodeGains(r, got) && got.size() == 1 &&
        got[0] == (net::ReputationGain{0x104C22, -5, 15}));
    Bytes none = net::EncodeGains({{0x104C22, 0, 0}});
    Reader rn(none);
    CHECK(rn.Type(t) && !net::DecodeGains(rn, got));
    Bytes huge = net::EncodeGains({{0x104C22, 1e9f, 0}});
    Reader rh(huge);
    CHECK(rh.Type(t) && !net::DecodeGains(rh, got));
    Bytes nan = net::EncodeGains({{0x104C22, std::nanf(""), 0}});
    Reader rq(nan);
    CHECK(rq.Type(t) && !net::DecodeGains(rq, got));

    coop::Coop host, buddy;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(buddy.Join("127.0.0.1", kTestPort, "Buddy"));
    CHECK((RunUntil({&host, &buddy}, [&] { return buddy.GetFriendState() == coop::FriendState::Joined; })));
    buddy.ShareReputationGains({{0x104C22, 0, 15}});
    std::vector<std::pair<uint8_t, net::ReputationGain>> heard;
    CHECK((RunUntil({&host, &buddy}, [&] {
        for (auto& g : host.TakeReputationGains())
            heard.push_back(g);
        return !heard.empty();
    })));
    CHECK((heard.size() == 1 && heard[0].second == net::ReputationGain{0x104C22, 0, 15}));
}

TEST(a_place_anyone_can_fast_travel_to_everyone_can)
{
    coop::TravelWatcher w;
    CHECK(w.Look(0x10C69A));
    CHECK(!w.Look(0x10C69A));
    w.Heard(0x0E1A1B);
    CHECK(!w.Look(0x0E1A1B) && w.Known() == 2);
    w.Forget();
    CHECK(w.Look(0x10C69A));

    MsgType t;
    std::vector<uint32_t> got;
    Bytes b = net::EncodeTravelMarkers({0x10C69A, 0x0E1A1B});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::TravelMarkers && net::DecodeTravelMarkers(r, got) &&
        got == (std::vector<uint32_t>{0x10C69A, 0x0E1A1B}));
    Bytes made = net::EncodeTravelMarkers({0xFF000801});
    Reader rm(made);
    CHECK(rm.Type(t) && !net::DecodeTravelMarkers(rm, got));
    Bytes none = net::EncodeTravelMarkers({});
    Reader rn(none);
    CHECK(rn.Type(t) && !net::DecodeTravelMarkers(rn, got));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    a.ShareTravelMarkers({0x10C69A});
    std::vector<uint32_t> atHost, atBob;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (uint32_t m : host.TakeTravelMarkers())
            atHost.push_back(m);
        return !atHost.empty();
    }));
    CHECK(atHost == std::vector<uint32_t>{0x10C69A});
    host.ShareTravelMarkers(atHost);
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (uint32_t m : b2.TakeTravelMarkers())
            atBob.push_back(m);
        return !atBob.empty();
    }));
    CHECK(atBob == std::vector<uint32_t>{0x10C69A});
}

TEST(a_players_furniture_animation_reaches_the_others)
{
    const std::string chair = "Meshes\\Characters\\_Male\\IdleAnims\\DynamicIdle_ChairSit.kf";
    CHECK(net::IsAnimPath(chair));
    CHECK(net::IsAnimPath("characters\\_male\\idleanims\\stool.KF"));
    CHECK(!net::IsAnimPath("Meshes\\Characters\\..\\..\\..\\x.kf"));
    CHECK(!net::IsAnimPath("C:\\Windows\\x.kf"));
    CHECK(!net::IsAnimPath("Characters\\_Male\\x.nif"));
    CHECK(!net::IsAnimPath("Textures\\x.kf"));
    CHECK(!net::IsAnimPath("Characters\\x\" ; tcl \".kf"));
    MsgType t;
    net::PlayerAnim m;
    Bytes bad = Encode(net::PlayerAnim{1, "Characters/_Male/x.kf"});
    Reader rb(bad);
    CHECK(rb.Type(t) && t == MsgType::PlayerAnim && !Decode(rb, m));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    uint8_t ann = a.MyPlayerId();
    a.SetMyAnim(chair);
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        auto h = host.Anims().find(ann), b = b2.Anims().find(ann);
        return h != host.Anims().end() && h->second == chair && b != b2.Anims().end() && b->second == chair;
    }));
    a.SetMyAnim("");
    CHECK(RunUntil({&host, &a, &b2}, [&] { return !host.Anims().count(ann) && !b2.Anims().count(ann); }));
    host.SetMyAnim(chair);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return a.Anims().count(coop::kHostPlayerId) == 1; }));
    coop::Coop late;
    CHECK(late.Join("127.0.0.1", kTestPort, "Cat"));
    CHECK(RunUntil({&host, &a, &b2, &late}, [&] { return late.Anims().count(coop::kHostPlayerId) == 1; }));
}

TEST(a_players_speed_and_mod_animations_reach_the_others)
{
    MsgType t;
    PlayerState state;
    Bytes s = Encode(PlayerState{1, 0, 0, 0, 0, 0, 0, 0, 0x201, 0, 610});
    Reader rs(s);
    CHECK(rs.Type(t) && Decode(rs, state) && state.runSpeed == 610);

    const std::string sprint = "characters\\_male\\em_sprintm\\mtfastforward.kf";
    CHECK(net::IsAnimPath(sprint));
    CHECK(net::IsAnimPath("meshes\\animgroupoverride\\em_prone\\_male\\sneakmtidle.kf"));
    net::PlayerModAnims m;
    Bytes b = Encode(net::PlayerModAnims{2, {sprint}});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::PlayerModAnims && Decode(r, m) && m.paths == std::vector<std::string>{sprint});
    Bytes bad = Encode(net::PlayerModAnims{2, {"textures\\x.kf"}});
    Reader rb(bad);
    CHECK(rb.Type(t) && !Decode(rb, m));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    uint8_t ann = a.MyPlayerId();
    a.SetMyModAnims({sprint});
    CHECK(RunUntil({&host, &a, &b2}, [&] { return host.ModAnims().count(ann) && b2.ModAnims().count(ann); }));
    a.SetMyModAnims({});
    CHECK(RunUntil({&host, &a, &b2}, [&] { return !host.ModAnims().count(ann) && !b2.ModAnims().count(ann); }));
}

TEST(a_body_looted_with_nobody_looking_is_noticed)
{
    coop::BodyLootWatch w;
    std::vector<net::ItemCount> full{{100, 2, 50, 0}, {200, 1, 50, 0}}, less{{100, 2, 50, 0}};
    CHECK(!w.Look(7, full, false));
    CHECK(!w.Look(7, full, false));
    CHECK(w.Look(7, less, false));
    CHECK(!w.Look(7, less, false));
    CHECK(w.Look(7, full, false));
    CHECK(w.Look(7, {}, false));
}

TEST(what_a_body_drops_as_it_dies_is_not_loot_but_taking_from_it_is)
{
    coop::BodyLootWatch w;
    std::vector<net::ItemCount> before{{100, 1, 50, 0}}, dropped{{100, 1, 50, 0}, {300, 1, 80, 0}};
    CHECK(!w.Look(7, before, true));
    CHECK(!w.Look(7, dropped, true));
    CHECK(w.Look(7, before, true));
}

TEST(bodies_gone_from_the_list_are_forgotten)
{
    coop::BodyLootWatch w;
    std::vector<net::ItemCount> full{{100, 1, 50, 0}};
    w.Look(7, full, false);
    w.Look(8, full, false);
    w.KeepOnly({8});
    CHECK(w.Size() == 1);
    CHECK(!w.Look(7, {}, false));
    CHECK(w.Look(8, {}, false));
}

TEST(body_loot_messages_read_back)
{
    std::vector<net::ItemCount> items{{100, 2, 50, 0}, {200, 1, 80, 1}};
    Bytes b = net::EncodeBodyLoot(0x00146338, items);
    Reader r(b);
    MsgType t;
    net::ContainerContents m;
    CHECK(r.Type(t) && t == MsgType::BodyLoot && Decode(r, m));
    CHECK(m.ref == 0x00146338 && m.items.size() == 2 && m.items[1].item == 200 && m.items[1].mods == 1);
}

TEST(join_addresses_are_checked)
{
    std::string host;
    uint16_t port = 27960;
    CHECK(net::ParseHostAddress("203.0.113.5", host, port) && host == "203.0.113.5" && port == 27960);
    CHECK(net::ParseHostAddress(" 100.64.1.2:27961 ", host, port) && host == "100.64.1.2" && port == 27961);
    CHECK(net::ParseHostAddress("my-pc.tailnet.ts.net", host, port) && host == "my-pc.tailnet.ts.net");
    port = 27960;
    CHECK(!net::ParseHostAddress("", host, port));
    CHECK(!net::ParseHostAddress("1.2.3.4:0", host, port));
    CHECK(!net::ParseHostAddress("1.2.3.4:70000", host, port));
    CHECK(!net::ParseHostAddress("1.2.3.4:", host, port));
    CHECK(!net::ParseHostAddress("http://1.2.3.4", host, port));
    CHECK(!net::ParseHostAddress("1.2.3.4 5.6.7.8", host, port) && port == 27960);
}

TEST(the_teleporter_brings_a_player_or_goes_to_them)
{
    MsgType t;
    net::Teleport m;
    Bytes b = Encode(net::Teleport{2, 0});
    Reader r(b);
    CHECK(r.Type(t) && t == MsgType::Teleport && Decode(r, m) && m.mover == 2 && m.target == 0);
    Bytes self = Encode(net::Teleport{2, 2});
    Reader rs(self);
    CHECK(rs.Type(t) && !Decode(rs, m));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined &&
            a.Remotes().count(b2.MyPlayerId()) && b2.Remotes().count(a.MyPlayerId());
    }));
    a.AskTeleport(b2.MyPlayerId(), a.MyPlayerId());
    std::vector<uint8_t> bob;
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (uint8_t p : b2.TakeTeleports())
            bob.push_back(p);
        return !bob.empty();
    }));
    CHECK(bob == std::vector<uint8_t>{a.MyPlayerId()});
    a.AskTeleport(a.MyPlayerId(), coop::kHostPlayerId);
    CHECK(a.TakeTeleports() == std::vector<uint8_t>{coop::kHostPlayerId});
    a.AskTeleport(b2.MyPlayerId(), coop::kHostPlayerId);
    std::vector<uint8_t> host0;
    RunUntil({&host, &a, &b2}, [&] {
        for (uint8_t p : b2.TakeTeleports())
            bob.push_back(p);
        for (uint8_t p : host.TakeTeleports())
            host0.push_back(p);
        return false;
    }, 300);
    CHECK(bob.size() == 1 && host0.empty());
    b2.AskTeleport(coop::kHostPlayerId, b2.MyPlayerId());
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        for (uint8_t p : host.TakeTeleports())
            host0.push_back(p);
        return !host0.empty();
    }));
    CHECK(host0 == std::vector<uint8_t>{b2.MyPlayerId()});
}

TEST(the_host_tells_friends_when_enemies_fight_them)
{
    MsgType t;
    net::Fighting m;
    Bytes on = Encode(net::Fighting{true});
    Reader r(on);
    CHECK(r.Type(t) && t == MsgType::Fighting && Decode(r, m) && m.fighting);
    Bytes bad = on;
    bad.back() = 2;
    Reader rb(bad);
    CHECK(rb.Type(t) && !Decode(rb, m));

    coop::Coop host, a, b2;
    CHECK(host.Host(kTestPort, "Host"));
    CHECK(a.Join("127.0.0.1", kTestPort, "Ann"));
    CHECK(b2.Join("127.0.0.1", kTestPort, "Bob"));
    CHECK(RunUntil({&host, &a, &b2}, [&] {
        return a.GetFriendState() == coop::FriendState::Joined && b2.GetFriendState() == coop::FriendState::Joined;
    }));
    CHECK(!a.HostSaysFighting() && !b2.HostSaysFighting());
    uint8_t ann = 0;
    for (const auto& [id, remote] : host.Remotes())
        if (remote.name == "Ann")
            ann = id;
    CHECK(ann != 0);
    host.SetFighting(ann, true);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return a.HostSaysFighting(); }));
    CHECK(!b2.HostSaysFighting());
    host.SetFighting(ann, false);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return !a.HostSaysFighting(); }));
    host.SetFighting(ann, true);
    CHECK(RunUntil({&host, &a, &b2}, [&] { return a.HostSaysFighting(); }));
    a.Stop();
    CHECK(!a.HostSaysFighting());
    b2.Stop();
    host.Stop();
}

TEST(an_item_leaves_the_games_list_without_breaking_it)
{
    int a, b, c, other;
    coop::ItemNode third{&c, nullptr}, second{&b, &third}, head{&a, &second};
    CHECK(!coop::RemoveFromList(&head, &other));
    CHECK(coop::RemoveFromList(&head, &b));
    CHECK(head.item == &a && head.next == &second && second.item == &c && second.next == nullptr);
    CHECK(coop::RemoveFromList(&head, &c));
    CHECK(head.item == &a && head.next == nullptr);
    CHECK(coop::RemoveFromList(&head, &a));
    CHECK(head.item == nullptr && head.next == nullptr);
    CHECK(!coop::RemoveFromList(&head, &a));

    // Taking out the first of several keeps the first node, which belongs to whatever owns the list.
    coop::ItemNode two{&b, nullptr}, one{&a, &two};
    CHECK(coop::RemoveFromList(&one, &a));
    CHECK(one.item == &b && one.next == nullptr);

    CHECK(!coop::RemoveFromList(nullptr, &a));
    coop::ItemNode broken{&a, nullptr};
    broken.next = &broken;
    CHECK(!coop::RemoveFromList(&broken, &other));
}

TEST(a_stack_merged_away_is_seen_gone)
{
    int a, b, c, merged;
    coop::StackNode third{&c, nullptr}, second{&b, &third}, head{&a, &second};
    CHECK(coop::HoldsStack(&head, &a) && coop::HoldsStack(&head, &b) && coop::HoldsStack(&head, &c));
    CHECK(!coop::HoldsStack(&head, &merged));
    coop::StackNode none{nullptr, nullptr};
    CHECK(!coop::HoldsStack(&none, &a));
    CHECK(!coop::HoldsStack(nullptr, &a));
    coop::StackNode broken{&b, nullptr};
    broken.next = &broken;
    CHECK(coop::HoldsStack(&broken, &a));
}
