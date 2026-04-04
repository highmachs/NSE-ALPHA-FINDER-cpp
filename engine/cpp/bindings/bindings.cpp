#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "data_ingestion.hpp"
#include "indicators.hpp"
#include "signals.hpp"
#include "backtest.hpp"
#include "benchmark.hpp"

namespace py = pybind11;

PYBIND11_MODULE(nse_engine_cpp, m) {
    m.doc() = "NSE Alpha Engine — high-performance C++ quant core";

    // ── MissingValuePolicy ────────────────────────────────────────────────
    py::enum_<MissingValuePolicy>(m, "MissingValuePolicy")
        .value("DROP",         MissingValuePolicy::DROP)
        .value("FORWARD_FILL", MissingValuePolicy::FORWARD_FILL)
        .export_values();

    // ── OHLCVData ─────────────────────────────────────────────────────────
    py::class_<OHLCVData>(m, "OHLCVData")
        .def(py::init<>())
        .def_readwrite("timestamp", &OHLCVData::timestamp)
        .def_readwrite("open",      &OHLCVData::open)
        .def_readwrite("high",      &OHLCVData::high)
        .def_readwrite("low",       &OHLCVData::low)
        .def_readwrite("close",     &OHLCVData::close)
        .def_readwrite("volume",    &OHLCVData::volume)
        .def("size",                &OHLCVData::size)
        .def("__repr__", [](const OHLCVData& d) {
            return "<OHLCVData rows=" + std::to_string(d.size()) + ">";
        });

    // ── DataIngestionEngine ───────────────────────────────────────────────
    py::class_<DataIngestionEngine>(m, "DataIngestionEngine")
        .def_static("load_from_csv",
            &DataIngestionEngine::loadFromCSV,
            py::arg("filepath"),
            py::arg("policy") = MissingValuePolicy::DROP)
        .def_static("load_from_string",
            &DataIngestionEngine::loadFromString,
            py::arg("csv_content"),
            py::arg("policy") = MissingValuePolicy::DROP);

    // ── MACDResult ────────────────────────────────────────────────────────
    py::class_<MACDResult>(m, "MACDResult")
        .def(py::init<>())
        .def_readwrite("macd_line",   &MACDResult::macd_line)
        .def_readwrite("signal_line", &MACDResult::signal_line)
        .def_readwrite("histogram",   &MACDResult::histogram);

    // ── BollingerBandsResult ──────────────────────────────────────────────
    py::class_<BollingerBandsResult>(m, "BollingerBandsResult")
        .def(py::init<>())
        .def_readwrite("upper",  &BollingerBandsResult::upper)
        .def_readwrite("middle", &BollingerBandsResult::middle)
        .def_readwrite("lower",  &BollingerBandsResult::lower);

    // ── IndicatorEngine ───────────────────────────────────────────────────
    py::class_<IndicatorEngine>(m, "IndicatorEngine")
        .def_static("sma",
            &IndicatorEngine::sma,
            py::arg("close"), py::arg("window"))
        .def_static("ema",
            &IndicatorEngine::ema,
            py::arg("close"), py::arg("window"))
        .def_static("rsi",
            &IndicatorEngine::rsi,
            py::arg("close"), py::arg("window") = 14)
        .def_static("macd",
            &IndicatorEngine::macd,
            py::arg("close"),
            py::arg("fast_period")   = 12,
            py::arg("slow_period")   = 26,
            py::arg("signal_period") = 9)
        .def_static("bollinger_bands",
            &IndicatorEngine::bollingerBands,
            py::arg("close"),
            py::arg("window") = 20,
            py::arg("k")      = 2.0);

    // ── Signal enum ───────────────────────────────────────────────────────
    py::enum_<Signal>(m, "Signal")
        .value("BUY",  Signal::BUY)
        .value("SELL", Signal::SELL)
        .value("HOLD", Signal::HOLD)
        .export_values();

    // ── SignalPoint ───────────────────────────────────────────────────────
    py::class_<SignalPoint>(m, "SignalPoint")
        .def(py::init<>())
        .def_readwrite("timestamp", &SignalPoint::timestamp)
        .def_readwrite("signal",    &SignalPoint::signal)
        .def_readwrite("price",     &SignalPoint::price)
        .def("signal_str", [](const SignalPoint& sp) {
            return std::string(signalToStr(sp.signal));
        })
        .def("__repr__", [](const SignalPoint& sp) {
            return "<SignalPoint ts=" + sp.timestamp +
                   " signal=" + signalToStr(sp.signal) +
                   " price=" + std::to_string(sp.price) + ">";
        });

    // ── SignalEngine ──────────────────────────────────────────────────────
    py::class_<SignalEngine>(m, "SignalEngine")
        .def_static("sma_crossover",
            &SignalEngine::smaCrossover,
            py::arg("close"), py::arg("timestamps"),
            py::arg("short_window"), py::arg("long_window"))
        .def_static("rsi_strategy",
            &SignalEngine::rsiStrategy,
            py::arg("close"), py::arg("timestamps"),
            py::arg("window")     = 14,
            py::arg("oversold")   = 30.0,
            py::arg("overbought") = 70.0)
        .def_static("macd_strategy",
            &SignalEngine::macdStrategy,
            py::arg("close"), py::arg("timestamps"),
            py::arg("fast_period")   = 12,
            py::arg("slow_period")   = 26,
            py::arg("signal_period") = 9);

    // ── Trade ─────────────────────────────────────────────────────────────
    py::class_<Trade>(m, "Trade")
        .def(py::init<>())
        .def_readwrite("entry_timestamp", &Trade::entry_timestamp)
        .def_readwrite("exit_timestamp",  &Trade::exit_timestamp)
        .def_readwrite("entry_price",     &Trade::entry_price)
        .def_readwrite("exit_price",      &Trade::exit_price)
        .def_readwrite("duration_bars",   &Trade::duration_bars)
        .def_readwrite("pnl_pct",         &Trade::pnl_pct)
        .def_readwrite("is_win",          &Trade::is_win);

    // ── BacktestResult ────────────────────────────────────────────────────
    py::class_<BacktestResult>(m, "BacktestResult")
        .def(py::init<>())
        .def_readwrite("trades",            &BacktestResult::trades)
        .def_readwrite("total_return_pct",  &BacktestResult::total_return_pct)
        .def_readwrite("win_rate",          &BacktestResult::win_rate)
        .def_readwrite("num_trades",        &BacktestResult::num_trades)
        .def_readwrite("max_drawdown_pct",  &BacktestResult::max_drawdown_pct);

    // ── BacktestEngine ────────────────────────────────────────────────────
    py::class_<BacktestEngine>(m, "BacktestEngine")
        .def_static("run",
            &BacktestEngine::run,
            py::arg("signals"), py::arg("close"), py::arg("timestamps"));

    // ── BenchmarkResult ───────────────────────────────────────────────────
    py::class_<BenchmarkResult>(m, "BenchmarkResult")
        .def(py::init<>())
        .def_readwrite("name",               &BenchmarkResult::name)
        .def_readwrite("elapsed_us",         &BenchmarkResult::elapsed_us)
        .def_readwrite("throughput_per_sec", &BenchmarkResult::throughput_per_sec)
        .def_readwrite("data_points",        &BenchmarkResult::data_points);

    // ── BenchmarkModule ───────────────────────────────────────────────────
    py::class_<BenchmarkModule>(m, "BenchmarkModule")
        .def_static("measure",
            &BenchmarkModule::measure,
            py::arg("name"), py::arg("data_points"), py::arg("fn"))
        .def_static("now_us", &BenchmarkModule::nowUs);
}
