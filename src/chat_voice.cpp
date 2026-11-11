// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "nvse/PluginAPI.h"
#include "config.h"
#include "net/session.h"
#include "coop/activator_uses.h"
#include "coop/clock.h"
#include "coop/coop.h"
#include "coop/vats_feel.h"
#include "coop/game_mode.h"
#include "coop/quests.h"
#include "coop/frame_parts.h"
#include "coop/threat.h"
#include "coop/reinforcements.h"
#include "coop/balance.h"
#include "coop/steer.h"
#include "speech.h"
#include "display.h"
#include "voice_out.h"
#include "game/steam.h"
#include "crash.h"
#include "dll_blocker.h"
#include "dev/commands.h"
#include "dev/lab_channel.h"
#include "dev/dump_game.h"
#include "dev/second_copy.h"
#include "coop/party_perks.h"
#include "game/addresses.h"
#include "game/far_cells.h"
#include "game/game.h"
#include "game/escape_menu.h"
#include "game/level_up.h"
#include "game/perk_menu.h"
#include "game/remove_all_items.h"
#include "game/inspector.h"
#include "game/mirror.h"
#include "game/mover.h"
#include "game/companions.h"
#include "game/containers.h"
#include "game/doors.h"
#include "game/objects.h"
#include "game/terminals.h"
#include "game/activations.h"
#include "game/mines.h"
#include "game/items.h"
#include "game/progress.h"
#include "game/dialogue.h"
#include "game/sleepwait.h"
#include "game/steam.h"
#include "game/quests.h"
#include "game/npcs.h"
#include "game/standins.h"
#include "log.h"
#include "version.h"
#include <algorithm>
#include <shlobj.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "plugin.h"

    coop::ChatHistory g_chatHistory;
    coop::EventFeed g_eventFeed;
    coop::PartyWatch g_partyWatch;

namespace
{
    // Chat's scroll keys, read by any route (see game::IsKeyDown) so a controller remapper that presses them works too.
    // DirectInput codes: Page Up, Page Down, Up, Down.
    int ChatKeyScroll(uint32_t now, bool typing)
    {
        static coop::HoldRepeat s_keys[4];
        constexpr int kKeys[4] = {0xC9, 0xD1, 0xC8, 0xD0};
        constexpr int kLines[4] = {coop::ChatPadControl::kPage, -coop::ChatPadControl::kPage, 1, -1};
        int scroll = 0;
        for (int i = 0; i < 4; ++i)
            if (s_keys[i].Update(typing && game::IsKeyDown(kKeys[i]), now))
                scroll += kLines[i];
        return scroll;
    }

    // A controller in chat (coop::ChatPadControl): adds what it sends to typed, and gives back how far it scrolled
    int UpdateChatPad(uint32_t now, bool on, std::vector<std::string>& typed)
    {
        static coop::ChatPadControl s_control;
        static size_t s_pick = 0;
        static bool s_hiding = false;
        bool typing = game::ChatTyping();
        uint16_t openButton = on ? coop::ChatPadButton(g_config.chatPadButton) : 0;
        bool inChat = on && g_config.chatPad;
        coop::ChatPadInput pad;
        if (openButton || (inChat && typing) || s_hiding)
            game::ReadChatPad(pad);
        coop::ChatPadActions act = s_control.Update(now, pad, typing, openButton, inChat);
        const std::vector<std::string>& quick = g_config.quickMessages;
        if (act.open && !game::InMenu() && game::OpenChat())
        {
            LOG_INFO("Chat: opened with the controller");
            typing = true;
        }
        else if (act.close)
            game::CancelChat();
        else if (act.send)
        {
            if (!game::ChatDraftEmpty())
                game::TypeChat('\r');
            else if (!quick.empty())
            {
                typed.push_back(quick[s_pick % quick.size()]);
                game::CancelChat();
            }
        }
        if (!quick.empty())
            s_pick = (s_pick + quick.size() + act.pick % static_cast<int>(quick.size())) % quick.size();
        typing = game::ChatTyping();
        bool padHere = inChat && typing && pad.connected;
        game::SetChatHint(padHere ? coop::QuickMessageHint(quick, s_pick) : std::string());
        // After chat closes the game goes on not seeing the controller until everything's let go, so the A that sent a
        // message or the B that closed chat isn't then a press for the game too
        s_hiding = (inChat && typing) || (s_hiding && pad.connected && pad.buttons != 0);
        game::HidePadFromGame(s_hiding);
        if (!typing)
            s_pick = 0;
        return act.scroll;
    }

    void UpdateChatPanel(uint32_t now, int scroll)
    {
        static bool s_wasTyping = false;
        bool inGame = g_coop.InGame();
        if (inGame && !g_partyWatch.InSession())
            g_chatHistory.Add(g_coop.GetRole() == coop::Role::Host ? "--- Hosting a session ---" : "--- Joined a session ---");

        std::vector<coop::PartyWatch::Member> roster;
        if (inGame)
            for (const auto& [id, remote] : g_coop.Remotes())
                roster.push_back({id, remote.name, remote.maxHealth > 0, remote.Downed()});
        for (const std::string& line : g_partyWatch.Observe(now, inGame, roster))
        {
            LOG_INFO("Event: %s", line.c_str());
            g_chatHistory.Add(line);
            if (g_config.eventFeed)
                g_eventFeed.Add(line);
        }
        if (!inGame || (!g_config.eventFeed && !g_config.chatPopup))
            g_eventFeed.Clear();

        bool typing = game::ChatTyping();
        scroll += ChatKeyScroll(now, typing);
        if (typing != s_wasTyping)
        {
            g_chatHistory.ResetScroll();
            scroll = 0;
        }
        s_wasTyping = typing;

        bool on = inGame && g_worldLoaded;
        g_eventFeed.SetLineMs(static_cast<uint32_t>(g_config.feedSeconds) * 1000);
        std::vector<coop::ChatPanelRow> feed = g_eventFeed.Update(now, on && game::InMenu());
        g_chatHistory.SetShape(coop::ChatPanelRows(g_config.feedSize),
            coop::ChatPanelCharsPerRow(g_config.feedWidth, g_config.feedSize));
        std::vector<coop::ChatPanelRow> rows;
        if (g_config.feedPreview)
            rows = coop::ChatPanelPreview();
        else if (on && typing && g_config.chatHistory)
        {
            g_chatHistory.Scroll(scroll);
            rows = coop::HistoryPanelRows(g_chatHistory.View());
        }
        else if (on)
            rows = std::move(feed);
        game::ChatPanelLook look;
        look.xPercent = g_config.feedX;
        look.yPercent = g_config.feedY;
        look.width = static_cast<float>(g_config.feedWidth);
        look.scale = g_config.feedSize / 100.0f;
        look.alignRight = g_config.feedAlign == 1;
        look.gap = static_cast<float>(g_config.feedGap);
        look.style = g_config.feedStyle;
        look.dividers = g_config.feedDividers;
        game::ShowChatPanel(rows, look);
    }
}

    void UpdateChat()
    {
        static bool s_keyWas = false;
        bool on = g_config.chat && g_coop.InGame() && g_worldLoaded;
        if (!on && game::ChatTyping())
            game::CancelChat();
        bool key = on && !game::ChatTyping() && game::TextEntryState() != game::EntryState::Typing &&
            game::IsKeyDown(g_config.chatKey);
        if (s_keyWas && !key && on && !game::ChatTyping())
            game::OpenChat();
        s_keyWas = key;
        uint32_t now = GetTickCount();
        std::vector<std::string> padSent;
        int padScroll = UpdateChatPad(now, on, padSent);
        std::vector<std::string> typed = game::UpdateChat();
        for (std::string& t : padSent)
            typed.push_back(std::move(t));
        for (std::string& t : g_labChat)
            typed.push_back(std::move(t));
        g_labChat.clear();
        for (const std::string& text : typed)
        {
            std::string line = coop::CleanChat(text);
            if (!on || line.empty())
                continue;
            LOG_INFO("Chat: we say \"%s\"", line.c_str());
            g_chatHistory.Add(g_coop.NameOf(g_coop.MyPlayerId()) + ": " + line);
            coop::VoiceStyle style{static_cast<uint8_t>(g_config.voice), static_cast<uint8_t>(g_config.voiceVariant),
                static_cast<int8_t>(g_config.voicePitch), static_cast<int8_t>(g_config.voiceSpeed),
                static_cast<uint8_t>(g_config.voiceEffect)};
            g_coop.SendChat(line, style);
            if (g_config.chatSpeech)
                g_speech.Say(line, g_coop.MyPlayerId(), style, g_config.chatSpeechVolume / 100.0f, 0);
        }
        for (const coop::Coop::ChatHeard& h : g_coop.TakeChat())
        {
            LOG_INFO("Chat: %s says \"%s\"", h.name.c_str(), h.text.c_str());
            if (!g_config.chat)
                continue;
            g_chatHistory.Add(h.name + ": " + h.text);
            // The panel shows it whether or not chat is open, so the corner notification would only say it twice
            if (g_config.chatPopup)
                g_eventFeed.Add(h.name + ": " + h.text, coop::ChatPopupMs(h.text.size()));
            TESObjectREFR* who = g_standIns.Character(h.player);
            if (who && game::DistanceToPlayer(who) < 2500)
                game::ShowChatAbove(h.player, who, h.text);
            else if (!g_config.chatPopup)
                game::Notify((h.name + ": " + h.text).c_str(), 6);
            coop::Pose me, them;
            if (g_config.chatSpeech && who && game::GetPose(game::Player(), me) && game::GetPose(who, them) &&
                me.space == them.space)
            {
                coop::SpeechMix mix = coop::MixSpeech(them.x - me.x, them.y - me.y, them.z - me.z, me.heading);
                if (mix.volume > 0.02f)
                    g_speech.Say(h.text, h.player, h.style, mix.volume * g_config.chatSpeechVolume / 100.0f, mix.pan);
            }
        }
        game::UpdateChatAbove();
        static std::map<uint8_t, uint32_t> s_downedSince;
        static uint32_t s_lastMarks = 0;
        uint32_t step = s_lastMarks ? now - s_lastMarks : 0;
        s_lastMarks = now;
        std::vector<game::FriendMark> friends;
        for (const auto& [id, remote] : g_coop.InGame() ? g_coop.Remotes() : std::map<uint8_t, coop::RemotePlayer>())
        {
            bool dragged = g_coop.DraggedBy(id) >= 0;
            if (!remote.Downed())
                s_downedSince.erase(id);
            else if (!s_downedSince.count(id))
                s_downedSince[id] = now;
            else if (dragged)
                s_downedSince[id] += step;
            TESObjectREFR* who = g_standIns.Character(id);
            if (!who || g_cutscene || game::TradeMenuOpen() || (!g_config.friendMarkers && !remote.Downed()))
                continue;
            if (!remote.Downed())
            {
                friends.push_back({who, g_coop.NameOf(id), false});
                continue;
            }
            uint32_t gone = now - s_downedSince[id];
            uint32_t left = gone < coop::kBleedOutMs ? (coop::kBleedOutMs - gone + 999) / 1000 : 0;
            friends.push_back({who, g_coop.NameOf(id) + (dragged ? std::string("\nDOWNED, dragged") :
                "\nDOWNED " + std::to_string(left) + "s"), true});
        }
        if (!g_coop.InGame())
            s_downedSince.clear();
        game::ShowFriendMarkers(friends);
        UpdateChatPanel(now, padScroll);
    }

    // What a ping follows, as this game has it: a player by ID, or an NPC, body or item by the host's ref, which a visitor
    // turns into its own copy (an NPC made, rerolled or held; an item placed in its stead).
    TESObjectREFR* PingedThing(uint8_t kind, uint32_t target)
    {
        if (kind == static_cast<uint8_t>(net::PingKind::Friend))
            return target == g_coop.MyPlayerId() ? game::Player() : g_standIns.Character(static_cast<uint8_t>(target));
        if (!target || (kind != static_cast<uint8_t>(net::PingKind::Enemy) && kind != static_cast<uint8_t>(net::PingKind::Person) &&
                kind != static_cast<uint8_t>(net::PingKind::Loot)))
            return nullptr;
        if (g_coop.GetRole() == coop::Role::Host)
            return game::LookupRef(target);
        if (TESObjectREFR* npc = g_npcs.OurRefOf(target); npc && game::IsActor(npc))
            return npc;
        if (kind != static_cast<uint8_t>(net::PingKind::Loot))
            return nullptr;
        TESObjectREFR* item = game::LookupRef(g_coop.OurItemOf(target));
        return item && !game::IsActor(item) ? item : nullptr;
    }

    bool PingSpotOn(TESObjectREFR* thing, float (&at)[3])
    {
        return game::OverHead(thing, at, game::IsActor(thing) ? 130.0f : 8.0f);
    }

    void UpdatePings()
    {
        static bool s_keyWas = false;
        static coop::PingGate s_gate(coop::kPingSendGapMs);
        static coop::PingBoard s_board;
        uint32_t now = GetTickCount();
        bool on = g_config.pings && g_coop.InGame() && g_worldLoaded && !g_loadingSave;
        if (!on)
            s_board.Clear();
        coop::Pose me;
        bool placed = on && game::GetPose(game::Player(), me);
        // IsKeyPressed also counts a key another mod presses for the player (HoldKey or TapKey), so a pad button a
        // controller mod maps to the ping key pings too.
        uint16_t padMask = coop::PingPadMask(g_config.pingPadButton);
        bool ready = placed && !g_cutscene && !game::ChatTyping();
        bool key = ready && (game::IsKeyPressed(g_config.pingKey) ||
            (padMask && (game::PadButtons() & padMask) == padMask && !game::InMenuMode()));
        if (key && !s_keyWas && s_gate.Allow(now))
        {
            game::CrosshairPick aim;
            bool hit = game::CrosshairPoint(aim) && aim.hit;
            coop::PingThing chosen = hit ? aim.thing : coop::PingThing::Nothing;
            coop::PingLook look = coop::ClassifyPing(chosen, aim.name);
            net::Ping ping;
            ping.kind = look.kind;
            ping.space = me.space;
            ping.x = hit ? aim.at[0] : me.x;
            ping.y = hit ? aim.at[1] : me.y;
            ping.z = hit ? aim.at[2] : me.z;
            ping.name = look.name;
            if (chosen == coop::PingThing::Loot)
            {
                bool host = g_coop.GetRole() == coop::Role::Host;
                ping.target = coop::PingItemTarget(ping.kind, host ? aim.ref : g_coop.HostItemOf(aim.ref));
                float above[3];
                if (ping.target && PingSpotOn(game::LookupRef(aim.ref), above))
                {
                    ping.x = above[0];
                    ping.y = above[1];
                    ping.z = above[2];
                }
            }
            TESObjectREFR* who = chosen == coop::PingThing::Actor ? game::LookupRef(aim.ref) : nullptr;
            if (who && game::IsActor(who))
            {
                coop::PingCharacter c;
                c.player = g_standIns.PlayerIdOf(who);
                c.name = c.player >= 0 ? g_coop.NameOf(static_cast<uint8_t>(c.player)) : std::string(game::Name(who));
                c.dead = game::IsDead(who);
                c.hostile = c.player < 0 && !c.dead && game::WouldAttackPlayer(who);
                c.hostRef = g_coop.GetRole() == coop::Role::Host ? game::RefId(who) : g_npcs.HostRefOf(game::RefId(who));
                coop::PingAim pinged = coop::ClassifyCharacterPing(c);
                ping.kind = pinged.kind;
                ping.name = pinged.name;
                ping.target = pinged.target;
                float head[3];
                if (PingSpotOn(who, head))
                {
                    ping.x = head[0];
                    ping.y = head[1];
                    ping.z = head[2];
                }
                coop::Pose feet;
                float above = game::GetPose(who, feet) ? ping.z - feet.z : 0;
                LOG_INFO("Pings: we mark character %08X (%s, host's %08X, player %d, dead %d, enemy %d), %.0f above their feet",
                    aim.ref, ping.name.c_str(), c.hostRef, c.player, c.dead, c.hostile, above);
            }
            float at[3] = {ping.x, ping.y, ping.z}, sx = 0, sy = 0;
            bool inView = false;
            game::SpotOnScreen(at, sx, sy, inView);
            LOG_INFO("Pings: we mark %.0f %.0f %.0f in place %08X (%s, kind %u, ref %08X thing %u, %.0f away on \"%s\"), %.3f %.3f on screen",
                ping.x, ping.y, ping.z, ping.space, ping.name.c_str(), static_cast<unsigned>(ping.kind), aim.ref,
                static_cast<unsigned>(aim.thing), aim.distance, aim.what, sx, sy);
            g_coop.SendPing(ping);
            s_board.Add(g_coop.MyPlayerId(), "You", ping.name, ping.x, ping.y, ping.z, now, static_cast<uint8_t>(ping.kind),
                ping.target);
            game::PlayPingSound(g_config.pingSound);
        }
        s_keyWas = key;
        const auto& remotes = g_coop.Remotes();
        for (coop::Coop::PingHeard& h : g_coop.TakePings())
        {
            coop::Pose sender;
            auto remote = remotes.find(h.ping.player);
            bool known = remote != remotes.end() && remote->second.motion.Newest(sender);
            bool inRange = placed && h.ping.space == me.space && coop::PingInRange(&me, known ? &sender : nullptr, coop::kPingRadius);
            uint8_t kind = static_cast<uint8_t>(h.ping.kind);
            TESObjectREFR* who = PingedThing(kind, h.ping.target);
            LOG_INFO("Pings: %s marks %.0f %.0f %.0f in place %08X (%s, kind %u, target %08X, ours %08X), %s", h.who.c_str(),
                h.ping.x, h.ping.y, h.ping.z, h.ping.space, h.ping.name.c_str(), static_cast<unsigned>(kind), h.ping.target,
                game::RefId(who), inRange ? "shown" : "out of range");
            if (!inRange)
                continue;
            if (h.ping.kind == net::PingKind::Friend && h.ping.target == g_coop.MyPlayerId())
                h.ping.name = "You";
            s_board.Add(h.ping.player, h.who, h.ping.name, h.ping.x, h.ping.y, h.ping.z, now, kind, h.ping.target);
            game::PlayPingSound(g_config.pingSound);
        }
        s_board.Expire(now);
        std::array<game::PingMark, coop::kPingSlots> marks;
        for (int slot = 0; slot < coop::kPingSlots; ++slot)
        {
            const coop::ActivePing& p = s_board.Slots()[slot];
            if (!p.used)
                continue;
            if (p.player != g_coop.MyPlayerId() && !remotes.count(p.player))
            {
                s_board.Remove(p.player);
                continue;
            }
            float head[3];
            if (TESObjectREFR* thing = PingedThing(p.kind, p.target); thing && PingSpotOn(thing, head))
                s_board.Follow(slot, head[0], head[1], head[2]);
            marks[slot].active = true;
            marks[slot].text = coop::PingLine(p.who, p.name, p.x - me.x, p.y - me.y);
            marks[slot].at[0] = p.x;
            marks[slot].at[1] = p.y;
            marks[slot].at[2] = p.z;
            marks[slot].kind = p.kind;
        }
        game::ShowPings(marks, static_cast<coop::PingIcons>(g_config.pingIcons));
    }

    bool g_labTalking = false;

    void UpdateVoice()
    {
        bool on = g_config.voiceChat && g_coop.InGame() && g_worldLoaded;
        bool talk = on && !game::ChatTyping() &&
            (g_config.voiceChatMode == 1 || g_labTalking || game::KeyDown(g_config.voiceChatKey));
        static bool s_talking = false, s_warned = false;
        if (talk != s_talking)
        {
            if (!game::VoiceRecording(talk) && talk && !s_warned)
            {
                s_warned = true;
                LOG_WARN("Voice: Steam's voice isn't there; no voice chat");
            }
            s_talking = talk;
            LOG_DEBUG("Voice: %s", talk ? "talking" : "stopped talking");
        }
        game::ShowTalking(talk && g_config.voiceChatMode == 0);
        net::Bytes recorded;
        for (int i = 0; i < 4 && game::TakeVoice(recorded); ++i)
            g_coop.SendVoice(recorded);
        std::vector<int16_t> samples;
        for (const coop::Coop::VoiceHeard& v : g_coop.TakeVoice())
            if (g_config.voiceChat && game::DecompressVoice(v.data, coop::kVoiceRate, samples))
                g_voiceOut.Push(v.player, samples);
        coop::Pose me;
        bool here = game::GetPose(game::Player(), me);
        for (const auto& [id, remote] : g_coop.Remotes())
        {
            coop::Pose them;
            TESObjectREFR* who = g_standIns.Character(id);
            coop::SpeechMix mix{0, 0};
            if (here && who && game::GetPose(who, them) && them.space == me.space)
                mix = coop::MixSpeech(them.x - me.x, them.y - me.y, them.z - me.z, me.heading);
            g_voiceOut.SetMix(id, mix.volume * g_config.voiceChatVolume / 100.0f, mix.pan);
            static std::map<uint8_t, bool> s_moving;
            float opening = g_config.mouths ? coop::MouthOpening((std::max)(g_voiceOut.Level(id), g_speech.Level(id))) : 0;
            bool& moving = s_moving[id];
            if (who && (opening > 0 || moving))
                game::SetMouth(who, opening);
            moving = opening > 0;
        }
    }
