#!/usr/bin/env bash
# =============================================================================
#  scripts/param_sweep.sh  --  sweep the market maker's spread / requote cadence
#
#  Shows the core market-making trade-off: a wider quoted spread earns more per
#  fill and suffers less adverse selection (higher P&L, lower drawdown) but
#  trades less often. Run after building:  ./scripts/param_sweep.sh
# =============================================================================
set -euo pipefail

BIN="${1:-build/pulse_backtest}"
STEPS="${2:-200000}"
SEED="${3:-7}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found -- build first:" >&2
    echo "    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j" >&2
    exit 1
fi

printf "%-12s %-12s %-14s %-9s %s\n" "half_spread" "requote_thr" "PnL" "fills" "max_drawdown"
printf -- "-------------------------------------------------------------------\n"
for hs in 2 3 4 6 8; do
    for rt in 1 3 5; do
        out="$("$BIN" "$STEPS" "$SEED" "$TMP/r.csv" "$hs" "$rt")"
        pnl=$(echo "$out"   | grep 'total P&L'      | grep -oE '\-?[0-9]+(\.[0-9]+)?' | head -1)
        fills=$(echo "$out" | grep 'strategy fills' | grep -oE '[0-9]+'                | head -1)
        dd=$(echo "$out"    | grep 'max drawdown'   | grep -oE '[0-9]+(\.[0-9]+)?'     | head -1)
        printf "%-12s %-12s %-14s %-9s %s\n" "$hs" "$rt" "$pnl" "$fills" "$dd"
    done
done
