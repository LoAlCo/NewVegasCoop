// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

// Trading between two players, like a stall: each puts items up at a price (0 is free) while the trade window is
// open, and the other buys them. A purchase is two escrowed transfers: the buyer's game takes its caps out before
// asking, the seller's takes the item out before sending it, and each gives back from its own record if the other
// half fails. The host only routes and keeps a list of what's on its way.
namespace coop
{
    constexpr float kTradeDistance = 1400;
    constexpr size_t kMaxListings = 64;
    constexpr size_t kMaxOpenPurchases = 64;
    constexpr uint32_t kTradeAskMs = 30000;
    constexpr uint32_t kMaxTradeMessagesPerSecond = 80;

    // What `count` items at `price` each cost, or -1 when it's more caps than a stack can hold.
    inline int32_t TradeCost(int32_t price, int32_t count)
    {
        if (price < 0 || count < 1)
            return -1;
        int64_t total = int64_t(price) * count;
        return total > net::kMaxItemCount ? -1 : static_cast<int32_t>(total);
    }

    struct Listing
    {
        uint32_t id = 0;
        net::ItemCount item;
        uint8_t itemType = 0;
        int32_t price = 0;
    };

    // One player's stall: what they've put up. Putting up the same kind again replaces its listing's count and price:
    // a stall never adds up, so it can't show more than the seller said they have.
    class Stall
    {
    public:
        const Listing* Put(const net::ItemCount& item, uint8_t itemType, int32_t price)
        {
            if (item.count < 1 || price < 0 || price > net::kMaxItemCount)
                return nullptr;
            for (Listing& l : m_listings)
                if (l.item.SameKind(item))
                {
                    l.item.count = item.count;
                    l.price = price;
                    return &l;
                }
            if (m_listings.size() >= kMaxListings)
                return nullptr;
            m_listings.push_back(Listing{m_nextId++, item, itemType, price});
            return &m_listings.back();
        }
        // The partner's stall, as their game describes it.
        void Set(const Listing& listing)
        {
            for (Listing& l : m_listings)
                if (l.id == listing.id)
                {
                    l = listing;
                    return;
                }
            if (m_listings.size() < kMaxListings)
                m_listings.push_back(listing);
        }
        const Listing* Find(uint32_t id) const
        {
            for (const Listing& l : m_listings)
                if (l.id == id)
                    return &l;
            return nullptr;
        }
        const Listing* FindKind(const net::ItemCount& kind) const
        {
            for (const Listing& l : m_listings)
                if (l.item.SameKind(kind))
                    return &l;
            return nullptr;
        }
        // Takes `count` off a listing. False if there isn't that many; what's left is in `left`.
        bool Take(uint32_t id, int32_t count, int32_t& left)
        {
            left = 0;
            for (size_t i = 0; i < m_listings.size(); ++i)
                if (m_listings[i].id == id)
                {
                    if (count < 1 || count > m_listings[i].item.count)
                        return false;
                    left = m_listings[i].item.count -= count;
                    if (left == 0)
                        m_listings.erase(m_listings.begin() + i);
                    return true;
                }
            return false;
        }
        bool Remove(uint32_t id)
        {
            for (size_t i = 0; i < m_listings.size(); ++i)
                if (m_listings[i].id == id)
                {
                    m_listings.erase(m_listings.begin() + i);
                    return true;
                }
            return false;
        }
        void Clear() { m_listings.clear(); }
        const std::vector<Listing>& All() const { return m_listings; }

    private:
        uint32_t m_nextId = 1;
        std::vector<Listing> m_listings;
    };

    // What one game has taken out and not yet heard about, by id. Ids it makes never restart, so a late answer
    // finds nothing.
    template <typename T>
    class PendingBook
    {
    public:
        uint32_t Add(const T& value)
        {
            uint32_t id = m_nextId++;
            m_open[id] = value;
            return id;
        }
        void Put(uint32_t id, const T& value) { m_open[id] = value; }
        const T* Find(uint32_t id) const
        {
            auto it = m_open.find(id);
            return it == m_open.end() ? nullptr : &it->second;
        }
        bool Finish(uint32_t id, T& out)
        {
            auto it = m_open.find(id);
            if (it == m_open.end())
                return false;
            out = it->second;
            m_open.erase(it);
            return true;
        }
        std::vector<std::pair<uint32_t, T>> TakeAll()
        {
            std::vector<std::pair<uint32_t, T>> all(m_open.begin(), m_open.end());
            m_open.clear();
            return all;
        }
        void Forget() { m_open.clear(); }
        size_t Open() const { return m_open.size(); }

    private:
        uint32_t m_nextId = 1;
        std::map<uint32_t, T> m_open;
    };

    // The host's view: who is asking whom, who is trading with whom, and purchases on their way.
    class TradeLedger
    {
    public:
        struct Purchase
        {
            uint8_t buyer = 0;
            uint32_t buyerId = 0;
            uint8_t seller = 0;
            bool sold = false;
        };

        bool Busy(uint8_t player) const
        {
            if (m_partner.count(player))
                return true;
            for (const auto& [from, ask] : m_asks)
                if (from == player || ask.first == player)
                    return true;
            return false;
        }
        void Ask(uint8_t from, uint8_t to, uint32_t now) { m_asks[from] = {to, now}; }
        // The asker, if `to` was asked by `from` and it hasn't run out.
        bool TakeAsk(uint8_t from, uint8_t to, uint32_t now)
        {
            auto it = m_asks.find(from);
            if (it == m_asks.end() || it->second.first != to)
                return false;
            bool fresh = now - it->second.second <= kTradeAskMs + 5000;
            m_asks.erase(it);
            return fresh;
        }
        void Open(uint8_t a, uint8_t b)
        {
            m_partner[a] = b;
            m_partner[b] = a;
        }
        bool Together(uint8_t a, uint8_t b) const
        {
            auto it = m_partner.find(a);
            return it != m_partner.end() && it->second == b;
        }
        // Ends `player`'s trade. Their partner, or 0xFF when they weren't trading.
        uint8_t Close(uint8_t player)
        {
            auto it = m_partner.find(player);
            if (it == m_partner.end())
                return 0xFF;
            uint8_t other = it->second;
            m_partner.erase(it);
            m_partner.erase(other);
            return other;
        }
        // Asks from or to `player`: what's left, as {asker, asked}.
        std::vector<std::pair<uint8_t, uint8_t>> DropAsks(uint8_t player)
        {
            std::vector<std::pair<uint8_t, uint8_t>> gone;
            for (auto it = m_asks.begin(); it != m_asks.end();)
                if (it->first == player || it->second.first == player)
                {
                    gone.push_back({it->first, it->second.first});
                    it = m_asks.erase(it);
                }
                else
                    ++it;
            return gone;
        }

        uint32_t OpenPurchase(uint8_t buyer, uint32_t buyerId, uint8_t seller)
        {
            if (m_purchases.size() >= kMaxOpenPurchases)
                return 0;
            uint32_t id = m_nextId++;
            m_purchases[id] = Purchase{buyer, buyerId, seller, false};
            return id;
        }
        Purchase* FindPurchase(uint32_t id)
        {
            auto it = m_purchases.find(id);
            return it == m_purchases.end() ? nullptr : &it->second;
        }
        // The open purchase `buyer` calls `buyerId`.
        uint32_t FindByBuyer(uint8_t buyer, uint32_t buyerId) const
        {
            for (const auto& [id, p] : m_purchases)
                if (p.buyer == buyer && p.buyerId == buyerId)
                    return id;
            return 0;
        }
        void ClosePurchase(uint32_t id) { m_purchases.erase(id); }
        // Purchases involving `player`, closed and returned with their ids.
        std::vector<std::pair<uint32_t, Purchase>> PlayerLeft(uint8_t player)
        {
            std::vector<std::pair<uint32_t, Purchase>> gone;
            for (auto it = m_purchases.begin(); it != m_purchases.end();)
                if (it->second.buyer == player || it->second.seller == player)
                {
                    gone.push_back(*it);
                    it = m_purchases.erase(it);
                }
                else
                    ++it;
            return gone;
        }
        void Clear()
        {
            m_asks.clear();
            m_partner.clear();
            m_purchases.clear();
        }

    private:
        uint32_t m_nextId = 1;
        std::map<uint8_t, std::pair<uint8_t, uint32_t>> m_asks;
        std::map<uint8_t, uint8_t> m_partner;
        std::map<uint32_t, Purchase> m_purchases;
    };
}
