// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// Too Many Emotes (TooManyEmotes.esp): what the local player's emote is, read from the mod's own quest (TMEWheel), and its
// song played from a friend's character. Nothing here runs unless the mod is loaded: the scripts name its quest and
// global, so they're only compiled once the game's plugin list has it.

#include "game/internal.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <Xinput.h>

namespace game
{
    namespace
    {
        // A gamepad: read straight from XInput (the game's own copy), since xNVSE's IsControlPressed only sees the
        // keyboard and mouse. The game's pad bindings say which button does what.
        struct PadButton
        {
            uint8_t code;
            uint16_t mask;
            const char* name;
        };
        constexpr PadButton kPadButtons[] = {
            {1, XINPUT_GAMEPAD_DPAD_UP, "D-pad Up"}, {2, XINPUT_GAMEPAD_DPAD_DOWN, "D-pad Down"},
            {4, XINPUT_GAMEPAD_DPAD_RIGHT, "D-pad Right"}, {5, XINPUT_GAMEPAD_DPAD_LEFT, "D-pad Left"},
            {6, XINPUT_GAMEPAD_START, "Start"}, {7, XINPUT_GAMEPAD_BACK, "Back"},
            {8, XINPUT_GAMEPAD_LEFT_THUMB, "LS"}, {9, XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
            {10, XINPUT_GAMEPAD_A, "A"}, {11, XINPUT_GAMEPAD_B, "B"}, {12, XINPUT_GAMEPAD_X, "X"},
            {13, XINPUT_GAMEPAD_Y, "Y"}, {14, XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
            {15, XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
        };

        bool ReadPad(XINPUT_GAMEPAD& out)
        {
            using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
            static GetStateFn s_get = nullptr;
            static bool s_tried = false;
            if (!s_tried)
            {
                s_tried = true;
                HMODULE xinput = GetModuleHandleA("xinput1_3.dll");
                if (!xinput)
                    xinput = LoadLibraryA("xinput1_3.dll");
                if (!xinput)
                    xinput = LoadLibraryA("xinput9_1_0.dll");
                if (xinput)
                    s_get = reinterpret_cast<GetStateFn>(GetProcAddress(xinput, "XInputGetState"));
                if (!s_get)
                    LOG_WARN("Emotes: no XInput, so a gamepad can't stop a dance you joined (moving with keys can)");
            }
            XINPUT_STATE state{};
            if (!s_get || s_get(0, &state) != ERROR_SUCCESS)
                return false;
            out = state.Gamepad;
            return true;
        }

        const PadButton* PadButtonFor(int control)
        {
            auto* globals = *reinterpret_cast<const uint8_t* const*>(addr::kInputGlobals);
            if (!globals || control < 0 || control >= addr::kControlBinds)
                return nullptr;
            uint8_t code = globals[addr::kPadBinds + control];
            for (const PadButton& b : kPadButtons)
                if (b.code == code)
                    return &b;
            return nullptr;
        }

        // the player activating a friend's character, by whatever key or button (a pad's, or one a remapping mod
        // such as Dynamic Controller Mod presses for them): the game runs its activation either way
        TESObjectREFR* g_activated = nullptr;
        DWORD g_activatedAt = 0;

        void OnActivateStandIn(TESObjectREFR*, void* parameters)
        {
            auto** args = static_cast<void**>(parameters);
            auto* ref = args ? static_cast<TESObjectREFR*>(args[0]) : nullptr;
            auto* who = args ? static_cast<TESObjectREFR*>(args[1]) : nullptr;
            if (ref && who && who == Player() && IsStandIn(ref))
            {
                g_activated = ref;
                g_activatedAt = GetTickCount();
            }
        }

        bool g_emotesTried = false;
        Script* g_emoteNow = nullptr;
        Script* g_emoteSongPlay = nullptr;
        Script* g_emoteSongPlaying = nullptr;
        Script* g_emoteSongStop = nullptr;
        Script* g_emoteNoDance = nullptr;
        Script* g_emoteCamera = nullptr;

        bool ModLoaded(const char* file)
        {
            for (const std::string& mod : LoadedMods())
                if (_stricmp(mod.c_str(), file) == 0)
                    return true;
            return false;
        }

        TESForm* SongForm(uint32_t song)
        {
            TESForm* form = song ? LookupForm(song) : nullptr;
            return form && form->typeID == kFormType_TESSound ? form : nullptr;
        }
    }

    bool EmotesReady()
    {
        if (g_emotesTried)
            return g_emoteNow != nullptr;
        if (!g_compiled || !g_scripts || !g_arrays)
            return false;
        g_emotesTried = true;
        if (!ModLoaded("TooManyEmotes.esp"))
            return false;
        // The emote now: TMEWheel.wfIdle is the idle the mod made for this part (intro, then loop), and wfMusic says
        // which song it started (1 the intro's, 2 the loop's; 0 when its music is off).
        g_emoteNow = Compile("Too Many Emotes: the emote now",
            "int iStage\n"
            "int iMusic\n"
            "ref rIdle\n"
            "ref rSong\n"
            "array_var aNow\n"
            "Begin Function {}\n"
            "  let aNow := ar_Construct \"array\"\n"
            "  set iStage to TMEWheel.wfStage\n"
            "  if iStage\n"
            "    set rIdle to TMEWheel.wfIdle\n"
            "    set iMusic to TMEWheel.wfMusic\n"
            "    if iMusic == 1\n"
            "      set rSong to TMEWheel.wfSnd\n"
            "    elseif iMusic == 2\n"
            "      set rSong to TMEWheel.wfSndL\n"
            "    endif\n"
            "    ar_Append aNow rIdle\n"
            "    ar_Append aNow rSong\n"
            "  endif\n"
            "  SetFunctionValue aNow\n"
            "End\n");
        // Songs follow this game's own Too Many Emotes music switch too.
        g_emoteSongPlay = Compile("Too Many Emotes: play a song",
            "ref rSong\n"
            "Begin Function {rSong}\n"
            "  if TMEMusic\n"
            "    PlaySound3D rSong\n"
            "  endif\n"
            "End\n");
        g_emoteSongPlaying = Compile("Too Many Emotes: song playing",
            "ref rSong\n"
            "ref rFrom\n"
            "int iOn\n"
            "Begin Function {rSong, rFrom}\n"
            "  set iOn to IsSoundPlaying rSong rFrom\n"
            "  SetFunctionValue iOn\n"
            "End\n");
        g_emoteSongStop = Compile("Too Many Emotes: stop a song",
            "ref rSong\n"
            "ref rFrom\n"
            "Begin Function {rSong, rFrom}\n"
            "  StopSound rSong rFrom\n"
            "End\n");
        // The mod makes everyone near you dance along, friends' characters too, but they aren't dancing in their own
        // game. This takes one out of the mod's list of dancers (TMEWheel.wfNPC), so it stops making them dance.
        // (A script made while playing can't name another script's array as TMEWheel.wfNPC: "Invalid operands for
        // operator :=". GetArrayVariable reads it by name.)
        g_emoteNoDance = Compile("Too Many Emotes: not a dancer",
            "ref rWho\n"
            "ref rIn\n"
            "int i\n"
            "int n\n"
            "array_var aIn\n"
            "Begin Function {rWho}\n"
            "  let aIn := GetArrayVariable \"wfNPC\" TMEWheel\n"
            "  let n := ar_Size aIn\n"
            "  while i < n\n"
            "    let rIn := aIn[i]\n"
            "    if eval (rIn == rWho)\n"
            "      ar_Erase aIn i\n"
            "      SetFunctionValue 1\n"
            "      return\n"
            "    endif\n"
            "    let i += 1\n"
            "  loop\n"
            "End\n");
        // Joining a friend's emote: third person while dancing, as the mod does for its own (when its camera option
        // is on; called only from first person). Called with 1, back to first person.
        g_emoteCamera = Compile("Too Many Emotes: emote camera",
            "int iBack\n"
            "int iMoved\n"
            "Begin Function {iBack}\n"
            "  if iBack\n"
            "    ToggleFirstPerson 1\n"
            "  elseif TMECamera\n"
            "    ToggleFirstPerson 0\n"
            "    set iMoved to 1\n"
            "  endif\n"
            "  SetFunctionValue iMoved\n"
            "End\n");
        if (!g_events || !g_events->SetNativeEventHandler("OnActivate", OnActivateStandIn))
            LOG_WARN("Emotes: no OnActivate event, so only the keyboard's activate key joins a friend's emote");
        if (!g_emoteNow)
            LOG_ERROR("Emotes: Too Many Emotes is loaded but its emotes can't be read, so they won't show for friends");
        else
            LOG_INFO("Emotes: Too Many Emotes is loaded; your emotes and their songs show for friends%s",
                g_emoteSongPlay && g_emoteSongPlaying && g_emoteSongStop ? "" : " (songs can't be played here)");
        return g_emoteNow != nullptr;
    }

    MyEmote ReadMyEmote()
    {
        MyEmote out;
        TESObjectREFR* me = Player();
        NVSEArrayVarInterface::Element result;
        if (!EmotesReady() || !me || !g_scripts->CallFunction(g_emoteNow, me, nullptr, &result, 0))
            return out;
        auto* array = result.GetArray();
        uint32_t size = array ? g_arrays->GetArraySize(array) : 0;
        if (size != 2)
            return out;
        NVSEArrayVarInterface::Element elements[2];
        if (!g_arrays->GetElements(array, elements, nullptr))
            return out;
        TESForm* idle = elements[0].GetTESForm();
        if (!idle || idle->typeID != kFormType_TESIdleForm)
            return out;
        const char* path = static_cast<TESIdleForm*>(idle)->anim.nifPath.m_data;
        if (!path || !*path)
            return out;
        out.path = path;
        TESForm* song = elements[1].GetTESForm();
        out.song = song && song->typeID == kFormType_TESSound ? song->refID : 0;
        return out;
    }

    void PlayEmoteSong(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        if (EmotesReady() && g_emoteSongPlay && from && form)
            g_scripts->CallFunctionAlt(g_emoteSongPlay, from, 1, form);
    }

    bool EmoteSongPlaying(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        NVSEArrayVarInterface::Element result;
        if (!EmotesReady() || !g_emoteSongPlaying || !from || !form ||
            !g_scripts->CallFunction(g_emoteSongPlaying, from, nullptr, &result, 2, form, from))
            return true;
        return result.GetNumber() != 0;
    }

    void StopEmoteSong(TESObjectREFR* from, uint32_t song)
    {
        TESForm* form = SongForm(song);
        if (EmotesReady() && g_emoteSongStop && from && form)
            g_scripts->CallFunctionAlt(g_emoteSongStop, from, 2, form, from);
    }

    bool UsingGamepad()
    {
        return reinterpret_cast<bool(__cdecl*)()>(addr::kIsGamepad)();
    }

    std::string PadButtonName(int control)
    {
        const PadButton* b = PadButtonFor(control);
        return b ? b->name : std::string();
    }

    bool PadButtonPressed(int control)
    {
        const PadButton* b = PadButtonFor(control);
        XINPUT_GAMEPAD pad{};
        return b && ReadPad(pad) && (pad.wButtons & b->mask);
    }

    bool PadMoving()
    {
        constexpr float kStick = 0.3f * 32767, kTrigger = 0.5f * 255;
        XINPUT_GAMEPAD pad{};
        if (!ReadPad(pad))
            return false;
        // the left stick walks; the triggers aim and attack
        if (std::fabs(static_cast<float>(pad.sThumbLX)) > kStick || std::fabs(static_cast<float>(pad.sThumbLY)) > kStick ||
            pad.bLeftTrigger > kTrigger || pad.bRightTrigger > kTrigger)
            return true;
        for (int control : {layout::kControlAttack, layout::kControlAimBlock, layout::kControlReadyItem,
            layout::kControlJump})
            if (const PadButton* b = PadButtonFor(control); b && (pad.wButtons & b->mask))
                return true;
        return false;
    }

    TESObjectREFR* TakeActivatedStandIn()
    {
        // only a fresh one: one left from a moment we weren't looking for it doesn't count later
        TESObjectREFR* ref = GetTickCount() - g_activatedAt < 300 ? g_activated : nullptr;
        g_activated = nullptr;
        return ref;
    }

    bool EmoteCameraOut()
    {
        NVSEArrayVarInterface::Element result;
        TESObjectREFR* me = Player();
        if (!EmotesReady() || !g_emoteCamera || !me || static_cast<PlayerCharacter*>(me)->IsThirdPerson() ||
            !g_scripts->CallFunction(g_emoteCamera, me, nullptr, &result, 1, static_cast<uint32_t>(0)))
            return false;
        return result.GetNumber() != 0;
    }

    void EmoteCameraBack()
    {
        TESObjectREFR* me = Player();
        if (EmotesReady() && g_emoteCamera && me)
            g_scripts->CallFunctionAlt(g_emoteCamera, me, 1, static_cast<uint32_t>(1));
    }

    bool LeaveEmoteDance(TESObjectREFR* who)
    {
        NVSEArrayVarInterface::Element result;
        TESObjectREFR* me = Player();
        if (!EmotesReady() || !g_emoteNoDance || !me || !who ||
            !g_scripts->CallFunction(g_emoteNoDance, me, nullptr, &result, 1, who))
            return false;
        return result.GetNumber() != 0;
    }
}
