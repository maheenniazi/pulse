// =============================================================================
//  apps/backtest.cpp  --  run the market maker against a synthetic market
//
//  Usage:  pulse_backtest [steps] [seed] [out.csv]
//  Writes an equity-curve CSV that python/tearsheet.py turns into metrics/plots.
// =============================================================================
#include "pulse/backtester.hpp"
#include "pulse/feed.hpp"
#include "strategy/market_maker.hpp"

#include <cstdio>
#include <string>

using namespace pulse;

int main(int argc, char** argv) {
    const std::uint64_t steps = (argc > 1) ? std::stoull(argv[1]) : 200'000;
    const std::uint64_t seed  = (argc > 2) ? std::stoull(argv[2]) : 7;
    const std::string   out   = (argc > 3) ? argv[3] : "results.csv";

    SyntheticFeed feed(seed);

    MarketMaker::Config cfg;
    cfg.quote_size        = 20;
    cfg.half_spread       = (argc > 4) ? static_cast<Price>(std::stoll(argv[4])) : 4;
    cfg.max_inventory     = 200;
    cfg.skew_per_lot      = 1;    // lean against inventory
    cfg.requote_threshold = (argc > 5) ? static_cast<Price>(std::stoll(argv[5])) : 1;
    MarketMaker mm(cfg);

    Backtester bt(mm);
    bt.run(feed, steps);
    bt.write_csv(out);

    const auto m = bt.metrics();
    std::printf("Pulse backtest -- inventory-aware market maker\n");
    std::printf("==============================================\n");
    std::printf("steps processed : %llu\n", (unsigned long long)m.steps);
    std::printf("strategy fills  : %llu\n", (unsigned long long)m.fills);
    std::printf("traded volume   : %llu units\n", (unsigned long long)m.traded_volume);
    std::printf("end position    : %lld units\n", (long long)m.end_position);
    std::printf("total P&L       : %.1f ticks-units\n", m.pnl);
    std::printf("per-step Sharpe : %.4f\n", m.sharpe);
    std::printf("max drawdown    : %.1f ticks-units\n", m.max_drawdown);
    std::printf("\nequity curve written to %s  (plot it: python3 python/tearsheet.py %s)\n",
                out.c_str(), out.c_str());
    return 0;
}
