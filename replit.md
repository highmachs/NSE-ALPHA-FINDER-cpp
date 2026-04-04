# NSE Alpha Engine

## Overview

High-performance quantitative analysis library for NSE equities.
Pure C++17 core, pybind11 Python bindings, FastAPI REST server, and a standalone C++ CLI.
No Node.js in the quant engine path.

## Stack

- **Core engine**: C++17 (GCC 14), O3, struct-of-arrays layout
- **Python bindings**: pybind11 2.13.6 — all C++ classes exposed to Python
- **API server**: Python 3.12 + FastAPI + uvicorn (port 8000)
- **Build system**: CMake 3.31 (builds 3 targets in parallel)

## File Structure

```
engine/
├── cpp/
│   ├── include/
│   │   ├── data_ingestion.hpp   # OHLCVData, MissingValuePolicy, DataIngestionEngine
│   │   ├── indicators.hpp       # IndicatorEngine, MACDResult, BollingerBandsResult
│   │   ├── signals.hpp          # Signal enum, SignalPoint, SignalEngine
│   │   ├── backtest.hpp         # Trade, BacktestResult, BacktestEngine
│   │   └── benchmark.hpp        # BenchmarkResult, BenchmarkModule
│   ├── src/
│   │   ├── data_ingestion.cpp   # CSV parser, schema validation, stream ingestion
│   │   ├── indicators.cpp       # SMA, EMA, RSI, MACD, Bollinger Bands
│   │   ├── signals.cpp          # SMA crossover, RSI threshold, MACD crossover
│   │   ├── backtest.cpp         # Trade simulation, PnL, drawdown
│   │   ├── benchmark.cpp        # High-resolution chrono timing
│   │   └── main.cpp             # Standalone CLI executable
│   ├── bindings/
│   │   └── bindings.cpp         # pybind11 module: nse_engine_cpp
│   └── tests/
│       ├── test_runner.hpp      # Minimal test framework (extern counters)
│       ├── test_runner.cpp      # Global counter definitions
│       ├── test_main.cpp        # Test entry point, calls all suites
│       ├── test_data_ingestion.cpp   # 15 tests
│       ├── test_indicators.cpp       # 200+ tests (SMA, EMA, RSI, MACD, BB)
│       ├── test_signals.cpp          # Signal strategy tests
│       └── test_backtest.cpp         # Backtest correctness + edge cases
├── python/
│   ├── server.py          # FastAPI REST server
│   ├── nse_engine.py      # High-level Python wrapper over C++ bindings
│   └── data_fetcher.py    # Download NSE data: Yahoo Finance + Stooq
├── build_output/
│   ├── nse_engine_cpp.cpython-312-x86_64-linux-gnu.so  # Python module
│   ├── nse_engine        # Standalone CLI binary
│   └── nse_tests         # Unit test binary
├── build/                # CMake cache
├── CMakeLists.txt        # 3 targets: pybind11 module, CLI, test runner
├── build.sh              # Quick rebuild script
└── run.sh                # Unified run script (build/test/serve/cli/benchmark/fetch)
```

## Key Commands

```bash
# Build all C++ targets
bash engine/run.sh build

# Run 611 C++ unit tests
bash engine/run.sh test

# Start FastAPI server (port 8000)
bash engine/run.sh serve 8000

# Run CLI on your CSV
bash engine/run.sh cli data/RELIANCE.NS.csv sma_crossover --benchmark

# Performance benchmark (1M rows)
bash engine/run.sh benchmark 1000000

# Download NSE data
bash engine/run.sh fetch RELIANCE 2020-01-01 2024-12-31 auto data/
```

## REST API Endpoints (port 8000)

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/healthz` | Engine health check |
| POST | `/api/engine/load` | Parse CSV → OHLCV struct |
| POST | `/api/engine/indicators` | All 5 indicators in one call |
| POST | `/api/engine/signals` | BUY/SELL/HOLD with strategy choice |
| POST | `/api/engine/backtest` | Full backtest with metrics |
| GET  | `/api/engine/benchmark?rows=N` | Performance report |

Interactive docs: `localhost:8000/docs`

## C++ Engine Modules

### DataIngestionEngine
- Load from file or raw CSV string
- Struct-of-arrays layout (cache-friendly)
- Strict schema validation: timestamp, open, high, low, close, volume
- Alternate header names: date/datetime, adj close, vol
- MissingValuePolicy: DROP or FORWARD_FILL

### IndicatorEngine — all O(n), preallocated buffers, no heap allocs in loops
- `sma(close, window)` — rolling mean, O(n) rolling sum
- `ema(close, window)` — alpha = 2/(n+1), SMA-seeded
- `rsi(close, window)` — Wilder's smoothing, alpha = 1/n
- `macd(close, fast, slow, signal)` — dual EMA, then EMA of MACD line
- `bollinger_bands(close, window, k)` — O(n) incremental variance (sum + sum_sq)

### SignalEngine
- `sma_crossover` — BUY/SELL on short×long crossing, HOLD otherwise
- `rsi_strategy` — BUY when RSI < oversold, SELL when RSI > overbought
- `macd_strategy` — BUY/SELL on MACD×signal line crossing

### BacktestEngine — long-only, one position at a time
- Entry on BUY, exit on SELL, ignores double-BUY
- Metrics: total_return_pct (compounded), win_rate, num_trades, max_drawdown_pct
- O(n) scan of signal list

### BenchmarkModule
- `measure(name, n, fn)` — high_resolution_clock, returns elapsed_us + throughput_per_sec
- `now_us()` — raw microsecond timestamp

## Unit Tests — 611 total, 611 PASS

| Suite | Tests |
|-------|-------|
| DataIngestionEngine | 15 |
| IndicatorEngine SMA | 160+ |
| IndicatorEngine EMA | 50+ |
| IndicatorEngine RSI | edge cases + bounds |
| IndicatorEngine MACD | warmup, histogram identity |
| IndicatorEngine BB | upper≥middle≥lower, flat prices |
| SignalEngine | crossover detection, size mismatch throws |
| BacktestEngine | empty, single win/loss, compounding, drawdown, duration |

## Performance (500k rows, port 8000)

| Indicator | Time | PRD Limit |
|-----------|------|-----------|
| SMA(20) | ~38ms | 50ms ✓ |
| EMA(20) | ~39ms | 50ms ✓ |
| RSI(14) | ~37ms | 50ms ✓ |
| MACD(12,26,9) | ~27ms | 50ms ✓ |
| BollingerBands(20) | ~10ms | 50ms ✓ |

## Data Sources (PRD §6)
- **Yahoo Finance**: `data_fetcher.fetch("RELIANCE", start, end, source="yahoo")`
- **Stooq**: `data_fetcher.fetch("RELIANCE", start, end, source="stooq")`
- **Auto fallback**: tries Yahoo first, then Stooq
- Outputs standardized CSV: `timestamp,open,high,low,close,volume`

## Workflow
- **NSE Alpha Engine (Python)** — runs on port 8000
  - Command: `PORT=8000 python3 /home/runner/workspace/engine/python/server.py`
