// =============================================================================
//  pulse/order_book.hpp  --  a price-time-priority limit order book
//
//  DATA STRUCTURE OVERVIEW
//  -----------------------
//  A book has two sides. Each side is a sorted map from price -> PriceLevel:
//
//      bids_:  std::map<Price, PriceLevel, std::greater<>>  (best = highest)
//      asks_:  std::map<Price, PriceLevel, std::less<>>     (best = lowest)
//
//  So the best bid and best ask are always `begin()` of their map -- O(log L)
//  to insert a new price level, O(1) to find the top of book.
//
//  Each PriceLevel is a FIFO queue of orders (intrusive linked list). Orders
//  at the same price are matched in arrival order == time priority.
//
//  A flat hash map  orders_: OrderId -> Order*  gives O(1) cancel/modify: we
//  jump straight to the order node and unlink it from its level in O(1).
//
//  MATCHING RULE (price-time priority)
//  -----------------------------------
//  An incoming order first tries to *cross* with the opposite side: a buy
//  matches resting sells priced <= the buy's limit, cheapest first; a sell
//  matches resting buys priced >= the sell's limit, highest first. Within a
//  price level, the oldest resting order fills first. Any unfilled remainder
//  of a GTC limit order then rests in the book.
// =============================================================================
#pragma once

#include "pulse/object_pool.hpp"
#include "pulse/order.hpp"
#include "pulse/types.hpp"

#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pulse {

// A single price level: an intrusive FIFO of orders plus a cached aggregate.
struct PriceLevel {
    Order*   head{nullptr};   // oldest order (front of the queue)
    Order*   tail{nullptr};   // newest order (back of the queue)
    Quantity total_qty{0};    // sum of `remaining` over all orders here

    void push_back(Order* o) noexcept;
    void unlink(Order* o) noexcept;
    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
};

// Top-of-book snapshot handed to strategies / loggers.
struct BookQuote {
    std::optional<Price> bid;        // best bid price, if any
    std::optional<Price> ask;        // best ask price, if any
    Quantity             bid_qty{0}; // size resting at the best bid
    Quantity             ask_qty{0}; // size resting at the best ask
};

class OrderBook {
public:
    // Callback invoked once per trade produced by a submission.
    using TradeHandler = std::function<void(const Trade&)>;

    OrderBook() = default;
    explicit OrderBook(TradeHandler on_trade) : on_trade_(std::move(on_trade)) {}

    void set_trade_handler(TradeHandler h) { on_trade_ = std::move(h); }

    // Submit a new order. It is matched immediately against the opposite side;
    // any eligible remainder rests in the book per its time-in-force. Returns
    // the total quantity that was filled on submission.
    Quantity submit(OrderId id, Side side, OrderType type, TimeInForce tif,
                    Price price, Quantity qty);

    // Cancel a resting order by id. Returns true if it was found and removed.
    bool cancel(OrderId id);

    // Cancel/replace: change price and/or size. A price change or size *increase*
    // loses time priority (treated as cancel + new); a pure size *decrease*
    // keeps the order's place in the queue, matching real exchange semantics.
    bool modify(OrderId id, Price new_price, Quantity new_qty);

    // --- Read-only inspectors ------------------------------------------------
    [[nodiscard]] BookQuote top_of_book() const;
    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::optional<Price> mid() const;      // (bid+ask)/2, if both exist
    [[nodiscard]] std::optional<Price> spread() const;   // ask-bid, if both exist
    [[nodiscard]] std::size_t resting_count() const { return orders_.size(); }

    // One aggregated price level, for a market-depth snapshot.
    struct Level {
        Price         price{};
        Quantity      qty{};     // total resting size at this price
        std::uint32_t orders{};  // number of resting orders at this price
    };
    // Top `max_depth` levels of each side, best price first (bids high->low,
    // asks low->high). Handy for rendering depth or feeding a strategy.
    [[nodiscard]] std::vector<Level> bids(std::size_t max_depth) const;
    [[nodiscard]] std::vector<Level> asks(std::size_t max_depth) const;

    // Remaining size of a specific resting order, or nullopt if it isn't live.
    [[nodiscard]] std::optional<Quantity> order_remaining(OrderId id) const;

    // Assert internal invariants (used by tests). Returns true if consistent.
    [[nodiscard]] bool check_invariants() const;

private:
    using BidMap = std::map<Price, PriceLevel, std::greater<>>;
    using AskMap = std::map<Price, PriceLevel, std::less<>>;

    // Match `incoming` against the opposite book as far as price allows.
    // Fills are emitted through on_trade_ and `incoming->remaining` shrinks.
    template <typename OppMap>
    void match(Order* incoming, OppMap& opposite);

    // Can `incoming` be filled *completely* right now? (FOK pre-check.)
    template <typename OppMap>
    [[nodiscard]] bool fully_fillable(const Order& incoming, const OppMap& opposite) const;

    void rest(Order* o);                 // insert a resting remainder
    void emit(const Trade& t) { if (on_trade_) on_trade_(t); }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<std::uint64_t, Order*> orders_;  // id -> node (O(1) cancel)
    ObjectPool<Order> pool_;
    Sequence next_seq_{0};
    TradeHandler on_trade_;
};

}  // namespace pulse
