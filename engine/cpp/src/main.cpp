/*
 * NSE Alpha Engine — Standalone CLI
 *
 * Usage:
 *   ./nse_engine <csv_file> [strategy] [options]
 *
 * Strategies: sma_crossover | rsi | macd
 *
 * Examples:
 *   ./nse_engine data/RELIANCE.NS.csv sma_crossover
 *   ./nse_engine data/TCS.NS.csv rsi
 *   ./nse_engine data/INFY.NS.csv macd --benchmark
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <cmath>

#include "data_ingestion.hpp"
#include "indicators.hpp"
#include "signals.hpp"
#include "backtest.hpp"
#include "benchmark.hpp"

static void printBanner() {
    std::cout << "============================================\n";
    std::cout << "  NSE Alpha Engine — C++ Quant Core v1.0\n";
    std::cout << "============================================\n\n";
}

static void printSeparator() {
    std::cout << "--------------------------------------------\n";
}

static void printIndicatorSample(const std::string& name,
                                  const std::vector<double>& v,
                                  std::size_t n = 5) {
    std::cout << "  " << name << ": [";
    std::size_t printed = 0;
    for (std::size_t i = 0; i < v.size() && printed < n; ++i) {
        if (!std::isnan(v[i])) {
            if (printed > 0) std::cout << ", ";
            std::cout << std::fixed << std::setprecision(4) << v[i];
            ++printed;
        }
    }
    std::cout << " ... ]\n";
}

static void runFullPipeline(const std::string& filepath,
                             const std::string& strategy,
                             bool run_benchmark) {
    // ── 1. Data Ingestion ─────────────────────────────────────────────────
    std::cout << "[1/5] Loading data from: " << filepath << "\n";
    auto t_load = BenchmarkModule::measure("Data Load", 0,
        [&]() {});  // placeholder, actual below

    long long t0 = BenchmarkModule::nowUs();
    OHLCVData data;
    try {
        data = DataIngestionEngine::loadFromCSV(filepath, MissingValuePolicy::DROP);
    } catch (const std::exception& e) {
        std::cerr << "ERROR loading CSV: " << e.what() << "\n";
        return;
    }
    long long load_us = BenchmarkModule::nowUs() - t0;

    std::cout << "  Rows loaded  : " << data.size() << "\n";
    std::cout << "  First date   : " << data.timestamp.front() << "\n";
    std::cout << "  Last date    : " << data.timestamp.back()  << "\n";
    std::cout << "  Load time    : " << load_us << " us\n\n";

    // ── 2. Indicators ─────────────────────────────────────────────────────
    std::cout << "[2/5] Computing indicators...\n";
    auto& close = data.close;

    auto b_sma = BenchmarkModule::measure("SMA(20)",  close.size(), [&]{ IndicatorEngine::sma(close, 20); });
    auto b_ema = BenchmarkModule::measure("EMA(20)",  close.size(), [&]{ IndicatorEngine::ema(close, 20); });
    auto b_rsi = BenchmarkModule::measure("RSI(14)",  close.size(), [&]{ IndicatorEngine::rsi(close, 14); });
    auto b_mac = BenchmarkModule::measure("MACD",     close.size(), [&]{ IndicatorEngine::macd(close, 12, 26, 9); });
    auto b_bb  = BenchmarkModule::measure("BB(20)",   close.size(), [&]{ IndicatorEngine::bollingerBands(close, 20, 2.0); });

    auto sma20  = IndicatorEngine::sma(close, 20);
    auto ema20  = IndicatorEngine::ema(close, 20);
    auto rsi14  = IndicatorEngine::rsi(close, 14);
    auto macd_r = IndicatorEngine::macd(close, 12, 26, 9);
    auto bb     = IndicatorEngine::bollingerBands(close, 20, 2.0);

    printIndicatorSample("SMA(20)",     sma20);
    printIndicatorSample("EMA(20)",     ema20);
    printIndicatorSample("RSI(14)",     rsi14);
    printIndicatorSample("MACD Line",   macd_r.macd_line);
    printIndicatorSample("BB Upper",    bb.upper);
    printIndicatorSample("BB Middle",   bb.middle);
    printIndicatorSample("BB Lower",    bb.lower);

    std::cout << "\n  Timing:\n";
    for (auto& b : {b_sma, b_ema, b_rsi, b_mac, b_bb}) {
        std::cout << "    " << std::left << std::setw(12) << b.name
                  << " " << std::right << std::setw(8) << b.elapsed_us << " us"
                  << "   " << static_cast<long long>(b.throughput_per_sec / 1e6)
                  << "M pts/sec\n";
    }
    std::cout << "\n";

    // ── 3. Signals ────────────────────────────────────────────────────────
    std::cout << "[3/5] Generating signals (strategy: " << strategy << ")...\n";
    std::vector<SignalPoint> signals;
    long long sig_us = 0;
    {
        long long ts0 = BenchmarkModule::nowUs();
        try {
            if (strategy == "sma_crossover") {
                signals = SignalEngine::smaCrossover(close, data.timestamp, 10, 50);
            } else if (strategy == "rsi") {
                signals = SignalEngine::rsiStrategy(close, data.timestamp, 14, 30.0, 70.0);
            } else if (strategy == "macd") {
                signals = SignalEngine::macdStrategy(close, data.timestamp, 12, 26, 9);
            } else {
                std::cerr << "Unknown strategy: " << strategy << "\n";
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR generating signals: " << e.what() << "\n";
            return;
        }
        sig_us = BenchmarkModule::nowUs() - ts0;
    }

    int buys = 0, sells = 0, holds = 0;
    for (const auto& s : signals) {
        if (s.signal == Signal::BUY)       ++buys;
        else if (s.signal == Signal::SELL) ++sells;
        else                               ++holds;
    }
    std::cout << "  Total signals : " << signals.size() << "\n";
    std::cout << "  BUY           : " << buys << "\n";
    std::cout << "  SELL          : " << sells << "\n";
    std::cout << "  HOLD          : " << holds << "\n";
    std::cout << "  Time          : " << sig_us << " us\n\n";

    // Print first 5 non-HOLD signals
    std::cout << "  Sample signals:\n";
    int shown = 0;
    for (const auto& s : signals) {
        if (s.signal == Signal::HOLD) continue;
        std::cout << "    [" << signalToStr(s.signal) << "]"
                  << "  " << s.timestamp
                  << "  price=" << std::fixed << std::setprecision(2) << s.price << "\n";
        if (++shown >= 5) break;
    }
    std::cout << "\n";

    // ── 4. Backtest ───────────────────────────────────────────────────────
    std::cout << "[4/5] Running backtest...\n";
    BacktestResult bt;
    long long bt_us = 0;
    {
        long long ts0 = BenchmarkModule::nowUs();
        try {
            bt = BacktestEngine::run(signals, close, data.timestamp);
        } catch (const std::exception& e) {
            std::cerr << "ERROR in backtest: " << e.what() << "\n";
            return;
        }
        bt_us = BenchmarkModule::nowUs() - ts0;
    }

    std::cout << "  Trades        : " << bt.num_trades << "\n";
    std::cout << "  Total Return  : " << std::fixed << std::setprecision(2)
              << bt.total_return_pct << "%\n";
    std::cout << "  Win Rate      : " << bt.win_rate << "%\n";
    std::cout << "  Max Drawdown  : " << bt.max_drawdown_pct << "%\n";
    std::cout << "  Backtest time : " << bt_us << " us\n\n";

    if (!bt.trades.empty()) {
        std::cout << "  Trade log (first 5):\n";
        std::size_t show = std::min(bt.trades.size(), std::size_t(5));
        for (std::size_t i = 0; i < show; ++i) {
            const auto& t = bt.trades[i];
            std::cout << "    #" << (i+1) << "  "
                      << t.entry_timestamp << " → " << t.exit_timestamp
                      << "  entry=" << std::fixed << std::setprecision(2) << t.entry_price
                      << "  exit=" << t.exit_price
                      << "  PnL=" << (t.pnl_pct >= 0 ? "+" : "") << t.pnl_pct << "%"
                      << "  bars=" << t.duration_bars
                      << "  " << (t.is_win ? "WIN" : "LOSS") << "\n";
        }
        std::cout << "\n";
    }

    // ── 5. Benchmark ──────────────────────────────────────────────────────
    if (run_benchmark) {
        std::cout << "[5/5] Benchmark (1M synthetic rows)...\n";
        const std::size_t N = 1000000;
        std::vector<double> synth(N);
        std::vector<std::string> synth_ts(N);
        for (std::size_t i = 0; i < N; ++i) {
            synth[i]    = 100.0 + 50.0 * std::sin(i * 0.001);
            synth_ts[i] = "T" + std::to_string(i);
        }

        auto bench = [&](const std::string& name, std::function<void()> fn) {
            auto r = BenchmarkModule::measure(name, N, fn);
            std::cout << "  " << std::left << std::setw(26) << r.name
                      << std::right << std::setw(8) << r.elapsed_us / 1000 << " ms"
                      << "   " << std::setw(12)
                      << static_cast<long long>(r.throughput_per_sec) << " pts/sec\n";
        };

        bench("SMA(20)",            [&]{ IndicatorEngine::sma(synth, 20); });
        bench("EMA(20)",            [&]{ IndicatorEngine::ema(synth, 20); });
        bench("RSI(14)",            [&]{ IndicatorEngine::rsi(synth, 14); });
        bench("MACD(12,26,9)",      [&]{ IndicatorEngine::macd(synth, 12, 26, 9); });
        bench("BollingerBands(20)", [&]{ IndicatorEngine::bollingerBands(synth, 20, 2.0); });
        auto sigs = SignalEngine::smaCrossover(synth, synth_ts, 10, 50);
        bench("SMA Crossover",      [&]{ SignalEngine::smaCrossover(synth, synth_ts, 10, 50); });
        bench("RSI Strategy",       [&]{ SignalEngine::rsiStrategy(synth, synth_ts, 14); });
        bench("MACD Strategy",      [&]{ SignalEngine::macdStrategy(synth, synth_ts, 12, 26, 9); });
        bench("Backtest",           [&]{ BacktestEngine::run(sigs, synth, synth_ts); });
        std::cout << "\n";
    } else {
        std::cout << "[5/5] Benchmark skipped (pass --benchmark to enable)\n\n";
    }

    printSeparator();
    std::cout << "Done.\n";
}

int main(int argc, char* argv[]) {
    printBanner();

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> [strategy] [--benchmark]\n";
        std::cerr << "Strategies: sma_crossover (default) | rsi | macd\n";
        return 1;
    }

    std::string filepath  = argv[1];
    std::string strategy  = "sma_crossover";
    bool run_benchmark    = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "sma_crossover" || arg == "rsi" || arg == "macd") {
            strategy = arg;
        } else if (arg == "--benchmark") {
            run_benchmark = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    runFullPipeline(filepath, strategy, run_benchmark);
    return 0;
}
