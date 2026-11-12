// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

// The voice line wheel: hold a key, pick a phrase, and your character says a matching vanilla NPC line out loud.
// Every line here is in FalloutNV.esm with voice and lip files for all the voices below, and none runs a script.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace coop
{
    struct VoiceLineForm
    {
        uint32_t topic = 0, info = 0;
        const char* text = "";
        const char* file = ""; // the voice file's name in each voice's folder, without "_1.ogg"
    };

    struct VoicePhrase
    {
        const char* name = "";
        VoiceLineForm lines[5];
        uint8_t count = 0;
    };

    // The wheel's slots go all the way round, clockwise from the top; it has two pages of them (phrase = page * slots +
    // slot). After the wheel's phrases come the ones a character says by itself (Say Lines By Themselves).
    constexpr int kVoiceSlots = 10;
    constexpr int kVoicePages = 2;
    constexpr int kWheelPhrases = kVoiceSlots * kVoicePages;
    constexpr uint8_t kGrenadePhrase = 5;
    enum AutoPhrase : uint8_t { kAutoFight = kWheelPhrases, kAutoAttack, kAutoHit, kAutoHurt, kAutoFightOver, kAutoGreet };
    constexpr int kVoicePhraseCount = kWheelPhrases + 6;
    constexpr VoicePhrase kVoicePhrases[kVoicePhraseCount] = {
        {"Hey", {{0xD2, 0x0017529D, "Hey.", "genericadult_hello_0017529d"},
            {0xD2, 0x0017529A, "Hello.", "generic_hello_0017529a"}}, 2},
        {"Help", {{0xDE, 0x0017525B, "Help!", "genericadultcombat_flee_0017525b"},
            {0xEE, 0x001750E9, "Help me out!", "genericadu_normaltocombat_001750e9"}}, 2},
        {"Look Out", {{0xED, 0x00175210, "Look out!", "genericadu_avoidthreat_00175210"},
            {0x11A, 0x001750BD, "Watch out!", "genericadult_observecombat_001750bd"}}, 2},
        {"Over Here", {{0xEE, 0x001750E6, "Over here!", "generic_normaltocombat_001750e6"}}, 1},
        {"Enemy Spotted", {{0xEE, 0x001750E8, "I've got one!", "genericadu_normaltocombat_001750e8"},
            {0xF3, 0x001751DA, "Found you!", "genericadu_alerttocombat_001751da"}}, 2},
        {"Grenade", {{0xCA, 0x001750CF, "Grenade!", "genericadu_playerthrowgren_001750cf"},
            {0xEC, 0x0017528F, "Incoming!", "genericadu_fireexplosive_0017528f"},
            {0xEC, 0x0017528E, "Get down!", "genericadu_fireexplosive_0017528e"},
            {0xCA, 0x001750D0, "Take cover!", "genericadu_playerthrowgren_001750d0"}}, 4},
        {"Retreat", {{0xDE, 0x00175259, "I'm out of here!", "genericadultcombat_flee_00175259"}}, 1},
        {"Taunt", {{0xDC, 0x001751E8, "Had enough?", "genericadultcombat_attack_001751e8"},
            {0xDC, 0x001751E9, "Come on!", "genericadultcombat_attack_001751e9"},
            {0xDC, 0x001751EA, "You like that?", "genericadultcombat_attack_001751ea"},
            {0xF6, 0x0017518B, "Teach you to mess with me.", "genericadu_combattonormal_0017518b"}}, 4},
        {"Thanks", {{0x116, 0x0017511B, "Thanks.", "genericadult_barterexit_0017511b"}}, 1},
        {"Later", {{0xD4, 0x001751C5, "Later.", "genericadult_goodbye_001751c5"},
            {0xD4, 0x0004E5E3, "Bye.", "genericadult_goodbye_0004e5e3"}}, 2},
        // page 2
        {"Careful", {{0xCB, 0x00175137, "Whoa, whoa! Careful!", "genericadu_playerfireweapo_00175137"},
            {0xCB, 0x00175136, "Cut it out, will you?", "genericadu_playerfireweapo_00175136"},
            {0xCC, 0x00175120, "Whoa now. Be careful with that thing.", "genericadu_playerlaymine_00175120"}}, 3},
        {"What Was That?", {{0xF4, 0x00175171, "What was that?", "generic_normaltoalert_00175171"},
            {0xF4, 0x00175173, "Did you hear something?", "genericadu_normaltoalert_00175173"},
            {0x104, 0x00175249, "Who's there?", "generic_alertidle_00175249"},
            {0x104, 0x0017524A, "Anybody there?", "genericadu_alertidle_0017524a"}}, 4},
        {"Where'd They Go?", {{0xF7, 0x0017515B, "Where'd you go?", "generic_combattolost_0017515b"},
            {0xF7, 0x0017515D, "Damn it. Where'd he go?", "genericadu_combattolost_0017515d"},
            {0xF8, 0x0017519E, "Must have got away.", "genericadu_losttonormal_0017519e"}}, 3},
        {"Get 'Em", {{0x1FB51, 0x001751AF, "Flank 'em!", "genericadu_deathresponse_001751af"},
            {0x1FB51, 0x001751AE, "We'll get 'em.", "genericadu_deathresponse_001751ae"},
            {0xF9, 0x00175219, "There you are!", "generic_losttocombat_00175219"}}, 3},
        {"All Clear", {{0xF5, 0x0017527D, "Guess it was nothing.", "generic_alerttonormal_0017527d"},
            {0xF6, 0x00175189, "Phew. I'm glad that's over.", "generic_combattonormal_00175189"},
            {0xF6, 0x0017518C, "That takes care of that.", "genericadu_combattonormal_0017518c"}}, 3},
        {"Man Down", {{0x1FB51, 0x001751AB, "They killed him!", "genericadu_deathresponse_001751ab"},
            {0x1FB51, 0x001751AC, "They killed her!", "genericadu_deathresponse_001751ac"}}, 2},
        {"Thief!", {{0xDF, 0x00175202, "Thief!", "genericadultcombat_steal_00175202"},
            {0xDF, 0x00175200, "That doesn't belong to you!", "genericadultcombat_steal_00175200"},
            {0xDF, 0x00175201, "You can't just take that!", "genericadultcombat_steal_00175201"}}, 3},
        {"No!", {{0xE5, 0x00175229, "No!", "genericadultcombat_murder_00175229"},
            {0xE5, 0x0017522B, "What have you done?", "genericadultcombat_murder_0017522b"},
            {0xE5, 0x0017522A, "Murderer!", "genericadultcombat_murder_0017522a"}}, 3},
        {"Keep Moving", {{0xF2, 0x0017513F, "Keep moving.", "genericadu_guardtrespass_0017513f"},
            {0xF2, 0x00175140, "You'd better get out of here.", "genericadu_guardtrespass_00175140"}}, 2},
        {"Good Business", {{0x116, 0x0017511E, "Pleasure doing business with you.", "genericadult_barterexit_0017511e"},
            {0x116, 0x0017511D, "Another satisfied customer.", "genericadult_barterexit_0017511d"},
            {0x116, 0x0017511C, "Give me a shout if you need anything else.", "genericadult_barterexit_0017511c"}}, 3},
        {"(fight starts)", {{0xEE, 0x001750E7, "Hey!", "genericadu_normaltocombat_001750e7"},
            {0xEE, 0x001750E8, "I've got one!", "genericadu_normaltocombat_001750e8"},
            {0xF3, 0x001751DA, "Found you!", "genericadu_alerttocombat_001751da"},
            {0xF3, 0x001751D8, "I've got you now.", "generic_alerttocombat_001751d8"},
            {0xF3, 0x001751DB, "Aha!", "genericadu_alerttocombat_001751db"}}, 5},
        {"(attacking)", {{0xDC, 0x001751E8, "Had enough?", "genericadultcombat_attack_001751e8"},
            {0xDC, 0x001751E9, "Come on!", "genericadultcombat_attack_001751e9"},
            {0xDC, 0x001751EA, "You like that?", "genericadultcombat_attack_001751ea"}}, 3},
        {"(hit)", {{0xDD, 0x0017523D, "Damn!", "genericadultcombat_hit_0017523d"},
            {0xDD, 0x0017523E, "I'm hit!", "genericadultcombat_hit_0017523e"}}, 2},
        {"(badly hurt)", {{0xDE, 0x0017525B, "Help!", "genericadultcombat_flee_0017525b"},
            {0xEE, 0x001750E9, "Help me out!", "genericadu_normaltocombat_001750e9"},
            {0xDE, 0x0017525A, "This is hopeless!", "genericadultcombat_flee_0017525a"}}, 3},
        {"(fight over)", {{0xF6, 0x0017518A, "Well, that's the end of that.", "genericadu_combattonormal_0017518a"},
            {0xF6, 0x0017518B, "Teach you to mess with me.", "genericadu_combattonormal_0017518b"},
            {0xF6, 0x0017518C, "That takes care of that.", "genericadu_combattonormal_0017518c"},
            {0xF6, 0x00175189, "Phew. I'm glad that's over.", "generic_combattonormal_00175189"}}, 4},
        {"(greeting)", {{0xD2, 0x0017529D, "Hey.", "genericadult_hello_0017529d"},
            {0xD2, 0x0017529A, "Hello.", "generic_hello_0017529a"}}, 2},
    };

    // Generic voices that have every line above (voice type forms in FalloutNV.esm, and their voice folders).
    struct WheelVoice
    {
        uint32_t form;
        const char* folder;
    };
    constexpr WheelVoice kMaleVoices[] = {
        {0x0000002D, "MaleAdult01Default"},
        {0x0002AB62, "MaleAdult01DefaultB"},
        {0x00019FC8, "MaleAdult02"},
        {0x00019FCA, "MaleAdult04"},
        {0x000717E1, "MaleAdult05"},
        {0x0013C8D7, "MaleAdult07"},
        {0x0013C8D8, "MaleAdult08"},
        {0x0013C8D9, "MaleAdult09"},
        {0x00019FD2, "MaleOld01"},
        {0x0013C8DB, "MaleOld02"},
        {0x00061EA7, "MaleGenericGhoul"},
    };
    constexpr WheelVoice kFemaleVoices[] = {
        {0x0000002E, "FemaleAdult01Default"},
        {0x00019FCB, "FemaleAdult02"},
        {0x0013C8CD, "FemaleAdult03"},
        {0x00019FCD, "FemaleAdult04"},
        {0x0013C8CF, "FemaleAdult06"},
        {0x0013C8D0, "FemaleAdult07"},
        {0x0013C8D2, "FemaleAdult09"},
        {0x0013C8D3, "FemaleAdult10"},
        {0x00019FD5, "FemaleOld01"},
        {0x0013C8D5, "FemaleOld02"},
        {0x00061EA8, "FemaleGenericGhoul"},
        {0x00143779, "FemaleGhoul02"},
    };
    // The voice's folder under Sound/Voice/FalloutNV.esm, or null if it isn't one of the wheel's voices.
    inline const char* VoiceFolder(uint32_t voice)
    {
        for (const WheelVoice& v : kMaleVoices)
            if (v.form == voice)
                return v.folder;
        for (const WheelVoice& v : kFemaleVoices)
            if (v.form == voice)
                return v.folder;
        return nullptr;
    }

    inline bool IsWheelVoice(uint32_t voice) { return VoiceFolder(voice) != nullptr; }

    // The MCM's Voice choice: 0 matches the character, then every voice by name, the men's first (1-11) and then the
    // women's (12-23).
    constexpr int kVoiceMatchCharacter = 0;
    constexpr int kVoiceChoices = 1 + static_cast<int>(std::size(kMaleVoices) + std::size(kFemaleVoices));

    inline bool IsFemaleVoice(uint32_t voice)
    {
        for (const WheelVoice& v : kFemaleVoices)
            if (v.form == voice)
                return true;
        return false;
    }

    // Which voice says the lines, and whether the wheel shows Vault Girl (a female voice) or Vault Boy.
    struct VoicePick
    {
        uint32_t voice;
        bool female;
    };

    // own: the character's own voice type; raceMale/raceFemale: its race's default voices (0 when unknown);
    // bodyFemale: whether the character is a woman. A voice the game has no wheel lines for (a mod's own voice) can't
    // be used, but still tells the wheel which voice 1 to fall back on when it's one of the race's two.
    inline VoicePick PickVoice(int choice, uint32_t own, uint32_t raceMale, uint32_t raceFemale, bool bodyFemale)
    {
        constexpr int men = static_cast<int>(std::size(kMaleVoices));
        if (choice >= 1 && choice <= men)
            return {kMaleVoices[choice - 1].form, false};
        if (choice > men && choice < kVoiceChoices)
            return {kFemaleVoices[choice - 1 - men].form, true};
        if (IsWheelVoice(own))
            return {own, IsFemaleVoice(own)};
        bool female = bodyFemale;
        if (own && own == raceFemale)
            female = true;
        else if (own && own == raceMale)
            female = false;
        else
        {
            uint32_t race = bodyFemale ? raceFemale : raceMale;
            if (IsWheelVoice(race))
                return {race, IsFemaleVoice(race)};
        }
        return female ? VoicePick{kFemaleVoices[0].form, true} : VoicePick{kMaleVoices[0].form, false};
    }

    inline bool IsVoiceLine(uint8_t phrase, uint8_t line)
    {
        return phrase < kVoicePhraseCount && line < kVoicePhrases[phrase].count;
    }

    // The pointer is how far the mouse moved since the wheel opened (the game's mouse counts): x right, y up.
    constexpr float kWheelDeadZone = 40;
    constexpr float kWheelReach = 120;

    // The wedges go all the way round like Fallout 76's emote wheel, each the same size, slot 0 centred at the top.
    constexpr float kWedgeDegrees = 360.0f / kVoiceSlots;

    // Which slot the pointer is on, or -1 inside the dead zone (letting go there says nothing).
    inline int WheelSlot(float x, float y)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || x * x + y * y < kWheelDeadZone * kWheelDeadZone)
            return -1;
        float degrees = std::atan2(x, y) * 57.2957795f;
        if (degrees < 0)
            degrees += 360;
        int slot = static_cast<int>(std::floor((degrees + kWedgeDegrees / 2) / kWedgeDegrees));
        return slot % kVoiceSlots;
    }

    // The middle of a slot's wedge, in degrees clockwise from the top.
    inline float WheelSlotDegrees(int slot)
    {
        if (slot < 0 || slot >= kVoiceSlots)
            return 0;
        return slot * kWedgeDegrees;
    }

    // Keeps the pointer within reach, so turning far one way doesn't need turning as far back.
    inline void ClampWheelPointer(float& x, float& y)
    {
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            x = y = 0;
            return;
        }
        float length = std::sqrt(x * x + y * y);
        if (length > kWheelReach)
        {
            x *= kWheelReach / length;
            y *= kWheelReach / length;
        }
    }

    // The game's own Vault Boy for each phrase, by path: shown only when the wheel's own pictures below are missing.
    constexpr const char* kVoiceIcons[kWheelPhrases] = {
        "interface\\companion_wheel\\comp_talk_on.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_in_pain.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_surprised.dds",
        "interface\\companion_wheel\\comp_stepback_on.dds",
        "interface\\icons\\pipboyimages\\perks\\perk_spotting.dds",
        "interface\\icons\\pipboyimages\\perks\\perk_hit_the_deck.dds",
        "interface\\icons\\pipboyimages\\perks\\perk_run_n_gun.dds",
        "interface\\icons\\pipboyimages\\perks\\perk_terrifying_presence.dds",
        "interface\\icons\\stats\\special_charisma.dds",
        "interface\\companion_wheel\\comp_stayfar_on.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
        "interface\\icons\\message icons\\glow_message_vaultboy_neutral.dds",
    };

    // The wheel's own pictures (Data\Textures\interface\nvcoop\voicewheel, shipped with the mod), looked for in order:
    // a female character takes the first Vault Girl one installed, then the Vault Boy ones; a male character the Vault
    // Boy ones; and if none is there, the game's picture above. Look Out and Retreat have no drawing yet, so they show
    // a TEMP card (Later too); dropping vaultboy_/vaultgirl_<phrase>.dds in replaces it. Pop-Up Message Icons'
    // smiling Vault Girl is a last choice for Hey (its other long-haired faces are Vault Boy in costume).
    constexpr const char* kVoiceIconTemp = "interface\\nvcoop\\voicewheel\\temp.dds";
    struct PhraseIcons
    {
        const char* boy[2];
        const char* girl[2];
    };
    constexpr PhraseIcons kPhraseIcons[kWheelPhrases] = {
        {{"interface\\nvcoop\\voicewheel\\vaultboy_hey.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_hey.dds",
                "interface\\icons\\pipboyimages\\itsmejesushchrist\\message icons\\glow_vaultgirl.dds"}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_help.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_help.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_lookout.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_lookout.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_overhere.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_overhere.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_enemyspotted.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_enemyspotted.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_grenade.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_grenade.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_retreat.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_retreat.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_taunt.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_taunt.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_thanks.dds", nullptr},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_thanks.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_later.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_later.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_careful.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_careful.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_whatwasthat.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_whatwasthat.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_wheredtheygo.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_wheredtheygo.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_getem.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_getem.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_allclear.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_allclear.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_mandown.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_mandown.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_thief.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_thief.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_no.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_no.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_keepmoving.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_keepmoving.dds", nullptr}},
        {{"interface\\nvcoop\\voicewheel\\vaultboy_goodbusiness.dds", kVoiceIconTemp},
            {"interface\\nvcoop\\voicewheel\\vaultgirl_goodbusiness.dds", nullptr}},
    };

    // Lines a player may say: one every so often, and a short burst at most, so the wheel can't be spammed.
    constexpr uint32_t kVoiceLineGapMs = 1200;
    constexpr int kVoiceLinesPerWindow = 4;
    constexpr uint32_t kVoiceLineWindowMs = 8000;

    class VoiceLineLimiter
    {
    public:
        // The host checks with half the gap, since lines can arrive bunched up.
        explicit VoiceLineLimiter(uint32_t gapMs = kVoiceLineGapMs) : m_gap(gapMs) {}

        bool Allow(uint32_t now)
        {
            if (m_any && static_cast<int32_t>(now - m_last) < static_cast<int32_t>(m_gap))
                return false;
            int recent = 0;
            for (int i = 0; i < m_filled; ++i)
                if (static_cast<int32_t>(now - m_times[i]) < static_cast<int32_t>(kVoiceLineWindowMs))
                    ++recent;
            if (recent >= kVoiceLinesPerWindow)
                return false;
            m_times[m_next] = now;
            m_next = (m_next + 1) % kVoiceLinesPerWindow;
            if (m_filled < kVoiceLinesPerWindow)
                ++m_filled;
            m_last = now;
            m_any = true;
            return true;
        }

    private:
        uint32_t m_gap;
        uint32_t m_times[kVoiceLinesPerWindow] = {};
        int m_next = 0, m_filled = 0;
        uint32_t m_last = 0;
        bool m_any = false;
    };

    // Say Lines By Themselves: what happened to the character, and which phrase (if any) it says about it.
    enum class AutoEvent : uint8_t { FightStarts, Attacking, Hit, BadlyHurt, FightOver, Greeting, Count };

    struct AutoRule
    {
        uint8_t phrase;
        float chance;    // how often it's said when it happens
        uint32_t gapMs;  // at least this long between two of the same kind
    };
    constexpr AutoRule kAutoRules[static_cast<int>(AutoEvent::Count)] = {
        {kAutoFight, 0.9f, 20000},
        {kAutoAttack, 0.25f, 15000},
        {kAutoHit, 0.4f, 10000},
        {kAutoHurt, 1.0f, 30000},
        {kAutoFightOver, 0.7f, 30000},
        {kAutoGreet, 1.0f, 0}, // its gap is the Greeting Gap setting
    };

    class AutoLines
    {
    public:
        // roll: a random number in [0, 1). gapMs: at least this long after any line said by itself. greetGapMs: between
        // two greetings (0 = never greet). Returns the phrase to say, or -1.
        int Pick(AutoEvent e, uint32_t now, float roll, uint32_t gapMs, uint32_t greetGapMs)
        {
            int k = static_cast<int>(e);
            if (k < 0 || k >= static_cast<int>(AutoEvent::Count))
                return -1;
            const AutoRule& rule = kAutoRules[k];
            if (e == AutoEvent::Greeting && greetGapMs == 0)
                return -1;
            uint32_t own = e == AutoEvent::Greeting ? greetGapMs : rule.gapMs;
            if (m_any && static_cast<int32_t>(now - m_last) < static_cast<int32_t>(gapMs))
                return -1;
            if (m_said[k] && static_cast<int32_t>(now - m_lastOf[k]) < static_cast<int32_t>(own))
                return -1;
            if (!(roll < rule.chance))
                return -1;
            m_any = m_said[k] = true;
            m_last = m_lastOf[k] = now;
            return rule.phrase;
        }

        // Counts as a greeting just said, so the first one waits a whole Greeting Gap after the game starts.
        void Started(uint32_t now)
        {
            int k = static_cast<int>(AutoEvent::Greeting);
            m_said[k] = true;
            m_lastOf[k] = now;
        }

    private:
        bool m_any = false;
        uint32_t m_last = 0;
        bool m_said[static_cast<int>(AutoEvent::Count)] = {};
        uint32_t m_lastOf[static_cast<int>(AutoEvent::Count)] = {};
    };
}
