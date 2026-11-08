// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace coop
{
    inline std::string TalkLine(std::string_view who, std::string_view text)
    {
        std::string line;
        for (char c : who)
            line += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        if (!line.empty())
            line += ": ";
        line += text;
        return line;
    }

    class TalkLog
    {
    public:
        static constexpr size_t kMostLines = 10;
        static constexpr uint32_t kFadeMs = 1500;

        struct Shown
        {
            std::string text;
            uint32_t id;
            float alpha;
        };

        void Configure(size_t maxLines, uint32_t lineMs)
        {
            m_maxLines = std::clamp<size_t>(maxLines, 1, kMostLines);
            m_lineMs = std::max<uint32_t>(lineMs, kFadeMs);
            while (m_lines.size() > m_maxLines)
                m_lines.pop_front();
        }

        void Add(std::string line, uint32_t now)
        {
            if (++m_lastId == 0)
                m_lastId = 1;
            m_lines.push_back({std::move(line), m_lastId, now + m_lineMs});
            while (m_lines.size() > m_maxLines)
                m_lines.pop_front();
        }

        std::vector<Shown> Update(uint32_t now)
        {
            while (!m_lines.empty() && static_cast<int32_t>(now - m_lines.front().until) >= 0)
                m_lines.pop_front();
            std::vector<Shown> shown;
            for (const Line& l : m_lines)
            {
                uint32_t left = l.until - now;
                shown.push_back({l.text, l.id, left >= kFadeMs ? 1.0f : static_cast<float>(left) / kFadeMs});
            }
            return shown;
        }

        void Clear() { m_lines.clear(); }

    private:
        struct Line { std::string text; uint32_t id, until; };
        std::deque<Line> m_lines;
        size_t m_maxLines = 4;
        uint32_t m_lineMs = 15000;
        uint32_t m_lastId = 0;
    };

    struct TalkLogCorner { float x, y; };
    constexpr float kTalkLogMargin = 8;
    inline float KeepTalkLogOnScreen(float at, float screen, float size)
    {
        return std::clamp(at, kTalkLogMargin, std::max<float>(kTalkLogMargin, screen - size - kTalkLogMargin));
    }
    inline TalkLogCorner PlaceTalkLog(float xPercent, float yPercent, float screenWidth, float screenHeight,
        float width, float height)
    {
        return {KeepTalkLogOnScreen(screenWidth * xPercent / 100, screenWidth, width),
            KeepTalkLogOnScreen(screenHeight * yPercent / 100, screenHeight, height)};
    }
    constexpr float kTalkLogAboveHp = 8;
    inline TalkLogCorner PlaceTalkLogAboveHp(float hpX, float hpTop, float screenWidth, float screenHeight,
        float width, float height)
    {
        return {KeepTalkLogOnScreen(hpX, screenWidth, width),
            KeepTalkLogOnScreen(hpTop - kTalkLogAboveHp - height, screenHeight, height)};
    }

    inline std::vector<TalkLog::Shown> TalkLogPreview(int lines)
    {
        static constexpr std::string_view kSamples[TalkLog::kMostLines][2] = {
            {"Preview", "Nearby conversations show here."},
            {"Friend", "Anything you can tell me about this place?"},
            {"NPC", "Not much to tell. Folks come through, trade what they have, and move on before dark."},
            {"Friend", "Who runs things around here?"},
            {"NPC", "Depends who you ask."},
            {"Friend", "I'm asking you."},
            {"NPC", "Then you should talk to the sheriff, and you should keep that weapon where it can be seen."},
            {"Friend", "Fair enough."},
            {"NPC", "Watch yourself out there."},
            {"Friend", "I will. Thanks."},
        };
        constexpr uint32_t kFirstId = 0xFFFFFF00;
        std::vector<TalkLog::Shown> shown;
        const size_t count = std::min<size_t>(static_cast<size_t>(std::max<int>(lines, 0)), TalkLog::kMostLines);
        for (size_t i = 0; i < count; ++i)
            shown.push_back({TalkLine(kSamples[i][0], kSamples[i][1]), kFirstId + static_cast<uint32_t>(i), 1.0f});
        return shown;
    }
}
