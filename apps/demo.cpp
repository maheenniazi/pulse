// =============================================================================
//  apps/demo.cpp  --  a tiny guided tour of the matching engine
//
//  Run it to watch orders match in real time. Prints every trade and the
//  resulting top-of-book after each submission.
// =============================================================================
#include "pulse/order_book.hpp"

#include <cstdio>

using namespace pulse;

namespace {

void print_top(const OrderBook& book, const char* label) {
    const BookQuote q = book.top_of_book();
    std::printf("  %-22s bid ", label);
    if (q.bid) std::printf("%ld x%lu", static_cast<long>(*q.bid), q.bid_qty);
    else       std::printf("  --   ");
    std::printf("   |   ask ");
    if (q.ask) std::printf("%ld x%lu", static_cast<long>(*q.ask), q.ask_qty);
    else       std::printf("  --   ");
    std::printf("\n");
}

}  // namespace

int main() {
    OrderBook book([](const Trade& t) {
        std::printf("    >> TRADE  %s %lu @ %ld  (maker #%lu, taker #%lu)\n",
                    to_string(t.taker_side), t.quantity, static_cast<long>(t.price),
                    raw(t.maker_id), raw(t.taker_id));
    });

    std::printf("Pulse matching engine -- demo\n");
    std::printf("=============================\n\n");

    std::printf("1) Build a resting book (liquidity providers quote both sides):\n");
    book.submit(OrderId{1}, Side::Buy,  OrderType::Limit, TimeInForce::GTC,  99, 100);
    book.submit(OrderId{2}, Side::Buy,  OrderType::Limit, TimeInForce::GTC,  98, 200);
    book.submit(OrderId{3}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 101, 150);
    book.submit(OrderId{4}, Side::Sell, OrderType::Limit, TimeInForce::GTC, 102, 300);
    print_top(book, "after quoting");

    std::printf("\n2) An aggressive marketable buy of 200 @ 101 crosses the ask:\n");
    book.submit(OrderId{5}, Side::Buy, OrderType::Limit, TimeInForce::GTC, 101, 200);
    print_top(book, "after the buy");
    std::printf("   (50 of order #5 rested at 101 once the 150 at 101 was consumed)\n");

    std::printf("\n3) A market sell of 120 sweeps the bids, best price first:\n");
    book.submit(OrderId{6}, Side::Sell, OrderType::Market, TimeInForce::IOC, 0, 120);
    print_top(book, "after the market sell");

    std::printf("\n4) Cancel the resting 102 ask (#4):\n");
    book.cancel(OrderId{4});
    print_top(book, "after cancel");

    std::printf("\nInvariants hold: %s\n", book.check_invariants() ? "yes" : "NO");
    return 0;
}
