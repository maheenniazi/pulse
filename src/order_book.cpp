// =============================================================================
//  pulse/order_book.cpp  --  matching engine implementation
// =============================================================================
#include "pulse/order_book.hpp"

#include <algorithm>

namespace pulse {

// --- PriceLevel: pure intrusive-list surgery (no accounting here) -----------
// total_qty is maintained by OrderBook at every call site, so these two
// methods only ever re-thread pointers. Keeping them dumb makes it impossible
// for the list links and the aggregate to disagree about who did the math.
void PriceLevel::push_back(Order* o) noexcept {
    o->next = nullptr;
    o->prev = tail;
    if (tail) tail->next = o;
    else      head = o;         // first order on the level
    tail = o;
}

void PriceLevel::unlink(Order* o) noexcept {
    if (o->prev) o->prev->next = o->next;
    else         head = o->next;    // o was the front
    if (o->next) o->next->prev = o->prev;
    else         tail = o->prev;    // o was the back
    o->prev = o->next = nullptr;
}

// --- Matching ----------------------------------------------------------------
template <typename OppMap>
void OrderBook::match(Order* incoming, OppMap& opposite) {
    const bool is_market = incoming->type == OrderType::Market;

    // Walk the opposite book from its best price outward.
    while (incoming->remaining > 0 && !opposite.empty()) {
        auto         level_it    = opposite.begin();  // best opposite price
        const Price  level_price = level_it->first;

        // A limit order only crosses while the resting price is acceptable.
        // Market orders ignore price and sweep whatever is there.
        if (!is_market) {
            const bool crosses = (incoming->side == Side::Buy)
                                     ? (incoming->price >= level_price)
                                     : (incoming->price <= level_price);
            if (!crosses) break;
        }

        PriceLevel& level = level_it->second;

        // Fill against this level in strict FIFO (time-priority) order.
        while (incoming->remaining > 0 && level.head != nullptr) {
            Order*         maker = level.head;
            const Quantity fill  = std::min(incoming->remaining, maker->remaining);

            emit(Trade{.maker_id   = maker->id,
                       .taker_id   = incoming->id,
                       .taker_side = incoming->side,
                       .price      = level_price,   // trade prints at maker's price
                       .quantity   = fill,
                       .seq        = incoming->seq});

            maker->remaining    -= fill;
            incoming->remaining -= fill;
            level.total_qty     -= fill;

            if (maker->remaining == 0) {            // resting order exhausted
                level.unlink(maker);
                orders_.erase(raw(maker->id));
                pool_.deallocate(maker);
            }
        }

        if (level.empty()) {
            opposite.erase(level_it);               // no liquidity left here
        }
    }
}

template <typename OppMap>
bool OrderBook::fully_fillable(const Order& incoming, const OppMap& opposite) const {
    const bool is_market = incoming.type == OrderType::Market;
    Quantity   need       = incoming.remaining;

    for (const auto& [level_price, level] : opposite) {
        if (!is_market) {
            const bool crosses = (incoming.side == Side::Buy)
                                     ? (incoming.price >= level_price)
                                     : (incoming.price <= level_price);
            if (!crosses) break;   // prices only get worse from here
        }
        if (level.total_qty >= need) return true;
        need -= level.total_qty;
    }
    return need == 0;
}

void OrderBook::rest(Order* o) {
    orders_.emplace(raw(o->id), o);
    if (o->side == Side::Buy) {
        PriceLevel& level = bids_[o->price];
        level.push_back(o);
        level.total_qty += o->remaining;
    } else {
        PriceLevel& level = asks_[o->price];
        level.push_back(o);
        level.total_qty += o->remaining;
    }
}

Quantity OrderBook::submit(OrderId id, Side side, OrderType type, TimeInForce tif,
                           Price price, Quantity qty) {
    if (qty == 0) return 0;

    // Fill-Or-Kill is all-or-nothing: verify the whole order can fill *before*
    // printing a single trade, otherwise we'd leak partial fills on a kill.
    if (tif == TimeInForce::FOK) {
        Order probe{};
        probe.side      = side;
        probe.type      = type;
        probe.price     = price;
        probe.remaining = qty;
        const bool ok = (side == Side::Buy) ? fully_fillable(probe, asks_)
                                            : fully_fillable(probe, bids_);
        if (!ok) return 0;
    }

    Order* o     = pool_.allocate();
    o->id        = id;
    o->side      = side;
    o->type      = type;
    o->tif       = tif;
    o->price     = price;
    o->quantity  = qty;
    o->remaining = qty;
    o->seq       = next_seq_++;

    if (side == Side::Buy) match(o, asks_);
    else                   match(o, bids_);

    const Quantity filled = qty - o->remaining;

    if (o->remaining > 0 && type == OrderType::Limit && tif == TimeInForce::GTC) {
        rest(o);                 // park the remainder in the book
    } else {
        pool_.deallocate(o);     // market / IOC / (fully-filled FOK) -> nothing rests
    }
    return filled;
}

bool OrderBook::cancel(OrderId id) {
    auto it = orders_.find(raw(id));
    if (it == orders_.end()) return false;

    Order* o = it->second;
    if (o->side == Side::Buy) {
        auto lit = bids_.find(o->price);
        lit->second.total_qty -= o->remaining;
        lit->second.unlink(o);
        if (lit->second.empty()) bids_.erase(lit);
    } else {
        auto lit = asks_.find(o->price);
        lit->second.total_qty -= o->remaining;
        lit->second.unlink(o);
        if (lit->second.empty()) asks_.erase(lit);
    }
    orders_.erase(it);
    pool_.deallocate(o);
    return true;
}

bool OrderBook::modify(OrderId id, Price new_price, Quantity new_qty) {
    auto it = orders_.find(raw(id));
    if (it == orders_.end()) return false;
    Order* o = it->second;

    // Fast path: same price, pure size reduction -> keep time priority.
    if (new_price == o->price && new_qty != 0 && new_qty <= o->remaining) {
        const Quantity delta = o->remaining - new_qty;
        o->remaining -= delta;
        o->quantity   = new_qty;
        PriceLevel& level = (o->side == Side::Buy) ? bids_[o->price] : asks_[o->price];
        level.total_qty -= delta;
        return true;
    }

    // Slow path: price change or size increase -> cancel + re-submit, which
    // (correctly) sends the order to the back of its new queue and may even
    // cross the book. Preserve the order's other attributes.
    const Side        side = o->side;
    const OrderType   type = o->type;
    const TimeInForce tif  = o->tif;
    cancel(id);
    if (new_qty == 0) return true;      // modify-to-zero == cancel
    submit(id, side, type, tif, new_price, new_qty);
    return true;
}

// --- Inspectors --------------------------------------------------------------
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::mid() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return (bids_.begin()->first + asks_.begin()->first) / 2;  // floor mid, in ticks
}

std::optional<Price> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return asks_.begin()->first - bids_.begin()->first;
}

std::vector<OrderBook::Level> OrderBook::bids(std::size_t max_depth) const {
    std::vector<Level> out;
    out.reserve(max_depth);
    for (const auto& [price, level] : bids_) {
        if (out.size() >= max_depth) break;
        std::uint32_t n = 0;
        for (const Order* o = level.head; o != nullptr; o = o->next) ++n;
        out.push_back(Level{price, level.total_qty, n});
    }
    return out;
}

std::vector<OrderBook::Level> OrderBook::asks(std::size_t max_depth) const {
    std::vector<Level> out;
    out.reserve(max_depth);
    for (const auto& [price, level] : asks_) {
        if (out.size() >= max_depth) break;
        std::uint32_t n = 0;
        for (const Order* o = level.head; o != nullptr; o = o->next) ++n;
        out.push_back(Level{price, level.total_qty, n});
    }
    return out;
}

std::optional<Quantity> OrderBook::order_remaining(OrderId id) const {
    const auto it = orders_.find(raw(id));
    if (it == orders_.end()) return std::nullopt;
    return it->second->remaining;
}

BookQuote OrderBook::top_of_book() const {
    BookQuote q;
    if (!bids_.empty()) {
        q.bid     = bids_.begin()->first;
        q.bid_qty = bids_.begin()->second.total_qty;
    }
    if (!asks_.empty()) {
        q.ask     = asks_.begin()->first;
        q.ask_qty = asks_.begin()->second.total_qty;
    }
    return q;
}

// --- Invariant checker (used by the test suite) ------------------------------
bool OrderBook::check_invariants() const {
    std::size_t counted = 0;

    auto check_side = [&](const auto& book) -> bool {
        for (const auto& [price, level] : book) {
            if (level.empty()) return false;              // empty levels must be erased
            Quantity sum = 0;
            Order*   prev = nullptr;
            for (Order* o = level.head; o != nullptr; o = o->next) {
                if (o->prev != prev) return false;        // back-links consistent
                if (o->price != price) return false;      // order filed at right price
                if (o->remaining == 0) return false;      // zero-qty orders never rest
                sum += o->remaining;
                prev = o;
                ++counted;
            }
            if (prev != level.tail) return false;         // tail points at the last node
            if (sum != level.total_qty) return false;     // aggregate matches the list
        }
        return true;
    };

    if (!check_side(bids_)) return false;
    if (!check_side(asks_)) return false;
    if (counted != orders_.size()) return false;          // map & lists agree on count

    // The book must never be crossed after matching settles.
    if (!bids_.empty() && !asks_.empty() &&
        bids_.begin()->first >= asks_.begin()->first) {
        return false;
    }
    return true;
}

}  // namespace pulse
