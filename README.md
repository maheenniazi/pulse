# Pulse — a limit order book matching engine & backtester

[![CI](https://github.com/maheenniazi/pulse/actions/workflows/ci.yml/badge.svg)](https://github.com/maheenniazi/pulse/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![sanitizers](https://img.shields.io/badge/sanitizers-ASan%20%7C%20UBSan%20%7C%20TSan-success)

**▶ Live demo: https://maheenniazi.github.io/pulse** — drop in some orders and
watch them match, right in your browser. It's the actual C++ engine compiled to
WebAssembly, not a rewrite.

Pulse is the thing that lives inside a stock exchange: orders come in, it pairs
up buyers and sellers by **price-time priority**, and prints the trades. Written
in modern **C++20**, with a backtester and a sample trading strategy on top.

**The good bits:**
- ~**5M orders/sec**, ~**110 ns** median / ~**440 ns** p99 per order on one core
- Zero-allocation hot path (a custom object pool) + intrusive data structures
- A **lock-free queue** for feeding the engine across threads — proven race-free
  under ThreadSanitizer with a 1M-message test
- 13 tests (incl. a fuzz test), clean under ASan/UBSan/TSan, CI on GCC **and** Clang

## How it works (the short version)

Bids are buyers, asks are sellers. A new order matches against the other side by
two rules: **best price first**, then **oldest-first at the same price** (FIFO).
Anything that doesn't match rests in the book. That's the whole game — the rest
is just making it fast and correct.

Under the hood: sorted price-level maps (O(1) to read the best bid/ask),
intrusive linked lists so cancelling an order is O(1), a hash index to jump
straight to any order, and integer *tick* prices instead of floats (because
`10.10 != 10.10` bugs are real). Handles limit + market orders, GTC/IOC/FOK, and
cancel/modify.

## The backtester

It replays a market feed into the engine and lets a strategy trade against it,
tracking position, cash, and P&L. The sample strategy is an **inventory-aware
market maker** — it quotes both sides and leans its prices against its own
position so it doesn't pile up risk. On a calm market it steadily earns the
spread:

```
total P&L       : 105230
per-step Sharpe : 0.0172
max drawdown    : 4075
```

`python/tearsheet.py` turns that into Sharpe / drawdown / equity charts.

## Run it

Needs CMake 3.20+ and a C++20 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build     # run the tests
./build/pulse_demo         # watch orders match
./build/pulse_backtest     # run the market maker
./build/pulse_bench        # speed + latency numbers
```

## What's next
- Flatten the latency tail (pre-sized pool, array-indexed price levels)
- A networked gateway feeding the engine over the lock-free queue
- More strategies + Python bindings
