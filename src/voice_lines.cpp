// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// The voice line wheel: hold the key, turn the view towards a phrase, let go, and your character says it in an NPC
// voice. Friends hear it from your character, with lip sync, through the game's own speech.

#include "nvse/PluginAPI.h"
#include "config.h"
#include "coop/coop.h"
#include "coop/voice_lines.h"
#include "game/game.h"
#include "game/standins.h"
#include "log.h"
#include "plugin.h"

#include <random>

namespace
{
    bool g_wheelOpen = false;
    float g_pointerX = 0, g_pointerY = 0;
    int g_chosen = 0;
    coop::VoiceLineLimiter g_myLimit;
    std::minstd_rand g_random{GetTickCount()};

    const char* Text(uint8_t phrase, uint8_t line)
    {
        return coop::IsVoiceLine(phrase, line) ? coop::kVoicePhrases[phrase].lines[line].text : "?";
    }

    bool Say(TESObjectREFR* who, uint8_t phrase, uint8_t line, uint32_t voice, game::Sound& sound)
    {
        const coop::VoiceLineForm& form = coop::kVoicePhrases[phrase].lines[line];
        return game::SayInVoice(who, voice, form.topic, form.info, sound);
    }

    void SayMine(int slot, uint32_t now)
    {
        if (slot < 0 || slot >= coop::kVoiceSlots)
            return;
        if (!g_myLimit.Allow(now))
        {
            LOG_DEBUG("Voice lines: too soon after the last one; \"%s\" not said", coop::kVoicePhrases[slot].name);
            return;
        }
        auto phrase = static_cast<uint8_t>(slot);
        auto line = static_cast<uint8_t>(g_random() % coop::kVoicePhrases[slot].count);
        net::PlayerLooks looks;
        bool female = game::ReadMyLooks(looks) && looks.female;
        uint32_t voice = coop::VoiceFor(female, g_config.voiceLineVoice);
        game::Sound sound;
        bool said = Say(game::Player(), phrase, line, voice, sound);
        bool sent = g_coop.SendVoiceLine(phrase, line, voice);
        if (said)
            LOG_INFO("Voice lines: you say \"%s\" (%s, voice %08X, sound %08X%s)", Text(phrase, line),
                coop::kVoicePhrases[slot].name, voice, sound.id, sent ? ", sent to friends" : "");
        else
            LOG_WARN("Voice lines: your character couldn't say \"%s\" (voice %08X)%s", Text(phrase, line), voice,
                sent ? "; friends still hear it" : "");
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
}

void UpdateVoiceLines(uint32_t now)
{
    TESObjectREFR* me = game::Player();
    bool can = g_config.voiceLines && g_worldLoaded && me && !g_downed && !game::IsDead(me) && !game::ChatTyping() &&
        !game::InMenuMode();
    bool held = can && game::IsKeyPressed(g_config.voiceLineKey);
    game::VoiceWheelLook look;
    look.hudColor = g_config.voiceLineHudColor;
    look.red = static_cast<float>(g_config.voiceLineRed);
    look.green = static_cast<float>(g_config.voiceLineGreen);
    look.blue = static_cast<float>(g_config.voiceLineBlue);
    if (held)
    {
        if (!g_wheelOpen)
        {
            g_wheelOpen = true;
            g_pointerX = g_pointerY = 0;
            g_chosen = 0;
            LOG_DEBUG("Voice lines: wheel open");
        }
        float dx = 0, dy = 0;
        game::WheelPointer(true, dx, dy);
        g_pointerX += dx;
        g_pointerY += dy;
        coop::ClampWheelPointer(g_pointerX, g_pointerY);
        g_chosen = coop::WheelSlot(g_pointerX, g_pointerY);
        game::ShowVoiceWheel(true, g_chosen, look);
    }
    else if (g_wheelOpen)
    {
        g_wheelOpen = false;
        float dx, dy;
        game::WheelPointer(false, dx, dy);
        game::ShowVoiceWheel(false, g_chosen, look);
        if (can)
            SayMine(g_chosen, now);
        else
            LOG_DEBUG("Voice lines: wheel closed without saying anything");
    }
    HearFriends();
}
