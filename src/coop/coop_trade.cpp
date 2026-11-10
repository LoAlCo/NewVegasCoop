// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "coop/coop.h"
#include "log.h"

#include <windows.h>

namespace coop
{
    using namespace net;

    namespace
    {
        Trade Make(TradeStage stage, uint8_t other, uint32_t id = 0, TradeReason reason = TradeReason::Ok)
        {
            Trade m;
            m.stage = stage;
            m.other = other;
            m.id = id;
            m.reason = reason;
            return m;
        }
    }

    // The trading side every player has, the host included.

    bool Coop::AskTrade(uint8_t to)
    {
        if (!InGame() || to == m_myId || !IsPlayer(to) || m_tradePartner != kNoTrade || m_tradeAsking != kNoTrade ||
            m_tradeAsker != kNoTrade)
            return false;
        m_tradeAsking = to;
        LOG_INFO("Trade: asking %s", NameOf(to).c_str());
        SendTradeToHost(Make(TradeStage::Ask, to));
        return true;
    }

    void Coop::AnswerTrade(uint8_t from, bool yes, TradeReason no)
    {
        if (m_tradeAsker != from)
            return;
        m_tradeAsker = kNoTrade;
        LOG_INFO("Trade: %s %s", yes ? "opening the window with" : "not trading with", NameOf(from).c_str());
        SendTradeToHost(Make(TradeStage::Answer, from, 0, yes ? TradeReason::Ok : no));
        if (yes)
        {
            m_tradePartner = from;
            m_myStall.Clear();
            m_partnerStall.Clear();
            TradeEvent e;
            e.kind = TradeEvent::Kind::Opened;
            e.player = from;
            m_tradeEvents.push_back(e);
        }
    }

    void Coop::CloseTrade(TradeReason reason)
    {
        if (m_tradePartner == kNoTrade && m_tradeAsking == kNoTrade)
            return;
        EndTradeHere(reason, true);
    }

    void Coop::EndTradeHere(TradeReason reason, bool tell)
    {
        uint8_t other = m_tradePartner != kNoTrade ? m_tradePartner : m_tradeAsking;
        if (tell && other != kNoTrade)
            SendTradeToHost(Make(TradeStage::Close, other, 0, reason));
        if (m_tradePartner != kNoTrade)
            LOG_INFO("Trade: closed with %s (reason %d)", NameOf(m_tradePartner).c_str(), static_cast<int>(reason));
        m_tradePartner = m_tradeAsking = kNoTrade;
        m_myStall.Clear();
        m_partnerStall.Clear();
    }

    const Listing* Coop::ListItem(const ItemCount& item, uint8_t itemType, int32_t price)
    {
        if (m_tradePartner == kNoTrade)
            return nullptr;
        const Listing* l = m_myStall.Put(item, itemType, price);
        if (!l)
            return nullptr;
        LOG_INFO("Trade: put up %d of %08X (condition %d, mods %d) at %d caps each (listing %u)", l->item.count,
            l->item.item, l->item.health, l->item.mods, l->price, l->id);
        Trade m = Make(TradeStage::List, m_tradePartner, l->id);
        m.item = l->item;
        m.itemType = l->itemType;
        m.price = l->price;
        SendTradeToHost(m);
        return l;
    }

    void Coop::UnlistItem(uint32_t listing)
    {
        if (m_tradePartner != kNoTrade && m_myStall.Remove(listing))
            SendTradeToHost(Make(TradeStage::Unlist, m_tradePartner, listing));
    }

    uint32_t Coop::BuyListing(uint32_t listing, int32_t count, int32_t caps)
    {
        const Listing* l = m_partnerStall.Find(listing);
        if (m_tradePartner == kNoTrade || !l || count < 1 || count > l->item.count || TradeCost(l->price, count) != caps)
            return 0;
        ItemCount kind = l->item;
        kind.count = count;
        uint32_t id = m_buys.Add(PendingBuy{m_tradePartner, listing, kind, caps});
        LOG_INFO("Trade: buying %d of %08X (condition %d, mods %d) from %s for %d caps (purchase %u)", count, kind.item,
            kind.health, kind.mods, NameOf(m_tradePartner).c_str(), caps, id);
        Trade m = Make(TradeStage::Buy, m_tradePartner, id);
        m.listing = listing;
        m.count = count;
        m.price = l->price;
        SendTradeToHost(m);
        return id;
    }

    void Coop::SaleTakenOut(uint32_t purchase, uint32_t listing, bool ok, TradeReason why, const ItemCount& item,
        uint8_t itemType, int32_t caps)
    {
        if (!ok)
        {
            LOG_INFO("Trade: can't sell listing %u (reason %d)", listing, static_cast<int>(why));
            SendTradeToHost(Make(TradeStage::Refuse, 0, purchase, why));
            return;
        }
        int32_t left = 0;
        m_myStall.Take(listing, item.count, left);
        if (m_tradePartner != kNoTrade)
        {
            if (left > 0)
            {
                const Listing* l = m_myStall.Find(listing);
                Trade update = Make(TradeStage::List, m_tradePartner, listing);
                update.item = l->item;
                update.itemType = l->itemType;
                update.price = l->price;
                SendTradeToHost(update);
            }
            else
                SendTradeToHost(Make(TradeStage::Unlist, m_tradePartner, listing));
        }
        uint8_t buyer = m_tradePartner;
        m_sales.Put(purchase, PendingSale{buyer, item, caps});
        Trade m = Make(TradeStage::Sold, buyer, purchase);
        m.item = item;
        m.itemType = itemType;
        SendTradeToHost(m);
    }

    void Coop::PurchaseReceived(uint32_t purchase, TradeReason reason)
    {
        PendingBuy buy;
        if (!m_buys.Finish(purchase, buy))
            return;
        if (reason != TradeReason::Ok)
        {
            TradeEvent e;
            e.kind = TradeEvent::Kind::CapsBack;
            e.player = buy.seller;
            e.id = purchase;
            e.item = buy.item;
            e.caps = buy.caps;
            e.reason = reason;
            m_tradeEvents.push_back(e);
        }
        SendTradeToHost(Make(TradeStage::Got, buy.seller, purchase, reason));
    }

    void Coop::ForgetTrades()
    {
        m_buys.Forget();
        m_sales.Forget();
        m_tradeLedger.Clear();
        m_myStall.Clear();
        m_partnerStall.Clear();
        m_tradePartner = m_tradeAsking = m_tradeAsker = kNoTrade;
        m_tradeEvents.clear();
    }

    void Coop::TradeLostHost()
    {
        RefundPendingTrades();
        uint8_t partner = m_tradePartner;
        EndTradeHere(TradeReason::PlayerLeft, false);
        m_tradeAsker = kNoTrade;
        if (partner != kNoTrade)
        {
            TradeEvent e;
            e.kind = TradeEvent::Kind::Closed;
            e.player = partner;
            e.reason = TradeReason::PlayerLeft;
            m_tradeEvents.push_back(e);
        }
    }

    void Coop::RefundPendingTrades()
    {
        for (const auto& [id, buy] : m_buys.TakeAll())
        {
            TradeEvent e;
            e.kind = TradeEvent::Kind::CapsBack;
            e.player = buy.seller;
            e.id = id;
            e.item = buy.item;
            e.caps = buy.caps;
            e.reason = TradeReason::PlayerLeft;
            m_tradeEvents.push_back(e);
        }
        for (const auto& [id, sale] : m_sales.TakeAll())
        {
            TradeEvent e;
            e.kind = TradeEvent::Kind::ItemBack;
            e.player = sale.buyer;
            e.id = id;
            e.item = sale.item;
            e.caps = sale.caps;
            e.reason = TradeReason::PlayerLeft;
            m_tradeEvents.push_back(e);
        }
    }

    void Coop::ClientTrade(const Trade& m)
    {
        TradeEvent e;
        e.player = m.other;
        e.id = m.id;
        e.reason = m.reason;
        switch (m.stage)
        {
        case TradeStage::Ask:
            if (m_tradePartner != kNoTrade || m_tradeAsking != kNoTrade || m_tradeAsker != kNoTrade)
            {
                SendTradeToHost(Make(TradeStage::Answer, m.other, 0, TradeReason::Busy));
                return;
            }
            m_tradeAsker = m.other;
            e.kind = TradeEvent::Kind::Asked;
            break;
        case TradeStage::Answer:
            if (m_tradeAsking != m.other)
                return;
            m_tradeAsking = kNoTrade;
            if (m.reason == TradeReason::Ok)
            {
                m_tradePartner = m.other;
                m_myStall.Clear();
                m_partnerStall.Clear();
                e.kind = TradeEvent::Kind::Opened;
            }
            else
                e.kind = TradeEvent::Kind::Refused;
            break;
        case TradeStage::Close:
            if (m_tradeAsker == m.other)
            {
                m_tradeAsker = kNoTrade;
                e.kind = TradeEvent::Kind::Closed;
                break;
            }
            if (m_tradePartner != m.other && m_tradeAsking != m.other)
                return;
            EndTradeHere(m.reason, false);
            e.kind = TradeEvent::Kind::Closed;
            break;
        case TradeStage::List:
        {
            if (m_tradePartner != m.other)
                return;
            m_partnerStall.Set(Listing{m.id, m.item, m.itemType, m.price});
            e.kind = TradeEvent::Kind::Listed;
            e.listing = m.id;
            e.item = m.item;
            e.itemType = m.itemType;
            e.caps = m.price;
            break;
        }
        case TradeStage::Unlist:
            if (m_tradePartner != m.other || !m_partnerStall.Remove(m.id))
                return;
            e.kind = TradeEvent::Kind::Unlisted;
            e.listing = m.id;
            break;
        case TradeStage::Buy:
        {
            const Listing* l = m_myStall.Find(m.listing);
            TradeReason no = m_tradePartner != m.other ? TradeReason::Closed : !l ? TradeReason::NotHeld :
                l->price != m.price ? TradeReason::PriceChanged : m.count > l->item.count ? TradeReason::NotHeld :
                TradeCost(l->price, m.count) < 0 ? TradeReason::BadItem : TradeReason::Ok;
            if (no != TradeReason::Ok)
            {
                LOG_INFO("Trade: refusing %s's purchase of listing %u (reason %d)", NameOf(m.other).c_str(), m.listing,
                    static_cast<int>(no));
                SendTradeToHost(Make(TradeStage::Refuse, m.other, m.id, no));
                return;
            }
            e.kind = TradeEvent::Kind::SellRequest;
            e.listing = m.listing;
            e.item = l->item;
            e.item.count = m.count;
            e.itemType = l->itemType;
            e.caps = TradeCost(l->price, m.count);
            break;
        }
        case TradeStage::Sold:
        {
            const PendingBuy* found = m_buys.Find(m.id);
            bool known = found != nullptr;
            PendingBuy buy = known ? *found : PendingBuy{};
            if (!known || !m.item.SameKind(buy.item) || m.item.count != buy.item.count)
            {
                // Nothing of ours is waiting for it (or it isn't what we bought): the seller gets it back.
                if (known)
                    PurchaseReceived(m.id, TradeReason::BadItem);
                else
                    SendTradeToHost(Make(TradeStage::Got, m.other, m.id, TradeReason::Closed));
                return;
            }
            e.kind = TradeEvent::Kind::Delivered;
            e.item = m.item;
            e.itemType = m.itemType;
            e.caps = buy.caps;
            break;
        }
        case TradeStage::Refuse:
        {
            PendingBuy buy;
            if (!m_buys.Finish(m.id, buy))
                return;
            e.kind = TradeEvent::Kind::CapsBack;
            e.player = buy.seller;
            e.item = buy.item;
            e.caps = buy.caps;
            break;
        }
        case TradeStage::Paid:
        case TradeStage::ItemBack:
        {
            PendingSale sale;
            if (!m_sales.Finish(m.id, sale))
                return;
            e.kind = m.stage == TradeStage::Paid ? TradeEvent::Kind::Paid : TradeEvent::Kind::ItemBack;
            e.player = sale.buyer;
            e.item = sale.item;
            e.caps = sale.caps;
            break;
        }
        default:
            return;
        }
        m_tradeEvents.push_back(e);
    }

    // The host: routes between the two, and keeps the list of what's on its way.

    void Coop::HostTrade(uint8_t from, const Trade& m)
    {
        uint32_t now = GetTickCount();
        switch (m.stage)
        {
        case TradeStage::Ask:
        {
            Pose a, b;
            TradeReason no = m.other == from || !IsPlayer(m.other) ? TradeReason::PlayerLeft :
                m_tradeLedger.Busy(from) || m_tradeLedger.Busy(m.other) ? TradeReason::Busy :
                !PoseOf(from, a) || !PoseOf(m.other, b) || !Within(a, b, kTradeDistance) ? TradeReason::Far :
                TradeReason::Ok;
            if (no != TradeReason::Ok)
            {
                SendTradeTo(from, Make(TradeStage::Answer, m.other, 0, no));
                return;
            }
            m_tradeLedger.Ask(from, m.other, now);
            SendTradeTo(m.other, Make(TradeStage::Ask, from));
            return;
        }
        case TradeStage::Answer:
        {
            if (!m_tradeLedger.TakeAsk(m.other, from, now))
            {
                if (m.reason == TradeReason::Ok)
                    SendTradeTo(from, Make(TradeStage::Close, m.other, 0, TradeReason::PlayerLeft));
                return;
            }
            if (m.reason == TradeReason::Ok)
                m_tradeLedger.Open(m.other, from);
            SendTradeTo(m.other, Make(TradeStage::Answer, from, 0, m.reason));
            return;
        }
        case TradeStage::Close:
        {
            for (const auto& [asker, asked] : m_tradeLedger.DropAsks(from))
                SendTradeTo(asker == from ? asked : asker, Make(TradeStage::Close, from, 0, m.reason));
            uint8_t other = m_tradeLedger.Close(from);
            if (other != kNoTrade)
                SendTradeTo(other, Make(TradeStage::Close, from, 0, m.reason));
            return;
        }
        case TradeStage::List:
        case TradeStage::Unlist:
            if (m_tradeLedger.Together(from, m.other))
            {
                Trade out = m;
                out.other = from;
                SendTradeTo(m.other, out);
            }
            return;
        case TradeStage::Buy:
        {
            uint32_t id = m_tradeLedger.Together(from, m.other) ? m_tradeLedger.OpenPurchase(from, m.id, m.other) : 0;
            if (!id)
            {
                SendTradeTo(from, Make(TradeStage::Refuse, m.other, m.id,
                    m_tradeLedger.Together(from, m.other) ? TradeReason::Busy : TradeReason::Closed));
                return;
            }
            Trade out = m;
            out.id = id;
            out.other = from;
            SendTradeTo(m.other, out);
            return;
        }
        case TradeStage::Sold:
        {
            TradeLedger::Purchase* p = m_tradeLedger.FindPurchase(m.id);
            if (!p || p->seller != from || p->sold)
            {
                SendTradeTo(from, Make(TradeStage::ItemBack, 0, m.id, TradeReason::PlayerLeft));
                return;
            }
            p->sold = true;
            Trade out = m;
            out.id = p->buyerId;
            out.other = from;
            SendTradeTo(p->buyer, out);
            return;
        }
        case TradeStage::Refuse:
        {
            TradeLedger::Purchase* p = m_tradeLedger.FindPurchase(m.id);
            if (!p || p->seller != from || p->sold)
                return;
            TradeLedger::Purchase gone = *p;
            m_tradeLedger.ClosePurchase(m.id);
            SendTradeTo(gone.buyer, Make(TradeStage::Refuse, from, gone.buyerId, m.reason));
            return;
        }
        case TradeStage::Got:
        {
            uint32_t id = m_tradeLedger.FindByBuyer(from, m.id);
            TradeLedger::Purchase* p = id ? m_tradeLedger.FindPurchase(id) : nullptr;
            if (!p || !p->sold)
                return;
            uint8_t seller = p->seller;
            m_tradeLedger.ClosePurchase(id);
            SendTradeTo(seller, Make(m.reason == TradeReason::Ok ? TradeStage::Paid : TradeStage::ItemBack, from, id,
                m.reason));
            return;
        }
        default:
            return;
        }
    }

    void Coop::TradePlayerLeft(uint8_t player)
    {
        for (const auto& [asker, asked] : m_tradeLedger.DropAsks(player))
            if (asker == player)
                SendTradeTo(asked, Make(TradeStage::Close, player, 0, TradeReason::PlayerLeft));
            else
                SendTradeTo(asker, Make(TradeStage::Answer, player, 0, TradeReason::PlayerLeft));
        uint8_t other = m_tradeLedger.Close(player);
        if (other != kNoTrade)
            SendTradeTo(other, Make(TradeStage::Close, player, 0, TradeReason::PlayerLeft));
        for (const auto& [id, p] : m_tradeLedger.PlayerLeft(player))
        {
            if (p.seller == player)
                SendTradeTo(p.buyer, Make(TradeStage::Refuse, player, p.buyerId, TradeReason::PlayerLeft));
            else if (p.sold)
                SendTradeTo(p.seller, Make(TradeStage::ItemBack, player, id, TradeReason::PlayerLeft));
        }
    }

    void Coop::SendTradeToHost(const Trade& m)
    {
        if (m_role == Role::Host)
            HostTrade(m_myId, m);
        else if (m_role == Role::Friend)
            m_net->Send(1, Encode(m), true);
    }

    void Coop::SendTradeTo(uint8_t player, const Trade& m)
    {
        if (player == m_myId)
        {
            ClientTrade(m);
            return;
        }
        for (const auto& [peer, p] : m_players)
            if (p.joined && p.id == player)
                m_net->Send(peer, Encode(m), true);
    }
}
