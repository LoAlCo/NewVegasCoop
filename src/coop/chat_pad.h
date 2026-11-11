// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace coop
{
    // XInput's button bits
    namespace pad
    {
        constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010,
            kBack = 0x0020, kLeftStick = 0x0040, kRightStick = 0x0080, kLeftBumper = 0x0100, kRightBumper = 0x0200,
            kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
    }

    // The buttons chat can be opened with (MCM Controller Button), the same list as the voice wheel's. Start and the face
    // buttons aren't offered: the game needs them, and A and B work chat once it's open.
    constexpr uint16_t kChatPadButtons[] = {0, pad::kUp, pad::kDown, pad::kLeft, pad::kRight, pad::kBack, pad::kLeftStick,
        pad::kRightStick, pad::kLeftBumper, pad::kRightBumper};

    inline uint16_t ChatPadButton(int setting)
    {
        return setting > 0 && setting < static_cast<int>(std::size(kChatPadButtons)) ? kChatPadButtons[setting] : 0;
    }

    constexpr const char* kDefaultQuickMessages =
        "Follow me|Wait here|Come here|On my way|Help!|Enemies here|I need healing|I need ammo|Ready|Yes|No|Thanks";
    constexpr size_t kMostQuickMessages = 20;
    constexpr size_t kQuickMessageChars = 80;

    // The ini's list, with | between messages. What the game's font or a chat line can't hold is left out, and an empty
    // list falls back to the default one.
    inline std::vector<std::string> QuickMessages(std::string_view setting)
    {
        std::vector<std::string> out;
        auto add = [&](std::string_view part)
        {
            std::string text;
            for (char c : part)
            {
                if (static_cast<unsigned char>(c) < 0x20 || c == '<' || c == '>' || c == '{' || c == '}' || c == '&')
                    continue;
                text += c;
            }
            size_t first = text.find_first_not_of(' '), last = text.find_last_not_of(' ');
            if (first == std::string::npos)
                return;
            text = text.substr(first, std::min<size_t>(last - first + 1, kQuickMessageChars));
            if (out.size() < kMostQuickMessages)
                out.push_back(std::move(text));
        };
        size_t start = 0;
        while (start <= setting.size())
        {
            size_t bar = setting.find('|', start);
            if (bar == std::string_view::npos)
                bar = setting.size();
            add(setting.substr(start, bar - start));
            start = bar + 1;
        }
        if (out.empty() && setting != kDefaultQuickMessages)
            return QuickMessages(kDefaultQuickMessages);
        return out;
    }

    // Fires when pressed, then again and again while held, like a keyboard's own repeat
    class HoldRepeat
    {
    public:
        static constexpr uint32_t kFirstMs = 400;
        static constexpr uint32_t kEveryMs = 90;

        bool Update(bool down, uint32_t now)
        {
            if (!down)
            {
                m_down = false;
                return false;
            }
            if (!m_down)
            {
                m_down = true;
                m_next = now + kFirstMs;
                return true;
            }
            if (static_cast<int32_t>(now - m_next) < 0)
                return false;
            m_next = now + kEveryMs;
            return true;
        }

    private:
        bool m_down = false;
        uint32_t m_next = 0;
    };

    struct ChatPadInput
    {
        bool connected = false;
        uint16_t buttons = 0;
        float ry = 0;  // right stick, -1..1, up positive
    };

    struct ChatPadActions
    {
        bool open = false, close = false, send = false;
        int scroll = 0;  // lines, more is further back
        int pick = 0;    // quick messages to move along
    };

    // What a controller does to chat. Closed, the chosen button opens it. Open: A sends, B or the chosen button closes it,
    // the d-pad's up and down and the right stick read back through the history, the bumpers turn a page, and left and
    // right pick a quick message.
    class ChatPadControl
    {
    public:
        static constexpr int kPage = 10;
        static constexpr float kDeadZone = 0.3f;
        static constexpr float kSlowest = 3, kFastest = 25;  // lines a second

        ChatPadActions Update(uint32_t now, const ChatPadInput& in, bool chatOpen, uint16_t openButton, bool controlsInChat)
        {
            ChatPadActions act;
            uint32_t step = m_started ? now - m_last : 0;
            m_started = true;
            m_last = now;
            uint16_t buttons = in.connected ? in.buttons : 0;
            if (chatOpen != m_wasOpen)
            {
                // Whatever's held as chat opens or closes was meant for what came before, so it waits to be let go
                m_ignore = buttons;
                m_stick = 0;
                m_wasOpen = chatOpen;
            }
            m_ignore &= buttons;
            uint16_t usable = buttons & ~m_ignore;
            uint16_t pressed = usable & ~m_held;
            m_held = usable;

            if (!chatOpen)
            {
                if (openButton && (pressed & openButton))
                {
                    act.open = true;
                    m_ignore |= openButton;
                }
                return act;
            }
            if (openButton && (pressed & openButton))
            {
                act.close = true;
                return act;
            }
            if (!controlsInChat)
                return act;
            if (pressed & pad::kB)
            {
                act.close = true;
                return act;
            }
            if (pressed & pad::kA)
                act.send = true;
            act.scroll += m_older.Update((usable & pad::kUp) != 0, now) - m_newer.Update((usable & pad::kDown) != 0, now);
            if (pressed & pad::kLeftBumper)
                act.scroll += kPage;
            if (pressed & pad::kRightBumper)
                act.scroll -= kPage;
            act.pick += m_right.Update((usable & pad::kRight) != 0, now) - m_left.Update((usable & pad::kLeft) != 0, now);

            float tilt = in.connected ? in.ry : 0;
            float amount = std::fabs(tilt);
            if (amount < kDeadZone)
                m_stick = 0;
            else
            {
                float rate = kSlowest + (kFastest - kSlowest) * std::min<float>((amount - kDeadZone) / (1 - kDeadZone), 1.0f);
                m_stick += (tilt > 0 ? rate : -rate) * std::min<uint32_t>(step, 250) / 1000.0f;
                int lines = static_cast<int>(m_stick);
                m_stick -= static_cast<float>(lines);
                act.scroll += lines;
            }
            return act;
        }

    private:
        HoldRepeat m_older, m_newer, m_left, m_right;
        uint16_t m_held = 0, m_ignore = 0;
        float m_stick = 0;
        bool m_wasOpen = false;
        bool m_started = false;
        uint32_t m_last = 0;
    };

    // Shown in place of the empty "Say:" line while a controller works chat. No < or >: the game's font reads them as tags.
    inline std::string QuickMessageHint(const std::vector<std::string>& messages, size_t pick)
    {
        if (messages.empty())
            return "B: close   D-pad up/down: read back";
        return "Say: [" + messages[pick % messages.size()] + "]   A: send   B: close   D-pad: pick, read back";
    }
}
