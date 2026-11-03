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
    // "Easy Pete" and "Who are you?" -> "EASY PETE: Who are you?"
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

    // The lines of other players' conversations heard nearby, newest last; each fades out at the end of its time.
    class TalkLog
    {
    public:
        static constexpr size_t kMostLines = 10;
        static constexpr uint32_t kFadeMs = 1500;

        struct Shown
        {
            std::string text;
            uint32_t id;  // the same for as long as the line is shown, never 0
            float alpha;  // 1, down to 0 as it fades out
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

        // The lines to show now, oldest first
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
}
