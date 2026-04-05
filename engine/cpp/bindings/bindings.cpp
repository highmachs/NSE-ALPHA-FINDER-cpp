/**
 * @file bindings.cpp
 * @brief pybind11 module definition — exposes all C++ engine classes to Python.
 *
 * Module name: ``nse_engine_cpp``
 * Compiled to: ``engine/build_output/nse_engine_cpp.cpython-3xx-*.so``
 *
 * Exposed classes and enums:
 *   MissingValuePolicy    — DROP | FORWARD_FILL
 *   OHLCVData             — struct-of-arrays OHLCV container
 *   DataIngestionEngine   — CSV / string loader
 *   ValidationError       — per-row OHLC consistency error record
 *   DataUtils             — timestamp normalisation + OHLCV validation
 *   MACDResult            — MACD line / signal / histogram
 *   BollingerBandsResult  — upper / middle / lower bands
 *   IndicatorEngine       — SMA, EMA, RSI, MACD, Bollinger Bands
 *   Signal                — BUY | SELL | HOLD enum
 *   SignalPoint           — timestamped signal with price
 *   SignalEngine          — smaCrossover, rsiStrategy, macdStrategy
 *   Trade                 — single round-trip trade record
 *   BacktestResult        — aggregate performance metrics
 *   BacktestEngine        — event-driven backtester
 *   BenchmarkResult       — timing + throughput record
 *   BenchmarkModule       — high-resolution measurement
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "data_ingestion.hpp"
#include "data_utils.hpp"
#include "indicators.hpp"
#include "signals.hpp"
#include "backtest.hpp"
#include "benchmark.hpp"

namespace py = pybind11;

PYBIND11_MODULE(nse_engine_cpp, m) {
    m.doc() = "NSE Alpha Engine — high-performance C++ quant core (pybind11)";

    // ── MissingValuePolicy ────────────────────────────────────────────────────
    py::enum_<MissingValuePolicy>(m, "MissingValuePolicy",
            "Policy for handling malformed rows during CSV ingestion.")
        .value("DROP",         MissingValuePolicy::DROP,
               "Silently discard rows that fail schema or value validation.")
        .value("FORWARD_FILL", MissingValuePolicy::FORWARD_FILL,
               "Replace bad row with the previous valid row's values.")
        .export_values();

    // ── OHLCVData ─────────────────────────────────────────────────────────────
    py::class_<OHLCVData>(m, "OHLCVData",
            "Struct-of-arrays OHLCV container. All column vectors share the same length.")
        .def(py::init<>())
        .def_readwrite("timestamp", &OHLCVData::timestamp,
                       "List[str] — ISO 8601 date strings as parsed from source.")
        .def_readwrite("open",   &OHLCVData::open,   "List[float] — opening prices.")
        .def_readwrite("high",   &OHLCVData::high,   "List[float] — intrabar high prices.")
        .def_readwrite("low",    &OHLCVData::low,    "List[float] — intrabar low prices.")
        .def_readwrite("close",  &OHLCVData::close,  "List[float] — closing/adjusted prices.")
        .def_readwrite("volume", &OHLCVData::volume, "List[float] — trade volumes.")
        .def("size", &OHLCVData::size, "Number of rows (same for all columns).")
        .def("__repr__", [](const OHLCVData& d) {
            return "<OHLCVData rows=" + std::to_string(d.size()) + ">";
        });

    // ── DataIngestionEngine ───────────────────────────────────────────────────
    py::class_<DataIngestionEngine>(m, "DataIngestionEngine",
            "High-performance CSV ingestion engine. All methods are static.")
        .def_static("load_from_csv",
            &DataIngestionEngine::loadFromCSV,
            py::arg("filepath"),
            py::arg("policy") = MissingValuePolicy::DROP,
            "Load OHLCV data from a CSV file on disk.\n\n"
            "Args:\n"
            "    filepath: Absolute or relative path to the CSV file.\n"
            "    policy:   MissingValuePolicy.DROP or FORWARD_FILL.\n"
            "Returns:\n"
            "    OHLCVData\n"
            "Raises:\n"
            "    RuntimeError: file not found, missing columns, or no valid rows.")
        .def_static("load_from_string",
            &DataIngestionEngine::loadFromString,
            py::arg("csv_content"),
            py::arg("policy") = MissingValuePolicy::DROP,
            "Load OHLCV data from a raw CSV string.\n\n"
            "Args:\n"
            "    csv_content: Full CSV text including header row.\n"
            "    policy:      MissingValuePolicy.DROP or FORWARD_FILL.\n"
            "Returns:\n"
            "    OHLCVData\n"
            "Raises:\n"
            "    RuntimeError: missing columns, empty input, or no valid rows.");

    // ── ValidationError ───────────────────────────────────────────────────────
    py::class_<ValidationError>(m, "ValidationError",
            "Per-row OHLCV consistency error record produced by DataUtils.validate().")
        .def(py::init<>())
        .def_readwrite("row",    &ValidationError::row,
                       "int — zero-based row index in the OHLCVData struct.")
        .def_readwrite("field",  &ValidationError::field,
                       "str — name of the offending column.")
        .def_readwrite("reason", &ValidationError::reason,
                       "str — human-readable description of the violation.")
        .def("__repr__", [](const ValidationError& e) {
            return "<ValidationError row=" + std::to_string(e.row) +
                   " field=" + e.field + " reason=" + e.reason + ">";
        });

    // ── DataUtils ─────────────────────────────────────────────────────────────
    py::class_<DataUtils>(m, "DataUtils",
            "Static utilities for OHLCV timestamp normalisation and validation (PRD §6.2).")
        .def_static("normalise_timestamps",
            &DataUtils::normaliseTimestamps,
            py::arg("data"),
            "Normalise all timestamps in data to 'YYYY-MM-DD' format (in-place).\n\n"
            "Handles ISO 8601 with/without time and TZ, US MM/DD/YYYY, Bloomberg DD-Mon-YYYY.")
        .def_static("normalise_one_timestamp",
            &DataUtils::normaliseOneTimestamp,
            py::arg("ts"),
            "Parse a single timestamp string and return its 'YYYY-MM-DD' component.")
        .def_static("validate",
            &DataUtils::validate,
            py::arg("data"),
            "Validate OHLCV data for internal consistency and price sanity.\n\n"
            "Returns:\n"
            "    List[ValidationError] — empty iff all rows pass.")
        .def_static("drop_invalid_rows",
            &DataUtils::dropInvalidRows,
            py::arg("data"),
            "Return a copy of data with all invalid rows removed.");

    // ── MACDResult ────────────────────────────────────────────────────────────
    py::class_<MACDResult>(m, "MACDResult",
            "Output of IndicatorEngine.macd(). All three vectors parallel the input close series.")
        .def(py::init<>())
        .def_readwrite("macd_line",   &MACDResult::macd_line,
                       "List[float] — fast-EMA minus slow-EMA; NaN during warm-up.")
        .def_readwrite("signal_line", &MACDResult::signal_line,
                       "List[float] — EMA of macd_line; NaN during warm-up.")
        .def_readwrite("histogram",   &MACDResult::histogram,
                       "List[float] — macd_line minus signal_line (momentum bar).");

    // ── BollingerBandsResult ──────────────────────────────────────────────────
    py::class_<BollingerBandsResult>(m, "BollingerBandsResult",
            "Output of IndicatorEngine.bollinger_bands(). All vectors parallel the input.")
        .def(py::init<>())
        .def_readwrite("upper",  &BollingerBandsResult::upper,
                       "List[float] — middle + k × σ (resistance / overbought level).")
        .def_readwrite("middle", &BollingerBandsResult::middle,
                       "List[float] — SMA(close, window) — centre band.")
        .def_readwrite("lower",  &BollingerBandsResult::lower,
                       "List[float] — middle − k × σ (support / oversold level).");

    // ── IndicatorEngine ───────────────────────────────────────────────────────
    py::class_<IndicatorEngine>(m, "IndicatorEngine",
            "Stateless O(n) technical indicator engine. All methods are static.")
        .def_static("sma",
            &IndicatorEngine::sma,
            py::arg("close"), py::arg("window"),
            "Simple Moving Average — rolling arithmetic mean.\n\n"
            "Warm-up indices [0, window-2] contain NaN.\n\n"
            "Args:\n"
            "    close:  Closing price series.\n"
            "    window: Rolling window length (>= 1).\n"
            "Returns:\n"
            "    List[float] of length len(close).")
        .def_static("ema",
            &IndicatorEngine::ema,
            py::arg("close"), py::arg("window"),
            "Exponential Moving Average — alpha = 2 / (n + 1), seeded with SMA.\n\n"
            "Warm-up indices [0, window-2] contain NaN.\n\n"
            "Args:\n"
            "    close:  Closing price series.\n"
            "    window: EMA period (>= 1).\n"
            "Returns:\n"
            "    List[float] of length len(close).")
        .def_static("rsi",
            &IndicatorEngine::rsi,
            py::arg("close"), py::arg("window") = 14,
            "Relative Strength Index — Wilder's smoothing, alpha = 1/n.\n\n"
            "Warm-up indices [0, window-1] contain NaN. Values in [0, 100].\n\n"
            "Args:\n"
            "    close:  Closing price series.\n"
            "    window: RSI period (default 14).\n"
            "Returns:\n"
            "    List[float] of length len(close).")
        .def_static("macd",
            &IndicatorEngine::macd,
            py::arg("close"),
            py::arg("fast_period")   = 12,
            py::arg("slow_period")   = 26,
            py::arg("signal_period") = 9,
            "Moving Average Convergence Divergence.\n\n"
            "macd_line = EMA(fast) - EMA(slow); signal = EMA(macd_line).\n\n"
            "Args:\n"
            "    close:         Closing price series.\n"
            "    fast_period:   Fast EMA period (default 12).\n"
            "    slow_period:   Slow EMA period (default 26).\n"
            "    signal_period: Signal line EMA period (default 9).\n"
            "Returns:\n"
            "    MACDResult with macd_line, signal_line, histogram.")
        .def_static("bollinger_bands",
            &IndicatorEngine::bollingerBands,
            py::arg("close"),
            py::arg("window") = 20,
            py::arg("k")      = 2.0,
            "Bollinger Bands — SMA ± k × population SD (O(n) incremental variance).\n\n"
            "Args:\n"
            "    close:  Closing price series.\n"
            "    window: Rolling window (default 20).\n"
            "    k:      Standard deviation multiplier (default 2.0).\n"
            "Returns:\n"
            "    BollingerBandsResult with upper, middle, lower.");

    // ── Signal enum ───────────────────────────────────────────────────────────
    py::enum_<Signal>(m, "Signal", "Discrete trading signal type.")
        .value("BUY",  Signal::BUY,  "Enter a long position at this bar.")
        .value("SELL", Signal::SELL, "Exit the current long position.")
        .value("HOLD", Signal::HOLD, "No actionable event; hold current position.")
        .export_values();

    // ── SignalPoint ───────────────────────────────────────────────────────────
    py::class_<SignalPoint>(m, "SignalPoint",
            "A single timestamped trading signal with its associated closing price.")
        .def(py::init<>())
        .def_readwrite("timestamp", &SignalPoint::timestamp,
                       "str — date string matching the source OHLCVData row.")
        .def_readwrite("signal",    &SignalPoint::signal,
                       "Signal — BUY, SELL, or HOLD.")
        .def_readwrite("price",     &SignalPoint::price,
                       "float — closing price at the signal bar.")
        .def("signal_str", [](const SignalPoint& sp) {
            return std::string(signalToStr(sp.signal));
        }, "Return 'BUY', 'SELL', or 'HOLD' as a Python string.")
        .def("__repr__", [](const SignalPoint& sp) {
            return "<SignalPoint ts=" + sp.timestamp +
                   " signal=" + signalToStr(sp.signal) +
                   " price=" + std::to_string(sp.price) + ">";
        });

    // ── SignalEngine ──────────────────────────────────────────────────────────
    py::class_<SignalEngine>(m, "SignalEngine",
            "Stateless signal generation engine. All methods are static.")
        .def_static("sma_crossover",
            &SignalEngine::smaCrossover,
            py::arg("close"), py::arg("timestamps"),
            py::arg("short_window"), py::arg("long_window"),
            "SMA Crossover — trend-following.\n\n"
            "BUY when short SMA crosses above long SMA; SELL when it crosses below.\n\n"
            "Args:\n"
            "    close:        Closing price series.\n"
            "    timestamps:   Parallel timestamp strings.\n"
            "    short_window: Fast SMA period.\n"
            "    long_window:  Slow SMA period.\n"
            "Returns:\n"
            "    List[SignalPoint]")
        .def_static("rsi_strategy",
            &SignalEngine::rsiStrategy,
            py::arg("close"), py::arg("timestamps"),
            py::arg("window")     = 14,
            py::arg("oversold")   = 30.0,
            py::arg("overbought") = 70.0,
            "RSI threshold strategy — mean-reversion.\n\n"
            "BUY when RSI < oversold; SELL when RSI > overbought.\n\n"
            "Args:\n"
            "    close:       Closing price series.\n"
            "    timestamps:  Parallel timestamp strings.\n"
            "    window:      RSI period (default 14).\n"
            "    oversold:    Lower threshold (default 30).\n"
            "    overbought:  Upper threshold (default 70).\n"
            "Returns:\n"
            "    List[SignalPoint]")
        .def_static("macd_strategy",
            &SignalEngine::macdStrategy,
            py::arg("close"), py::arg("timestamps"),
            py::arg("fast_period")   = 12,
            py::arg("slow_period")   = 26,
            py::arg("signal_period") = 9,
            "MACD crossover strategy — trend-following + momentum.\n\n"
            "BUY when MACD line crosses above signal line; SELL when below.\n\n"
            "Returns:\n"
            "    List[SignalPoint]");

    // ── Trade ─────────────────────────────────────────────────────────────────
    py::class_<Trade>(m, "Trade",
            "Record for a single completed BUY→SELL round-trip trade.")
        .def(py::init<>())
        .def_readwrite("entry_timestamp", &Trade::entry_timestamp,
                       "str — timestamp of the BUY bar.")
        .def_readwrite("exit_timestamp",  &Trade::exit_timestamp,
                       "str — timestamp of the SELL bar.")
        .def_readwrite("entry_price",     &Trade::entry_price,
                       "float — execution price at entry.")
        .def_readwrite("exit_price",      &Trade::exit_price,
                       "float — execution price at exit.")
        .def_readwrite("duration_bars",   &Trade::duration_bars,
                       "int — number of bars held.")
        .def_readwrite("pnl_pct",         &Trade::pnl_pct,
                       "float — round-trip PnL as a percentage.")
        .def_readwrite("is_win",          &Trade::is_win,
                       "bool — True iff pnl_pct > 0.");

    // ── BacktestResult ────────────────────────────────────────────────────────
    py::class_<BacktestResult>(m, "BacktestResult",
            "Aggregate performance metrics for a completed backtest run.")
        .def(py::init<>())
        .def_readwrite("trades",           &BacktestResult::trades,
                       "List[Trade] — all closed round-trip trades.")
        .def_readwrite("total_return_pct", &BacktestResult::total_return_pct,
                       "float — compounded return over all trades (%).")
        .def_readwrite("win_rate",         &BacktestResult::win_rate,
                       "float — fraction of winning trades × 100.")
        .def_readwrite("num_trades",       &BacktestResult::num_trades,
                       "int — total number of closed trades.")
        .def_readwrite("max_drawdown_pct", &BacktestResult::max_drawdown_pct,
                       "float — peak-to-trough equity curve decline (%).");

    // ── BacktestEngine ────────────────────────────────────────────────────────
    py::class_<BacktestEngine>(m, "BacktestEngine",
            "Deterministic O(n) long-only backtesting engine. All methods are static.")
        .def_static("run",
            &BacktestEngine::run,
            py::arg("signals"), py::arg("close"), py::arg("timestamps"),
            "Replay a signal stream against historical close prices.\n\n"
            "Args:\n"
            "    signals:    List[SignalPoint] from SignalEngine.\n"
            "    close:      Original closing price series.\n"
            "    timestamps: Timestamps parallel to close.\n"
            "Returns:\n"
            "    BacktestResult\n"
            "Raises:\n"
            "    ValueError: size mismatch between close and timestamps.");

    // ── BenchmarkResult ───────────────────────────────────────────────────────
    py::class_<BenchmarkResult>(m, "BenchmarkResult",
            "Result of a single BenchmarkModule.measure() call.")
        .def(py::init<>())
        .def_readwrite("name",               &BenchmarkResult::name,
                       "str — human-readable label for the measured operation.")
        .def_readwrite("elapsed_us",         &BenchmarkResult::elapsed_us,
                       "int — wall-clock elapsed time in microseconds.")
        .def_readwrite("throughput_per_sec", &BenchmarkResult::throughput_per_sec,
                       "float — data_points / elapsed_seconds.")
        .def_readwrite("data_points",        &BenchmarkResult::data_points,
                       "int — number of data points passed to the operation.");

    // ── BenchmarkModule ───────────────────────────────────────────────────────
    py::class_<BenchmarkModule>(m, "BenchmarkModule",
            "Stateless high-resolution performance measurement. All methods are static.")
        .def_static("measure",
            &BenchmarkModule::measure,
            py::arg("name"), py::arg("data_points"), py::arg("fn"),
            "Measure the wall-clock time of an arbitrary callable.\n\n"
            "Args:\n"
            "    name:        Label stored in the returned BenchmarkResult.\n"
            "    data_points: Logical data points processed (for throughput calc).\n"
            "    fn:          Zero-argument callable to time (called once).\n"
            "Returns:\n"
            "    BenchmarkResult")
        .def_static("now_us", &BenchmarkModule::nowUs,
                    "Return the current wall-clock time as microseconds since epoch.");
}
