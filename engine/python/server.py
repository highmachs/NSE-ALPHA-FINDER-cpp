"""
NSE Alpha Engine — FastAPI REST Server.

Exposes the C++ engine over HTTP on the port specified by the PORT
environment variable (default 8080 for the Replit workflow).

All heavy computation is delegated to the pybind11 C++ module
``nse_engine_cpp``.  The server adds:
  - Input validation via Pydantic models.
  - NaN / Inf sanitisation before JSON serialisation.
  - Per-endpoint microsecond timing headers.
  - CORS wide-open for development (restrict in production).

Endpoints
---------
GET  /api/healthz                 Engine health check.
POST /api/engine/load             Parse CSV → OHLCV struct.
POST /api/engine/validate         OHLC consistency + price-sanity check.
POST /api/engine/standardise      Normalise timestamps + drop invalid rows.
POST /api/engine/indicators       All 5 indicators in one call.
POST /api/engine/signals          BUY/SELL/HOLD signal stream.
POST /api/engine/backtest         Full backtest with PnL metrics.
GET  /api/engine/benchmark?rows=N Performance report.

Interactive docs: http://localhost:{PORT}/docs
"""

import sys
import os
import math
import time

# ── C++ module path ────────────────────────────────────────────────────────────
BUILD_OUTPUT = os.path.join(os.path.dirname(__file__), "..", "build_output")
sys.path.insert(0, os.path.abspath(BUILD_OUTPUT))

import nse_engine_cpp as engine

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from typing import List, Literal, Optional
import uvicorn

# ── App setup ──────────────────────────────────────────────────────────────────
app = FastAPI(
    title="NSE Alpha Engine API",
    description=(
        "High-performance C++ quantitative analysis library for NSE equities. "
        "C++ core compiled with -O3; Python (FastAPI) serves the REST layer."
    ),
    version="1.0.0",
    docs_url="/docs",
    redoc_url="/redoc",
    openapi_url="/openapi.json",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# ── JSON sanitisation helpers ──────────────────────────────────────────────────

def _sanitize(v: float) -> Optional[float]:
    """Return None for NaN/Inf (not JSON-serialisable), else v."""
    if math.isnan(v) or math.isinf(v):
        return None
    return v


def _san_list(lst) -> list:
    """Sanitise an entire vector, replacing NaN/Inf with None."""
    return [_sanitize(v) for v in lst]


def _ohlcv_to_dict(data: engine.OHLCVData) -> dict:
    """Serialise an OHLCVData struct to a plain dict for JSON response."""
    return {
        "timestamp": list(data.timestamp),
        "open":      _san_list(data.open),
        "high":      _san_list(data.high),
        "low":       _san_list(data.low),
        "close":     _san_list(data.close),
        "volume":    _san_list(data.volume),
        "rows":      data.size(),
    }


# ── Pydantic request models ────────────────────────────────────────────────────

class LoadCSVRequest(BaseModel):
    """Request body for POST /api/engine/load."""
    csv_content: str = Field(
        ...,
        description="Full CSV text with OHLCV columns (including header row).",
    )
    missing_policy: Literal["drop", "forward_fill"] = Field(
        "drop",
        description="'drop' discards bad rows; 'forward_fill' reuses previous values.",
    )


class ValidateRequest(BaseModel):
    """Request body for POST /api/engine/validate."""
    csv_content: str = Field(..., description="Raw CSV text to validate.")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    normalise: bool = Field(
        True,
        description="If true, normalise timestamps before validation.",
    )


class StandardiseRequest(BaseModel):
    """Request body for POST /api/engine/standardise."""
    csv_content: str = Field(..., description="Raw CSV text to standardise.")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    drop_invalid: bool = Field(
        True,
        description="If true, remove rows that fail OHLC consistency checks.",
    )


class IndicatorsRequest(BaseModel):
    """Request body for POST /api/engine/indicators."""
    csv_content: str = Field(..., description="Raw CSV text.")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    sma_window:  int   = Field(20,   ge=2,  description="SMA rolling window.")
    ema_window:  int   = Field(20,   ge=2,  description="EMA period.")
    rsi_window:  int   = Field(14,   ge=2,  description="RSI period (Wilder's smoothing).")
    macd_fast:   int   = Field(12,   ge=2,  description="MACD fast EMA period.")
    macd_slow:   int   = Field(26,   ge=2,  description="MACD slow EMA period.")
    macd_signal: int   = Field(9,    ge=2,  description="MACD signal EMA period.")
    bb_window:   int   = Field(20,   ge=2,  description="Bollinger Bands window.")
    bb_k:        float = Field(2.0,  gt=0,  description="Bollinger Bands std-dev multiplier.")


class SignalsRequest(BaseModel):
    """Request body for POST /api/engine/signals."""
    csv_content:    str   = Field(..., description="Raw CSV text.")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    strategy:       Literal["sma_crossover", "rsi", "macd"] = Field(
        ..., description="Signal generation strategy."
    )
    sma_short:      int   = Field(10,   ge=2)
    sma_long:       int   = Field(50,   ge=2)
    rsi_window:     int   = Field(14,   ge=2)
    rsi_oversold:   float = Field(30.0, ge=0, le=100)
    rsi_overbought: float = Field(70.0, ge=0, le=100)
    macd_fast:      int   = Field(12,   ge=2)
    macd_slow:      int   = Field(26,   ge=2)
    macd_signal:    int   = Field(9,    ge=2)


class BacktestRequest(BaseModel):
    """Request body for POST /api/engine/backtest."""
    csv_content:    str   = Field(..., description="Raw CSV text.")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    strategy:       Literal["sma_crossover", "rsi", "macd"]
    sma_short:      int   = Field(10,   ge=2)
    sma_long:       int   = Field(50,   ge=2)
    rsi_window:     int   = Field(14,   ge=2)
    rsi_oversold:   float = Field(30.0, ge=0, le=100)
    rsi_overbought: float = Field(70.0, ge=0, le=100)
    macd_fast:      int   = Field(12,   ge=2)
    macd_slow:      int   = Field(26,   ge=2)
    macd_signal:    int   = Field(9,    ge=2)


# ── Internal helpers ───────────────────────────────────────────────────────────

def _load_data(csv_content: str, missing_policy: str) -> engine.OHLCVData:
    """Parse CSV string → OHLCVData, raising HTTP 422 on failure."""
    policy = (
        engine.MissingValuePolicy.FORWARD_FILL
        if missing_policy == "forward_fill"
        else engine.MissingValuePolicy.DROP
    )
    try:
        return engine.DataIngestionEngine.load_from_string(csv_content, policy)
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"CSV parse error: {exc}")


def _generate_signals(
    data:           engine.OHLCVData,
    req_strategy:   str,
    sma_short:      int,
    sma_long:       int,
    rsi_window:     int,
    rsi_oversold:   float,
    rsi_overbought: float,
    macd_fast:      int,
    macd_slow:      int,
    macd_signal:    int,
) -> list:
    """Dispatch to the chosen SignalEngine strategy; raises HTTP 422 on error."""
    ts    = list(data.timestamp)
    close = list(data.close)
    try:
        if req_strategy == "sma_crossover":
            return engine.SignalEngine.sma_crossover(close, ts, sma_short, sma_long)
        elif req_strategy == "rsi":
            return engine.SignalEngine.rsi_strategy(
                close, ts, rsi_window, rsi_oversold, rsi_overbought
            )
        elif req_strategy == "macd":
            return engine.SignalEngine.macd_strategy(
                close, ts, macd_fast, macd_slow, macd_signal
            )
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"Signal generation error: {exc}")


# ── Endpoints ──────────────────────────────────────────────────────────────────

@app.get("/api/healthz", summary="Engine health check")
def health():
    """Return engine status. Always 200 when the server is up."""
    return {"status": "ok", "engine": "NSE Alpha Engine v1.0.0"}


@app.post("/api/engine/load", summary="Parse CSV → OHLCV struct")
def load_csv(req: LoadCSVRequest):
    """
    Parse a raw CSV string into the OHLCV struct and return all columns.

    Accepts any NSE-compatible CSV (Yahoo Finance, Stooq, manual export).
    """
    t0 = time.perf_counter_ns()
    data = _load_data(req.csv_content, req.missing_policy)
    elapsed_us = (time.perf_counter_ns() - t0) // 1000
    result = _ohlcv_to_dict(data)
    result["load_time_us"] = elapsed_us
    return result


@app.post("/api/engine/validate", summary="OHLCV consistency + price-sanity check")
def validate_ohlcv(req: ValidateRequest):
    """
    Validate OHLCV data for internal consistency and price sanity (PRD §6.2).

    Checks: high ≥ open/close ≥ low, all prices > 0, volume ≥ 0.
    Returns a list of per-row errors (empty iff all rows pass).
    """
    data = _load_data(req.csv_content, req.missing_policy)
    if req.normalise:
        engine.DataUtils.normalise_timestamps(data)

    t0 = time.perf_counter_ns()
    errors = engine.DataUtils.validate(data)
    elapsed_us = (time.perf_counter_ns() - t0) // 1000

    return {
        "rows_loaded": data.size(),
        "errors_found": len(errors),
        "is_valid": len(errors) == 0,
        "errors": [
            {"row": e.row, "field": e.field, "reason": e.reason}
            for e in errors
        ],
        "validation_time_us": elapsed_us,
    }


@app.post("/api/engine/standardise", summary="Normalise timestamps + drop invalid rows")
def standardise_ohlcv(req: StandardiseRequest):
    """
    Standardise raw OHLCV data (PRD §6.2):

    1. Normalise all timestamps to 'YYYY-MM-DD' format.
    2. Optionally drop rows that fail OHLC consistency checks.

    Returns the cleaned OHLCV data ready for indicator computation.
    """
    data = _load_data(req.csv_content, req.missing_policy)
    rows_before = data.size()

    t0 = time.perf_counter_ns()
    engine.DataUtils.normalise_timestamps(data)
    if req.drop_invalid:
        data = engine.DataUtils.drop_invalid_rows(data)
    elapsed_us = (time.perf_counter_ns() - t0) // 1000

    result = _ohlcv_to_dict(data)
    result["rows_before"] = rows_before
    result["rows_dropped"] = rows_before - data.size()
    result["standardise_time_us"] = elapsed_us
    return result


@app.post("/api/engine/indicators", summary="Compute all 5 technical indicators")
def compute_indicators(req: IndicatorsRequest):
    """
    Compute SMA, EMA, RSI, MACD, and Bollinger Bands in a single call.

    Each indicator is timed independently. NaN values (warm-up period) are
    represented as null in the JSON response.
    """
    data  = _load_data(req.csv_content, req.missing_policy)
    close = list(data.close)
    n     = len(close)

    results = {}
    timings = {}

    def timed(name: str, fn):
        t0  = time.perf_counter_ns()
        val = fn()
        timings[name + "_us"] = (time.perf_counter_ns() - t0) // 1000
        return val

    try:
        results["sma"] = _san_list(
            timed("sma", lambda: engine.IndicatorEngine.sma(close, req.sma_window))
        )
        results["ema"] = _san_list(
            timed("ema", lambda: engine.IndicatorEngine.ema(close, req.ema_window))
        )
        results["rsi"] = _san_list(
            timed("rsi", lambda: engine.IndicatorEngine.rsi(close, req.rsi_window))
        )

        macd_r = timed(
            "macd",
            lambda: engine.IndicatorEngine.macd(
                close, req.macd_fast, req.macd_slow, req.macd_signal
            ),
        )
        results["macd"] = {
            "macd_line":   _san_list(macd_r.macd_line),
            "signal_line": _san_list(macd_r.signal_line),
            "histogram":   _san_list(macd_r.histogram),
        }

        bb_r = timed(
            "bollinger_bands",
            lambda: engine.IndicatorEngine.bollinger_bands(
                close, req.bb_window, req.bb_k
            ),
        )
        results["bollinger_bands"] = {
            "upper":  _san_list(bb_r.upper),
            "middle": _san_list(bb_r.middle),
            "lower":  _san_list(bb_r.lower),
        }
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"Indicator error: {exc}")

    return {
        "rows":                 n,
        "timestamp":            list(data.timestamp),
        "close":                _san_list(close),
        "indicators":           results,
        "computation_time_us":  timings,
    }


@app.post("/api/engine/signals", summary="Generate BUY/SELL/HOLD signal stream")
def generate_signals(req: SignalsRequest):
    """
    Generate a timestamped signal stream using the selected strategy.

    Returns every signal point after the indicator warm-up period,
    plus aggregate BUY/SELL/HOLD counts.
    """
    data    = _load_data(req.csv_content, req.missing_policy)
    t0      = time.perf_counter_ns()
    signals = _generate_signals(
        data, req.strategy,
        req.sma_short, req.sma_long,
        req.rsi_window, req.rsi_oversold, req.rsi_overbought,
        req.macd_fast,  req.macd_slow,    req.macd_signal,
    )
    elapsed_us = (time.perf_counter_ns() - t0) // 1000

    signal_list = [
        {
            "timestamp": sp.timestamp,
            "signal":    sp.signal_str(),
            "price":     _sanitize(sp.price),
        }
        for sp in signals
    ]

    counts = {"BUY": 0, "SELL": 0, "HOLD": 0}
    for s in signal_list:
        counts[s["signal"]] = counts.get(s["signal"], 0) + 1

    return {
        "strategy":            req.strategy,
        "total_signals":       len(signal_list),
        "counts":              counts,
        "signals":             signal_list,
        "generation_time_us":  elapsed_us,
    }


@app.post("/api/engine/backtest", summary="Run full backtest with PnL metrics")
def run_backtest(req: BacktestRequest):
    """
    Replay a signal stream against historical close prices and compute metrics.

    Long-only, one position at a time, no slippage or transaction costs.
    Metrics: total_return_pct (compounded), win_rate, num_trades, max_drawdown_pct.
    """
    data       = _load_data(req.csv_content, req.missing_policy)
    close      = list(data.close)
    timestamps = list(data.timestamp)

    signals = _generate_signals(
        data, req.strategy,
        req.sma_short,  req.sma_long,
        req.rsi_window, req.rsi_oversold, req.rsi_overbought,
        req.macd_fast,  req.macd_slow,    req.macd_signal,
    )

    t0 = time.perf_counter_ns()
    try:
        result = engine.BacktestEngine.run(signals, close, timestamps)
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"Backtest error: {exc}")
    elapsed_us = (time.perf_counter_ns() - t0) // 1000

    trades_out = [
        {
            "entry_timestamp": t.entry_timestamp,
            "exit_timestamp":  t.exit_timestamp,
            "entry_price":     _sanitize(t.entry_price),
            "exit_price":      _sanitize(t.exit_price),
            "duration_bars":   t.duration_bars,
            "pnl_pct":         _sanitize(t.pnl_pct),
            "is_win":          t.is_win,
        }
        for t in result.trades
    ]

    return {
        "strategy":         req.strategy,
        "num_trades":       result.num_trades,
        "total_return_pct": _sanitize(result.total_return_pct),
        "win_rate_pct":     _sanitize(result.win_rate),
        "max_drawdown_pct": _sanitize(result.max_drawdown_pct),
        "trades":           trades_out,
        "backtest_time_us": elapsed_us,
    }


@app.get("/api/engine/benchmark", summary="Performance report for N data points")
def run_benchmark(rows: int = 1000000):
    """
    Run all engine operations against a synthetic price series of `rows` points.

    PRD §8 targets: all indicators < 50 ms at 500 000 rows.

    Query params:
        rows: Number of synthetic data points (1 000 – 5 000 000, default 1 000 000).
    """
    if rows < 1000 or rows > 5_000_000:
        raise HTTPException(
            status_code=400,
            detail="rows must be between 1000 and 5000000",
        )

    # Deterministic synthetic price series (oscillating sine + drift)
    close      = [100.0 + 50.0 * math.sin(i * 0.001) + i * 0.0001 for i in range(rows)]
    timestamps = [f"2020-01-01T{i:09d}" for i in range(rows)]

    def bench(name, fn):
        r = engine.BenchmarkModule.measure(name, rows, fn)
        return {
            "name":               r.name,
            "elapsed_us":         r.elapsed_us,
            "elapsed_ms":         round(r.elapsed_us / 1000, 3),
            "throughput_per_sec": round(r.throughput_per_sec),
            "data_points":        r.data_points,
        }

    results = [
        bench("SMA(20)",            lambda: engine.IndicatorEngine.sma(close, 20)),
        bench("EMA(20)",            lambda: engine.IndicatorEngine.ema(close, 20)),
        bench("RSI(14)",            lambda: engine.IndicatorEngine.rsi(close, 14)),
        bench("MACD(12,26,9)",      lambda: engine.IndicatorEngine.macd(close, 12, 26, 9)),
        bench("BollingerBands(20)", lambda: engine.IndicatorEngine.bollinger_bands(close, 20, 2.0)),
    ]

    # Pre-compute signals so backtest bench is timing only the backtest step
    signals = engine.SignalEngine.sma_crossover(close, timestamps, 10, 50)
    results += [
        bench("SMA Crossover Signals",
              lambda: engine.SignalEngine.sma_crossover(close, timestamps, 10, 50)),
        bench("RSI Signals",
              lambda: engine.SignalEngine.rsi_strategy(close, timestamps, 14)),
        bench("MACD Signals",
              lambda: engine.SignalEngine.macd_strategy(close, timestamps, 12, 26, 9)),
        bench("Backtest (SMA signals)",
              lambda: engine.BacktestEngine.run(signals, close, timestamps)),
    ]

    return {
        "data_points": rows,
        "benchmarks":  results,
    }


# ── Entry point ────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8080"))
    uvicorn.run("server:app", host="0.0.0.0", port=port, log_level="info")
