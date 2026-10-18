// =============================================================================
//  tests/test_order_book.cpp  --  correctness tests for the matching engine
// =============================================================================
#include "pulse/order_book.hpp"
#include "test_framework.hpp"

#include <random>
#include <vector>

using namespace pulse;

namespace {

// Small helper: collect every trade a book produces into a vector.
struct Recorder {
    std::vector<Trade> trades;
    void operator()(const Trade& t) { trades.push_back(t); }
    Quantity volume() const {
        Quantity v = 0;
        for (const auto& t : trades) v += t.quantity;
        return v;
    }
};

}  // namespace

TEST(resting_order_sets_top_of_book) {
    OrderBook book;
    book.submit(OrderId{1}, Side::Buy,  OrderType::Limit, TimeInForce::GTC, 100, 10);
    book.submit(OrderId{2}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 102, 7);

    CHECK_EQ(book.best_bid().value(), 100);
    CHECK_EQ(book.best_ask().value(), 102);
    CHECK_EQ(book.mid().value(), 101);
    CHECK_EQ(book.spread().value(), 2);
    CHECK(book.check_invariants());
}

TEST(crossing_limit_matches_and_prints_at_maker_price) {
    OrderBook book;
    Recorder rec;
    book.set_trade_handler(std::ref(rec));

    // Resting sell at 101 for 5.
    book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 5);
    // Aggressive buy at 105 for 5 -> should fill 5 at the maker's 101.
    const Quantity filled =
        book.submit(OrderId{2}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 105, 5);

    CHECK_EQ(filled, 5u);
    CHECK_EQ(rec.trades.size(), 1u);
    CHECK_EQ(rec.trades[0].price, 101);      // trade prints at resting price
    CHECK_EQ(rec.trades[0].quantity, 5u);
    CHECK_EQ(book.resting_count(), 0u);      // both orders fully consumed
    CHECK(book.check_invariants());
}

TEST(price_priority_best_price_fills_first) {
    OrderBook book;
    Recorder rec;
    book.set_trade_handler(std::ref(rec));

    // Two resting sells: 100 is a better (cheaper) ask than 101.
    book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 5);
    book.submit(OrderId{2}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 5);

    // Buy 6 @ 101: takes all 5 at 100 first, then 1 at 101.
    book.submit(OrderId{3}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 101, 6);

    CHECK_EQ(rec.trades.size(), 2u);
    CHECK_EQ(rec.trades[0].price, 100);      // best price first
    CHECK_EQ(rec.trades[0].quantity, 5u);
    CHECK_EQ(rec.trades[1].price, 101);
    CHECK_EQ(rec.trades[1].quantity, 1u);
    CHECK(book.check_invariants());
}

TEST(time_priority_oldest_fills_first_at_same_price) {
    OrderBook book;
    Recorder rec;
    book.set_trade_handler(std::ref(rec));

    // Two resting sells at the same price; id 1 arrived first.
    book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 5);
    book.submit(OrderId{2}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 5);

    book.submit(OrderId{3}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 5);

    CHECK_EQ(rec.trades.size(), 1u);
    CHECK_EQ(rec.trades[0].maker_id, OrderId{1});  // oldest resting order fills first
    CHECK(book.check_invariants());
}

TEST(market_order_sweeps_until_filled_or_dry) {
    OrderBook book;
    Recorder rec;
    book.set_trade_handler(std::ref(rec));

    book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 3);
    book.submit(OrderId{2}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 3);

    // Market buy of 4: 3 @ 100 then 1 @ 101; price is irrelevant to a market order.
    const Quantity filled =
        book.submit(OrderId{3}, Side::Buy, OrderType::Market, TimeInForce::IOC, 0, 4);

    CHECK_EQ(filled, 4u);
    CHECK_EQ(rec.volume(), 4u);
    CHECK_EQ(book.best_ask().value(), 101);   // 2 left resting at 101
    CHECK_EQ(book.top_of_book().ask_qty, 2u);
    CHECK(book.check_invariants());
}

TEST(ioc_fills_what_it_can_and_cancels_the_rest) {
    OrderBook book;
    book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 3);

    // IOC buy for 10: fills 3, the other 7 are cancelled (never rest).
    const Quantity filled =
        book.submit(OrderId{2}, Side::Buy, OrderType::Limit, TimeInForce::IOC, 100, 10);

    CHECK_EQ(filled, 3u);
    CHECK_EQ(book.resting_count(), 0u);       // nothing left on either side
    CHECK(book.check_invariants());
}

TEST(fok_all_or_nothing) {
    {   // Not enough liquidity -> zero fills, book untouched.
        OrderBook book;
        Recorder rec;
        book.set_trade_handler(std::ref(rec));
        book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 3);

        const Quantity filled =
            book.submit(OrderId{2}, Side::Buy, OrderType::Limit, TimeInForce::FOK, 100, 10);
        CHECK_EQ(filled, 0u);
        CHECK_EQ(rec.trades.size(), 0u);               // not a single partial print
        CHECK_EQ(book.best_ask().value(), 100);        // resting sell still intact
    }
    {   // Enough liquidity -> fills completely.
        OrderBook book;
        book.submit(OrderId{1}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 6);
        book.submit(OrderId{2}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 6);
        const Quantity filled =
            book.submit(OrderId{3}, Side::Buy, OrderType::Limit, TimeInForce::FOK, 101, 10);
        CHECK_EQ(filled, 10u);
    }
}

TEST(cancel_removes_order_and_collapses_empty_level) {
    OrderBook book;
    book.submit(OrderId{1}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 5);
    CHECK_EQ(book.best_bid().value(), 100);

    CHECK(book.cancel(OrderId{1}));
    CHECK(!book.best_bid().has_value());      // level gone
    CHECK(!book.cancel(OrderId{1}));          // second cancel is a no-op
    CHECK(book.check_invariants());
}

TEST(modify_size_down_keeps_priority_price_change_loses_it) {
    OrderBook book;
    Recorder rec;
    book.set_trade_handler(std::ref(rec));

    // Two bids at 100; id 1 is first in line.
    book.submit(OrderId{1}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 10);
    book.submit(OrderId{2}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 10);

    // Shrink id 1 to 4 -> keeps its spot at the front of the 100 queue.
    CHECK(book.modify(OrderId{1}, 100, 4));
    CHECK_EQ(book.top_of_book().bid_qty, 14u);

    // A sell of 1 should hit id 1 first (still oldest).
    book.submit(OrderId{3}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 1);
    CHECK_EQ(rec.trades.back().maker_id, OrderId{1});
    CHECK(book.check_invariants());
}

TEST(depth_snapshot_reports_levels_best_first) {
    OrderBook book;
    book.submit(OrderId{1}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 5);
    book.submit(OrderId{2}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 7);  // same level
    book.submit(OrderId{3}, Side::Buy, OrderType::Limit, TimeInForce::GTC,  99, 3);
    book.submit(OrderId{4}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 4);

    const auto bids = book.bids(10);
    CHECK_EQ(bids.size(), 2u);
    CHECK_EQ(bids[0].price, 100);      // best (highest) bid first
    CHECK_EQ(bids[0].qty, 12u);        // 5 + 7 aggregated
    CHECK_EQ(bids[0].orders, 2u);
    CHECK_EQ(bids[1].price, 99);

    const auto asks = book.asks(10);
    CHECK_EQ(asks.size(), 1u);
    CHECK_EQ(asks[0].price, 101);

    CHECK_EQ(book.order_remaining(OrderId{1}).value(), 5u);
    CHECK(!book.order_remaining(OrderId{999}).has_value());
}

// -----------------------------------------------------------------------------
//  Property / fuzz test: hammer the book with random operations and assert the
//  structural invariants never break. This is where subtle pointer bugs die.
// -----------------------------------------------------------------------------
TEST(randomized_operations_preserve_invariants) {
    OrderBook book;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> side_d(0, 1);
    std::uniform_int_distribution<int> price_d(95, 105);
    std::uniform_int_distribution<int> qty_d(1, 20);
    std::uniform_int_distribution<int> op_d(0, 9);

    std::vector<OrderId> live;
    std::uint64_t next_id = 1;

    for (int i = 0; i < 20000; ++i) {
        const int op = op_d(rng);
        if (op < 7 || live.empty()) {                 // ~70% submits
            const OrderId id{next_id++};
            book.submit(id,
                        side_d(rng) ? Side::Buy : Side::Sell,
                        OrderType::Limit, TimeInForce::GTC,
                        price_d(rng), static_cast<Quantity>(qty_d(rng)));
            live.push_back(id);
        } else if (op < 9) {                          // cancel a tracked id
            const std::size_t k = rng() % live.size();
            book.cancel(live[k]);
            live.erase(live.begin() + static_cast<long>(k));
        } else {                                      // modify a tracked id
            const std::size_t k = rng() % live.size();
            book.modify(live[k], price_d(rng), static_cast<Quantity>(qty_d(rng)));
        }
        if (!book.check_invariants()) {
            CHECK(false);   // fail loudly with the iteration count visible above
            return;
        }
    }
    CHECK(book.check_invariants());
}
