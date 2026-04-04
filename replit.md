# NSE Alpha Engine

## Overview

High-performance quantitative analysis library for NSE equities. Pure C++17 core with Python (FastAPI) serving a REST API. No Node.js in the quant engine path.

## Stack

- **Core engine**: C++17 (GCC 14)
- **Python bindings**: pybind11 2.13.6 — exposes all C++ classes to Python
- **API server**: Python 3.12 + FastAPI + uvicorn (port 8000)
- **Build system**: CMake 3.31
- **Monorepo tool**: pnpm workspaces (Node.js server still registered as artifact but separate from engine)

## Architecture

```
engine/
├── cpp/
│   ├── include/          # C++ headers
│   │   ├── data_ingestion.hpp
│   │   ├── indicators.hpp
│   │   ├── signals.hpp
│   │   ├── backtest.hpp
│   │   └── benchmark.hpp
│   ├── src/              # C++ implementations
│   │   ├── data_ingestion.cpp
│   │   ├── indicators.cpp
│   │   ├── signals.cpp
│   │   ├── backtest.cpp
│   │   └── benchmark.cpp
│   └── bindings/
│       └── bindings.cpp  # pybind11 module (nse_engine_cpp)
├── python/
│   └── server.py         # FastAPI REST server
├── build_output/         # compiled .so module
├── build/                # cmake build cache
├── CMakeLists.txt
└── build.sh              # rebuild script
```

## Key Commands

- Rebuild C++: `cd engine && cmake --build build --parallel 4`
- Full rebuild (after CMake changes): `cd engine && bash build.sh`
- Run server manually: `PORT=8000 python3 engine/python/server.py`
- Workflow: "NSE Alpha Engine (Python)" runs on port 8000

## REST API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/healthz` | Health check |
| POST | `/api/engine/load` | Parse CSV → OHLCV struct |
| POST | `/api/engine/indicators` | SMA, EMA, RSI, MACD, Bollinger Bands |
| POST | `/api/engine/signals` | BUY/SELL/HOLD signal generation |
| POST | `/api/engine/backtest` | Simulate trades, compute metrics |
| GET | `/api/engine/benchmark?rows=N` | Performance benchmark (default 1M rows) |

## C++ Modules

### DataIngestionEngine
- `load_from_csv(filepath, policy)` — load from file
- `load_from_string(csv_content, policy)` — load from raw CSV string
- Struct-of-arrays layout for cache efficiency
- Schema validation: timestamp, open, high, low, close, volume
- MissingValuePolicy: DROP or FORWARD_FILL

### IndicatorEngine (all O(n), preallocated buffers)
- `sma(close, window)` — rolling mean
- `ema(close, window)` — alpha = 2/(n+1)
- `rsi(close, window)` — Wilder's smoothing (alpha = 1/n)
- `macd(close, fast, slow, signal)` — EMA(fast) - EMA(slow), then EMA of MACD
- `bollinger_bands(close, window, k)` — SMA ± k×σ, O(n) incremental variance

### SignalEngine
- `sma_crossover(close, timestamps, short_win, long_win)` — crossover detection
- `rsi_strategy(close, timestamps, window, oversold, overbought)` — threshold
- `macd_strategy(close, timestamps, fast, slow, signal)` — MACD/signal crossover

### BacktestEngine
- `run(signals, close, timestamps)` — long-only, one position at a time
- Metrics: total_return_pct, win_rate, num_trades, max_drawdown_pct

### BenchmarkModule
- `measure(name, data_points, fn)` — high-resolution chrono timing
- Returns elapsed_us and throughput_per_sec

## Performance (1M data points)
- SMA(20): ~66ms
- EMA(20): ~66ms
- RSI(14): ~66ms
- MACD(12,26,9): ~50ms
- Bollinger Bands(20): ~20ms (O(n) incremental variance)
- All indicators: deterministic, zero dynamic allocations in hot loops

## Supported Data Sources
NSE CSV, Yahoo Finance (.NS tickers), Stooq, Alpha Vantage — any CSV with timestamp/open/high/low/close/volume columns.
