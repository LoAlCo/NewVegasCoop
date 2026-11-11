// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// The voice line wheel on the HUD, and the mouse that points at its phrases.

#include "game/game.h"
#include "game/internal.h"
#include "game/addresses.h"
#include "coop/voice_lines.h"
#include "log.h"
#include "nvse/GameObjects.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace game
{
    namespace
    {
        // Laid out like Fallout 76's emote wheel, drawn in New Vegas HUD style: the companion wheel's dark disc, our
        // dial (rim with ticks, a ring of wedges all the way round, the middle circle), a highlight per wedge and a
        // Vault Boy or Vault Girl per phrase, with what you'll say written under the wheel. Sizes and places are on the
        // HUD's 960-high scale, measured from the middle of the wheel (which sits a little above the screen's middle).
        constexpr float kDial = 700, kHalf = kDial / 2, kRaise = 40;
        constexpr float kIconRing = (165 + 328) / 2.0f, kIconSize = 150, kFaceSize = 118, kMiddleIcon = 260;
        constexpr float kHighlight = 256;
        constexpr float kIconAlpha = 140, kChosenIconAlpha = 255, kHighlightAlpha = 255, kMatteAlpha = 225;
        constexpr const char* kTitle = "Voice Lines";
        constexpr const char* kPick = "Point at a phrase and let go to say it; let go here to say nothing";
        constexpr const char* kTile = "HUDMainMenu/NVCoopVW";

        void Around(int slot, float r, float& x, float& y)
        {
            float a = coop::WheelSlotDegrees(slot) / 57.2957795f;
            x = r * std::sin(a);
            y = -r * std::cos(a);
        }

        bool g_wheelMade = false;
        bool g_wheelTried = false;
        Script* g_wheelSlot[coop::kVoiceSlots] = {};
        Script* g_wheelFrame = nullptr;

        // A picture is used when its file is there (loose, under Data\Textures); looked for once a session.
        bool Installed(const char* texture)
        {
            static std::string s_textures;
            if (s_textures.empty())
            {
                char exe[MAX_PATH] = {};
                GetModuleFileNameA(nullptr, exe, MAX_PATH);
                s_textures = exe;
                s_textures = s_textures.substr(0, s_textures.find_last_of("\\/") + 1) + "Data\\Textures\\";
            }
            DWORD attributes = GetFileAttributesA((s_textures + texture).c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }

        const char* FirstInstalled(const char* const (&paths)[2])
        {
            for (const char* path : paths)
                if (path && Installed(path))
                    return path;
            return nullptr;
        }

        const char* IconFor(int slot, bool female)
        {
            static const char* s_boy[coop::kVoiceSlots];
            static const char* s_girl[coop::kVoiceSlots];
            static bool s_looked = false;
            if (slot < 0 || slot >= coop::kVoiceSlots)
                slot = 0;
            if (!s_looked)
            {
                s_looked = true;
                for (int i = 0; i < coop::kVoiceSlots; ++i)
                {
                    const char* boy = FirstInstalled(coop::kPhraseIcons[i].boy);
                    const char* girl = FirstInstalled(coop::kPhraseIcons[i].girl);
                    s_boy[i] = boy ? boy : coop::kVoiceIcons[i];
                    s_girl[i] = girl ? girl : s_boy[i];
                    LOG_INFO("Voice lines: %s shows %s (Vault Boy) and %s (Vault Girl)", coop::kVoicePhrases[i].name,
                        s_boy[i], s_girl[i]);
                }
            }
            return female ? s_girl[slot] : s_boy[slot];
        }

        // Head-and-shoulders pictures (the message pop-up faces) are drawn smaller than whole-body poses.
        bool IsFace(const char* icon)
        {
            return std::strstr(icon, "message icons") != nullptr;
        }

        std::string F(float v) { return std::to_string(static_cast<int>(std::lround(v))); }

        // x and y measured from the middle of the wheel.
        std::string Place(float x, float y)
        {
            return "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div><add>" + F(x) + "</add></x>"
                "<y><copy src=\"screen()\" trait=\"height\"/><div>2</div><add>" + F(y - kRaise) + "</add></y>";
        }

        std::string Image(const std::string& name, const std::string& file, float x, float y, float size, int depth,
            bool tinted)
        {
            return "<image name=\"NVCoopVW" + name + "\"><visible>0</visible>"
                "<filename>" + file + "</filename>" + Place(x, y) + "<depth>" + std::to_string(depth) + "</depth>"
                "<width>" + F(size) + "</width><height>" + F(size) + "</height><zoom>-1</zoom>" +
                (tinted ? "<systemcolor>1</systemcolor><brightness>-1</brightness><red>255</red><green>255</green>"
                    "<blue>255</blue>" : "") + "</image>";
        }

        std::string Text(const std::string& name, const std::string& text, float y, int font, float wrap, int depth)
        {
            return "<text name=\"NVCoopVW" + name + "\"><visible>0</visible>" + Place(0, y) + "<depth>" +
                std::to_string(depth) + "</depth><font>" + std::to_string(font) + "</font><justify>&center;</justify>"
                "<wrapwidth>" + F(wrap) + "</wrapwidth><systemcolor>1</systemcolor><brightness>-1</brightness>"
                "<red>255</red><green>255</green><blue>255</blue><string>" + text + "</string></text>";
        }

        // The script lines that colour one tile: the HUD colour (brightness 255) or the chosen one.
        std::string Tint(const std::string& tile)
        {
            std::string path = std::string(kTile) + tile;
            return "  SetUIFloat \"" + path + "/brightness\" fBright\n"
                "  SetUIFloat \"" + path + "/red\" fRed\n"
                "  SetUIFloat \"" + path + "/green\" fGreen\n"
                "  SetUIFloat \"" + path + "/blue\" fBlue\n";
        }

        bool MakeWheel()
        {
            if (g_wheelMade)
                return true;
            if (!g_chatHud || !Player() || !g_scripts)
                return false;
            if (!g_wheelTried)
            {
                g_wheelTried = true;
                for (int i = 0; i < coop::kVoiceSlots; ++i)
                {
                    std::string n = std::to_string(i);
                    std::string icon = std::string(kTile) + "Icon" + n, face = std::string(kTile) + "Face" + n,
                        wedge = std::string(kTile) + "Wedge" + n;
                    g_wheelSlot[i] = Compile("voice wheel slot",
                        "float fAlpha\n"
                        "int iChosen\n"
                        "string_var sIcon\n"
                        "float fBright\n"
                        "float fRed\n"
                        "float fGreen\n"
                        "float fBlue\n"
                        "int iShow\n"
                        "int iFace\n"
                        "int iBody\n"
                        "int iHead\n"
                        "Begin Function {fAlpha, iChosen, fBright, fRed, fGreen, fBlue, iShow, sIcon, iFace}\n"
                        "  set iHead to iShow * iFace\n"
                        "  set iBody to iShow - iHead\n"
                        "  SetUIStringEx \"" + icon + "/filename\" \"%z\" sIcon\n"
                        "  SetUIStringEx \"" + face + "/filename\" \"%z\" sIcon\n"
                        "  SetUIFloat \"" + icon + "/alpha\" fAlpha\n"
                        "  SetUIFloat \"" + face + "/alpha\" fAlpha\n" + Tint("Icon" + n) + Tint("Face" + n) +
                        "  SetUIFloat \"" + icon + "/visible\" iBody\n"
                        "  SetUIFloat \"" + face + "/visible\" iHead\n" + Tint("Wedge" + n) +
                        "  SetUIFloat \"" + wedge + "/visible\" iChosen\n"
                        "End\n");
                }
                std::string frame;
                for (const char* t : {"Dial", "Name", "Line"})
                    frame += Tint(t) + "  SetUIFloat \"" + std::string(kTile) + t + "/visible\" iShow\n";
                g_wheelFrame = Compile("voice wheel frame",
                    "int iShow\n"
                    "string_var sName\n"
                    "string_var sLine\n"
                    "string_var sIcon\n"
                    "int iIcon\n"
                    "float fBright\n"
                    "float fRed\n"
                    "float fGreen\n"
                    "float fBlue\n"
                    "Begin Function {iShow, sName, sLine, sIcon, iIcon, fBright, fRed, fGreen, fBlue}\n"
                    "  SetUIStringEx \"" + std::string(kTile) + "Name/string\" \"%z\" sName\n"
                    "  SetUIStringEx \"" + std::string(kTile) + "Line/string\" \"%z\" sLine\n"
                    "  SetUIStringEx \"" + std::string(kTile) + "MiddleIcon/filename\" \"%z\" sIcon\n"
                    "  SetUIFloat \"" + std::string(kTile) + "Matte/visible\" iShow\n" + frame + Tint("MiddleIcon") +
                    "  SetUIFloat \"" + std::string(kTile) + "MiddleIcon/visible\" iIcon\n"
                    "End\n");
            }
            for (int i = 0; i < coop::kVoiceSlots; ++i)
                if (!g_wheelSlot[i])
                    return false;
            if (!g_wheelFrame)
                return false;
            NVSEArrayVarInterface::Element result;
            auto inject = [&](const std::string& xml) {
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            };
            auto withAlpha = [](std::string xml, float alpha) {
                return xml.substr(0, xml.size() - 8) + "<alpha>" + F(alpha) + "</alpha></image>";
            };
            constexpr float kMatteMore = 60;
            inject(withAlpha(Image("Matte", "interface\\companion_wheel\\hud_comp_bkg.dds", -kHalf - kMatteMore / 2,
                -kHalf - kMatteMore / 2, kDial + kMatteMore, 20, false), kMatteAlpha));
            inject(Image("Dial", "interface\\nvcoop\\voicewheel\\dial.dds", -kHalf, -kHalf, kDial, 21, true));
            for (int i = 0; i < coop::kVoiceSlots; ++i)
            {
                std::string n = std::to_string(i);
                float x, y;
                Around(i, kIconRing, x, y);
                inject(withAlpha(Image("Wedge" + n, "interface\\nvcoop\\voicewheel\\highlight" + n + ".dds",
                    x - kHighlight / 2, y - kHighlight / 2, kHighlight, 22, true), kHighlightAlpha));
                // One spot sized for a whole-body pose and one for a face; the picture shown picks which.
                inject(Image("Icon" + n, coop::kVoiceIcons[i], x - kIconSize / 2, y - kIconSize / 2, kIconSize, 23,
                    true));
                inject(Image("Face" + n, coop::kVoiceIcons[i], x - kFaceSize / 2, y - kFaceSize / 2, kFaceSize, 23,
                    true));
            }
            inject(Image("MiddleIcon", coop::kVoiceIcons[0], -kMiddleIcon / 2, -kMiddleIcon / 2, kMiddleIcon, 23,
                true));
            inject(Text("Name", kTitle, kHalf + 18, 1, 600, 24));
            inject(Text("Line", kPick, kHalf + 60, 3, 700, 24));
            g_wheelMade = true;
            LOG_INFO("Voice lines: wheel added to the HUD (full ring, %d phrases)", coop::kVoiceSlots);
            return true;
        }

        bool g_pointing = false;
        float g_heading = 0, g_pitch = 0;

        // The game asks one small function how far the mouse moved this frame (axis 1 = x, 2 = y, 3 = wheel); the
        // numbers sit in its input object at +0x1B24/+0x1B28/+0x1B2C. While the voice wheel is open we answer "not at
        // all" for x and y, so the view can't turn, and read the movement ourselves.
        constexpr uint32_t kMouseX = 0x1B24, kMouseY = 0x1B28, kMouseWheel = 0x1B2C;
        std::atomic<bool> g_blockLook{false};
        uintptr_t g_mouseBefore = 0;
        bool g_mouseHooked = false, g_mouseTried = false;

        int __fastcall MouseMovementHook(void* input, void*, int axis)
        {
            if (g_blockLook && (axis == 1 || axis == 2))
                return 0;
            if (g_mouseBefore)
                return reinterpret_cast<int(__thiscall*)(void*, int)>(g_mouseBefore)(input, axis);
            switch (axis)
            {
            case 1: return At<int>(input, kMouseX);
            case 2: return At<int>(input, kMouseY);
            case 3: return At<int>(input, kMouseWheel);
            default: return 0;
            }
        }

        // Puts a jump to our own version at the start of the game's function. If another plugin got there first (a
        // jump already), ours passes on to theirs.
        bool HookMouseMovement()
        {
            if (g_mouseTried)
                return g_mouseHooked;
            g_mouseTried = true;
            auto* at = reinterpret_cast<uint8_t*>(addr::kMouseMovement);
            if (at[0] == 0xE9)
            {
                int32_t rel;
                std::memcpy(&rel, at + 1, 4);
                g_mouseBefore = addr::kMouseMovement + 5 + rel;
            }
            DWORD old;
            if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old))
            {
                LOG_WARN("Voice lines: couldn't hold the view still (the mouse function can't be changed)");
                return false;
            }
            int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&MouseMovementHook) -
                (addr::kMouseMovement + 5));
            at[0] = 0xE9;
            std::memcpy(at + 1, &rel, 4);
            VirtualProtect(at, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, 5);
            g_mouseHooked = true;
            LOG_INFO("Voice lines: the view holds still while the wheel is open%s", g_mouseBefore ?
                " (passing on to another plugin's mouse hook)" : "");
            return true;
        }
    }

    void ShowVoiceWheel(bool show, int chosen, const char* line, const VoiceWheelLook& look)
    {
        static bool s_shown = false;
        static int s_chosen = -2;
        static std::string s_line;
        static VoiceWheelLook s_look;
        if (chosen < 0 || chosen >= coop::kVoiceSlots)
            chosen = -1;
        std::string said = chosen >= 0 && line && *line ? std::string("\"") + line + "\"" : std::string(kPick);
        bool lookChanged = look.hudColor != s_look.hudColor || look.red != s_look.red || look.green != s_look.green ||
            look.blue != s_look.blue || look.female != s_look.female;
        if (show == s_shown && (!show || (chosen == s_chosen && said == s_line && !lookChanged)))
            return;
        if (!MakeWheel())
            return;
        float bright = look.hudColor ? 255.0f : -1.0f;
        for (int i = 0; i < coop::kVoiceSlots; ++i)
        {
            const char* icon = IconFor(i, look.female);
            g_scripts->CallFunction(g_wheelSlot[i], Player(), nullptr, nullptr, 9,
                FloatArg(i == chosen ? kChosenIconAlpha : kIconAlpha), static_cast<uint32_t>(show && i == chosen ? 1 : 0),
                FloatArg(bright), FloatArg(look.red), FloatArg(look.green), FloatArg(look.blue),
                static_cast<uint32_t>(show ? 1 : 0), icon, static_cast<uint32_t>(IsFace(icon) ? 1 : 0));
        }
        g_scripts->CallFunction(g_wheelFrame, Player(), nullptr, nullptr, 9, static_cast<uint32_t>(show ? 1 : 0),
            chosen >= 0 ? coop::kVoicePhrases[chosen].name : kTitle, said.c_str(),
            IconFor(chosen >= 0 ? chosen : 0, look.female), static_cast<uint32_t>(show && chosen >= 0 ? 1 : 0),
            FloatArg(bright), FloatArg(look.red), FloatArg(look.green), FloatArg(look.blue));
        s_shown = show;
        s_chosen = chosen;
        s_line = said;
        s_look = look;
    }

    bool WheelPointer(bool open, float& dx, float& dy)
    {
        dx = dy = 0;
        TESObjectREFR* me = Player();
        auto* input = *reinterpret_cast<uint8_t**>(addr::kInputGlobals);
        if (open)
            HookMouseMovement();
        g_blockLook = open && g_mouseHooked;
        if (!open || !me || !input)
        {
            g_pointing = false;
            return false;
        }
        // This frame's mouse movement (y grows downwards), taken from the game so it can't turn the view either.
        int mx = At<int>(input, kMouseX), my = At<int>(input, kMouseY);
        *reinterpret_cast<int*>(input + kMouseX) = 0;
        *reinterpret_cast<int*>(input + kMouseY) = 0;
        if (!g_pointing)
        {
            g_pointing = true;
            g_heading = me->rotZ;
            g_pitch = me->rotX;
            return true;
        }
        dx = static_cast<float>(mx);
        dy = static_cast<float>(-my);
        // In case anything still turned it, put the view back where it was when the wheel opened.
        if (me->rotZ != g_heading || me->rotX != g_pitch)
        {
            me->rotZ = g_heading;
            SetPitch(me, g_pitch);
        }
        return true;
    }
}
