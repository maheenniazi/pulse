// =============================================================================
//  web/pulse_wasm.cpp  --  Emscripten/embind bindings for the browser demo
//
//  This wraps the SAME pulse::OrderBook used everywhere else and exposes a
//  tiny JSON-returning API to JavaScript. Compiled to WebAssembly with emcc,
//  it means the live web demo runs the real C++ matching engine in the
//  browser -- not a reimplementation.
// =============================================================================
#include "pulse/order_book.hpp"

#include <deque>
#include <emscripten/bind.h>
#include <sstream>
#include <string>

using namespace pulse;

namespace {

// newest-first rolling log of recent trades for the tape display
struct TapeTrade {
    Price    price;
    Quantity qty;
    int      taker_side;  // 0 buy, 1 sell
    uint64_t maker;
    uint64_t taker;
};

void append_price(std::ostringstream& os, const std::optional<Price>& p) {
    if (p) os << *p; else os << "null";
}

}  // namespace

class Engine {
public:
    Engine() { attach(); }

    // Returns {"id":N,"filled":F}
    std::string submit(int side, int type, int tif, double price, double qty) {
        const OrderId id{next_id_++};
        const Quantity filled = book_.submit(
            id,
            side == 0 ? Side::Buy : Side::Sell,
            type == 0 ? OrderType::Limit : OrderType::Market,
            tif == 0 ? TimeInForce::GTC : (tif == 1 ? TimeInForce::IOC : TimeInForce::FOK),
            static_cast<Price>(price),
            static_cast<Quantity>(qty));
        std::ostringstream os;
        os << "{\"id\":" << raw(id) << ",\"filled\":" << filled << "}";
        return os.str();
    }

    bool cancel(double id) { return book_.cancel(OrderId{static_cast<uint64_t>(id)}); }

    // -1 if the order is no longer resting, else its remaining size.
    double orderRemaining(double id) {
        const auto r = book_.order_remaining(OrderId{static_cast<uint64_t>(id)});
        return r ? static_cast<double>(*r) : -1.0;
    }

    void reset() {
        book_ = OrderBook{};
        attach();
        tape_.clear();
        next_id_ = 1;
    }

    // Full book + stats + recent trades as JSON.
    std::string snapshot(int depth) {
        std::ostringstream os;
        os << "{\"bids\":";
        emit_levels(os, book_.bids(static_cast<std::size_t>(depth)));
        os << ",\"asks\":";
        emit_levels(os, book_.asks(static_cast<std::size_t>(depth)));

        os << ",\"bid\":";    append_price(os, book_.best_bid());
        os << ",\"ask\":";    append_price(os, book_.best_ask());
        os << ",\"mid\":";    append_price(os, book_.mid());
        os << ",\"spread\":"; append_price(os, book_.spread());
        os << ",\"resting\":" << book_.resting_count();

        os << ",\"trades\":[";
        for (std::size_t i = 0; i < tape_.size(); ++i) {
            const auto& t = tape_[i];
            if (i) os << ',';
            os << "{\"p\":" << t.price << ",\"q\":" << t.qty
               << ",\"s\":" << t.taker_side
               << ",\"mk\":" << t.maker << ",\"tk\":" << t.taker << "}";
        }
        os << "]}";
        return os.str();
    }

private:
    void attach() {
        book_.set_trade_handler([this](const Trade& t) {
            tape_.push_front(TapeTrade{t.price, t.quantity,
                                       t.taker_side == Side::Buy ? 0 : 1,
                                       raw(t.maker_id), raw(t.taker_id)});
            if (tape_.size() > 40) tape_.pop_back();
        });
    }

    static void emit_levels(std::ostringstream& os, const std::vector<OrderBook::Level>& lv) {
        os << '[';
        for (std::size_t i = 0; i < lv.size(); ++i) {
            if (i) os << ',';
            os << "{\"p\":" << lv[i].price << ",\"q\":" << lv[i].qty
               << ",\"n\":" << lv[i].orders << "}";
        }
        os << ']';
    }

    OrderBook            book_;
    std::deque<TapeTrade> tape_;
    uint64_t             next_id_{1};
};

EMSCRIPTEN_BINDINGS(pulse_engine) {
    emscripten::class_<Engine>("Engine")
        .constructor<>()
        .function("submit", &Engine::submit)
        .function("cancel", &Engine::cancel)
        .function("orderRemaining", &Engine::orderRemaining)
        .function("reset", &Engine::reset)
        .function("snapshot", &Engine::snapshot);
}
