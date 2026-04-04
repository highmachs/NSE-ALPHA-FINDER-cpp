"""
NSE Alpha Engine — Python High-Level Wrapper

Provides a clean, Pythonic interface on top of the C++ pybind11 bindings.
All heavy computation runs in C++; this module handles type conversion,
result formatting, and convenience methods.
"""

import sys
import os
import math
from typing import Optional, Literal, List, Dict, Any

_BUILD_OUTPUT = os.path.join(os.path.dirname(__file__), "..", "build_output")
sys.path.insert(0, os.path.abspath(_BUILD_OUTPUT))

import nse_engine_cpp as _cpp


# ── Re-export enums ───────────────────────────────────────────────────────────

MissingValuePolicy = _cpp.MissingValuePolicy
Signal             = _cpp.Signal


# ── Helpers ───────────────────────────────────────────────────────────────────

def _clean(v: float) -> Optional[float]:
    return None if (math.isnan(v) or math.isinf(v)) else v


def _clean_list(lst) -> List[Optional[float]]:
    return [_clean(v) for v in lst]


# ── Data Loading ──────────────────────────────────────────────────────────────

def load_csv(filepath: str,
             missing_policy: Literal["drop", "forward_fill"] = "drop") -> Dict[str, Any]:
    """
    Load OHLCV data from a CSV file.

    Parameters
    ----------
    filepath : str
        Path to CSV file. Must contain: timestamp, open, high, low, close, volume.
    missing_policy : "drop" | "forward_fill"
        How to handle malformed rows.

    Returns
    -------
    dict with keys: timestamp, open, high, low, close, volume, rows
    """
    policy = (MissingValuePolicy.FORWARD_FILL
              if missing_policy == "forward_fill"
              else MissingValuePolicy.DROP)
    data = _cpp.DataIngestionEngine.load_from_csv(filepath, policy)
    return _ohlcv_to_dict(data)


def load_string(csv_content: str,
                missing_policy: Literal["drop", "forward_fill"] = "drop") -> Dict[str, Any]:
    """
    Load OHLCV data from a raw CSV string.

    Parameters
    ----------
    csv_content : str
        Full CSV text including header row.
    missing_policy : "drop" | "forward_fill"
        How to handle malformed rows.

    Returns
    -------
    dict with keys: timestamp, open, high, low, close, volume, rows
    """
    policy = (MissingValuePolicy.FORWARD_FILL
              if missing_policy == "forward_fill"
              else MissingValuePolicy.DROP)
    data = _cpp.DataIngestionEngine.load_from_string(csv_content, policy)
    return _ohlcv_to_dict(data)


def _load_raw(csv_content: str, missing_policy: str) -> _cpp.OHLCVData:
    policy = (MissingValuePolicy.FORWARD_FILL
              if missing_policy == "forward_fill"
              else MissingValuePolicy.DROP)
    return _cpp.DataIngestionEngine.load_from_string(csv_content, policy)


def _ohlcv_to_dict(data: _cpp.OHLCVData) -> Dict[str, Any]:
    return {
        "timestamp": list(data.timestamp),
        "open":      _clean_list(data.open),
        "high":      _clean_list(data.high),
        "low":       _clean_list(data.low),
        "close":     _clean_list(data.close),
        "volume":    _clean_list(data.volume),
        "rows":      data.size(),
    }


# ── Indicators ────────────────────────────────────────────────────────────────

def sma(close: List[float], window: int) -> List[Optional[float]]:
    """Simple Moving Average — O(n), rolling mean over window."""
    return _clean_list(_cpp.IndicatorEngine.sma(close, window))


def ema(close: List[float], window: int) -> List[Optional[float]]:
    """Exponential Moving Average — alpha = 2 / (n + 1)."""
    return _clean_list(_cpp.IndicatorEngine.ema(close, window))


def rsi(close: List[float], window: int = 14) -> List[Optional[float]]:
    """Relative Strength Index — Wilder's smoothing method."""
    return _clean_list(_cpp.IndicatorEngine.rsi(close, window))


def macd(close: List[float],
         fast: int = 12,
         slow: int = 26,
         signal: int = 9) -> Dict[str, List[Optional[float]]]:
    """
    MACD — EMA(fast) minus EMA(slow), signal = EMA(signal) of MACD.

    Returns dict: { macd_line, signal_line, histogram }
    """
    r = _cpp.IndicatorEngine.macd(close, fast, slow, signal)
    return {
        "macd_line":   _clean_list(r.macd_line),
        "signal_line": _clean_list(r.signal_line),
        "histogram":   _clean_list(r.histogram),
    }


def bollinger_bands(close: List[float],
                    window: int = 20,
                    k: float = 2.0) -> Dict[str, List[Optional[float]]]:
    """
    Bollinger Bands — SMA ± (k × std).
    Uses O(n) incremental variance.

    Returns dict: { upper, middle, lower }
    """
    r = _cpp.IndicatorEngine.bollinger_bands(close, window, k)
    return {
        "upper":  _clean_list(r.upper),
        "middle": _clean_list(r.middle),
        "lower":  _clean_list(r.lower),
    }


def all_indicators(close: List[float],
                   sma_window: int = 20,
                   ema_window: int = 20,
                   rsi_window: int = 14,
                   macd_fast: int = 12,
                   macd_slow: int = 26,
                   macd_signal: int = 9,
                   bb_window: int = 20,
                   bb_k: float = 2.0) -> Dict[str, Any]:
    """
    Compute all indicators in one call. Returns nested dict.
    """
    return {
        "sma":             sma(close, sma_window),
        "ema":             ema(close, ema_window),
        "rsi":             rsi(close, rsi_window),
        "macd":            macd(close, macd_fast, macd_slow, macd_signal),
        "bollinger_bands": bollinger_bands(close, bb_window, bb_k),
    }


# ── Signals ───────────────────────────────────────────────────────────────────

def _signal_to_dict(sp: _cpp.SignalPoint) -> Dict[str, Any]:
    return {
        "timestamp": sp.timestamp,
        "signal":    sp.signal_str(),
        "price":     _clean(sp.price),
    }


def sma_crossover_signals(close: List[float],
                           timestamps: List[str],
                           short_window: int = 10,
                           long_window: int = 50) -> List[Dict[str, Any]]:
    """SMA Crossover: BUY when short crosses above long, SELL when below."""
    raw = _cpp.SignalEngine.sma_crossover(close, timestamps, short_window, long_window)
    return [_signal_to_dict(s) for s in raw]


def rsi_signals(close: List[float],
                timestamps: List[str],
                window: int = 14,
                oversold: float = 30.0,
                overbought: float = 70.0) -> List[Dict[str, Any]]:
    """RSI Strategy: BUY when RSI < oversold, SELL when RSI > overbought."""
    raw = _cpp.SignalEngine.rsi_strategy(close, timestamps, window, oversold, overbought)
    return [_signal_to_dict(s) for s in raw]


def macd_signals(close: List[float],
                 timestamps: List[str],
                 fast: int = 12,
                 slow: int = 26,
                 signal: int = 9) -> List[Dict[str, Any]]:
    """MACD Crossover: BUY when MACD crosses above signal, SELL when below."""
    raw = _cpp.SignalEngine.macd_strategy(close, timestamps, fast, slow, signal)
    return [_signal_to_dict(s) for s in raw]


# ── Backtest ──────────────────────────────────────────────────────────────────

def _raw_signals(close: List[float],
                 timestamps: List[str],
                 strategy: str,
                 **kwargs) -> list:
    if strategy == "sma_crossover":
        return _cpp.SignalEngine.sma_crossover(
            close, timestamps,
            kwargs.get("short_window", 10),
            kwargs.get("long_window", 50))
    elif strategy == "rsi":
        return _cpp.SignalEngine.rsi_strategy(
            close, timestamps,
            kwargs.get("window", 14),
            kwargs.get("oversold", 30.0),
            kwargs.get("overbought", 70.0))
    elif strategy == "macd":
        return _cpp.SignalEngine.macd_strategy(
            close, timestamps,
            kwargs.get("fast", 12),
            kwargs.get("slow", 26),
            kwargs.get("signal", 9))
    else:
        raise ValueError(f"Unknown strategy: {strategy!r}. Choose: sma_crossover | rsi | macd")


def backtest(close: List[float],
             timestamps: List[str],
             strategy: Literal["sma_crossover", "rsi", "macd"] = "sma_crossover",
             **strategy_kwargs) -> Dict[str, Any]:
    """
    Run a backtest using the given strategy.

    Parameters
    ----------
    close : list of float
    timestamps : list of str
    strategy : "sma_crossover" | "rsi" | "macd"
    **strategy_kwargs : forwarded to the signal generator

    Returns
    -------
    dict: { num_trades, total_return_pct, win_rate_pct, max_drawdown_pct, trades }
    """
    signals = _raw_signals(close, timestamps, strategy, **strategy_kwargs)
    result  = _cpp.BacktestEngine.run(signals, close, timestamps)

    trades_out = [
        {
            "entry_timestamp": t.entry_timestamp,
            "exit_timestamp":  t.exit_timestamp,
            "entry_price":     _clean(t.entry_price),
            "exit_price":      _clean(t.exit_price),
            "duration_bars":   t.duration_bars,
            "pnl_pct":         _clean(t.pnl_pct),
            "is_win":          t.is_win,
        }
        for t in result.trades
    ]

    return {
        "strategy":          strategy,
        "num_trades":        result.num_trades,
        "total_return_pct":  _clean(result.total_return_pct),
        "win_rate_pct":      _clean(result.win_rate),
        "max_drawdown_pct":  _clean(result.max_drawdown_pct),
        "trades":            trades_out,
    }


# ── Benchmark ─────────────────────────────────────────────────────────────────

def benchmark(rows: int = 1_000_000) -> List[Dict[str, Any]]:
    """
    Run the full benchmark suite against N synthetic rows.

    Returns list of { name, elapsed_us, elapsed_ms, throughput_per_sec }.
    """
    close = [100.0 + 50.0 * math.sin(i * 0.001) for i in range(rows)]
    timestamps = [f"T{i}" for i in range(rows)]

    def _b(name, fn):
        r = _cpp.BenchmarkModule.measure(name, rows, fn)
        return {
            "name":               r.name,
            "elapsed_us":         r.elapsed_us,
            "elapsed_ms":         round(r.elapsed_us / 1000, 3),
            "throughput_per_sec": int(r.throughput_per_sec),
            "data_points":        r.data_points,
        }

    results = [
        _b("SMA(20)",             lambda: _cpp.IndicatorEngine.sma(close, 20)),
        _b("EMA(20)",             lambda: _cpp.IndicatorEngine.ema(close, 20)),
        _b("RSI(14)",             lambda: _cpp.IndicatorEngine.rsi(close, 14)),
        _b("MACD(12,26,9)",       lambda: _cpp.IndicatorEngine.macd(close, 12, 26, 9)),
        _b("BollingerBands(20)",  lambda: _cpp.IndicatorEngine.bollinger_bands(close, 20, 2.0)),
        _b("SMA Crossover",       lambda: _cpp.SignalEngine.sma_crossover(close, timestamps, 10, 50)),
        _b("RSI Strategy",        lambda: _cpp.SignalEngine.rsi_strategy(close, timestamps, 14)),
        _b("MACD Strategy",       lambda: _cpp.SignalEngine.macd_strategy(close, timestamps, 12, 26, 9)),
    ]

    sigs = _cpp.SignalEngine.sma_crossover(close, timestamps, 10, 50)
    results.append(_b("Backtest", lambda: _cpp.BacktestEngine.run(sigs, close, timestamps)))
    return results
