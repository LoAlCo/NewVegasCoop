// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// The voice line wheel: hold the key, move the mouse towards a phrase, let go, and your character says it in an NPC
// voice. Friends hear it from your character, with lip sync, through the game's own speech. With Say Lines By
// Themselves on, your character also shouts now and then by itself (a fight starting, attacking, being hit...), the
// way other players' characters did by accident in co-op 0.1.10 and before; those lines go to friends the same way.

#include "nvse/PluginAPI.h"
#include "nvse/GameObjects.h"
#include "config.h"
#include "coop/coop.h"
#include "coop/voice_lines.h"
#include "game/game.h"
#include "game/standins.h"
#include "log.h"
#include "plugin.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <random>
#include <string>

namespace
{
    bool g_wheelOpen = false;
    bool g_wheelFemale = false;
    float g_pointerX = 0, g_pointerY = 0;
    int g_chosen = -1;
    int g_chosenLine = 0;   // which of the hovered phrase's lines it'll be (shown under the wheel)
    coop::VoiceLineLimiter g_myLimit;
    coop::AutoLines g_auto;
    std::minstd_rand g_random{GetTickCount()};

    float Roll() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_random); }

    const char* Text(uint8_t phrase, uint8_t line)
    {
        return coop::IsVoiceLine(phrase, line) ? coop::kVoicePhrases[phrase].lines[line].text : "?";
    }

    bool Say(TESObjectREFR* who, uint8_t phrase, uint8_t line, uint32_t voice, game::Sound& sound)
    {
        const coop::VoiceLineForm& form = coop::kVoicePhrases[phrase].lines[line];
        return game::SayInVoice(who, voice, form.topic, form.info, sound);
    }

    // The game does make our own character speak (and move its mouth), but that copy can't be heard well from our
    // own view, so it's turned down and the same voice file is played to us directly instead.
    uint32_t g_muteUntil = 0;
    uint32_t g_mutedId = 0xFFFFFFFF;

    void MuteMine(const game::Sound& spoken)
    {
        g_mutedId = spoken.id;
        g_muteUntil = GetTickCount() + 2000;
        game::Sound s = spoken;
        game::SetSoundVolume(s, 0);
    }

    // The first time a voice file loads, its sound turns up a little after the line starts: turn that down too.
    void KeepMineMuted(uint32_t now)
    {
        if (!g_muteUntil)
            return;
        if (static_cast<int32_t>(now - g_muteUntil) >= 0)
        {
            g_muteUntil = 0;
            return;
        }
        game::Sound s;
        if (game::Player() && game::SpokenSound(game::Player(), s) && s.id != g_mutedId && s.id != 0xFFFFFFFF)
        {
            g_mutedId = s.id;
            game::SetSoundVolume(s, 0);
            LOG_DEBUG("Voice lines: turned down your character's own copy of the line (sound %08X)", s.id);
        }
    }

    void HearMyself(uint8_t phrase, uint8_t line, uint32_t voice, game::Sound& spoken)
    {
        MuteMine(spoken);
        if (!g_config.voiceLineHearOwn)
            return;
        const char* folder = coop::VoiceFolder(voice);
        if (!folder)
            return;
        std::string path = std::string("Data\\Sound\\Voice\\FalloutNV.esm\\") + folder + "\\" +
            coop::kVoicePhrases[phrase].lines[line].file + "_1.ogg";
        game::Sound flat;
        if (!game::PlayFlatSound(path, flat))
            LOG_WARN("Voice lines: couldn't play %s for you to hear", path.c_str());
    }

    // Says a phrase on our character and sends it to friends. why: "" for the wheel, else what made us say it.
    bool Speak(uint8_t phrase, uint32_t now, const char* why, int pickedLine = -1)
    {
        if (phrase >= coop::kVoicePhraseCount)
            return false;
        if (!g_myLimit.Allow(now))
        {
            LOG_DEBUG("Voice lines: too soon after the last one; \"%s\" not said", coop::kVoicePhrases[phrase].name);
            return false;
        }
        auto line = static_cast<uint8_t>(pickedLine >= 0 && pickedLine < coop::kVoicePhrases[phrase].count ? pickedLine :
            g_random() % coop::kVoicePhrases[phrase].count);
        net::PlayerLooks looks;
        bool female = game::ReadMyLooks(looks) && looks.female;
        uint32_t voice = coop::VoiceFor(female, g_config.voiceLineVoice);
        game::Sound sound;
        bool said = Say(game::Player(), phrase, line, voice, sound);
        if (said)
            HearMyself(phrase, line, voice, sound);
        bool sent = g_coop.SendVoiceLine(phrase, line, voice);
        if (said)
            LOG_INFO("Voice lines: you say \"%s\" (%s%s, voice %08X, sound %08X%s)", Text(phrase, line),
                *why ? "by itself: " : "", *why ? why : coop::kVoicePhrases[phrase].name, voice, sound.id,
                sent ? ", sent to friends" : "");
        else
            LOG_WARN("Voice lines: your character couldn't say \"%s\" (voice %08X)%s", Text(phrase, line), voice,
                sent ? "; friends still hear it" : "");
        return true;
    }

    void HearFriends()
    {
        for (const net::VoiceLine& v : g_coop.TakeVoiceLines())
        {
            if (!g_config.voiceLines)
                continue;
            TESObjectREFR* who = g_standIns.Character(v.player);
            if (!who)
            {
                LOG_DEBUG("Voice lines: %s says \"%s\", but their character isn't here", g_coop.NameOf(v.player).c_str(),
                    Text(v.phrase, v.line));
                continue;
            }
            game::Sound sound;
            if (Say(who, v.phrase, v.line, v.voice, sound))
                LOG_INFO("Voice lines: %s says \"%s\" (voice %08X, sound %08X)", g_coop.NameOf(v.player).c_str(),
                    Text(v.phrase, v.line), v.voice, sound.id);
            else
                LOG_WARN("Voice lines: %s's character couldn't say \"%s\" (voice %08X)", g_coop.NameOf(v.player).c_str(),
                    Text(v.phrase, v.line), v.voice);
        }
    }

    // Say Lines By Themselves.
    bool g_autoOn = false;
    bool g_inFight = false;
    uint32_t g_fightSince = 0;
    float g_lastHealth = -1;
    bool g_wasDowned = false;
    uint32_t g_nextAutoLook = 0, g_nextGreetLook = 0;
    std::deque<uint32_t> g_greeted;
    constexpr uint32_t kAutoLookMs = 250, kGreetLookMs = 1000;
    constexpr float kGreetReach = 250;
    constexpr uint32_t kShortestFightMs = 3000;

    bool AutoAllowed() { return g_config.voiceLines && g_config.autoVoiceLines && g_autoOn; }

    void Auto(coop::AutoEvent e, uint32_t now, const char* why)
    {
        int phrase = g_auto.Pick(e, now, Roll(), static_cast<uint32_t>(g_config.autoVoiceLineGap) * 1000,
            static_cast<uint32_t>(g_config.autoGreetingGap) * 60000);
        if (phrase >= 0)
            Speak(static_cast<uint8_t>(phrase), now, why);
    }

    bool Fighting(TESObjectREFR* me)
    {
        if (game::InCombat(me))
            return true;
        return g_coop.InGame() && g_coop.GetRole() == coop::Role::Friend && g_coop.HostSaysFighting();
    }

    TESObjectREFR* SomeoneToGreet(TESObjectREFR* me)
    {
        std::vector<TESObjectREFR*> actors;
        game::NearbyActors(actors);
        TESObjectREFR* best = nullptr;
        float bestDistance = kGreetReach;
        for (TESObjectREFR* a : actors)
        {
            if (a == me || g_standIns.IsStandIn(a) || game::IsDead(a) || game::IsTeammate(a) || game::CombatTarget(a))
                continue;
            uint32_t id = game::RefId(a);
            if (std::find(g_greeted.begin(), g_greeted.end(), id) != g_greeted.end())
                continue;
            float d = std::hypot(a->posX - me->posX, a->posY - me->posY);
            if (d < bestDistance && std::fabs(a->posZ - me->posZ) < 200)
            {
                best = a;
                bestDistance = d;
            }
        }
        return best;
    }

    void UpdateAuto(uint32_t now, TESObjectREFR* me, bool able)
    {
        bool on = g_config.voiceLines && g_config.autoVoiceLines && me && g_worldLoaded;
        if (on && !g_autoOn)
        {
            g_auto.Started(now);
            g_inFight = false;
            g_lastHealth = -1;
            g_wasDowned = g_downed;
            LOG_INFO("Voice lines: your character says lines by itself now");
        }
        g_autoOn = on;
        if (!on || !coop::IsDue(now, g_nextAutoLook))
            return;
        g_nextAutoLook = now + kAutoLookMs;

        if (g_downed && !g_wasDowned)
            Auto(coop::AutoEvent::BadlyHurt, now, "went down");
        g_wasDowned = g_downed;
        if (!able)
            return;

        bool fight = Fighting(me);
        if (fight && !g_inFight)
        {
            g_fightSince = now;
            Auto(coop::AutoEvent::FightStarts, now, "a fight starts");
        }
        else if (!fight && g_inFight && now - g_fightSince >= kShortestFightMs)
            Auto(coop::AutoEvent::FightOver, now, "the fight is over");
        g_inFight = fight;

        float health = game::Health(me), most = game::MaxHealth(me);
        if (fight && g_lastHealth > 0 && most > 0 && health < g_lastHealth - (std::max)(1.0f, most * 0.03f))
        {
            if (health < most * 0.3f)
                Auto(coop::AutoEvent::BadlyHurt, now, "badly hurt");
            else
                Auto(coop::AutoEvent::Hit, now, "hit");
        }
        g_lastHealth = health;

        if (!fight && coop::IsDue(now, g_nextGreetLook) && g_config.autoGreetingGap > 0)
        {
            g_nextGreetLook = now + kGreetLookMs;
            if (TESObjectREFR* who = SomeoneToGreet(me))
            {
                g_greeted.push_back(game::RefId(who));
                if (g_greeted.size() > 32)
                    g_greeted.pop_front();
                std::string why = std::string("greeting ") + game::Name(who);
                Auto(coop::AutoEvent::Greeting, now, why.c_str());
            }
        }
    }
}

void NoteMyAttack(uint8_t shots, bool release)
{
    if ((shots || release) && AutoAllowed() && g_inFight && !g_downed)
        Auto(coop::AutoEvent::Attacking, GetTickCount(), "attacking");
}

namespace
{
    // The controller buttons the wheel can be opened with (MCM Controller Button), as XInput button bits. Start and the
    // face buttons are left out: the game needs them.
    constexpr uint16_t kPadButtons[] = {0, 0x0001, 0x0002, 0x0004, 0x0008, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200};
    constexpr uint16_t kPadB = 0x2000, kPadLeftBumper = 0x0100, kPadRightBumper = 0x0200;
    constexpr float kStickPush = 0.5f;   // how far the right stick must be pushed to pick a wedge
    constexpr int kScrollNotch = 120;    // one notch of the mouse wheel

    enum class WheelBy { Keyboard, Pad };
    WheelBy g_by = WheelBy::Keyboard;
    int g_page = 0;
    int g_scroll = 0;
    uint16_t g_padWas = 0;
    bool g_cancelled = false;

    uint16_t PadButton()
    {
        int i = g_config.voiceLinePadButton;
        return i > 0 && i < static_cast<int>(std::size(kPadButtons)) ? kPadButtons[i] : 0;
    }

    int Phrase() { return g_chosen >= 0 ? g_page * coop::kVoiceSlots + g_chosen : -1; }

    void Pick(int slot, int page)
    {
        int was = Phrase();
        g_chosen = slot;
        g_page = page;
        int now = Phrase();
        if (now >= 0 && now != was)
            g_chosenLine = static_cast<int>(g_random() % coop::kVoicePhrases[now].count);
    }

    void TurnPage(int by)
    {
        int page = ((g_page + by) % coop::kVoicePages + coop::kVoicePages) % coop::kVoicePages;
        Pick(g_chosen, page);
        LOG_DEBUG("Voice lines: page %d", page + 1);
    }
}

void UpdateVoiceLines(uint32_t now)
{
    TESObjectREFR* me = game::Player();
    bool alive = me && !game::IsDead(me);
    bool can = g_config.voiceLines && g_worldLoaded && alive && !g_downed && !game::ChatTyping() &&
        !game::InMenuMode();
    bool keyHeld = can && game::IsKeyPressed(g_config.voiceLineKey);
    game::PadState pad;
    uint16_t padButton = PadButton();
    bool padHeld = can && padButton && game::ReadPad(pad) && (pad.buttons & padButton);
    uint16_t padPressed = static_cast<uint16_t>(pad.buttons & ~g_padWas);
    g_padWas = pad.buttons;
    // Pressing B with the controller cancels: nothing is said until the button is let go and held again.
    if (!keyHeld && !padHeld)
        g_cancelled = false;
    bool held = (keyHeld || padHeld) && !g_cancelled;
    game::VoiceWheelLook look;
    look.hudColor = g_config.voiceLineHudColor;
    look.red = static_cast<float>(g_config.voiceLineRed);
    look.green = static_cast<float>(g_config.voiceLineGreen);
    look.blue = static_cast<float>(g_config.voiceLineBlue);
    if (held && !g_wheelOpen)
    {
        net::PlayerLooks mine;
        g_wheelFemale = game::ReadMyLooks(mine) && mine.female;
    }
    look.female = g_wheelFemale;
    if (held)
    {
        if (!g_wheelOpen)
        {
            g_wheelOpen = true;
            g_by = padHeld && !keyHeld ? WheelBy::Pad : WheelBy::Keyboard;
            g_pointerX = g_pointerY = 0;
            g_scroll = 0;
            g_chosen = -1;
            g_page = 0;
            LOG_DEBUG("Voice lines: wheel open (%s)", g_by == WheelBy::Pad ? "controller" : "keyboard and mouse");
        }
        if (g_by == WheelBy::Keyboard)
        {
            float dx = 0, dy = 0;
            int scroll = 0;
            game::WheelPointer(true, dx, dy, scroll);
            g_pointerX += dx;
            g_pointerY += dy;
            coop::ClampWheelPointer(g_pointerX, g_pointerY);
            Pick(coop::WheelSlot(g_pointerX, g_pointerY), g_page);
            g_scroll += scroll;
            while (g_scroll >= kScrollNotch) { g_scroll -= kScrollNotch; TurnPage(-1); }
            while (g_scroll <= -kScrollNotch) { g_scroll += kScrollNotch; TurnPage(1); }
        }
        else
        {
            game::HoldControlsForWheel(true);
            // The right stick picks a wedge, which stays picked when the stick is let go.
            if (std::hypot(pad.rx, pad.ry) > kStickPush)
                Pick(coop::WheelSlot(pad.rx * coop::kWheelReach, pad.ry * coop::kWheelReach), g_page);
            if (padPressed & kPadLeftBumper) TurnPage(-1);
            if (padPressed & kPadRightBumper) TurnPage(1);
            if (padPressed & kPadB)
            {
                g_cancelled = true;
                LOG_DEBUG("Voice lines: cancelled with B");
            }
        }
        int phrase = Phrase();
        game::ShowVoiceWheel(!g_cancelled, g_page, g_chosen,
            phrase >= 0 ? coop::kVoicePhrases[phrase].lines[g_chosenLine].text : "",
            g_by == WheelBy::Pad ? "LB / RB to turn" : "scroll to turn", look);
    }
    if (g_wheelOpen && (!held || g_cancelled))
    {
        g_wheelOpen = false;
        float dx, dy;
        int scroll;
        game::WheelPointer(false, dx, dy, scroll);
        game::HoldControlsForWheel(false);
        game::ShowVoiceWheel(false, g_page, g_chosen, "", "", look);
        int phrase = Phrase();
        if (can && phrase >= 0 && !g_cancelled)
            Speak(static_cast<uint8_t>(phrase), now, "", g_chosenLine);
        else
            LOG_DEBUG("Voice lines: wheel closed without saying anything%s",
                g_cancelled ? " (cancelled)" : phrase < 0 ? " (let go in the middle)" : "");
    }
    else if (!g_wheelOpen)
        game::HoldControlsForWheel(false);
    KeepMineMuted(now);
    UpdateAuto(now, me, alive && !g_wheelOpen && !game::InMenuMode() && !game::ChatTyping());
    HearFriends();
}
