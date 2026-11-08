// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "game/dialogue.h"

#include "coop/clock.h"
#include "game/addresses.h"
#include "game/game.h"
#include "game/npcs.h"
#include "game/progress.h"
#include "log.h"

#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace game
{
    namespace
    {
        constexpr uint32_t kOurConversationMs = 2000;
        constexpr const char* kNotHereYet = "Not here yet: the host's game hasn't loaded them";
        constexpr uint32_t kSettleConversationMs = 500;

        Dialogue* g_dialogue = nullptr;
        uint8_t* g_original = nullptr;

        void RunOriginal(void* info, uint32_t onEnd, TESObjectREFR* speaker)
        {
            using Fn = void(__thiscall*)(void*, uint32_t, TESObjectREFR*);
            DialogueLineRunning(true);
            reinterpret_cast<Fn>(g_original)(info, onEnd, speaker);
            DialogueLineRunning(false);
        }

        void __fastcall RunResultScriptHook(void* info, void*, uint32_t onEnd, TESObjectREFR* speaker)
        {
            if (g_dialogue)
                g_dialogue->Line(info, onEnd, speaker);
            else
                RunOriginal(info, onEnd, speaker);
        }

        void JumpTo(uint8_t* from, const void* to)
        {
            from[0] = 0xE9;
            int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(from + 5));
            std::memcpy(from + 1, &rel, 4);
        }

        bool InstallHook()
        {
            static const uint8_t kStart[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C};
            auto* at = reinterpret_cast<uint8_t*>(addr::kTopicInfoRunResultScript);
            auto* back = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE));
            if (!back)
                return false;
            size_t length = 6;
            if (std::memcmp(at, kStart, 6) == 0)
            {
                std::memcpy(back, at, 6);
                JumpTo(back + 6, at + 6);
            }
            else if (at[0] == 0xE9)
            {
                int32_t rel;
                std::memcpy(&rel, at + 1, 4);
                JumpTo(back, at + 5 + rel);
                length = 5;
                LOG_INFO("Dialogue: another plugin hooks the game's dialogue scripts too; ours goes first and passes on");
            }
            else
            {
                VirtualFree(back, 0, MEM_RELEASE);
                LOG_ERROR("Dialogue: the game's dialogue script function isn't as expected (%02X %02X %02X); "
                    "conversations won't be shared", at[0], at[1], at[2]);
                return false;
            }
            DWORD old;
            if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old))
            {
                VirtualFree(back, 0, MEM_RELEASE);
                return false;
            }
            g_original = back;
            JumpTo(at, reinterpret_cast<const void*>(&RunResultScriptHook));
            if (length == 6)
                at[5] = 0x90;
            VirtualProtect(at, length, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, length);
            return true;
        }
    }

    void Dialogue::Install(coop::Coop& coop, const Npcs& npcs, Progress& progress)
    {
        if (g_original)
            return;
        m_coop = &coop;
        m_npcs = &npcs;
        m_progress = &progress;
        m_thread = GetCurrentThreadId();
        if (InstallHook())
        {
            g_dialogue = this;
            LOG_INFO("Dialogue: hooked dialogue lines' scripts");
        }
        if (InstallSayHooks())
            LOG_INFO("Dialogue: other players' conversations can be heard (hooked the game's line picking)");
        else
            LOG_ERROR("Dialogue: the game's line picking isn't as expected; other players' conversations are "
                "subtitles only");
    }

    uint32_t Dialogue::HostRef(TESObjectREFR* ref) const
    {
        if (!ref)
            return 0;
        return m_coop->GetRole() == coop::Role::Host ? RefId(ref) : m_npcs->HostRefOf(RefId(ref));
    }

    void Dialogue::Line(void* info, uint32_t onEnd, TESObjectREFR* speaker)
    {
        if (speaker && speaker != DialoguePartner() && SaidForOthers(speaker, info))
            return;
        if (m_replaying || GetCurrentThreadId() != m_thread || !m_coop || !m_coop->InGame() || !speaker)
            return RunOriginal(info, onEnd, speaker);
        TESObjectREFR* partner = DialoguePartner();
        LOG_DEBUG("Dialogue: line %08X (%s) %s, said by %s %08X; conversation screen %s %08X", TopicInfoId(info),
            TopicInfoPrompt(info).c_str(), onEnd ? "ends" : "starts", Name(speaker), RefId(speaker),
            partner ? "with" : "closed", partner ? RefId(partner) : 0);
        bool host = m_coop->GetRole() == coop::Role::Host;
        if (speaker != partner)
            return RunOriginal(info, onEnd, speaker);
        if (!host && m_npcs->IsHeld(RefId(speaker)))
        {
            ++m_linesSkipped;
            LOG_INFO("Dialogue: line %08X's script not run here (%s is held: not in the host's game here yet)",
                TopicInfoId(info), Name(speaker));
            return;
        }
        uint32_t now = GetTickCount();
        if (!host && !m_openSince && now - m_lastActivate > kOurConversationMs && OwnUseHoldsControls())
        {
            m_lastActivate = now;
            LOG_INFO("Dialogue: %s started a conversation while our own use holds our controls: ours", Name(speaker));
        }
        bool ours = host || (m_openSince ? m_ours : now - m_lastActivate <= kOurConversationMs);
        if (!ours || !m_coop->MayTalk())
        {
            ++m_linesSkipped;
            LOG_INFO("Dialogue: line %08X's script not run here (%s)", TopicInfoId(info),
                ours ? "someone else is talking" : "an NPC started this conversation here");
            return;
        }
        if (host)
            return RunOriginal(info, onEnd, speaker);
        int32_t xp = PlayerXp(), karma = PlayerKarma();
        RunOriginal(info, onEnd, speaker);
        int32_t gainedXp = PlayerXp() - xp, gainedKarma = PlayerKarma() - karma;
        if (gainedXp || gainedKarma)
            m_progress->Expect(gainedXp, gainedKarma);
        uint32_t hostSpeaker = HostRef(speaker);
        if (!hostSpeaker)
            return LOG_WARN("Dialogue: %s isn't in the host's game; line %08X not sent", Name(speaker),
                TopicInfoId(info));
        if (!m_reportedTalking || m_reportedNpc != hostSpeaker)
        {
            m_coop->SetMyConversation(true, hostSpeaker);
            m_reportedTalking = true;
            m_reportedNpc = hostSpeaker;
        }
        m_coop->SendDialogueLine({TopicInfoId(info), onEnd != 0, hostSpeaker});
        ++m_linesSent;
        LOG_DEBUG("Dialogue: sent line %08X (%s) to the host (we gained %d XP, %d karma)", TopicInfoId(info),
            onEnd ? "end" : "start", gainedXp, gainedKarma);
    }

    void Dialogue::RunForFriend(uint8_t player, const net::DialogueLine& line)
    {
        m_lastFriendLine = player;
        m_lastFriendLineAt = GetTickCount();
        void* info = TopicInfo(line.info);
        TESObjectREFR* speaker = LookupRef(line.speaker);
        if (!info || !speaker || !IsActor(speaker))
            return LOG_WARN("Dialogue: %s's line %08X said by %08X: %s", m_coop->NameOf(player).c_str(), line.info,
                line.speaker, !info ? "not a dialogue line" : "no such character here");
        if (!HasResultScript(info, line.onEnd))
            return;
        if (uint16_t command = ScreenCommandIn(info, line.onEnd))
            return LOG_INFO("Dialogue: %s's line %08X opens a screen (command %04X): not run here",
                m_coop->NameOf(player).c_str(), line.info, command);
        std::vector<Item> before, after;
        bool counted = Contents(Player(), before);
        int32_t xp = PlayerXp(), karma = PlayerKarma();
        m_replaying = true;
        ReplayingOthersLine(true);
        RunOriginal(info, line.onEnd ? 1 : 0, speaker);
        ReplayingOthersLine(false);
        m_replaying = false;
        ++m_linesRun;
        int putBack = 0;
        if (counted && Contents(Player(), after))
        {
            std::map<uint32_t, int32_t> change;
            for (const Item& i : after)
                change[i.id] += i.count;
            for (const Item& i : before)
                change[i.id] -= i.count;
            for (auto& [id, delta] : change)
                if (delta)
                {
                    ChangeItem(Player(), id, -delta);
                    ++putBack;
                    LOG_INFO("Dialogue: %s's line changed our player's %08X by %d; put back",
                        m_coop->NameOf(player).c_str(), id, delta);
                }
        }
        else
            LOG_WARN("Dialogue: couldn't count our inventory around %s's line", m_coop->NameOf(player).c_str());
        int32_t gainedXp = PlayerXp() - xp, gainedKarma = PlayerKarma() - karma;
        if (gainedXp || gainedKarma)
        {
            m_progress->Expect(gainedXp, gainedKarma);
            m_coop->ShareProgressFor(player, gainedXp, gainedKarma);
        }
        LOG_INFO("Dialogue: ran %s's line %08X (\"%s\", %s) said by %s; %d item kinds put back, %d XP, %d karma",
            m_coop->NameOf(player).c_str(), line.info, TopicInfoPrompt(info).c_str(), line.onEnd ? "end" : "start",
            Name(speaker), putBack, gainedXp, gainedKarma);
    }

    void Dialogue::LabRun(uint32_t info, bool onEnd, uint32_t speaker)
    {
        if (g_dialogue && g_dialogue->m_coop)
            g_dialogue->RunForFriend(1, {info, onEnd, speaker});
    }

    void Dialogue::LabOwnActivate()
    {
        if (g_dialogue)
            g_dialogue->m_lastActivate = GetTickCount();
    }

    void Dialogue::Update(uint32_t now)
    {
        m_thread = GetCurrentThreadId();
        if (!m_coop || !m_coop->InGame())
        {
            m_openSince = 0;
            m_reportedTalking = false;
            m_reportedNpc = 0;
            m_talkLog.Clear();
            ShowTalk({});
            return;
        }
        bool host = m_coop->GetRole() == coop::Role::Host;
        if (IsControlPressed(layout::kControlActivate))
            m_lastActivate = now;
        bool open = InDialogue();
        TESObjectREFR* partner = open ? DialoguePartner() : nullptr;
        if (!open)
        {
            if (m_openSince && m_ours && !m_closed)
                m_afterOurs = true;
            if (m_afterOurs && InMenu())
                m_lastActivate = now;
            else
                m_afterOurs = false;
            m_openSince = 0;
            m_closed = false;
        }
        else if (!m_openSince)
        {
            m_openSince = now ? now : 1;
            m_ourPlayerStarted = now - m_lastActivate <= kOurConversationMs || OwnUseHoldsControls();
            m_ours = host || m_ourPlayerStarted;
            LOG_INFO("Dialogue: a conversation with %s (%08X) opened (%s)", partner ? Name(partner) : "?",
                partner ? RefId(partner) : 0, m_ourPlayerStarted ? "ours" : "an NPC started it");
            if (!host && partner && m_npcs->IsHeld(RefId(partner)))
            {
                m_closed = true;
                ++m_closedCount;
                CloseMenu();
                LOG_INFO("Dialogue: %s (%08X) is held here (not in the host's game here yet); conversation closed",
                    Name(partner), RefId(partner));
                Notify(kNotHereYet);
            }
            uint8_t leader = 0;
            uint32_t ref = partner ? HostRef(partner) : 0;
            if (!host && !m_closed && m_ours && ref && m_coop->CompanionLeader(ref, leader) &&
                !(ref == m_talkAllowed && now < m_talkAllowedUntil))
            {
                m_closed = true;
                CloseMenu();
                m_companionAsk = ref;
                LOG_INFO("Dialogue: %s is a companion; asking what to tell it", Name(partner));
            }
        }

        bool inScreen = !open && m_afterOurs && m_reportedTalking;
        bool talking = (open && m_ours && !m_closed) || inScreen;
        uint32_t npc = inScreen ? m_reportedNpc : talking ? HostRef(partner) : 0;
        if (talking != m_reportedTalking || npc != m_reportedNpc)
        {
            m_coop->SetMyConversation(talking, npc);
            m_reportedTalking = talking;
            m_reportedNpc = npc;
        }

        if (open && !m_closed && host && m_leaving && !m_ourPlayerStarted)
        {
            m_closed = true;
            ++m_closedCount;
            CloseMenu();
            LOG_INFO("Dialogue: %s (%08X) started a conversation with us as the group leaves; closed",
                partner ? Name(partner) : "?", partner ? RefId(partner) : 0);
        }
        if (open && !m_closed && now - m_openSince >= kSettleConversationMs && (!m_ours || !m_coop->MayTalk()))
        {
            m_closed = true;
            ++m_closedCount;
            CloseMenu();
            GiveBackOwnUseControls("a conversation was closed on us");
            if (m_ours)
            {
                std::string who = m_coop->NameOf(m_coop->Talk().Talker());
                LOG_INFO("Dialogue: %s is talking; our conversation closed", who.c_str());
                Notify((who + " is already talking to someone. One conversation at a time.").c_str());
            }
            else
                LOG_INFO("Dialogue: an NPC started a conversation with us; closing it (the host's game runs those)");
        }

        if (host)
            for (const coop::Coop::DialogueLineFrom& l : m_coop->TakeDialogueLines())
                RunForFriend(l.playerId, l.line);
        Subtitles(open && talking && m_coop->MayTalk(), partner, now);

        if (coop::IsDue(now, m_nextLog))
        {
            if (m_nextLog && (m_linesSent || m_linesRun || m_linesSkipped || m_closedCount || m_subtitlesSent ||
                m_subtitlesShown || m_subtitlesFar || m_linesSaid))
                LOG_INFO("Dialogue: %u lines sent to the host, %u friends' lines run, %u not run here, %u "
                    "conversations closed; spoken lines: %u sent, %u said here, %u as subtitles only, %u too far "
                    "away, %u stopped", m_linesSent, m_linesRun, m_linesSkipped, m_closedCount, m_subtitlesSent,
                    m_linesSaid, m_subtitlesShown, m_subtitlesFar, m_stops);
            m_nextLog = now + 60000;
            m_linesSent = m_linesRun = m_linesSkipped = m_closedCount = 0;
            m_subtitlesSent = m_subtitlesShown = m_subtitlesFar = m_linesSaid = m_stops = 0;
        }
    }

    void Dialogue::Subtitles(bool talking, TESObjectREFR* partner, uint32_t now)
    {
        constexpr uint32_t kStartTalkingMs = 1000;
        Spoken spoken = talking ? SpokenNow() : Spoken{};
        if (talking && spoken.id && spoken.id != m_lastSpoken && *spoken.text)
        {
            m_lastSpoken = spoken.id;
            if (uint32_t speaker = HostRef(partner))
            {
                if (m_replyInfo && (spoken.info != m_replyInfo || spoken.response <= m_replyResponse))
                {
                    std::string said = PlayerReplyText(spoken.topic, spoken.info);
                    if (!said.empty())
                    {
                        net::Subtitle r;
                        r.speaker = speaker;
                        r.reply = true;
                        r.topic = spoken.topic;
                        r.info = spoken.info;
                        r.response = static_cast<uint8_t>(spoken.response);
                        r.text = said;
                        m_coop->ShareSubtitle(r);
                        LOG_DEBUG("Dialogue: we say \"%s\" (line %08X; shared)", said.c_str(), spoken.info);
                    }
                }
                m_replyInfo = spoken.info;
                m_replyResponse = spoken.response;
                net::Subtitle s;
                s.speaker = speaker;
                s.topic = spoken.topic;
                s.info = spoken.info;
                s.response = static_cast<uint8_t>(spoken.response);
                s.text = spoken.text;
                m_coop->ShareSubtitle(s);
                m_spokenSpeaker = speaker;
                m_spokenAt = now;
                m_sawTalking = false;
                m_spokenLive = true;
                ++m_subtitlesSent;
                LOG_DEBUG("Dialogue: %s says \"%s\" (line %08X response %d; shared)", Name(partner), spoken.text,
                    spoken.info, spoken.response);
            }
        }
        if (m_spokenLive)
        {
            bool saying = talking && IsTalking(partner);
            m_sawTalking = m_sawTalking || saying;
            if (!talking || (m_sawTalking && !saying))
            {
                net::Subtitle s;
                s.speaker = m_spokenSpeaker;
                s.stop = true;
                m_coop->ShareSubtitle(s);
                m_spokenLive = false;
                LOG_DEBUG("Dialogue: stopped talking (shared)");
            }
            else if (!m_sawTalking && now - m_spokenAt > kStartTalkingMs)
                m_spokenLive = false;
        }
        if (!talking)
            m_lastSpoken = 0, m_replyInfo = 0, m_replyResponse = 0;

        bool host = m_coop->GetRole() == coop::Role::Host;
        coop::Pose me, at;
        bool placed = GetPose(Player(), me);
        const TalkLogSettings& log = m_talkLogSettings;
        auto within = [&](const net::Subtitle& s, float distance) -> TESObjectREFR* {
            TESObjectREFR* speaker = host ? LookupRef(s.speaker) : m_npcs->OurRefOf(s.speaker);
            return placed && speaker && GetPose(speaker, at) && coop::Within(me, at, distance) ? speaker : nullptr;
        };
        for (const net::Subtitle& s : m_coop->TakeSubtitles())
        {
            if (s.reply)
            {
                if (log.on && within(s, static_cast<float>(log.range)))
                    m_talkLog.Add(coop::TalkLine(m_coop->NameOf(s.playerId), s.text), now);
                continue;
            }
            if (s.stop)
            {
                if (s.speaker == m_heardSpeaker)
                {
                    StopSound(m_heard);
                    EndSubtitle(m_heardText);
                    ++m_stops;
                }
                continue;
            }
            StopSound(m_heard);
            EndSubtitle(m_heardText);
            m_heardText = s.text;
            if (TESObjectREFR* who = log.on ? within(s, static_cast<float>(log.range)) : nullptr)
                m_talkLog.Add(coop::TalkLine(Name(who), s.text), now);
            TESObjectREFR* speaker = within(s, coop::kSamePlaceDistance);
            if (!speaker)
            {
                ++m_subtitlesFar;
                continue;
            }
            m_heardSpeaker = s.speaker;
            const char* name = Name(speaker);
            if (s.info && SayLine(speaker, s.topic, s.info, s.response, m_heard))
                ++m_linesSaid;
            else
            {
                m_heardText = *name ? std::string(name) + ": " + s.text : s.text;
                ShowSubtitle(speaker, m_heardText.c_str());
                ++m_subtitlesShown;
            }
            LOG_DEBUG("Dialogue: %s's conversation: %s says \"%s\" (line %08X response %u; sound %08X)",
                m_coop->NameOf(s.playerId).c_str(), name, s.text.c_str(), s.info, s.response, m_heard.id);
        }
        if (!log.on)
            m_talkLog.Clear();
        m_talkLog.Configure(static_cast<size_t>(log.lines), static_cast<uint32_t>(log.seconds) * 1000);
        ShowTalk(m_talkLog.Update(now));
    }

    void Dialogue::ShowTalk(const std::vector<coop::TalkLog::Shown>& lines)
    {
        const TalkLogSettings& log = m_talkLogSettings;
        TalkLogLook look;
        look.scale = log.size / 100.0f;
        look.gap = static_cast<float>(log.gap);
        look.place = log.place;
        look.xPercent = static_cast<float>(log.x);
        look.yPercent = static_cast<float>(log.y);
        look.width = static_cast<float>(log.width);
        look.alignRight = log.alignRight;
        look.dividers = log.dividers;
        look.style = log.style;
        look.hudColor = log.hudColor;
        look.red = log.red;
        look.green = log.green;
        look.blue = log.blue;
        if (!log.preview)
        {
            ShowTalkLog(lines, look);
            return;
        }
        if (m_talkPreview.size() != static_cast<size_t>(std::clamp(log.lines, 0, static_cast<int>(coop::TalkLog::kMostLines))))
            m_talkPreview = coop::TalkLogPreview(log.lines);
        ShowTalkLog(m_talkPreview, look);
    }
}
