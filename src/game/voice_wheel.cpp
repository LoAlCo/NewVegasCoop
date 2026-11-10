// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// The voice line wheel on the HUD, and the view turning that points at its phrases.

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
        constexpr float kRadius = 175;
        constexpr float kSlotAlpha = 150, kChosenAlpha = 255;
        constexpr float kBackAlpha = 120;
        constexpr float kLineWidth = 190;

        bool g_wheelMade = false;
        bool g_wheelTried = false;
        Script* g_wheelSlot[coop::kVoiceSlots] = {};
        Script* g_wheelBack = nullptr;

        std::string Slot(int i) { return "HUDMainMenu/NVCoopWheel" + std::to_string(i); }

        void Place(int slot, float& x, float& y)
        {
            coop::WheelSlotPlace(slot, x, y);
            x *= kRadius * 1.25f;
            y *= kRadius;
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
                    std::string path = Slot(i);
                    g_wheelSlot[i] = Compile("voice wheel slot",
                        "string_var sText\n"
                        "float fAlpha\n"
                        "float fBright\n"
                        "float fRed\n"
                        "float fGreen\n"
                        "float fBlue\n"
                        "int iShow\n"
                        "Begin Function {sText, fAlpha, fBright, fRed, fGreen, fBlue, iShow}\n"
                        "  SetUIStringEx \"" + path + "/string\" \"%z\" sText\n"
                        "  SetUIFloat \"" + path + "/alpha\" fAlpha\n"
                        "  SetUIFloat \"" + path + "/brightness\" fBright\n"
                        "  SetUIFloat \"" + path + "/red\" fRed\n"
                        "  SetUIFloat \"" + path + "/green\" fGreen\n"
                        "  SetUIFloat \"" + path + "/blue\" fBlue\n"
                        "  SetUIFloat \"" + path + "/visible\" iShow\n"
                        "End\n");
                }
                g_wheelBack = Compile("voice wheel back",
                    "int iShow\n"
                    "float fX\n"
                    "float fY\n"
                    "float fBright\n"
                    "float fRed\n"
                    "float fGreen\n"
                    "float fBlue\n"
                    "Begin Function {iShow, fX, fY, fBright, fRed, fGreen, fBlue}\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelBack/visible\" iShow\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/user0\" fX\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/user1\" fY\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/brightness\" fBright\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/red\" fRed\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/green\" fGreen\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/blue\" fBlue\n"
                    "  SetUIFloat \"HUDMainMenu/NVCoopWheelLine/visible\" iShow\n"
                    "End\n");
            }
            for (Script* s : g_wheelSlot)
                if (!s)
                    return false;
            if (!g_wheelBack)
                return false;
            NVSEArrayVarInterface::Element result;
            auto inject = [&](const std::string& xml) {
                g_scripts->CallFunction(g_chatHud, Player(), nullptr, &result, 1, xml.c_str());
            };
            auto f = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            const float backW = kRadius * 2.5f + 260, backH = kRadius * 2 + 110;
            inject("<image name=\"NVCoopWheelBack\"><visible>0</visible>"
                "<filename>interface\\shared\\solid_black.dds</filename>"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div><sub>" + f(backW / 2) + "</sub></x>"
                "<y><copy src=\"screen()\" trait=\"height\"/><div>2</div><sub>" + f(backH / 2) + "</sub></y>"
                "<depth>20</depth><width>" + f(backW) + "</width><height>" + f(backH) + "</height><zoom>-1</zoom>"
                "<alpha>" + f(kBackAlpha) + "</alpha></image>");
            inject("<image name=\"NVCoopWheelLine\"><visible>0</visible>"
                "<filename>interface\\shared\\line\\glow_tip_line_horizontal.dds</filename>"
                "<user0>0</user0><user1>0</user1>"
                "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div><add src=\"me()\" trait=\"user0\"/><sub>" +
                f(kLineWidth / 2) + "</sub></x>"
                "<y><copy src=\"screen()\" trait=\"height\"/><div>2</div><add src=\"me()\" trait=\"user1\"/><add>16</add></y>"
                "<depth>22</depth><width>" + f(kLineWidth) + "</width><height>8</height><zoom>-1</zoom>"
                "<brightness>-1</brightness><red>255</red><green>255</green><blue>255</blue></image>");
            for (int i = 0; i < coop::kVoiceSlots; ++i)
            {
                float x, y;
                Place(i, x, y);
                inject("<text name=\"NVCoopWheel" + std::to_string(i) + "\"><visible>0</visible>"
                    "<x><copy src=\"screen()\" trait=\"width\"/><div>2</div><add>" + f(x) + "</add></x>"
                    "<y><copy src=\"screen()\" trait=\"height\"/><div>2</div><add>" + f(y - 14) + "</add></y>"
                    "<depth>23</depth><font>" + std::string(i == 0 ? "1" : "2") + "</font><justify>&center;</justify>"
                    "<wrapwidth>320</wrapwidth><systemcolor>1</systemcolor><brightness>-1</brightness>"
                    "<red>255</red><green>255</green><blue>255</blue><string> </string></text>");
            }
            g_wheelMade = true;
            LOG_INFO("Voice lines: wheel added to the HUD");
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

    void ShowVoiceWheel(bool show, int chosen, const VoiceWheelLook& look)
    {
        static bool s_shown = false;
        static int s_chosen = -1;
        static VoiceWheelLook s_look;
        bool lookChanged = look.hudColor != s_look.hudColor || look.red != s_look.red || look.green != s_look.green ||
            look.blue != s_look.blue;
        if (show == s_shown && (!show || (chosen == s_chosen && !lookChanged)))
            return;
        if (!MakeWheel())
            return;
        float bright = look.hudColor ? 255.0f : -1.0f;
        for (int i = 0; i < coop::kVoiceSlots; ++i)
        {
            std::string text = coop::kVoicePhrases[i].name;
            if (i == chosen)
                text = "> " + text + " <";
            g_scripts->CallFunction(g_wheelSlot[i], Player(), nullptr, nullptr, 7, text.c_str(),
                FloatArg(i == chosen ? kChosenAlpha : kSlotAlpha), FloatArg(bright), FloatArg(look.red),
                FloatArg(look.green), FloatArg(look.blue), static_cast<uint32_t>(show ? 1 : 0));
        }
        float x, y;
        Place(chosen, x, y);
        g_scripts->CallFunction(g_wheelBack, Player(), nullptr, nullptr, 7, static_cast<uint32_t>(show ? 1 : 0),
            FloatArg(x), FloatArg(y), FloatArg(bright), FloatArg(look.red), FloatArg(look.green), FloatArg(look.blue));
        s_shown = show;
        s_chosen = chosen;
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
