from __future__ import annotations

import sys
import os
import math
import time
import csv as _csv
import argparse
from typing import List, Optional

_ENGINE = os.path.join(os.path.dirname(__file__), "..", "build_output")
sys.path.insert(0, os.path.abspath(_ENGINE))
import nse_engine_cpp as _cpp

_USE_COLOR = sys.stdout.isatty()

def _c(code: str, s: str) -> str:
    return f"\033[{code}m{s}\033[0m" if _USE_COLOR else s

def red(s):     return _c("31",    s)
def green(s):   return _c("32",    s)
def yellow(s):  return _c("33",    s)
def cyan(s):    return _c("36",    s)
def white(s):   return _c("97",    s)
def bold(s):    return _c("1",     s)
def dim(s):     return _c("2",     s)
def bgreen(s):  return _c("1;32",  s)
def bred(s):    return _c("1;31",  s)
def bcyan(s):   return _c("1;36",  s)
def byellow(s): return _c("1;33",  s)
def bmagenta(s):return _c("1;35",  s)

# Pure-Python implementations (deliberately naive — no numpy)

def py_sma(close: List[float], window: int) -> List[Optional[float]]:
        out = [None] * len(close)
    for i in range(window - 1, len(close)):
        out[i] = sum(close[i - window + 1 : i + 1]) / window
    return out

def py_ema(close: List[float], window: int) -> List[Optional[float]]:
        out = [None] * len(close)
    if len(close) < window:
        return out
    alpha = 2.0 / (window + 1)
    # Seed with SMA
    seed = sum(close[:window]) / window
    out[window - 1] = seed
    prev = seed
    for i in range(window, len(close)):
        prev = alpha * close[i] + (1 - alpha) * prev
        out[i] = prev
    return out

def py_rsi(close: List[float], window: int = 14) -> List[Optional[float]]:
        out  = [None] * len(close)
    n    = len(close)
    if n < window + 1:
        return out

    alpha = 1.0 / window
    gains = [max(0.0, close[i] - close[i - 1]) for i in range(1, n)]
    losses= [max(0.0, close[i - 1] - close[i]) for i in range(1, n)]

    avg_g = sum(gains[:window])  / window
    avg_l = sum(losses[:window]) / window

    def _rsi_val(ag, al):
        if al == 0.0:
            return 100.0
        rs = ag / al
        return 100.0 - 100.0 / (1.0 + rs)

    out[window] = _rsi_val(avg_g, avg_l)
    for i in range(window + 1, n):
        avg_g = (1 - alpha) * avg_g + alpha * gains[i - 1]
        avg_l = (1 - alpha) * avg_l + alpha * losses[i - 1]
        out[i] = _rsi_val(avg_g, avg_l)
    return out

def py_macd(close: List[float],
            fast: int = 12,
            slow: int = 26,
            signal: int = 9):
        fast_ema   = py_ema(close, fast)
    slow_ema   = py_ema(close, slow)
    n          = len(close)
    macd_line  = [None] * n
    for i in range(n):
        if fast_ema[i] is not None and slow_ema[i] is not None:
            macd_line[i] = fast_ema[i] - slow_ema[i]

    valid_macd = [(i, v) for i, v in enumerate(macd_line) if v is not None]
    sig_line   = [None] * n
    if len(valid_macd) >= signal:
        start = valid_macd[0][0]
        # Seed signal EMA
        seed = sum(macd_line[start : start + signal]) / signal   # type: ignore
        sig_line[start + signal - 1] = seed
        alpha = 2.0 / (signal + 1)
        prev = seed
        for i in range(start + signal, n):
            if macd_line[i] is not None:
                prev = alpha * macd_line[i] + (1 - alpha) * prev
                sig_line[i] = prev

    hist = [None] * n
    for i in range(n):
        if macd_line[i] is not None and sig_line[i] is not None:
            hist[i] = macd_line[i] - sig_line[i]   # type: ignore

    return {"macd_line": macd_line, "signal_line": sig_line, "histogram": hist}

def py_bollinger(close: List[float], window: int = 20, k: float = 2.0):
        n      = len(close)
    upper  = [None] * n
    middle = [None] * n
    lower  = [None] * n
    for i in range(window - 1, n):
        chunk = close[i - window + 1 : i + 1]
        mu    = sum(chunk) / window
        var   = sum((x - mu) ** 2 for x in chunk) / window
        sd    = math.sqrt(var)
        middle[i] = mu
        upper[i]  = mu + k * sd
        lower[i]  = mu - k * sd
    return {"upper": upper, "middle": middle, "lower": lower}

# Timing helper

def _timed(fn) -> float:
        t0 = time.perf_counter()
    fn()
    return time.perf_counter() - t0

def _timed_cpp(fn, n: int) -> float:
        r = _cpp.BenchmarkModule.measure("_", n, fn)
    return r.elapsed_us / 1e6

# Speedup bar

def _speedup_bar(x: float, max_x: float, width: int = 22) -> str:
    fill = int(round(x / max_x * width)) if max_x > 0 else 1
    fill = max(1, min(width, fill))
    bar  = "#" * fill + "." * (width - fill)
    return bgreen(bar)

# Main comparison runner

def run_comparison(rows: int, csv_out: Optional[str] = None) -> None:
    
    w = 74
    print()
    print(bcyan("═" * w))
    print(bold(white(
        "  NSE Alpha Engine — C++ vs Pure Python Speed Comparison".center(w)
    )))
    print(bcyan("═" * w))
    print(dim(f"  {rows:,} synthetic rows  •  pure Python (no numpy)  vs  C++17 -O3 -pybind11"))
    print()

    
    print(dim("  Generating synthetic price series..."))
    close  = [100.0 + 50.0 * math.sin(i * 0.001) + i * 0.0001 for i in range(rows)]
    ts_str = [f"T{i}" for i in range(rows)]
    print(dim(f"  {rows:,} bars ready.\n"))

    
    comparisons = [
        ("SMA(20)",
         lambda: py_sma(close, 20),
         lambda: _cpp.IndicatorEngine.sma(close, 20)),
        ("EMA(20)",
         lambda: py_ema(close, 20),
         lambda: _cpp.IndicatorEngine.ema(close, 20)),
        ("RSI(14)",
         lambda: py_rsi(close, 14),
         lambda: _cpp.IndicatorEngine.rsi(close, 14)),
        ("MACD(12,26,9)",
         lambda: py_macd(close, 12, 26, 9),
         lambda: _cpp.IndicatorEngine.macd(close, 12, 26, 9)),
        ("BollingerBands(20)",
         lambda: py_bollinger(close, 20, 2.0),
         lambda: _cpp.IndicatorEngine.bollinger_bands(close, 20, 2.0)),
    ]

    
    results = []
    print(dim("  Timing Python implementations..."))
    for name, py_fn, cpp_fn in comparisons:
        sys.stdout.write(f"    {name:<22} Python... ")
        sys.stdout.flush()
        py_s  = _timed(py_fn)
        sys.stdout.write(f"{py_s*1000:7.1f} ms  |  C++... ")
        sys.stdout.flush()
        cpp_s = _timed_cpp(cpp_fn, rows)
        speedup = py_s / cpp_s if cpp_s > 0 else float("inf")
        sys.stdout.write(f"{cpp_s*1000:7.1f} ms  |  {speedup:6.1f}x\n")
        sys.stdout.flush()
        results.append((name, py_s, cpp_s, speedup))

    
    max_speedup = max(r[3] for r in results)

    print()
    print(bcyan("─" * w))
    print(
        bold("  " + "Indicator".ljust(22))
        + bold("  " + "Python".rjust(9))
        + bold("  " + "C++".rjust(9))
        + bold("  " + "Speedup".rjust(8))
        + bold("  " + "Bar".ljust(24))
    )
    print(bcyan("─" * w))
    for name, py_s, cpp_s, speedup in results:
        bar    = _speedup_bar(speedup, max_speedup, 22)
        py_ms  = py_s  * 1000
        cp_ms  = cpp_s * 1000
        sp_str = f"{speedup:6.1f}x"
        py_str = f"{py_ms:>7.1f} ms"
        cp_str = f"{cp_ms:>7.1f} ms"
        nm_str = name.ljust(22)   # pad before applying colour

        print(
            f"  {yellow(nm_str)}"
            f"  {bred(py_str)}"
            f"  {bgreen(cp_str)}"
            f"  {byellow(sp_str)}"
            f"  {bar}"
        )

    print(bcyan("─" * w))

    # Summary
    avg_speedup = sum(r[3] for r in results) / len(results)
    max_sp_name = max(results, key=lambda r: r[3])[0]
    max_sp_val  = max(results, key=lambda r: r[3])[3]
    total_py_ms = sum(r[1] for r in results) * 1000
    total_cp_ms = sum(r[2] for r in results) * 1000

    print(f"\n  {bold('Average speedup :')}"
          f"  {byellow(f'{avg_speedup:.1f}x')}  faster than pure Python")
    print(f"  {bold('Peak speedup    :')}"
          f"  {bgreen(f'{max_sp_val:.1f}x')}  ({max_sp_name})")
    print(f"  {bold('Total Python    :')}"
          f"  {bred(f'{total_py_ms:.1f} ms')}  for all 5 indicators")
    print(f"  {bold('Total C++       :')}"
          f"  {bgreen(f'{total_cp_ms:.1f} ms')}  for all 5 indicators")
    print()

    
    print(bcyan("═" * w))
    print(bold(white("  SMA(20) Speedup Across Row Counts".center(w))))
    print(bcyan("═" * w))
    scales = [1_000, 5_000, 10_000, 50_000, 100_000]
    if rows >= 500_000:
        scales.append(500_000)
    if rows >= 1_000_000:
        scales.append(1_000_000)
    scales = [s for s in scales if s <= rows]

    print(
        f"  {'Rows':<12}  {'Python':>10}  {'C++':>10}  {'Speedup':>10}  {'Bar'}"
    )
    print(dim("  " + "─" * 60))
    for sc in scales:
        c2  = close[:sc]
        p_s = _timed(lambda: py_sma(c2, 20))
        c_s = _timed_cpp(lambda: _cpp.IndicatorEngine.sma(c2, 20), sc)
        spd = p_s / c_s if c_s > 0 else 0
        bar = _speedup_bar(spd, max_speedup, 16)
        print(
            f"  {sc:<12,}  {red(f'{p_s*1000:>8.1f} ms')}"
            f"  {bgreen(f'{c_s*1000:>8.1f} ms')}"
            f"  {byellow(f'{spd:>8.1f}x')}"
            f"  {bar}"
        )

    print(bcyan("═" * w))
    print()

    
    if csv_out:
        with open(csv_out, "w", newline="") as f:
            w2 = _csv.writer(f)
            w2.writerow(["indicator", "rows", "python_ms", "cpp_ms", "speedup_x"])
            for name, py_s, cpp_s, speedup in results:
                w2.writerow([name, rows,
                              round(py_s * 1000, 3),
                              round(cpp_s * 1000, 3),
                              round(speedup, 2)])
        print(f"  {bgreen('✔')} Comparison table exported → {csv_out}\n")

# CLI

if __name__ == "__main__":
    p = argparse.ArgumentParser(
        description="C++ vs pure-Python speed comparison for NSE Alpha Engine.",
    )
    p.add_argument("rows", nargs="?", type=int, default=100_000,
                   help="Number of synthetic data points (default: 100 000)")
    p.add_argument("--no-color", action="store_true", help="Disable ANSI colours")
    p.add_argument("--csv", metavar="FILE", default=None,
                   help="Export results to CSV file")
    args = p.parse_args()

    if args.no_color:
        _USE_COLOR = False

    run_comparison(args.rows, args.csv)
