#!/usr/bin/env python3
"""Turn a Pulse backtest equity curve into a performance tearsheet.

The C++ engine does the fast simulation and writes results.csv; Python does
the analysis and plotting. This is the standard quant workflow -- a fast
production/simulation core, a flexible research layer on top.

Usage:
    python3 python/tearsheet.py results.csv [out.png]

Computes P&L, per-step Sharpe, max drawdown, and (if matplotlib is installed)
renders a three-panel chart: equity curve, inventory, and drawdown.
"""
import csv
import math
import sys


def load(path):
    steps, mids, pos, equity = [], [], [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            steps.append(int(row["step"]))
            mids.append(float(row["mid"]))
            pos.append(float(row["position"]))
            equity.append(float(row["equity"]))
    return steps, mids, pos, equity


def metrics(equity):
    if len(equity) < 2:
        return {"pnl": 0.0, "sharpe": 0.0, "max_drawdown": 0.0}
    diffs = [equity[i] - equity[i - 1] for i in range(1, len(equity))]
    mean = sum(diffs) / len(diffs)
    var = sum((d - mean) ** 2 for d in diffs) / len(diffs)
    sd = math.sqrt(var)
    sharpe = mean / sd if sd > 0 else 0.0

    peak, max_dd = equity[0], 0.0
    for e in equity:
        peak = max(peak, e)
        max_dd = max(max_dd, peak - e)

    return {"pnl": equity[-1], "sharpe": sharpe, "max_drawdown": max_dd}


def drawdown_series(equity):
    out, peak = [], equity[0] if equity else 0.0
    for e in equity:
        peak = max(peak, e)
        out.append(e - peak)
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    steps, mids, pos, equity = load(path)
    m = metrics(equity)

    print("Pulse tearsheet")
    print("===============")
    print(f"rows            : {len(steps)}")
    print(f"total P&L       : {m['pnl']:.1f} ticks-units")
    print(f"per-step Sharpe : {m['sharpe']:.4f}")
    print(f"max drawdown    : {m['max_drawdown']:.1f} ticks-units")
    print(f"final inventory : {int(pos[-1]) if pos else 0} units")

    out = sys.argv[2] if len(sys.argv) > 2 else "tearsheet.png"
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not installed -- skipping chart)")
        return

    dd = drawdown_series(equity)
    fig, ax = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    fig.suptitle("Pulse -- inventory-aware market maker", fontweight="bold")

    ax[0].plot(steps, equity, color="#2a7ae2", lw=1.0)
    ax[0].axhline(0, color="#888", lw=0.6)
    ax[0].set_ylabel("equity (ticks-units)")
    ax[0].set_title(f"P&L {m['pnl']:.0f}   |   Sharpe {m['sharpe']:.3f}   |   maxDD {m['max_drawdown']:.0f}")

    ax[1].plot(steps, pos, color="#e2a32a", lw=0.8)
    ax[1].axhline(0, color="#888", lw=0.6)
    ax[1].set_ylabel("inventory (units)")

    ax[2].fill_between(steps, dd, 0, color="#e24a4a", alpha=0.5)
    ax[2].set_ylabel("drawdown")
    ax[2].set_xlabel("event step")

    fig.tight_layout()
    fig.savefig(out, dpi=120)
    print(f"\nchart written to {out}")


if __name__ == "__main__":
    main()
