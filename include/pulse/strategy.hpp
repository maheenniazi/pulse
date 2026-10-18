// =============================================================================
//  pulse/strategy.hpp  --  the trading-strategy interface
//
//  A strategy sees the book through a read-only view and acts through a small
//  API that the backtester supplies. The backtester tags every order the
//  strategy sends so it can attribute fills back and keep the P&L.
// =============================================================================
#pragma once

#include "pulse/order_book.hpp"

namespace pulse {

// The handle a strategy uses to place and pull orders. Implemented by the
// backtester (which owns the real book).
class StrategyApi {
public:
    virtual ~StrategyApi() = default;

    [[nodiscard]] virtual OrderId buy(Price price, Quantity qty,
                                      TimeInForce tif = TimeInForce::GTC)  = 0;
    [[nodiscard]] virtual OrderId sell(Price price, Quantity qty,
                                       TimeInForce tif = TimeInForce::GTC) = 0;
    virtual void cancel(OrderId id) = 0;

    [[nodiscard]] virtual const OrderBook& book() const = 0;
};

class Strategy {
public:
    virtual ~Strategy() = default;

    // Called once after each market message is applied to the book.
    virtual void on_tick(StrategyApi& api) = 0;

    // Called whenever one of *this strategy's* orders trades. `signed_qty` is
    // positive when the strategy bought and negative when it sold, so a
    // strategy can keep its own inventory just by summing these.
    virtual void on_fill(Price price, long long signed_qty) { (void)price; (void)signed_qty; }
};

}  // namespace pulse
