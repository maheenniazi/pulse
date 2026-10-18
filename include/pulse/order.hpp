// =============================================================================
//  pulse/order.hpp  --  the resting-order node + related event types
// =============================================================================
#pragma once

#include "pulse/types.hpp"

namespace pulse {

// A single order living in the book.
//
// `prev`/`next` make Order an *intrusive* doubly-linked-list node: the order
// at a price level is threaded directly through these pointers instead of
// being stored in a separate std::list. That buys us O(1) cancellation -- we
// can unlink an order from the middle of its price-level queue without any
// search -- which is exactly what a real book needs, because cancels vastly
// outnumber trades in live markets.
struct Order {
    OrderId     id{};
    Side        side{};
    OrderType   type{};
    TimeInForce tif{};
    Price       price{};       // ignored for Market orders
    Quantity    quantity{};    // original size
    Quantity    remaining{};   // unfilled size still live in the book
    Sequence    seq{};         // arrival order -> time priority

    // Intrusive FIFO links, owned by the PriceLevel this order rests on.
    Order* prev{nullptr};
    Order* next{nullptr};
};

// Emitted every time an aggressive ("taker") order crosses a resting
// ("maker") order. Trades always print at the *resting* order's price --
// the maker set the price, the taker accepted it.
struct Trade {
    OrderId  maker_id{};
    OrderId  taker_id{};
    Side     taker_side{};   // side of the aggressor
    Price    price{};
    Quantity quantity{};
    Sequence seq{};
};

}  // namespace pulse
