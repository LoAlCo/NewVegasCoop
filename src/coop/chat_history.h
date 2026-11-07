// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace coop
{
    // The game's font reads < and { as the start of a tag, and names and chat can hold them.
    inline std::string CleanFeedText(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
        {
            if (c == '<' || c == '{')
                c = '(';
            else if (c == '>' || c == '}')
                c = ')';
            else if (c == '&')
                c = '+';
            else if (static_cast<unsigned char>(c) < 0x20)
                c = ' ';
        }
        return out;
    }

    inline std::string JoinLines(const std::vector<std::string>& lines)
    {
        std::string out;
        for (const std::string& line : lines)
        {
            if (!out.empty())
                out += '\n';
            out += line;
        }
        return out;
    }

    class ChatHistory
    {
    public:
        static constexpr size_t kKeptLines = 200;
        static constexpr size_t kMostShown = 12;
        // A guess on the safe side at what fits the panel, so long lines that wrap don't run it off the screen
        static constexpr size_t kRows = 16;
        static constexpr size_t kCharsPerRow = 48;

        struct Shown
        {
            std::vector<std::string> lines;
            size_t older = 0, newer = 0;
        };

        explicit ChatHistory(size_t maxRows = kRows, size_t charsPerRow = kCharsPerRow) :
            m_maxRows(std::max<size_t>(maxRows, 1)), m_charsPerRow(std::max<size_t>(charsPerRow, 1)) {}

        // What fits changes with the panel's size and width
        void SetShape(size_t maxRows, size_t charsPerRow)
        {
            m_maxRows = std::max<size_t>(maxRows, 1);
            m_charsPerRow = std::max<size_t>(charsPerRow, 1);
            m_back = std::min<size_t>(m_back, MostBack());
        }

        // Reading back through the history stays on the same lines when new ones come in
        void Add(std::string_view line)
        {
            m_lines.push_back(CleanFeedText(line));
            while (m_lines.size() > kKeptLines)
                m_lines.pop_front();
            if (m_back > 0)
                ++m_back;
            m_back = std::min<size_t>(m_back, MostBack());
        }

        // More is further back
        void Scroll(int lines)
        {
            int64_t back = static_cast<int64_t>(m_back) + lines;
            m_back = static_cast<size_t>(std::clamp<int64_t>(back, 0, static_cast<int64_t>(MostBack())));
        }

        void ResetScroll() { m_back = 0; }
        size_t Size() const { return m_lines.size(); }

        Shown View() const
        {
            Shown shown;
            if (m_lines.empty())
                return shown;
            size_t last = m_lines.size() - 1 - std::min<size_t>(m_back, m_lines.size() - 1);
            size_t first = last, rows = RowsOf(m_lines[last]);
            while (first > 0 && last - first + 1 < kMostShown && rows + RowsOf(m_lines[first - 1]) <= m_maxRows)
                rows += RowsOf(m_lines[--first]);
            shown.lines.assign(m_lines.begin() + first, m_lines.begin() + last + 1);
            shown.older = first;
            shown.newer = m_lines.size() - 1 - last;
            return shown;
        }

    private:
        size_t RowsOf(const std::string& line) const
        {
            return std::max<size_t>((line.size() + m_charsPerRow - 1) / m_charsPerRow, 1);
        }

        // As far back as the oldest page, which is still a full page
        size_t MostBack() const
        {
            if (m_lines.empty())
                return 0;
            size_t last = 0, rows = RowsOf(m_lines[0]);
            while (last + 1 < m_lines.size() && last + 1 < kMostShown && rows + RowsOf(m_lines[last + 1]) <= m_maxRows)
                rows += RowsOf(m_lines[++last]);
            return m_lines.size() - 1 - last;
        }

        std::deque<std::string> m_lines;
        size_t m_back = 0;
        size_t m_maxRows, m_charsPerRow;
    };

    inline std::string HistoryPanelText(const ChatHistory::Shown& shown)
    {
        std::vector<std::string> rows;
        if (shown.older)
            rows.push_back("^ " + std::to_string(shown.older) + " older (PgUp)");
        rows.insert(rows.end(), shown.lines.begin(), shown.lines.end());
        if (shown.newer)
            rows.push_back("v " + std::to_string(shown.newer) + " newer (PgDn)");
        return JoinLines(rows);
    }

    class EventFeed
    {
    public:
        static constexpr size_t kMostLines = 4;
        static constexpr uint32_t kFadeMs = 1500;

        void SetLineMs(uint32_t ms) { m_lineMs = std::max<uint32_t>(ms, kFadeMs); }

        void Add(std::string_view line)
        {
            m_lines.push_back({CleanFeedText(line), m_lineMs});
            while (m_lines.size() > kMostLines)
                m_lines.pop_front();
        }

        // Paused lines wait, so a friend going down while the Pip-Boy is open isn't missed
        std::vector<std::string> Update(uint32_t now, bool paused)
        {
            uint32_t step = m_started ? now - m_last : 0;
            m_started = true;
            m_last = now;
            if (!paused)
            {
                for (Line& l : m_lines)
                    l.left -= std::min<uint32_t>(l.left, step);
                m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [](const Line& l) { return l.left == 0; }),
                    m_lines.end());
            }
            std::vector<std::string> shown;
            for (const Line& l : m_lines)
                shown.push_back(l.text);
            return shown;
        }

        // The panel is one piece of text, so it fades as one, led by its newest line
        float Alpha() const
        {
            if (m_lines.empty())
                return 0;
            uint32_t left = m_lines.back().left;
            return left >= kFadeMs ? 1.0f : static_cast<float>(left) / kFadeMs;
        }

        void Clear() { m_lines.clear(); }

    private:
        struct Line { std::string text; uint32_t left; };
        std::deque<Line> m_lines;
        uint32_t m_lineMs = 8000;
        uint32_t m_last = 0;
        bool m_started = false;
    };

    // Works out who joined, left, went down and got back up from what every player's game already knows, and says each
    // one once. Nothing here is sent, so nobody needs the same build to see these.
    class PartyWatch
    {
    public:
        static constexpr uint32_t kQuietMs = 10000;
        static constexpr uint32_t kHoldStateMs = 400;
        static constexpr uint32_t kJoinNameMs = 2000;
        static constexpr uint32_t kRepeatMs = 5000;

        struct Member
        {
            uint8_t id = 0;
            std::string name;
            bool healthKnown = false;
            bool downed = false;
        };

        std::vector<std::string> Observe(uint32_t now, bool inSession, const std::vector<Member>& roster)
        {
            std::vector<std::string> lines;
            if (!inSession)
            {
                // Everyone vanishing here is the session ending, not everyone leaving
                Forget();
                return lines;
            }
            if (!m_inSession)
            {
                m_inSession = true;
                m_rosterQuietUntil = m_healthQuietUntil = now + kQuietMs;
            }
            bool rosterQuiet = static_cast<int32_t>(now - m_rosterQuietUntil) < 0;
            bool healthQuiet = m_muted || static_cast<int32_t>(now - m_healthQuietUntil) < 0;

            for (auto it = m_seen.begin(); it != m_seen.end();)
            {
                bool here = std::any_of(roster.begin(), roster.end(),
                    [&](const Member& m) { return m.id == it->first && !m.name.empty(); });
                if (here)
                {
                    ++it;
                    continue;
                }
                if (!it->second.joinWaiting && !rosterQuiet)
                    Say(lines, now, it->first, Kind::Left, it->second.name + " left");
                it = m_seen.erase(it);
            }

            for (const Member& m : roster)
            {
                if (m.name.empty())
                    continue;
                auto found = m_seen.find(m.id);
                if (found == m_seen.end())
                {
                    Seen fresh;
                    fresh.joinWaiting = !rosterQuiet;
                    fresh.joinAt = now + kJoinNameMs;
                    found = m_seen.emplace(m.id, fresh).first;
                }
                Seen& seen = found->second;
                seen.name = m.name;
                if (seen.joinWaiting && static_cast<int32_t>(now - seen.joinAt) >= 0)
                {
                    seen.joinWaiting = false;
                    Say(lines, now, m.id, Kind::Joined, seen.name + " joined");
                }
                WatchHealth(lines, now, m, seen, healthQuiet);
            }
            return lines;
        }

        void Forget() { *this = PartyWatch(); }
        bool InSession() const { return m_inSession; }

        // Game over: everyone's health is about to look like a revive, so nothing about it means anything until a load
        void Mute() { m_muted = true; }

        // Health moves about during a load, so what it says afterwards is where everyone starts from
        void Rebaseline(uint32_t now)
        {
            m_muted = false;
            m_healthQuietUntil = now + kQuietMs;
            for (auto& [id, seen] : m_seen)
            {
                seen.healthKnown = false;
                seen.changing = false;
            }
        }

    private:
        enum class Kind : uint32_t { Joined, Left, Down, Revived };

        struct Seen
        {
            std::string name;
            bool joinWaiting = false;
            uint32_t joinAt = 0;
            bool healthKnown = false;
            bool saidDown = false;
            bool changing = false;
            uint32_t changingSince = 0;
        };

        void WatchHealth(std::vector<std::string>& lines, uint32_t now, const Member& m, Seen& seen, bool quiet)
        {
            if (!m.healthKnown)
            {
                seen.healthKnown = seen.changing = false;
                return;
            }
            if (!seen.healthKnown || quiet)
            {
                seen.healthKnown = true;
                seen.saidDown = m.downed;
                seen.changing = false;
                return;
            }
            if (m.downed == seen.saidDown)
            {
                seen.changing = false;
                return;
            }
            if (!seen.changing)
            {
                seen.changing = true;
                seen.changingSince = now;
            }
            if (now - seen.changingSince < kHoldStateMs)
                return;
            seen.changing = false;
            seen.saidDown = m.downed;
            if (m.downed)
                Say(lines, now, m.id, Kind::Down, seen.name + " is down");
            else
                Say(lines, now, m.id, Kind::Revived, seen.name + " was revived");
        }

        void Say(std::vector<std::string>& lines, uint32_t now, uint8_t id, Kind kind, const std::string& text)
        {
            uint32_t key = static_cast<uint32_t>(id) << 8 | static_cast<uint32_t>(kind);
            auto said = m_said.find(key);
            if (said != m_said.end() && now - said->second < kRepeatMs)
                return;
            m_said[key] = now;
            lines.push_back("* " + text);
        }

        std::map<uint8_t, Seen> m_seen;
        std::map<uint32_t, uint32_t> m_said;
        bool m_inSession = false;
        bool m_muted = false;
        uint32_t m_rosterQuietUntil = 0, m_healthQuietUntil = 0;
    };

    // The default panel holds 16 rows of 48 characters: a guess on the safe side, which keeps its height about the same
    // at any text size
    inline size_t ChatPanelRows(int sizePercent)
    {
        return std::clamp<size_t>(16 * 65 / static_cast<size_t>(std::max<int>(sizePercent, 1)), 4, 24);
    }

    inline size_t ChatPanelCharsPerRow(int width, int sizePercent)
    {
        return std::max<size_t>(static_cast<size_t>(std::max<int>(width, 1)) * 48 * 65 /
            (500 * static_cast<size_t>(std::max<int>(sizePercent, 1))), 10);
    }

    // As many lines as the panel can show, some of them long, so what it's moved clear of stays clear
    inline std::string ChatPanelPreview()
    {
        std::vector<std::string> lines = {"Preview: chat and party events show here.", "--- Joined a session ---",
            "* Sam joined", "Sam: Anyone know the way to the Strip?",
            "You: Follow the road north, past the old gas station and the bridge, and keep the river on your right.",
            "* Sam is down", "* Sam was revived", "Sam: Thanks, that one nearly had me.", "You: Stay close this time.",
            "* Alex joined", "Alex: Where are you two?", "* Alex left"};
        return JoinLines(lines);
    }

    struct FeedCorner
    {
        float left = 0, top = 0;
    };

    // X is where the side the text sits against goes, so the whole slider moves it. It stays on the screen whatever the
    // settings say.
    inline FeedCorner PlaceFeed(int xPercent, int yPercent, float screenWidth, float screenHeight, float width, float height,
        bool alignRight)
    {
        constexpr float kMargin = 8;
        FeedCorner corner;
        float left = screenWidth * xPercent / 100 - (alignRight ? width : 0);
        corner.left = std::clamp(left, kMargin, std::max<float>(kMargin, screenWidth - width - kMargin));
        corner.top = std::clamp(screenHeight * yPercent / 100, kMargin, std::max<float>(kMargin, screenHeight - height - kMargin));
        return corner;
    }
}
