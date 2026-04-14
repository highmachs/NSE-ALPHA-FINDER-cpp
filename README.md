# NSE Alpha Engine

## Overview

High-performance quantitative analysis library for NSE equities.
Pure C++17 core, pybind11 Python bindings, FastAPI REST server.
All Node.js artefacts have been removed from this project.

## Stack

| Layer | Technology |
|-------|-----------|
| Quant core | C++17, GCC 14, -O3, struct-of-arrays |
| Bindings | pybind11 2.13.6 |
| REST API | Python 3.12, FastAPI, uvicorn |
| Build | CMake 3.31 (3 parallel targets) |
| Data | Yahoo Finance, Alpha Vantage |

## File Structure

```
engine/
├── cpp/
│   ├── include/
│   │   ├── data_ingestion.hpp   # OHLCVData, MissingValuePolicy, DataIngestionEngine
│   │   ├── data_utils.hpp       # ValidationError, DataUtils (PRD §6.2)
│   │   ├── indicators.hpp       # IndicatorEngine, MACDResult, BollingerBandsResult
│   │   ├── signals.hpp          # Signal enum, SignalPoint, SignalEngine
│   │   ├── backtest.hpp         # Trade, BacktestResult, BacktestEngine
│   │   └── benchmark.hpp        # BenchmarkResult, BenchmarkModule
│   ├── src/
│   │   ├── data_ingestion.cpp   # CSV parser, schema validation, stream ingestion
│   │   ├── data_utils.cpp       # Timestamp normalisation, OHLC validation
│   │   ├── indicators.cpp       # SMA, EMA, RSI, MACD, Bollinger Bands
│   │   ├── signals.cpp          # SMA crossover, RSI, MACD strategies
│   │   ├── backtest.cpp         # Trade simulation, equity curve, max drawdown
│   │   ├── benchmark.cpp        # High-resolution chrono timing
│   │   └── main.cpp             # Standalone CLI executable
│   ├── bindings/
│   │   └── bindings.cpp         # pybind11 module: nse_engine_cpp (full docstrings)
│   └── tests/
│       ├── test_runner.hpp      # test::check, test::near, test::suite, test::summary
│       ├── test_runner.cpp      # Global counter definitions
│       ├── test_main.cpp        # Entry point, registers all 5 suites
│       ├── test_data_ingestion.cpp   # 15 tests
│       ├── test_indicators.cpp       # 200+ tests (SMA, EMA, RSI, MACD, BB)
│       ├── test_signals.cpp          # Strategy tests
│       ├── test_backtest.cpp         # Backtest correctness + edge cases
│       └── test_reference_values.cpp # PRD §9: hand-verified reference values
├── python/
│   ├── __init__.py          # Python package root
│   ├── server.py            # FastAPI REST server (8 endpoints, full docstrings)
│   ├── nse_engine.py        # High-level Python wrapper with docstrings
│   └── data_fetcher.py      # Yahoo Finance + Stooq + Alpha Vantage (full docs)
├── data/
│   └── sample_RELIANCE.csv  # 252-row realistic OHLCV sample for testing
├── build_output/
│   ├── nse_engine_cpp.cpython-312-x86_64-linux-gnu.so
│   ├── nse_engine           # Standalone CLI binary
│   └── nse_tests            # Unit test binary (647 tests)
├── build/                   # CMake cache
├── CMakeLists.txt           # 3 targets: pybind11 module, CLI, test runner
├── build.sh                 # Quick rebuild script
├── run.sh                   # Unified CLI: build/test/serve/cli/benchmark/fetch
├── Doxyfile                 # Doxygen config (doxygen Doxyfile → docs/html/)
└── requirements.txt         # Python dependencies
```

## Key Commands

```bash
bash engine/run.sh build           # Rebuild all C++ targets
bash engine/run.sh test            # Run 647 C++ unit tests
bash engine/run.sh serve 8000      # Start FastAPI server
bash engine/run.sh cli engine/data/sample_RELIANCE.csv sma_crossover
bash engine/run.sh benchmark 1000000
bash engine/run.sh fetch RELIANCE 2020-01-01 2024-12-31 auto data/
```

## REST API Endpoints (port 8000)

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/healthz` | Engine health check |
| POST | `/api/engine/load` | Parse CSV → OHLCV struct |
| POST | `/api/engine/validate` | OHLC consistency + price-sanity check (PRD §6.2) |
| POST | `/api/engine/standardise` | Normalise timestamps + drop invalid rows |
| POST | `/api/engine/indicators` | All 5 indicators in one call |
| POST | `/api/engine/signals` | BUY/SELL/HOLD signal stream |
| POST | `/api/engine/backtest` | Full backtest with PnL metrics |
| GET  | `/api/engine/benchmark?rows=N` | Performance report |

Interactive docs: `localhost:8000/docs`

## C++ Engine Modules

### DataIngestionEngine
- Load from file or raw CSV string, single O(n) pass, no intermediate copies
- Struct-of-arrays layout (cache-friendly)
- Accepts: timestamp|date|datetime, close|adj close, volume|vol (case-insensitive)
- MissingValuePolicy: DROP or FORWARD_FILL

### DataUtils (PRD §6.2)
- `normaliseTimestamps(data)` — ISO 8601, US MM/DD/YYYY, Bloomberg DD-Mon-YYYY → YYYY-MM-DD
- `validate(data)` → `vector<ValidationError>` — checks high≥open/close≥low, all prices > 0
- `dropInvalidRows(data)` — returns copy with bad rows removed

### IndicatorEngine — O(n), preallocated buffers, no heap allocs in loops
- `sma(close, n)` — O(1)-per-step rolling sum
- `ema(close, n)` — α = 2/(n+1), SMA-seeded
- `rsi(close, n)` — Wilder's smoothing, α = 1/n
- `macd(close, fast, slow, signal)` — dual EMA, EMA-of-MACD-line
- `bollingerBands(close, n, k)` — O(n) incremental variance (sum + sum²)

### SignalEngine
- `smaCrossover` — BUY/SELL on short×long SMA crossing
- `rsiStrategy` — BUY when RSI < 30, SELL when RSI > 70
- `macdStrategy` — BUY/SELL on MACD×signal crossing

### BacktestEngine — long-only, one position at a time
- Entry on BUY, exit on SELL, ignores double-BUY
- Metrics: total_return_pct (compounded), win_rate, num_trades, max_drawdown_pct

### BenchmarkModule
- `measure(name, n, fn)` — high_resolution_clock, returns elapsed_us + throughput/sec

## Unit Tests — 647 total, 647 PASS, 0 FAIL

| Suite | Tests | Description |
|-------|-------|-------------|
| DataIngestionEngine | 15 | Valid CSV, DROP/FORWARD_FILL, alt headers, exceptions |
| IndicatorEngine | 200+ | SMA vs naive, EMA alpha, RSI edge cases, MACD, BB |
| SignalEngine | ~30 | Crossover detection, size mismatch throws |
| BacktestEngine | ~40 | Empty, single win/loss, compounding, drawdown |
| Reference Values | ~36 | Hand-verified values (PRD §9), DataUtils, timestamp norm |

## Performance (500 000 rows, port 8000)

| Indicator | Time | PRD Limit |
|-----------|------|-----------|
| SMA(20) | ~38ms | 50ms ✓ |
| EMA(20) | ~39ms | 50ms ✓ |
| RSI(14) | ~37ms | 50ms ✓ |
| MACD(12,26,9) | ~27ms | 50ms ✓ |
| BollingerBands(20) | ~10ms | 50ms ✓ |

## Data Sources (PRD §6)

| Source | Access | Notes |
|--------|--------|-------|
| Yahoo Finance | `yfinance` library | `.NS` suffix, adjusted close |
| Stooq | HTTP CSV feed | `.IN` suffix, no auth required |
| Alpha Vantage | REST API | Requires `ALPHA_VANTAGE_KEY` env var, 25 req/day free |
| auto | Yahoo → Stooq fallback | Default for `data_fetcher.fetch()` |

## Documentation

All C++ headers have full Doxygen-style documentation:
- Function descriptions with @param, @return, @throws
- Class-level and file-level overview blocks
- `engine/Doxyfile` — run `doxygen Doxyfile` to generate HTML docs in `engine/docs/`

All Python files have comprehensive module-level and function-level docstrings.

## Workflow

- **NSE Alpha Engine (Python)** — runs on port 8000
  - Command: `python engine/python/server.py`
