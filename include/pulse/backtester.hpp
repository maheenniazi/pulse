// =============================================================================
//  pulse/backtester.hpp  --  event-driven backtest harness + P&L accounting
//
//  The backtester owns one OrderBook. It pumps a Feed of market messages into
//  the book and, after each message, gives the strategy a chance to act. Every
//  order the strategy sends gets an id from a reserved high range, so when a
//  trade prints the backtester can tell whether the strategy was involved and
//  update position and cash accordingly.
//
//  P&L is marked to the book mid at each step:
//       equity = cash + position * mid
//  Starting from flat (0 cash, 0 position), final equity IS the total P&L.
// =============================================================================
#pragma once

#include "pulse/feed.hpp"
#include "pulse/order_book.hpp"
#include "pulse/strategy.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pulse {

class Backtester final : public StrategyApi {
public:
    // One row of the equity curve, written out for the Python tearsheet.
    struct Record {
        std::uint64_t step{};
        Price         mid{};
        long long     position{};
        double        cash{};
        double        equity{};
    };

    struct Metrics {
        double        pnl{};            // final mark-to-market equity
        double        sharpe{};         // per-step mean/stdev of equity changes
        double        max_drawdown{};   // largest peak-to-trough equity drop
        long long     end_position{};
        std::uint64_t fills{};          // number of strategy fills
        std::uint64_t traded_volume{};  // units the strategy traded
        std::uint64_t steps{};
    };

    explicit Backtester(Strategy& strategy);

    // Run until the feed is exhausted or `max_steps` messages are processed.
    void run(Feed& feed, std::uint64_t max_steps);

    [[nodiscard]] Metrics             metrics() const;
    [[nodiscard]] const std::vector<Record>& history() const { return history_; }
    void write_csv(const std::string& path) const;

    // --- StrategyApi ---------------------------------------------------------
    OrderId buy(Price price, Quantity qty, TimeInForce tif) override;
    OrderId sell(Price price, Quantity qty, TimeInForce tif) override;
    void    cancel(OrderId id) override;
    const OrderBook& book() const override { return book_; }

private:
    // Strategy ids live at/above this base so they never collide with market
    // ids (which the feeds draw from a low range).
    static constexpr std::uint64_t kStrategyIdBase = 1ULL << 40;

    void on_trade(const Trade& t);   // book trade callback -> P&L attribution
    [[nodiscard]] Price mark_price() const;  // mid if available, else last trade

    OrderBook              book_;
    Strategy&              strategy_;
    std::uint64_t          next_strategy_id_{kStrategyIdBase};
    long long              position_{0};
    double                 cash_{0.0};
    Price                  last_trade_price_{0};
    std::uint64_t          fills_{0};
    std::uint64_t          traded_volume_{0};
    std::vector<Record>    history_;

    [[nodiscard]] bool is_strategy_id(OrderId id) const {
        return raw(id) >= kStrategyIdBase;
    }
};

}  // namespace pulse
