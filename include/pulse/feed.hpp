// =============================================================================
//  pulse/feed.hpp  --  a stream of market messages to drive the book
//
//  A Feed hands out one MarketMessage at a time until it is exhausted. The
//  backtester pumps these into the OrderBook to recreate "the rest of the
//  market", then lets the strategy react to the resulting book.
//
//  Two feeds are provided:
//    * SyntheticFeed -- a reproducible random-walk market with liquidity
//      providers quoting around a drifting fair value plus the occasional
//      marketable order. Needs no data files, so the backtest runs anywhere.
//    * CsvFeed       -- reads messages from a file so you can replay real or
//      recorded order flow later.
// =============================================================================
#pragma once

#include "pulse/types.hpp"

#include <cstdint>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace pulse {

struct MarketMessage {
    enum class Kind : std::uint8_t { Add, Cancel };
    Kind        kind{};
    OrderId     id{};       // Add: id to assign; Cancel: id to remove
    Side        side{};     // used for Add
    OrderType   type{};     // used for Add
    TimeInForce tif{};      // used for Add
    Price       price{};    // used for Add (ignored for Market)
    Quantity    qty{};      // used for Add
};

class Feed {
public:
    virtual ~Feed() = default;
    virtual std::optional<MarketMessage> next() = 0;
};

// --- Synthetic market --------------------------------------------------------
// The fair value follows a random walk. Each step we either post a fresh limit
// order a few ticks off fair value (adding depth), cancel a random live order,
// or fire a marketable order that crosses the spread (creating trades). Ids are
// drawn from a low range so they never collide with a strategy's id range.
class SyntheticFeed : public Feed {
public:
    explicit SyntheticFeed(std::uint64_t seed = 42, Price fair0 = 10'000)
        : rng_(seed), fair0_(fair0), fair_(fair0) {}

    std::optional<MarketMessage> next() override {
        const int roll = pct_(rng_);

        // 12%: cancel a previously-added order (if we have any live ids).
        if (roll < 12 && !live_ids_.empty()) {
            const std::size_t k  = rng_() % live_ids_.size();
            const OrderId     id = live_ids_[k];
            live_ids_[k] = live_ids_.back();
            live_ids_.pop_back();
            return MarketMessage{.kind = MarketMessage::Kind::Cancel, .id = id};
        }

        // Evolve the fair value as a mean-reverting (Ornstein-Uhlenbeck-like)
        // process rather than a pure random walk: a small random shock plus a
        // gentle pull back toward fair0_. This keeps the market *stationary*
        // (prices stay in a band instead of wandering off), which is the
        // regime where a spread-capturing maker has an edge. Swap in CsvFeed
        // with real, trending flow and you'll see adverse selection bite.
        const Price dev  = fair_ - fair0_;
        const Price pull = -dev / 32;                 // reversion strength
        fair_ += pull + static_cast<Price>(step_(rng_));

        // 20%: a marketable (aggressive) order that removes liquidity.
        if (roll < 32) {
            const Side side = (rng_() & 1u) ? Side::Buy : Side::Sell;
            return MarketMessage{.kind  = MarketMessage::Kind::Add,
                                 .id    = OrderId{next_id_++},
                                 .side  = side,
                                 .type  = OrderType::Market,
                                 .tif   = TimeInForce::IOC,
                                 .price = 0,
                                 .qty   = static_cast<Quantity>(small_qty_(rng_))};
        }

        // Otherwise: a passive limit order posted a few ticks off fair value.
        const Side  side   = (rng_() & 1u) ? Side::Buy : Side::Sell;
        const Price offset  = static_cast<Price>(off_(rng_));
        const Price price   = (side == Side::Buy) ? fair_ - offset : fair_ + offset;
        const OrderId id{next_id_++};
        live_ids_.push_back(id);
        return MarketMessage{.kind  = MarketMessage::Kind::Add,
                             .id    = id,
                             .side  = side,
                             .type  = OrderType::Limit,
                             .tif   = TimeInForce::GTC,
                             .price = price,
                             .qty   = static_cast<Quantity>(qty_(rng_))};
    }

    [[nodiscard]] Price fair_value() const noexcept { return fair_; }

private:
    std::mt19937_64 rng_;
    Price           fair0_;
    Price           fair_;
    std::uint64_t   next_id_{1};
    std::vector<OrderId> live_ids_;

    std::uniform_int_distribution<int> pct_{0, 99};
    std::uniform_int_distribution<int> step_{-1, 1};     // fair-value shock
    std::uniform_int_distribution<int> off_{1, 6};       // ticks off fair value
    std::uniform_int_distribution<int> qty_{10, 60};     // passive size
    std::uniform_int_distribution<int> small_qty_{5, 25};// aggressive size
};

// --- CSV replay --------------------------------------------------------------
// Line formats:  "A <id> <B|S> <L|M> <price> <qty>"  for an add,
//                "C <id>"                             for a cancel.
class CsvFeed : public Feed {
public:
    explicit CsvFeed(const std::string& path) : in_(path) {}

    std::optional<MarketMessage> next() override {
        std::string tag;
        while (in_ >> tag) {
            if (tag == "A") {
                std::uint64_t id, qty; std::string sd, ty; long long px;
                in_ >> id >> sd >> ty >> px >> qty;
                return MarketMessage{.kind  = MarketMessage::Kind::Add,
                                     .id    = OrderId{id},
                                     .side  = (sd == "B") ? Side::Buy : Side::Sell,
                                     .type  = (ty == "M") ? OrderType::Market : OrderType::Limit,
                                     .tif   = (ty == "M") ? TimeInForce::IOC : TimeInForce::GTC,
                                     .price = static_cast<Price>(px),
                                     .qty   = static_cast<Quantity>(qty)};
            }
            if (tag == "C") {
                std::uint64_t id; in_ >> id;
                return MarketMessage{.kind = MarketMessage::Kind::Cancel, .id = OrderId{id}};
            }
            // unknown tag: skip the rest of the line
            std::getline(in_, tag);
        }
        return std::nullopt;
    }

private:
    std::ifstream in_;
};

}  // namespace pulse
