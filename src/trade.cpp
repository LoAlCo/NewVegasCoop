// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

// Trading between players: asking, the trade window, putting items up at a price and buying, and everything given
// back when a purchase falls through. The window itself is in game/trade_window.cpp, the messages in coop_trade.cpp.

#include "nvse/PluginAPI.h"
#include "plugin.h"

#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"

#include "coop/clock.h"
#include "coop/gifts.h"
#include "coop/trade.h"
#include "game/addresses.h"
#include "game/trade_window.h"
#include "log.h"

#include <deque>
#include <string>
#include <vector>

namespace
{
    using Kind = coop::Coop::TradeEvent::Kind;
    constexpr uint8_t kNone = coop::Coop::kNoTrade;
    constexpr int kTradeFriendQuestion = 19, kTradeAskQuestion = 20, kTradePriceQuestion = 21, kTradeBuyQuestion = 22;
    constexpr int kMaxOpenTries = 5;

    std::vector<uint8_t> g_tradeFriends;
    bool g_pickingFriend = false;
    uint8_t g_askFrom = kNone;
    uint32_t g_askUntil = 0;
    bool g_windowWanted = false, g_closingOurselves = false, g_wasShown = false, g_stallDirty = false;
    uint32_t g_nextOpenTry = 0, g_nextDistanceCheck = 0, g_nextStallCheck = 0;
    int g_openTries = 0;

    struct ToPrice
    {
        net::ItemCount item;
        uint8_t itemType = 0;
    };
    std::deque<ToPrice> g_toPrice;
    // Sales wait while the screen's "how many?" is up: it points at a stack, which taking an item out can free.
    std::vector<coop::Coop::TradeEvent> g_sales;
    bool g_pricing = false;
    std::string g_priceShown;
    bool g_priceListed = false;
    bool g_buying = false;
    game::TradePick g_buyPick;

    std::string Many(int32_t count, const std::string& name)
    {
        return count == 1 ? name : std::to_string(count) + " x " + name;
    }

    std::string PriceWords(int32_t caps) { return caps == 0 ? "free" : std::to_string(caps) + " caps"; }

    const char* ReasonWords(net::TradeReason reason)
    {
        switch (reason)
        {
        case net::TradeReason::No: return "said no";
        case net::TradeReason::Busy: return "is busy";
        case net::TradeReason::Far: return "is too far away (trading needs you within about 20 metres)";
        case net::TradeReason::TurnedOff: return "has trading turned off";
        case net::TradeReason::PlayerLeft: return "left";
        case net::TradeReason::NotHeld: return "no longer has it";
        case net::TradeReason::PriceChanged: return "changed the price";
        case net::TradeReason::BadItem: return "couldn't take it";
        case net::TradeReason::Closed: return "closed the trade";
        default: return "couldn't";
        }
    }

    bool Near(uint8_t player)
    {
        coop::Pose mine, theirs;
        return g_coop.PlayerPose(g_coop.MyPlayerId(), mine) && g_coop.PlayerPose(player, theirs) &&
            coop::Within(mine, theirs, coop::kTradeDistance);
    }

    int32_t PartnerCapsShown() { return g_coop.PartnerCaps() > 0 ? g_coop.PartnerCaps() : 0; }

    std::vector<game::StallEntry> OurEntries()
    {
        std::vector<game::StallEntry> out;
        for (const coop::Listing& l : g_coop.MyStall().All())
            out.push_back({l.item, l.price});
        return out;
    }

    std::vector<game::StallEntry> PartnerEntries()
    {
        std::vector<game::StallEntry> out;
        for (const coop::Listing& l : g_coop.PartnerStall().All())
            out.push_back({l.item, l.price});
        return out;
    }

    int32_t KindCount(TESObjectREFR* me, const net::ItemCount& kind)
    {
        std::vector<game::Item> items;
        if (!game::Contents(me, items))
            return -1;
        int32_t n = 0;
        for (const game::Item& i : items)
            if (i.id == kind.item && i.health == kind.health && i.mods == kind.mods)
                n += i.count;
        return n;
    }

    void AddCaps(TESObjectREFR* me, int32_t caps)
    {
        if (me && caps > 0)
            game::ChangeItem(me, game::ids::kCaps, caps);
    }

    void StartSaveTimer(uint32_t now)
    {
        if (g_config.giftSave)
            g_giftSaveTimer.Delivered(now);
    }

    void CloseWindow()
    {
        if (game::TradeWindowShown())
        {
            g_closingOurselves = true;
            game::CloseTradeWindow();
        }
    }

    void AskTradeWith(uint8_t player)
    {
        std::string name = g_coop.NameOf(player);
        if (!Near(player))
        {
            game::ShowMessage(name + " is too far away. Trading needs you within about 20 metres.");
            return;
        }
        if (g_coop.AskTrade(player))
            game::Notify(("Co-op: asking " + name + " to trade...").c_str());
        else
            game::ShowMessage("Couldn't ask " + name + " to trade right now.");
    }

    // Puts the caps for each picked listing in escrow and asks for it.
    void Buy(TESObjectREFR* me, const game::TradePick& pick)
    {
        const coop::Listing* l = g_coop.PartnerStall().FindKind(pick.item);
        std::string name = game::ItemName(pick.item.item);
        if (!l)
            return;
        int32_t count = pick.item.count < l->item.count ? pick.item.count : l->item.count;
        int32_t cost = coop::TradeCost(l->price, count);
        TESForm* caps = game::LookupForm(game::ids::kCaps);
        int have = caps ? game::ItemCount(me, caps) : 0;
        if (cost < 0 || !caps || cost > have)
        {
            game::ShowMessage("You don't have enough caps for " + Many(count, name) + " (" + PriceWords(cost) + ").");
            return;
        }
        if (cost > 0)
        {
            game::ChangeItem(me, caps->refID, -cost);
            if (game::ItemCount(me, caps) != have - cost)
            {
                LOG_WARN("Trade: couldn't take %d caps out for %s; putting back what came out", cost, name.c_str());
                game::ChangeItem(me, caps->refID, have - game::ItemCount(me, caps));
                return;
            }
        }
        if (!g_coop.BuyListing(l->id, count, cost))
        {
            AddCaps(me, cost);
            game::ShowMessage("Couldn't buy " + Many(count, name) + " right now.");
            return;
        }
        game::Notify(("Co-op: buying " + Many(count, name) + " for " + PriceWords(cost) + "...").c_str());
        g_stallDirty = true;
    }

    void Sell(TESObjectREFR* me, const coop::Coop::TradeEvent& e)
    {
        net::ItemCount kind = e.item;
        std::string name = game::ItemName(kind.item);
        int32_t before = KindCount(me, kind);
        const char* why = game::WhyNotTradeable(kind);
        if (why || before < kind.count)
        {
            LOG_INFO("Trade: can't sell %d of %08X to %s (%s, have %d)", kind.count, kind.item,
                g_coop.NameOf(e.player).c_str(), why ? why : "not enough", before);
            g_coop.SaleTakenOut(e.id, e.listing, false, net::TradeReason::NotHeld, kind, e.itemType, e.caps);
            return;
        }
        game::TakeOffForTrade(kind);
        net::ItemCount out = kind;
        out.count = -kind.count;
        game::ChangeItem(me, out);
        int32_t after = KindCount(me, kind);
        if (after != before - kind.count)
        {
            LOG_WARN("Trade: only %d of %d of %08X came out; putting them back and not selling", before - after,
                kind.count, kind.item);
            if (after >= 0 && after < before)
            {
                net::ItemCount back = kind;
                back.count = before - after;
                game::ChangeItem(me, back);
            }
            g_coop.SaleTakenOut(e.id, e.listing, false, net::TradeReason::NotHeld, kind, e.itemType, e.caps);
            return;
        }
        LOG_INFO("Trade: took out %d of %08X (condition %d, mods %d) for %s, who pays %d caps", kind.count, kind.item,
            kind.health, kind.mods, g_coop.NameOf(e.player).c_str(), e.caps);
        g_coop.SaleTakenOut(e.id, e.listing, true, net::TradeReason::Ok, kind, e.itemType, e.caps);
        g_stallDirty = true;
        game::RefreshItemList();
    }

    void Receive(TESObjectREFR* me, const coop::Coop::TradeEvent& e, uint32_t now)
    {
        std::string name = game::ItemName(e.item.item);
        if (!game::IsItem(e.item.item) || game::FormTypeOf(e.item.item) != e.itemType)
        {
            LOG_WARN("Trade: %08X from %s isn't an item of type %u here: refused", e.item.item,
                g_coop.NameOf(e.player).c_str(), e.itemType);
            g_coop.PurchaseReceived(e.id, net::TradeReason::BadItem);
            return;
        }
        game::ChangeItem(me, e.item);
        LOG_INFO("Trade: got %d of %08X (condition %d, mods %d) from %s for %d caps (purchase %u)", e.item.count,
            e.item.item, e.item.health, e.item.mods, g_coop.NameOf(e.player).c_str(), e.caps, e.id);
        g_coop.PurchaseReceived(e.id, net::TradeReason::Ok);
        game::Notify(("Co-op: you bought " + Many(e.item.count, name) + " from " + g_coop.NameOf(e.player) + " (" +
            PriceWords(e.caps) + ")").c_str());
        g_stallDirty = true;
        StartSaveTimer(now);
        game::RefreshItemList();
    }

    void HandleEvents(TESObjectREFR* me, uint32_t now)
    {
        for (const coop::Coop::TradeEvent& e : g_coop.TakeTradeEvents())
        {
            std::string who = g_coop.NameOf(e.player);
            switch (e.kind)
            {
            case Kind::Asked:
                if (!g_config.trading)
                    g_coop.AnswerTrade(e.player, false, net::TradeReason::TurnedOff);
                else if (g_askFrom != kNone || g_pricing || game::TradeWindowExists())
                    g_coop.AnswerTrade(e.player, false, net::TradeReason::Busy);
                else if (game::AskYesNo(who + " wants to trade with you. Open the trade window?", kTradeAskQuestion))
                {
                    g_askFrom = e.player;
                    g_askUntil = now + coop::kTradeAskMs;
                }
                else
                    g_coop.AnswerTrade(e.player, false, net::TradeReason::Busy);
                break;
            case Kind::Opened:
                game::Notify(("Co-op: trading with " + who).c_str());
                g_windowWanted = true;
                g_openTries = 0;
                g_nextOpenTry = 0;
                break;
            case Kind::Refused:
                game::Notify(("Co-op: " + who + " " + ReasonWords(e.reason)).c_str());
                break;
            case Kind::Closed:
                if (g_askFrom == e.player)
                {
                    game::DropQuestion(kTradeAskQuestion);
                    g_askFrom = kNone;
                }
                CloseWindow();
                g_windowWanted = false;
                g_toPrice.clear();
                game::Notify(("Co-op: the trade with " + who + " ended" + (e.reason == net::TradeReason::Closed ? "" :
                    std::string(" (") + who + " " + ReasonWords(e.reason) + ")")).c_str());
                break;
            case Kind::Listed:
                g_stallDirty = true;
                game::Notify(("Co-op: " + who + " put up " + Many(e.item.count, game::ItemName(e.item.item)) + " (" +
                    PriceWords(e.caps) + (e.caps ? " each" : "") + ")").c_str());
                break;
            case Kind::Unlisted:
                g_stallDirty = true;
                break;
            case Kind::SellRequest:
                if (!me)
                    g_coop.SaleTakenOut(e.id, e.listing, false, net::TradeReason::NotHeld, e.item, e.itemType, e.caps);
                else if (game::QuantityPromptOpen())
                    g_sales.push_back(e);
                else
                    Sell(me, e);
                break;
            case Kind::PartnerCaps:
                g_stallDirty = true;
                break;
            case Kind::Delivered:
                if (me)
                    Receive(me, e, now);
                else
                    g_coop.PurchaseReceived(e.id, net::TradeReason::BadItem);
                break;
            case Kind::CapsBack:
                AddCaps(me, e.caps);
                LOG_INFO("Trade: purchase %u of %08X fell through (reason %d); %d caps back", e.id, e.item.item,
                    static_cast<int>(e.reason), e.caps);
                game::Notify(("Co-op: couldn't buy " + game::ItemName(e.item.item) + ": " + who + " " +
                    ReasonWords(e.reason) + (e.caps ? ". Your caps are back" : "")).c_str());
                game::RefreshItemList();
                break;
            case Kind::Paid:
                AddCaps(me, e.caps);
                LOG_INFO("Trade: %s paid %d caps for %d of %08X", who.c_str(), e.caps, e.item.count, e.item.item);
                game::Notify(("Co-op: " + who + " bought " + Many(e.item.count, game::ItemName(e.item.item)) +
                    (e.caps ? " for " + PriceWords(e.caps) : std::string(" (free)"))).c_str());
                StartSaveTimer(now);
                g_stallDirty = true;
                game::RefreshItemList();
                break;
            case Kind::ItemBack:
                if (me)
                    game::ChangeItem(me, e.item);
                LOG_INFO("Trade: %d of %08X given back to us (reason %d)", e.item.count, e.item.item,
                    static_cast<int>(e.reason));
                game::Notify(("Co-op: " + who + " didn't get " + Many(e.item.count, game::ItemName(e.item.item)) +
                    ", so you have it back").c_str());
                g_stallDirty = true;
                game::RefreshItemList();
                break;
            }
        }
    }

    std::string PriceBoxText(const ToPrice& p)
    {
        std::string now;
        if (const coop::Listing* l = g_coop.MyStall().FindKind(p.item))
            now = "Up now: " + std::to_string(l->item.count) + " at " + PriceWords(l->price) + (l->price ? " each" : "") +
                ".\n";
        return "Put up " + Many(p.item.count, game::ItemName(p.item.item)) + " for " +
            g_coop.NameOf(g_coop.TradePartner()) + "\n" + now + "\nPrice each, in caps: " + game::TextEntryText() +
            "_\n\nEnter alone puts it up for free. Esc cancels.";
    }

    void StartPricing()
    {
        if (!game::StartTextEntry())
        {
            g_toPrice.clear();
            game::ShowMessage("Couldn't take typing in the game (see NVCoop.log).");
            return;
        }
        g_priceShown = PriceBoxText(g_toPrice.front());
        g_priceListed = g_coop.MyStall().FindKind(g_toPrice.front().item) != nullptr;
        if (game::AskChoice(g_priceShown, {g_priceListed ? "Take it down" : "Cancel"}, kTradePriceQuestion))
            g_pricing = true;
        else
        {
            game::StopTextEntry();
            g_toPrice.pop_front();
        }
    }

    void UpdatePricing()
    {
        game::EntryState state = game::TextEntryState();
        int box = game::TakeChoice(kTradePriceQuestion);
        if (state == game::EntryState::Typing && box == -1)
        {
            std::string text = PriceBoxText(g_toPrice.front());
            if (text != g_priceShown)
            {
                game::SetQuestionText(kTradePriceQuestion, text);
                g_priceShown = text;
            }
            return;
        }
        g_pricing = false;
        game::DropQuestion(kTradePriceQuestion);
        ToPrice p = g_toPrice.front();
        g_toPrice.pop_front();
        if (state != game::EntryState::Done)
        {
            game::StopTextEntry();
            const coop::Listing* l = g_coop.MyStall().FindKind(p.item);
            if (box == 0 && g_priceListed && l)
            {
                g_coop.UnlistItem(l->id);
                g_stallDirty = true;
                game::Notify(("Co-op: took down " + game::ItemName(p.item.item)).c_str());
            }
            return;
        }
        std::string typed = game::TakeTextEntry();
        int32_t price = 0;
        coop::Amount parsed = typed.find_first_not_of(' ') == std::string::npos ? coop::Amount::Zero :
            coop::ParseAmount(typed, net::kMaxItemCount, price);
        if (parsed == coop::Amount::NotANumber || parsed == coop::Amount::TooMany)
        {
            game::ShowMessage("That isn't a price, so " + game::ItemName(p.item.item) + " wasn't put up.");
            return;
        }
        if (parsed == coop::Amount::Zero)
            price = 0;
        g_stallDirty = true;
        if (g_coop.ListItem(p.item, p.itemType, price))
            game::Notify(("Co-op: put up " + Many(p.item.count, game::ItemName(p.item.item)) + " (" + PriceWords(price) +
                (price ? " each" : "") + ")").c_str());
        else
            game::ShowMessage("Couldn't put up " + game::ItemName(p.item.item) + " (your stall may be full).");
    }

    std::string BuyBoxText(const game::TradePick& pick)
    {
        const coop::Listing* l = g_coop.PartnerStall().FindKind(pick.item);
        int32_t cost = l ? coop::TradeCost(l->price, pick.item.count) : -1;
        return "Buy " + Many(pick.item.count, game::ItemName(pick.item.item)) + " from " +
            g_coop.NameOf(g_coop.TradePartner()) + " for " + PriceWords(cost < 0 ? 0 : cost) + "?";
    }

    // A line clicked in the window, after the screen's own "how many?": ours goes up for sale, theirs is offered.
    void TakeClicks(TESObjectREFR* me)
    {
        std::vector<game::TradePick> accepted;
        game::TakeTradeAccept(accepted);
        if (g_pricing || g_buying)
            return;
        game::TradePick pick;
        if (!game::TakeTradeClick(pick))
            return;
        if (pick.fromPartner)
        {
            if (!g_coop.PartnerStall().FindKind(pick.item))
                return;
            g_buyPick = pick;
            g_buying = game::AskYesNo(BuyBoxText(pick), kTradeBuyQuestion);
            return;
        }
        if (const char* why = game::WhyNotTradeable(pick.item))
        {
            game::ShowMessage("Can't put up " + game::ItemName(pick.item.item) + ": " + why + ".");
            return;
        }
        (void)me;
        g_toPrice.push_back({pick.item, pick.itemType});
    }

    void UpdateBuying(TESObjectREFR* me)
    {
        int answer = game::TakeAnswer(kTradeBuyQuestion);
        if (answer < 0)
            return;
        g_buying = false;
        if (answer == 1)
            Buy(me, g_buyPick);
    }

    // Our listings never claim more than we hold: anything used, dropped or sold elsewhere comes off.
    void CheckOurStall(TESObjectREFR* me)
    {
        std::vector<coop::Listing> listings = g_coop.MyStall().All();
        for (const coop::Listing& l : listings)
        {
            int32_t held = KindCount(me, l.item);
            if (held < 0 || held >= l.item.count)
                continue;
            LOG_INFO("Trade: listing %u of %08X says %d but we hold %d; correcting it", l.id, l.item.item, l.item.count,
                held);
            g_stallDirty = true;
            if (held == 0)
                g_coop.UnlistItem(l.id);
            else
            {
                net::ItemCount kind = l.item;
                kind.count = held;
                g_coop.ListItem(kind, l.itemType, l.price);
            }
        }
    }

    void ResetTradeUi()
    {
        if (g_pricing)
            game::StopTextEntry();
        if (g_pickingFriend)
            game::DropQuestion(kTradeFriendQuestion);
        if (g_askFrom != kNone)
            game::DropQuestion(kTradeAskQuestion);
        if (g_pricing)
            game::DropQuestion(kTradePriceQuestion);
        if (g_buying)
            game::DropQuestion(kTradeBuyQuestion);
        CloseWindow();
        g_pickingFriend = g_pricing = g_buying = g_windowWanted = g_stallDirty = false;
        g_askFrom = kNone;
        g_toPrice.clear();
        TESObjectREFR* me = game::Player();
        for (const coop::Coop::TradeEvent& e : g_sales)
            if (me && !game::TradeWindowShown())
                Sell(me, e);
            else
                g_coop.SaleTakenOut(e.id, e.listing, false, net::TradeReason::NotHeld, e.item, e.itemType, e.caps);
        g_sales.clear();
    }
}

void TradeMenuPicked(TESObjectREFR* me)
{
    (void)me;
    if (!game::TradeWindowReady())
    {
        game::ShowMessage("Trading isn't available: the mod couldn't take over the barter screen (see NVCoop.log).");
        return;
    }
    if (g_coop.TradePartner() != kNone || g_coop.TradeAsking() != kNone)
    {
        game::ShowMessage("You're already trading.");
        return;
    }
    g_tradeFriends.clear();
    std::vector<std::string> buttons;
    for (const auto& [id, remote] : g_coop.Remotes())
        if (g_tradeFriends.size() < 9 && Near(id))
        {
            g_tradeFriends.push_back(id);
            buttons.push_back(g_coop.NameOf(id));
        }
    if (g_tradeFriends.empty())
    {
        game::ShowMessage("Nobody is close enough to trade with. Trading needs you within about 20 metres.");
        return;
    }
    if (g_tradeFriends.size() == 1)
    {
        AskTradeWith(g_tradeFriends[0]);
        return;
    }
    buttons.push_back("Cancel");
    g_pickingFriend = game::AskChoice("Trade with who?", buttons, kTradeFriendQuestion);
}

void StopTrade()
{
    // A save is loading or the game is going to the main menu: the world, the chest and anything waiting go with it.
    g_sales.clear();
    if (g_pricing)
        game::StopTextEntry();
    g_pickingFriend = g_pricing = g_buying = g_windowWanted = g_stallDirty = g_closingOurselves = g_wasShown = false;
    g_askFrom = kNone;
    g_toPrice.clear();
    game::ForgetTradeWindow();
}

void UpdateTrade(uint32_t now)
{
    TESObjectREFR* me = game::Player();
    HandleEvents(me, now);
    game::TidyTradeWindow();
    if (!g_coop.InGame() || !me)
    {
        ResetTradeUi();
        return;
    }
    if (g_pickingFriend)
    {
        int choice = game::TakeChoice(kTradeFriendQuestion);
        if (choice != -1)
        {
            g_pickingFriend = false;
            if (choice >= 0 && static_cast<size_t>(choice) < g_tradeFriends.size())
                AskTradeWith(g_tradeFriends[choice]);
        }
    }
    if (g_askFrom != kNone)
    {
        int answer = game::TakeAnswer(kTradeAskQuestion);
        if (answer >= 0 || coop::IsDue(now, g_askUntil))
        {
            if (answer < 0)
                game::DropQuestion(kTradeAskQuestion);
            uint8_t from = g_askFrom;
            g_askFrom = kNone;
            bool yes = answer == 1 && g_config.trading && Near(from);
            g_coop.AnswerTrade(from, yes, answer == 1 ? net::TradeReason::Far : net::TradeReason::No);
        }
    }

    uint8_t partner = g_coop.TradePartner();
    if (partner == kNone)
    {
        if (g_windowWanted || game::TradeWindowShown() || !g_toPrice.empty())
            ResetTradeUi();
        g_wasShown = false;
        return;
    }
    if (coop::IsDue(now, g_nextDistanceCheck))
    {
        g_nextDistanceCheck = now + 500;
        coop::Pose mine, theirs;
        bool known = g_coop.PlayerPose(g_coop.MyPlayerId(), mine) && g_coop.PlayerPose(partner, theirs);
        if (known && !coop::Within(mine, theirs, coop::kTradeDistance * 1.2f))
        {
            LOG_INFO("Trade: %s is too far away now; closing", g_coop.NameOf(partner).c_str());
            game::Notify(("Co-op: the trade with " + g_coop.NameOf(partner) + " ended (too far apart)").c_str());
            g_coop.CloseTrade(net::TradeReason::Far);
            ResetTradeUi();
            return;
        }
    }

    TakeClicks(me);
    if (g_buying)
        UpdateBuying(me);
    if (!g_sales.empty() && !game::QuantityPromptOpen())
    {
        for (const coop::Coop::TradeEvent& e : g_sales)
            Sell(me, e);
        g_sales.clear();
    }
    if (coop::IsDue(now, g_nextStallCheck))
    {
        g_nextStallCheck = now + 1000;
        TESForm* caps = game::LookupForm(game::ids::kCaps);
        if (caps)
            g_coop.ShareMyCaps(game::ItemCount(me, caps));
        if (!g_pricing && !game::QuantityPromptOpen())
            CheckOurStall(me);
    }
    bool shown = game::TradeWindowShown();
    if (g_wasShown && !shown && !g_closingOurselves)
    {
        LOG_INFO("Trade: we closed the window; ending the trade");
        g_coop.CloseTrade(net::TradeReason::Closed);
        ResetTradeUi();
        g_wasShown = false;
        return;
    }
    g_wasShown = shown;

    if (g_pricing)
        UpdatePricing();
    else if (!g_toPrice.empty() && !g_buying)
        StartPricing();

    game::SetOurListings(OurEntries());
    if (shown && g_stallDirty && game::RefreshTradeWindow(PartnerEntries(), PartnerCapsShown()))
        g_stallDirty = false;

    if (g_windowWanted && !shown && !g_pricing && !g_buying && !game::TradeWindowExists() &&
        !game::TradeMenuOpen() && !game::QuestionOpen() && coop::IsDue(now, g_nextOpenTry))
    {
        g_nextOpenTry = now + 500;
        if (game::OpenTradeWindow(g_coop.NameOf(partner) + "'s stall", PartnerEntries(), PartnerCapsShown()))
        {
            g_windowWanted = g_stallDirty = g_closingOurselves = false;
            g_openTries = 0;
        }
        else if (++g_openTries >= kMaxOpenTries)
        {
            LOG_WARN("Trade: couldn't open the trade window %d times; ending the trade", g_openTries);
            game::ShowMessage("Couldn't open the trade window (see NVCoop.log).");
            g_coop.CloseTrade(net::TradeReason::Closed);
            ResetTradeUi();
        }
    }
}
