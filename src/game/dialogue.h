// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/coop.h"
#include "coop/talk_log.h"
#include "game/game.h"

#include <cstdint>
#include <string>

class TESObjectREFR;

namespace game
{
    class Npcs;
    class Progress;

    class Dialogue
    {
    public:
        void Install(coop::Coop& coop, const Npcs& npcs, Progress& progress);
        void Update(uint32_t now);
        uint32_t TakeCompanionAsk() { uint32_t r = m_companionAsk; m_companionAsk = 0; return r; }
        void AllowTalk(uint32_t hostRef, uint32_t until) { m_talkAllowed = hostRef; m_talkAllowedUntil = until; }
        struct TalkLogSettings
        {
            bool on = true;
            int size = 65, lines = 4, seconds = 15, gap = 6, range = 1500;
            int x = 2, y = 25, width = 600;
            int style = 1;
            bool dividers = true, hudColor = true, preview = false, alignRight = false;
            uint8_t red = 255, green = 255, blue = 255;
        };
        void SetTalkLog(const TalkLogSettings& settings) { m_talkLogSettings = settings; }
        bool LastFriendLine(uint8_t& player, uint32_t& at) const
        {
            player = m_lastFriendLine;
            at = m_lastFriendLineAt;
            return m_lastFriendLineAt != 0;
        }

        static void LabRun(uint32_t info, bool onEnd, uint32_t speaker);
        void Line(void* info, uint32_t onEnd, TESObjectREFR* speaker);

    private:
        void RunForFriend(uint8_t player, const net::DialogueLine& line);
        void Subtitles(bool talking, TESObjectREFR* partner, uint32_t now);
        uint32_t HostRef(TESObjectREFR* ref) const;
        coop::Coop* m_coop = nullptr;
        const Npcs* m_npcs = nullptr;
        Progress* m_progress = nullptr;
        uint32_t m_thread = 0;
        bool m_replaying = false;
        uint32_t m_lastActivate = 0;
        uint32_t m_openSince = 0;
        bool m_ours = false;
        bool m_closed = false;
        bool m_afterOurs = false;
        uint32_t m_companionAsk = 0, m_talkAllowed = 0, m_talkAllowedUntil = 0;
        uint8_t m_lastFriendLine = 0;
        uint32_t m_lastFriendLineAt = 0;
        bool m_reportedTalking = false;
        uint32_t m_reportedNpc = 0;
        uintptr_t m_lastSpoken = 0;
        uint32_t m_replyInfo = 0;
        int m_replyResponse = 0;
        coop::TalkLog m_talkLog;
        TalkLogSettings m_talkLogSettings;
        uint32_t m_spokenSpeaker = 0;
        uint32_t m_spokenAt = 0;
        bool m_sawTalking = false;
        bool m_spokenLive = false;
        Sound m_heard;
        uint32_t m_heardSpeaker = 0;
        std::string m_heardText;
        uint32_t m_linesSent = 0, m_linesRun = 0, m_linesSkipped = 0, m_closedCount = 0, m_nextLog = 0;
        uint32_t m_subtitlesSent = 0, m_subtitlesShown = 0, m_subtitlesFar = 0, m_linesSaid = 0, m_stops = 0;
    };
}
