// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstddef>
#include <cstdint>

namespace coop
{
    enum class UseCommand
    {
        AsUsual,
        NotHere,
        OursAlone,
    };
    struct UseCommandFacts
    {
        bool replaying = false;
        bool onOurPlayer = false;
        bool screenOrControls = false;
        bool controls = false;
        bool opensScreen = false;
        bool ownUse = false;
        bool friendGame = false;
    };
    inline UseCommand ForUse(const UseCommandFacts& f)
    {
        if (f.replaying)
            return f.onOurPlayer || f.screenOrControls || f.opensScreen ? UseCommand::NotHere : UseCommand::AsUsual;
        if (f.ownUse && f.screenOrControls && (!f.friendGame || f.controls))
            return UseCommand::OursAlone;
        return UseCommand::AsUsual;
    }

    constexpr uint16_t kBlockOnActivate = 2;
    inline bool ScriptHasBlock(const uint8_t* data, size_t size, uint16_t blockType)
    {
        if (!data)
            return false;
        auto u16 = [&](size_t at) { return static_cast<uint16_t>(data[at] | (data[at + 1] << 8)); };
        bool found = false;
        size_t at = 0;
        while (at + 4 <= size)
        {
            uint16_t op = u16(at);
            if (op == 0x1C)
            {
                at += 4;
                continue;
            }
            uint16_t length = u16(at + 2);
            if (op == 0x10 && length >= 2 && at + 6 <= size && u16(at + 4) == blockType)
                found = true;
            at += 4 + length;
        }
        return at == size && found;
    }

    enum class QuestCommandHere
    {
        AsUsual,
        NotHere,
        ToHost,
    };
    struct QuestCommandFacts
    {
        bool friendGame = false;
        bool objectScript = false;
        bool oursOrLine = false;
        bool hostQuest = false;
        bool replaying = false;
        bool sentAsUse = false;
    };
    inline QuestCommandHere ForQuestCommand(const QuestCommandFacts& f)
    {
        if (!f.friendGame || !f.objectScript || f.oursOrLine || !f.hostQuest)
            return QuestCommandHere::AsUsual;
        return f.replaying || f.sentAsUse ? QuestCommandHere::NotHere : QuestCommandHere::ToHost;
    }

    class RecentQuestCommands
    {
    public:
        static constexpr uint32_t kMs = 10000;
        static constexpr size_t kKept = 32;
        bool ShouldSend(uint8_t kind, uint32_t quest, uint32_t number, uint8_t on, uint32_t now)
        {
            for (Sent& s : m_sent)
                if (s.used && s.kind == kind && s.quest == quest && s.number == number && s.on == on)
                {
                    if (now - s.at < kMs)
                        return false;
                    s.at = now;
                    return true;
                }
            Sent& slot = m_sent[m_next];
            m_next = (m_next + 1) % kKept;
            slot = {true, kind, on, quest, number, now};
            return true;
        }
        void Clear() { *this = RecentQuestCommands{}; }

    private:
        struct Sent { bool used = false; uint8_t kind = 0, on = 0; uint32_t quest = 0, number = 0, at = 0; };
        Sent m_sent[kKept];
        size_t m_next = 0;
    };

    class QuestCommandGate
    {
    public:
        static constexpr uint32_t kPerSecond = 16;
        bool Take(uint8_t player, uint32_t now)
        {
            if (player >= kPlayers)
                return false;
            Slot& s = m_slots[player];
            if (now - s.since >= 1000)
            {
                s.since = now;
                s.count = 0;
            }
            return s.count++ < kPerSecond;
        }

    private:
        static constexpr uint8_t kPlayers = 9;
        struct Slot { uint32_t since = 0; uint32_t count = 0; };
        Slot m_slots[kPlayers];
    };

    inline bool ShareExplosion(bool host, bool byOurPlayer, bool objectsOwn)
    {
        return !objectsOwn && (host || byOurPlayer);
    }

    class OwnUseLock
    {
    public:
        static constexpr uint32_t kMs = 60000;
        enum class Step
        {
            None,
            Over,
            GiveBack,
        };
        void Locked(uint32_t now, uint8_t switches)
        {
            m_on = true;
            m_since = now;
            m_switches |= switches;
        }
        bool On() const { return m_on; }
        uint8_t Switches() const { return m_switches; }
        Step Update(uint32_t now, bool controlsLocked, bool inConversation)
        {
            if (!m_on)
                return Step::None;
            if (!controlsLocked)
            {
                Clear();
                return Step::Over;
            }
            if (inConversation)
                m_since = now;
            else if (now - m_since >= kMs)
                return Step::GiveBack;
            return Step::None;
        }
        bool ConversationClosed() const { return m_on; }
        void Clear()
        {
            m_on = false;
            m_switches = 0;
        }

    private:
        bool m_on = false;
        uint32_t m_since = 0;
        uint8_t m_switches = 0;
    };
}
