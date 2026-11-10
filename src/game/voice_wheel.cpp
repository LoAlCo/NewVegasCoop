// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// The voice line wheel on the HUD, and the view turning that points at its phrases.

#include "game/game.h"
#include "game/internal.h"
#include "coop/voice_lines.h"
#include "log.h"
#include "nvse/GameObjects.h"

#include <cmath>
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
        if (!open || !me)
        {
            g_pointing = false;
            return false;
        }
        if (!g_pointing)
        {
            g_pointing = true;
            g_heading = me->rotZ;
            g_pitch = me->rotX;
            return true;
        }
        // Turning right raises the heading; looking down raises the pitch. Put the view back after reading it.
        dx = std::remainder(me->rotZ - g_heading, 6.2831853f);
        dy = -(me->rotX - g_pitch);
        if (!std::isfinite(dx) || !std::isfinite(dy))
            dx = dy = 0;
        me->rotZ = g_heading;
        SetPitch(me, g_pitch);
        return true;
    }
}
