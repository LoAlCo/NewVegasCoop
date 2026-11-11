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
        bool key = on && !game::ChatTyping() && game::IsKeyDown(g_config.chatKey);
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
            if (!who || g_cutscene || (!g_config.friendMarkers && !remote.Downed()))
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
