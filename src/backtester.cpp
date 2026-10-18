// =============================================================================
//  pulse/backtester.cpp
// =============================================================================
#include "pulse/backtester.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace pulse {

Backtester::Backtester(Strategy& strategy) : strategy_(strategy) {
    book_.set_trade_handler([this](const Trade& t) { on_trade(t); });
}

void Backtester::on_trade(const Trade& t) {
    last_trade_price_ = t.price;

    // Figure out whether (and how) the strategy was involved in this trade.
    // In any single trade the taker took `taker_side`; the maker took the
    // opposite side.
    const bool taker_is_strat = is_strategy_id(t.taker_id);
    const bool maker_is_strat = is_strategy_id(t.maker_id);
    if (!taker_is_strat && !maker_is_strat) return;  // pure market-vs-market

    Side strat_side;
    if (taker_is_strat) strat_side = t.taker_side;
    else                strat_side = opposite(t.taker_side);

    const long long qty        = static_cast<long long>(t.quantity);
    const long long signed_qty = (strat_side == Side::Buy) ? qty : -qty;

    // Cash moves opposite to position: buying spends cash, selling raises it.
    position_ += signed_qty;
    cash_     -= static_cast<double>(signed_qty) * static_cast<double>(t.price);
    fills_    += 1;
    traded_volume_ += t.quantity;

    strategy_.on_fill(t.price, signed_qty);
}

Price Backtester::mark_price() const {
    if (const auto m = book_.mid()) return *m;
    return last_trade_price_;
}

OrderId Backtester::buy(Price price, Quantity qty, TimeInForce tif) {
    const OrderId id{next_strategy_id_++};
    book_.submit(id, Side::Buy, OrderType::Limit, tif, price, qty);
    return id;
}

OrderId Backtester::sell(Price price, Quantity qty, TimeInForce tif) {
    const OrderId id{next_strategy_id_++};
    book_.submit(id, Side::Sell, OrderType::Limit, tif, price, qty);
    return id;
}

void Backtester::cancel(OrderId id) { book_.cancel(id); }

void Backtester::run(Feed& feed, std::uint64_t max_steps) {
    history_.clear();
    history_.reserve(max_steps);

    for (std::uint64_t step = 0; step < max_steps; ++step) {
        auto msg = feed.next();
        if (!msg) break;

        if (msg->kind == MarketMessage::Kind::Add) {
            book_.submit(msg->id, msg->side, msg->type, msg->tif, msg->price, msg->qty);
        } else {
            book_.cancel(msg->id);
        }

        strategy_.on_tick(*this);

        const Price  mid    = mark_price();
        const double equity = cash_ + static_cast<double>(position_) * static_cast<double>(mid);
        history_.push_back(Record{.step     = step,
                                  .mid      = mid,
                                  .position = position_,
                                  .cash     = cash_,
                                  .equity   = equity});
    }
}

Backtester::Metrics Backtester::metrics() const {
    Metrics m;
    m.end_position   = position_;
    m.fills          = fills_;
    m.traded_volume  = traded_volume_;
    m.steps          = history_.size();
    if (history_.empty()) return m;

    m.pnl = history_.back().equity;

    // Sharpe on per-step equity changes (not annualised -- there is no wall
    // clock in a message-driven backtest, so this is a unit-free risk-adjusted
    // return per event). mean / stdev of the increments.
    double sum = 0.0, sumsq = 0.0;
    std::size_t n = 0;
    double peak = history_.front().equity;
    for (std::size_t i = 1; i < history_.size(); ++i) {
        const double d = history_[i].equity - history_[i - 1].equity;
        sum += d; sumsq += d * d; ++n;

        peak = std::max(peak, history_[i].equity);
        m.max_drawdown = std::max(m.max_drawdown, peak - history_[i].equity);
    }
    if (n > 1) {
        const double mean = sum / static_cast<double>(n);
        const double var  = sumsq / static_cast<double>(n) - mean * mean;
        const double sd   = var > 0.0 ? std::sqrt(var) : 0.0;
        m.sharpe = sd > 0.0 ? mean / sd : 0.0;
    }
    return m;
}

void Backtester::write_csv(const std::string& path) const {
    std::ofstream out(path);
    out << "step,mid,position,cash,equity\n";
    for (const auto& r : history_) {
        out << r.step << ',' << r.mid << ',' << r.position << ','
            << r.cash << ',' << r.equity << '\n';
    }
}

}  // namespace pulse
