"""
NSE Alpha Engine — Comprehensive Benchmark Table.

Runs every indicator at multiple row counts and multiple window sizes,
prints a rich coloured terminal table with ASCII throughput bars, and
shows how performance scales with data size.

Usage
-----
    python3 engine/python/benchmark_table.py
    python3 engine/python/benchmark_table.py --max-rows 2000000
    python3 engine/python/benchmark_table.py --csv bench.csv
"""

from __future__ import annotations

import sys
import os
import math
import csv as _csv
import argparse

_ENGINE = os.path.join(os.path.dirname(__file__), "..", "build_output")
sys.path.insert(0, os.path.abspath(_ENGINE))
import nse_engine_cpp as _cpp

# ── ANSI ───────────────────────────────────────────────────────────────────────
_USE_COLOR = sys.stdout.isatty()


def _c(code, s):
    return f"\033[{code}m{s}\033[0m" if _USE_COLOR else s

def green(s):    return _c("32",   s)
def yellow(s):   return _c("33",   s)
def cyan(s):     return _c("36",   s)
def bold(s):     return _c("1",    s)
def dim(s):      return _c("2",    s)
def bgreen(s):   return _c("1;32", s)
def bred(s):     return _c("1;31", s)
def bcyan(s):    return _c("1;36", s)
def byellow(s):  return _c("1;33", s)
def bmagenta(s): return _c("1;35", s)
def white(s):    return _c("97",   s)


def _prd(ms):
    return bgreen("PASS") if ms < 50 else (byellow("WARN") if ms < 500 else bred("FAIL"))

def _mscol(ms):
    return bgreen if ms < 50 else (byellow if ms < 200 else bred)

def _bar(ms, max_ms, width=18):
    if max_ms <= 0:
        return dim("." * width)
    fill = max(1, min(width, int(round(ms / max_ms * width))))
    col  = bgreen if ms < 50 else (byellow if ms < 200 else bred)
    return col("#" * fill) + dim("." * (width - fill))


def _fmt_comma(n):
    s = str(n)
    for i in range(len(s) - 3, 0, -3):
        s = s[:i] + "," + s[i:]
    return s


def _bench(name, n, fn):
    r = _cpp.BenchmarkModule.measure(name, n, fn)
    return {
        "name": name,
        "rows": n,
        "ms":   round(r.elapsed_us / 1000, 3),
        "mpts": round(r.throughput_per_sec / 1e6, 1),
    }


# ─────────────────────────────────────────────────────────────────────────────

def _print_ind_row(r, max_ms, bar_width=22):
    bar    = _bar(r["ms"], max_ms, bar_width)
    ms_str = f"{r['ms']:>7.1f}"
    mc     = _mscol(r["ms"])
    mpts_s = f"{r['mpts']:>9.1f} M"
    print(
        f"  {_fmt_comma(r['rows']):<14}"
        f"  {mc(ms_str)}"
        f"  {dim(mpts_s)}"
        f"  {bar}"
    )


def _print_op_row(r, max_ms, bar_width=20):
    bar    = _bar(r["ms"], max_ms, bar_width)
    ms_str = f"{r['ms']:>7.1f}"
    mc     = _mscol(r["ms"])
    mpts_s = f"{r['mpts']:>9.1f} M"
    prd    = _prd(r["ms"])
    print(
        f"  {yellow(r['name']):<22}"
        f"  {mc(ms_str)}"
        f"  {dim(mpts_s)}"
        f"  {prd}"
        f"  {bar}"
    )


def _print_window_row(label, r, max_ms, bar_width=20):
    bar    = _bar(r["ms"], max_ms, bar_width)
    ms_str = f"{r['ms']:>7.1f}"
    mc     = _mscol(r["ms"])
    mpts_s = f"{r['mpts']:>9.1f} M"
    print(
        f"  {label:<12}"
        f"  {mc(ms_str)}"
        f"  {dim(mpts_s)}"
        f"  {bar}"
    )


# ─────────────────────────────────────────────────────────────────────────────

def run_benchmark_table(max_rows=1_000_000, csv_out=None):
    ROW_COUNTS = [1_000, 10_000, 50_000, 100_000, 250_000, 500_000]
    if max_rows >= 1_000_000:
        ROW_COUNTS.append(1_000_000)
    if max_rows >= 2_000_000:
        ROW_COUNTS.append(2_000_000)
    ROW_COUNTS = [r for r in ROW_COUNTS if r <= max_rows]

    W = 78
    print()
    print(bcyan("=" * W))
    print(bold(white("  NSE Alpha Engine — Full Benchmark Table".center(W))))
    print(bold(white("  C++17 -O3  |  pybind11 2.13.6  |  GCC 14".center(W))))
    print(bcyan("=" * W))
    print()

    # ── Build data caches ──────────────────────────────────────────────────────
    caches = {}
    print(dim("  Building synthetic data caches..."))
    for rc in ROW_COUNTS:
        caches[rc] = [
            100.0 + 50.0 * math.sin(i * 0.001) + i * 0.0001
            for i in range(rc)
        ]
    print(dim("  Done.\n"))

    INDICATORS = [
        ("SMA(20)",            lambda c: _cpp.IndicatorEngine.sma(c, 20)),
        ("EMA(20)",            lambda c: _cpp.IndicatorEngine.ema(c, 20)),
        ("RSI(14)",            lambda c: _cpp.IndicatorEngine.rsi(c, 14)),
        ("MACD(12,26,9)",      lambda c: _cpp.IndicatorEngine.macd(c, 12, 26, 9)),
        ("BollingerBands(20)", lambda c: _cpp.IndicatorEngine.bollinger_bands(c, 20, 2.0)),
    ]

    all_rows_for_csv = []

    # ── Indicator scaling tables ───────────────────────────────────────────────
    for ind_name, ind_fn in INDICATORS:
        sep_len = W - 6 - len(ind_name)
        print(bold(f"  -- {cyan(ind_name)} ") + dim("-" * max(0, sep_len)))

        print(bold(f"  {'Rows':<14}{'ms':>10}{'M pts/sec':>12}{'Bar':>6}"))
        print(dim("  " + "-" * (W - 4)))

        ms_vals       = []
        bench_results = []
        for rc in ROW_COUNTS:
            c = caches[rc]
            r = _bench(ind_name, rc, lambda fn=ind_fn, cl=c: fn(cl))
            ms_vals.append(r["ms"])
            bench_results.append(r)

        max_ms = max(ms_vals) if ms_vals else 1.0

        for r in bench_results:
            _print_ind_row(r, max_ms)
            all_rows_for_csv.append(r)

        print()

    # ── SMA window sweep ───────────────────────────────────────────────────────
    c100k   = caches.get(100_000, [
        100.0 + 50.0 * math.sin(i * 0.001) for i in range(100_000)
    ])

    print(bcyan("-" * W))
    print(bold(f"  SMA Window Sweep -- {_fmt_comma(100_000)} rows"))
    print(bcyan("-" * W))
    print(bold(f"  {'Window':<12}{'ms':>10}{'M pts/s':>12}{'Bar':>6}"))
    print(dim("  " + "-" * 44))

    windows = [5, 10, 20, 50, 100, 200]
    sw_res  = []
    for w in windows:
        r = _bench(
            f"SMA({w})", 100_000,
            lambda ww=w: _cpp.IndicatorEngine.sma(c100k, ww)
        )
        sw_res.append((w, r))
    max_sw = max(r["ms"] for _, r in sw_res)
    for w, r in sw_res:
        _print_window_row(f"SMA({w})", r, max_sw)

    # ── RSI window sweep ──────────────────────────────────────────────────────
    print()
    print(bcyan("-" * W))
    print(bold(f"  RSI Window Sweep -- {_fmt_comma(100_000)} rows"))
    print(bcyan("-" * W))
    print(bold(f"  {'Window':<12}{'ms':>10}{'M pts/s':>12}{'Bar':>6}"))
    print(dim("  " + "-" * 44))

    rsi_windows = [7, 9, 14, 21, 28]
    rw_res      = []
    for w in rsi_windows:
        r = _bench(
            f"RSI({w})", 100_000,
            lambda ww=w: _cpp.IndicatorEngine.rsi(c100k, ww)
        )
        rw_res.append((w, r))
    max_rw = max(r["ms"] for _, r in rw_res)
    for w, r in rw_res:
        _print_window_row(f"RSI({w})", r, max_rw)

    # ── Full pipeline timing ───────────────────────────────────────────────────
    rc  = min(500_000, max_rows)
    c   = caches.get(rc, [100.0 + 50.0 * math.sin(i * 0.001) for i in range(rc)])
    tss = [f"T{i}" for i in range(rc)]

    print()
    print(bcyan("-" * W))
    print(bold(f"  Full Pipeline -- {_fmt_comma(rc)} rows"))
    print(bcyan("-" * W))

    ops = [
        ("SMA(20)",            lambda: _cpp.IndicatorEngine.sma(c, 20)),
        ("EMA(20)",            lambda: _cpp.IndicatorEngine.ema(c, 20)),
        ("RSI(14)",            lambda: _cpp.IndicatorEngine.rsi(c, 14)),
        ("MACD(12,26,9)",      lambda: _cpp.IndicatorEngine.macd(c, 12, 26, 9)),
        ("BollingerBands(20)", lambda: _cpp.IndicatorEngine.bollinger_bands(c, 20, 2.0)),
        ("SMA Crossover",      lambda: _cpp.SignalEngine.sma_crossover(c, tss, 10, 50)),
        ("RSI Strategy",       lambda: _cpp.SignalEngine.rsi_strategy(c, tss, 14)),
        ("MACD Strategy",      lambda: _cpp.SignalEngine.macd_strategy(c, tss, 12, 26, 9)),
    ]
    sigs = _cpp.SignalEngine.sma_crossover(c, tss, 10, 50)
    ops.append(("Backtest", lambda: _cpp.BacktestEngine.run(sigs, c, tss)))

    results2  = []
    for name, fn in ops:
        r = _bench(name, rc, fn)
        results2.append(r)

    max_ms2  = max(r["ms"] for r in results2)
    total_ms = sum(r["ms"] for r in results2)

    print(bold(
        f"  {'Operation':<24}{'ms':>10}{'M pts/s':>12}{'PRD':>8}  Bar"
    ))
    print(dim("  " + "-" * (W - 2)))

    for r in results2:
        _print_op_row(r, max_ms2)

    print(dim("  " + "-" * (W - 2)))
    total_s = f"{total_ms:.1f} ms"
    print(
        f"  {bold('TOTAL'):<22}"
        f"  {bgreen(total_s)}"
        f"  for all {len(ops)} operations on {_fmt_comma(rc)} bars"
    )

    print()
    print(bcyan("=" * W))
    print(bold(white(
        "  PRD sect.8 Target: all indicators < 50 ms at 500k rows".center(W)
    )))
    ind_pass = all(r["ms"] < 50 for r in results2[:5])
    status   = bgreen("ALL PASS") if ind_pass else bred("SOME FAIL")
    print(bold(white(f"  Indicator targets: {status}".center(W + 15))))
    print(bcyan("=" * W))
    print()

    # ── CSV export ─────────────────────────────────────────────────────────────
    if csv_out:
        with open(csv_out, "w", newline="") as f:
            w2 = _csv.writer(f)
            w2.writerow(["indicator", "rows", "ms", "M_pts_per_sec"])
            for r in all_rows_for_csv:
                w2.writerow([r["name"], r["rows"], r["ms"], r["mpts"]])
        print(f"  {bgreen('OK')} Benchmark data exported -> {csv_out}\n")


# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    p = argparse.ArgumentParser(
        description="NSE Alpha Engine comprehensive benchmark table."
    )
    p.add_argument("--max-rows", type=int, default=1_000_000,
                   help="Maximum row count for scaling tests (default: 1M)")
    p.add_argument("--no-color", action="store_true",
                   help="Disable ANSI colours")
    p.add_argument("--csv", metavar="FILE", default=None,
                   help="Export benchmark results to CSV")
    args = p.parse_args()

    if args.no_color:
        _USE_COLOR = False

    run_benchmark_table(args.max_rows, args.csv)
