// =============================================================================
//  pulse/types.hpp  --  core value types for the matching engine
//
//  Design note: prices are integers (ticks), never floating point.
//  Exchanges quote on a discrete price grid (e.g. $0.01 per tick), and using
//  `double` for prices invites rounding bugs where 10.10 != 10.10. Every real
//  matching engine represents price as an integer number of ticks. We do too.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace pulse {

// --- Strong id type ---------------------------------------------------------
// OrderId is its own type (not a bare uint64) so the compiler stops us from
// ever passing a price where an order id is expected, and vice versa. This
// "strong typedef via enum class" trick costs nothing at runtime.
enum class OrderId : std::uint64_t {};

[[nodiscard]] constexpr std::uint64_t raw(OrderId id) noexcept {
    return static_cast<std::uint64_t>(id);
}

// --- Price / quantity -------------------------------------------------------
// Price is a signed integer count of ticks. Quantity is an unsigned count of
// lots/shares. Kept as plain aliases (not strong types) because strategies do
// real arithmetic on them (mid = (bid+ask)/2, etc.) and the ergonomics matter.
using Price    = std::int64_t;
using Quantity = std::uint64_t;

// Monotonic sequence number stamped on every message as it enters the engine.
// This is what gives us *time* priority: lower sequence == arrived earlier.
using Sequence = std::uint64_t;

// --- Enumerations -----------------------------------------------------------
enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t {
    Limit,   // rest in the book at a stated price if not fully matched
    Market   // match against whatever is available, never rest
};

// Time-in-force governs what happens to the unfilled remainder of an order.
enum class TimeInForce : std::uint8_t {
    GTC,  // Good-'Til-Cancelled: rest the remainder in the book
    IOC,  // Immediate-Or-Cancel: fill what you can now, cancel the rest
    FOK   // Fill-Or-Kill: fill the *entire* order now, or do nothing at all
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] inline const char* to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

}  // namespace pulse
