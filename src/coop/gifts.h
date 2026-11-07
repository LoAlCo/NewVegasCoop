// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/clock.h"
#include "coop/saves.h"
#include "net/protocol.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace coop
{
    constexpr uint32_t kGiftSaveDelayMs = 5000;
    constexpr uint32_t kMaxGiftsPerSecond = 5;
    constexpr size_t kMaxOpenGifts = 64;

    enum class Amount
    {
        Ok,
        NotANumber,
        Zero,
        TooMany
    };

    // What the player typed in the "how many?" box. `have` is how many they hold.
    inline Amount ParseAmount(std::string_view text, int32_t have, int32_t& out)
    {
        out = 0;
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ')
            text.remove_suffix(1);
        if (text.empty())
            return Amount::NotANumber;
        int64_t value = 0;
        for (char c : text)
        {
            if (c < '0' || c > '9')
                return Amount::NotANumber;
            value = (std::min<int64_t>)(value * 10 + (c - '0'), int64_t(net::kMaxItemCount) + 1);
        }
        if (value == 0)
            return Amount::Zero;
        if (value > have || value > net::kMaxItemCount)
            return Amount::TooMany;
        out = static_cast<int32_t>(value);
        return Amount::Ok;
    }

    // The sender's own record of what it took out and hasn't heard about. Refunds only ever come from here, so nothing
    // another game says can make items that weren't taken. Ids never restart, so a late answer finds nothing.
    class SenderBook
    {
    public:
        struct Entry
        {
            uint32_t id = 0;
            uint8_t to = 0;
            net::ItemCount item;
        };

        uint32_t Add(uint8_t to, const net::ItemCount& item)
        {
            uint32_t id = m_nextId++;
            m_open[id] = Entry{id, to, item};
            return id;
        }
        bool Finish(uint32_t id, Entry& out)
        {
            auto it = m_open.find(id);
            if (it == m_open.end())
                return false;
            out = it->second;
            m_open.erase(it);
            return true;
        }
        std::vector<Entry> TakeAll()
        {
            std::vector<Entry> all;
            for (const auto& [id, entry] : m_open)
                all.push_back(entry);
            m_open.clear();
            return all;
        }
        void Forget() { m_open.clear(); }
        size_t Open() const { return m_open.size(); }

    private:
        uint32_t m_nextId = 1;
        std::map<uint32_t, Entry> m_open;
    };

    // The host's list of gifts on their way. It only routes: it holds no items and can't make any.
    class HostLedger
    {
    public:
        struct Gift
        {
            uint32_t id = 0;
            uint8_t sender = 0;
            uint32_t senderGift = 0;
            uint8_t receiver = 0;
            bool senderHere = true;
        };

        net::GiveReason Check(uint8_t sender, uint8_t receiver, bool receiverHere, bool loading) const
        {
            if (sender == receiver || !receiverHere)
                return net::GiveReason::NoSuchPlayer;
            if (loading || m_open.size() >= kMaxOpenGifts)
                return net::GiveReason::Busy;
            return net::GiveReason::Ok;
        }
        uint32_t Open(uint8_t sender, uint32_t senderGift, uint8_t receiver)
        {
            uint32_t id = m_nextId++;
            m_open[id] = Gift{id, sender, senderGift, receiver, true};
            return id;
        }
        // The receiver's answer. Anyone else's, or one for a gift already closed, finds nothing.
        bool Close(uint32_t id, uint8_t from, Gift& out)
        {
            auto it = m_open.find(id);
            if (it == m_open.end() || it->second.receiver != from)
                return false;
            out = it->second;
            m_open.erase(it);
            return true;
        }
        std::vector<Gift> ReceiverLeft(uint8_t player)
        {
            std::vector<Gift> gone;
            for (auto it = m_open.begin(); it != m_open.end();)
                if (it->second.receiver == player)
                {
                    gone.push_back(it->second);
                    it = m_open.erase(it);
                }
                else
                    ++it;
            return gone;
        }
        // Delivery goes on, but nobody is told: a new player may get the same id.
        void SenderLeft(uint8_t player)
        {
            for (auto& [id, gift] : m_open)
                if (gift.sender == player)
                    gift.senderHere = false;
        }
        void Clear() { m_open.clear(); }
        size_t Open() const { return m_open.size(); }

    private:
        uint32_t m_nextId = 1;
        std::map<uint32_t, Gift> m_open;
    };

    // Which deliveries the receiver has already seen, so one can't be applied twice.
    class ReceiverQueue
    {
    public:
        bool Accept(uint32_t id)
        {
            if (std::find(m_seen.begin(), m_seen.end(), id) != m_seen.end())
                return false;
            m_seen.push_back(id);
            if (m_seen.size() > 256)
                m_seen.pop_front();
            return true;
        }
        void Reset() { m_seen.clear(); }

    private:
        std::deque<uint32_t> m_seen;
    };

    // Saves a moment after the last delivery, so a few gifts in a row make one save.
    class GiftSaveTimer
    {
    public:
        void Delivered(uint32_t now)
        {
            m_on = true;
            m_due = now + kGiftSaveDelayMs;
        }
        bool TakeDue(uint32_t now)
        {
            if (!m_on || !IsDue(now, m_due))
                return false;
            m_on = false;
            return true;
        }
        void Forget() { m_on = false; }

    private:
        bool m_on = false;
        uint32_t m_due = 0;
    };

    // Gift saves reuse one name each, so gifts don't fill the saves folder. Both names start the same so the host can
    // tell a friend's gift save from a save the friend chose to make.
    inline std::string GiftSaveForFriend(std::string_view host)
    {
        return SaveNameOf("Co-op Gift Friend of " + PlainName(host, "Host"));
    }
    inline std::string GiftSaveForHost(std::string_view host)
    {
        return SaveNameOf("Co-op Gift Host " + PlainName(host, "Host"));
    }
    inline bool IsGiftSave(std::string_view name) { return name.compare(0, 11, "Co-op Gift ") == 0; }
}
