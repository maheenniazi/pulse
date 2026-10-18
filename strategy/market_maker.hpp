// =============================================================================
//  strategy/market_maker.hpp  --  an inventory-aware, post-only market maker
//
//  A market maker quotes both sides of the book and earns the bid/ask spread
//  when both quotes get filled. The central risk is *inventory*: if the price
//  trends and you keep getting hit on one side, you accumulate a position and
//  take directional risk you weren't paid for. The standard defence is to
//  SKEW your quotes against your inventory -- long inventory pushes both quotes
//  down so you are keener to sell than buy, and vice versa. That is the one
//  idea every market-making interview wants to hear, and it is implemented
//  here in `requote()`.
//
//  "Post-only": the quotes are clamped so they never cross the current book,
//  i.e. the maker always adds liquidity and never pays the spread by taking.
// =============================================================================
#pragma once

#include "pulse/strategy.hpp"

#include <algorithm>
#include <optional>

namespace pulse {

class MarketMaker final : public Strategy {
public:
    struct Config {
        Quantity  quote_size{20};       // size posted on each side
        Price     half_spread{2};       // ticks from mid to each quote
        long long max_inventory{200};   // stop quoting the side that grows risk
        Price     skew_per_lot{0};      // extra ticks of skew per unit of inventory
        Price     requote_threshold{1}; // only re-quote once the mid moves this far
    };

    MarketMaker() : MarketMaker(Config{}) {}
    explicit MarketMaker(Config cfg) : cfg_(cfg) {}

    void on_tick(StrategyApi& api) override {
        const auto mid = api.book().mid();
        if (!mid) return;                       // need a two-sided market to quote

        // Re-quote only when the mid has moved past our threshold. Re-quoting
        // on every 1-tick wiggle sends our orders to the back of the queue
        // constantly, so they never rest long enough to earn the spread from
        // uninformed flow -- they only get picked off by large sweeps. Letting
        // quotes rest is what turns a market maker profitable.
        if (last_mid_) {
            const Price moved = (*mid > *last_mid_) ? (*mid - *last_mid_) : (*last_mid_ - *mid);
            if (moved < cfg_.requote_threshold) return;
        }
        last_mid_ = *mid;
        requote(api, *mid);
    }

    void on_fill(Price /*price*/, long long signed_qty) override {
        position_ += signed_qty;
    }

    [[nodiscard]] long long position() const noexcept { return position_; }

private:
    void requote(StrategyApi& api, Price mid) {
        // Pull our previous quotes (a no-op if they already filled/cancelled).
        if (have_bid_) api.cancel(bid_id_);
        if (have_ask_) api.cancel(ask_id_);
        have_bid_ = have_ask_ = false;

        // Inventory skew: long -> shift quotes down, short -> shift up.
        const long long lot  = std::max<long long>(1, static_cast<long long>(cfg_.quote_size));
        const Price     skew = cfg_.skew_per_lot * static_cast<Price>(position_ / lot);

        Price bid_px = mid - cfg_.half_spread - skew;
        Price ask_px = mid + cfg_.half_spread - skew;

        // Post-only clamp: stay strictly inside the opposite best so we add,
        // never take, liquidity.
        if (const auto ba = api.book().best_ask()) bid_px = std::min(bid_px, *ba - 1);
        if (const auto bb = api.book().best_bid()) ask_px = std::max(ask_px, *bb + 1);

        if (position_ < cfg_.max_inventory) {
            bid_id_   = api.buy(bid_px, cfg_.quote_size);
            have_bid_ = true;
        }
        if (position_ > -cfg_.max_inventory) {
            ask_id_   = api.sell(ask_px, cfg_.quote_size);
            have_ask_ = true;
        }
    }

    Config               cfg_;
    long long            position_{0};
    std::optional<Price> last_mid_;
    OrderId              bid_id_{}, ask_id_{};
    bool                 have_bid_{false}, have_ask_{false};
};

}  // namespace pulse
