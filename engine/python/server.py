import sys
import os
import math
import time

BUILD_OUTPUT = os.path.join(os.path.dirname(__file__), "..", "build_output")
sys.path.insert(0, os.path.abspath(BUILD_OUTPUT))

import nse_engine_cpp as engine

from fastapi import FastAPI, HTTPException, Body
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from typing import Optional, List, Literal
import uvicorn

app = FastAPI(
    title="NSE Alpha Engine API",
    description="High-performance C++ quantitative analysis library for NSE equities",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


def _sanitize(v: float) -> float:
    if math.isnan(v) or math.isinf(v):
        return None
    return v


def _san_list(lst) -> list:
    return [_sanitize(v) for v in lst]


def _ohlcv_to_dict(data: engine.OHLCVData) -> dict:
    return {
        "timestamp": list(data.timestamp),
        "open":      _san_list(data.open),
        "high":      _san_list(data.high),
        "low":       _san_list(data.low),
        "close":     _san_list(data.close),
        "volume":    _san_list(data.volume),
        "rows":      data.size(),
    }


class LoadCSVRequest(BaseModel):
    csv_content: str = Field(..., description="Raw CSV text with OHLCV columns")
    missing_policy: Literal["drop", "forward_fill"] = Field(
        "drop", description="How to handle missing/malformed rows"
    )


class IndicatorsRequest(BaseModel):
    csv_content: str = Field(..., description="Raw CSV text")
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    sma_window: int = Field(20, ge=2, description="SMA window")
    ema_window: int = Field(20, ge=2, description="EMA window")
    rsi_window: int = Field(14, ge=2, description="RSI window (Wilder's smoothing)")
    macd_fast: int = Field(12, ge=2, description="MACD fast EMA period")
    macd_slow: int = Field(26, ge=2, description="MACD slow EMA period")
    macd_signal: int = Field(9, ge=2, description="MACD signal EMA period")
    bb_window: int = Field(20, ge=2, description="Bollinger Bands window")
    bb_k: float = Field(2.0, gt=0, description="Bollinger Bands std-dev multiplier")


class SignalsRequest(BaseModel):
    csv_content: str
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    strategy: Literal["sma_crossover", "rsi", "macd"] = Field(
        ..., description="Trading signal strategy"
    )
    sma_short: int = Field(10, ge=2)
    sma_long: int = Field(50, ge=2)
    rsi_window: int = Field(14, ge=2)
    rsi_oversold: float = Field(30.0, ge=0, le=100)
    rsi_overbought: float = Field(70.0, ge=0, le=100)
    macd_fast: int = Field(12, ge=2)
    macd_slow: int = Field(26, ge=2)
    macd_signal: int = Field(9, ge=2)


class BacktestRequest(BaseModel):
    csv_content: str
    missing_policy: Literal["drop", "forward_fill"] = "drop"
    strategy: Literal["sma_crossover", "rsi", "macd"]
    sma_short: int = Field(10, ge=2)
    sma_long: int = Field(50, ge=2)
    rsi_window: int = Field(14, ge=2)
    rsi_oversold: float = Field(30.0, ge=0, le=100)
    rsi_overbought: float = Field(70.0, ge=0, le=100)
    macd_fast: int = Field(12, ge=2)
    macd_slow: int = Field(26, ge=2)
    macd_signal: int = Field(9, ge=2)


def _load_data(csv_content: str, missing_policy: str) -> engine.OHLCVData:
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
    data: engine.OHLCVData,
    req_strategy: str,
    sma_short: int,
    sma_long: int,
    rsi_window: int,
    rsi_oversold: float,
    rsi_overbought: float,
    macd_fast: int,
    macd_slow: int,
    macd_signal: int,
) -> list:
    ts = list(data.timestamp)
    close = list(data.close)
    try:
        if req_strategy == "sma_crossover":
            return engine.SignalEngine.sma_crossover(close, ts, sma_short, sma_long)
        elif req_strategy == "rsi":
            return engine.SignalEngine.rsi_strategy(close, ts, rsi_window, rsi_oversold, rsi_overbought)
        elif req_strategy == "macd":
            return engine.SignalEngine.macd_strategy(close, ts, macd_fast, macd_slow, macd_signal)
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"Signal generation error: {exc}")


@app.get("/api/healthz")
def health():
    return {"status": "ok", "engine": "NSE Alpha Engine v1.0.0"}


@app.post("/api/engine/load")
def load_csv(req: LoadCSVRequest):
    t0 = time.perf_counter_ns()
    data = _load_data(req.csv_content, req.missing_policy)
    elapsed_us = (time.perf_counter_ns() - t0) // 1000
    result = _ohlcv_to_dict(data)
    result["load_time_us"] = elapsed_us
    return result


@app.post("/api/engine/indicators")
def compute_indicators(req: IndicatorsRequest):
    data = _load_data(req.csv_content, req.missing_policy)
    close = list(data.close)
    n = len(close)

    results = {}
    timings = {}

    def timed(name: str, fn):
        t0 = time.perf_counter_ns()
        val = fn()
        timings[name + "_us"] = (time.perf_counter_ns() - t0) // 1000
        return val

    try:
        results["sma"] = _san_list(timed("sma", lambda: engine.IndicatorEngine.sma(close, req.sma_window)))
        results["ema"] = _san_list(timed("ema", lambda: engine.IndicatorEngine.ema(close, req.ema_window)))
        results["rsi"] = _san_list(timed("rsi", lambda: engine.IndicatorEngine.rsi(close, req.rsi_window)))

        macd_r = timed("macd", lambda: engine.IndicatorEngine.macd(close, req.macd_fast, req.macd_slow, req.macd_signal))
        results["macd"] = {
            "macd_line":   _san_list(macd_r.macd_line),
            "signal_line": _san_list(macd_r.signal_line),
            "histogram":   _san_list(macd_r.histogram),
        }

        bb_r = timed("bollinger_bands", lambda: engine.IndicatorEngine.bollinger_bands(close, req.bb_window, req.bb_k))
        results["bollinger_bands"] = {
            "upper":  _san_list(bb_r.upper),
            "middle": _san_list(bb_r.middle),
            "lower":  _san_list(bb_r.lower),
        }
    except Exception as exc:
        raise HTTPException(status_code=422, detail=f"Indicator error: {exc}")

    return {
        "rows": n,
        "timestamp": list(data.timestamp),
        "close": _san_list(close),
        "indicators": results,
        "computation_time_us": timings,
    }


@app.post("/api/engine/signals")
def generate_signals(req: SignalsRequest):
    data = _load_data(req.csv_content, req.missing_policy)
    t0 = time.perf_counter_ns()
    signals = _generate_signals(
        data, req.strategy,
        req.sma_short, req.sma_long,
        req.rsi_window, req.rsi_oversold, req.rsi_overbought,
        req.macd_fast, req.macd_slow, req.macd_signal,
    )
    elapsed_us = (time.perf_counter_ns() - t0) // 1000

    signal_list = [
        {
            "timestamp": sp.timestamp,
            "signal": sp.signal_str(),
            "price": _sanitize(sp.price),
        }
        for sp in signals
    ]

    counts = {"BUY": 0, "SELL": 0, "HOLD": 0}
    for s in signal_list:
        counts[s["signal"]] = counts.get(s["signal"], 0) + 1

    return {
        "strategy": req.strategy,
        "total_signals": len(signal_list),
        "counts": counts,
        "signals": signal_list,
        "generation_time_us": elapsed_us,
    }


@app.post("/api/engine/backtest")
def run_backtest(req: BacktestRequest):
    data = _load_data(req.csv_content, req.missing_policy)
    close = list(data.close)
    timestamps = list(data.timestamp)

    signals = _generate_signals(
        data, req.strategy,
        req.sma_short, req.sma_long,
        req.rsi_window, req.rsi_oversold, req.rsi_overbought,
        req.macd_fast, req.macd_slow, req.macd_signal,
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
        "strategy":          req.strategy,
        "num_trades":        result.num_trades,
        "total_return_pct":  _sanitize(result.total_return_pct),
        "win_rate_pct":      _sanitize(result.win_rate),
        "max_drawdown_pct":  _sanitize(result.max_drawdown_pct),
        "trades":            trades_out,
        "backtest_time_us":  elapsed_us,
    }


@app.get("/api/engine/benchmark")
def run_benchmark(rows: int = 1000000):
    if rows < 1000 or rows > 5000000:
        raise HTTPException(status_code=400, detail="rows must be between 1000 and 5000000")

    import math as _math
    close = [100.0 + 50.0 * _math.sin(i * 0.001) + i * 0.0001 for i in range(rows)]
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

    results = []
    results.append(bench("SMA(20)",           lambda: engine.IndicatorEngine.sma(close, 20)))
    results.append(bench("EMA(20)",           lambda: engine.IndicatorEngine.ema(close, 20)))
    results.append(bench("RSI(14)",           lambda: engine.IndicatorEngine.rsi(close, 14)))
    results.append(bench("MACD(12,26,9)",     lambda: engine.IndicatorEngine.macd(close, 12, 26, 9)))
    results.append(bench("BollingerBands(20)",lambda: engine.IndicatorEngine.bollinger_bands(close, 20, 2.0)))

    sma_short = engine.IndicatorEngine.sma(close, 10)
    sma_long  = engine.IndicatorEngine.sma(close, 50)
    signals   = engine.SignalEngine.sma_crossover(close, timestamps, 10, 50)
    results.append(bench("SMA Crossover Signals", lambda: engine.SignalEngine.sma_crossover(close, timestamps, 10, 50)))
    results.append(bench("RSI Signals",            lambda: engine.SignalEngine.rsi_strategy(close, timestamps, 14)))
    results.append(bench("MACD Signals",           lambda: engine.SignalEngine.macd_strategy(close, timestamps, 12, 26, 9)))
    results.append(bench("Backtest (SMA signals)", lambda: engine.BacktestEngine.run(signals, close, timestamps)))

    return {
        "data_points": rows,
        "benchmarks":  results,
    }


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8080"))
    uvicorn.run("server:app", host="0.0.0.0", port=port, log_level="info")
