// =============================================================================
//  bench/bench_order_book.cpp  --  throughput + latency-percentile benchmark
//
//  Quant/HFT work is judged in nanoseconds and tail percentiles, not averages
//  ("my p99 matters more than my mean"). This measures:
//    * throughput  -- orders processed per second
//    * per-op latency distribution -- p50 / p90 / p99 / p99.9 / max
//
//  It builds a realistic workload: a mix of passive orders that rest and
//  aggressive orders that cross, so the hot path exercises both insertion and
//  matching.
// =============================================================================
#include "pulse/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pulse;
using Clock = std::chrono::steady_clock;

namespace {

struct Msg {
    Side     side;
    Price    price;
    Quantity qty;
};

std::vector<Msg> make_workload(std::size_t n) {
    std::mt19937_64 rng(0xC0FFEE);
    std::normal_distribution<double>   price_d(10'000.0, 15.0);  // ~centered book
    std::uniform_int_distribution<int> qty_d(1, 50);
    std::uniform_int_distribution<int> side_d(0, 1);

    std::vector<Msg> w;
    w.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        w.push_back({side_d(rng) ? Side::Buy : Side::Sell,
                     static_cast<Price>(price_d(rng)),
                     static_cast<Quantity>(qty_d(rng))});
    }
    return w;
}

double percentile(std::vector<double>& xs, double p) {
    if (xs.empty()) return 0.0;
    const auto idx = static_cast<std::size_t>(
        p / 100.0 * static_cast<double>(xs.size() - 1));
    std::nth_element(xs.begin(), xs.begin() + static_cast<long>(idx), xs.end());
    return xs[idx];
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n = (argc > 1) ? std::stoull(argv[1]) : 2'000'000;
    const auto workload  = make_workload(n);

    OrderBook book;
    std::uint64_t volume = 0;
    book.set_trade_handler([&](const Trade& t) { volume += t.quantity; });

    std::vector<double> latencies_ns;
    latencies_ns.reserve(n);

    const auto t0 = Clock::now();
    std::uint64_t id = 1;
    for (const auto& m : workload) {
        const auto s = Clock::now();
        book.submit(OrderId{id++}, m.side, OrderType::Limit, TimeInForce::GTC, m.price, m.qty);
        const auto e = Clock::now();
        latencies_ns.push_back(
            std::chrono::duration<double, std::nano>(e - s).count());
    }
    const auto t1 = Clock::now();

    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("Pulse order-book benchmark\n");
    std::printf("==========================\n");
    std::printf("messages        : %zu\n", n);
    std::printf("wall time       : %.3f s\n", secs);
    std::printf("throughput      : %.2f M orders/s\n",
                static_cast<double>(n) / secs / 1e6);
    std::printf("matched volume  : %llu units\n", static_cast<unsigned long long>(volume));
    std::printf("resting at end  : %zu orders\n", book.resting_count());
    std::printf("\nper-order latency (nanoseconds)\n");
    std::printf("  p50   : %8.1f\n", percentile(latencies_ns, 50.0));
    std::printf("  p90   : %8.1f\n", percentile(latencies_ns, 90.0));
    std::printf("  p99   : %8.1f\n", percentile(latencies_ns, 99.0));
    std::printf("  p99.9 : %8.1f\n", percentile(latencies_ns, 99.9));
    std::printf("  max   : %8.1f\n",
                *std::max_element(latencies_ns.begin(), latencies_ns.end()));
    return 0;
}
