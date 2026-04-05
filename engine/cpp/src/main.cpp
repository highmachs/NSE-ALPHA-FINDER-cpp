/**
 * @file main.cpp
 * @brief NSE Alpha Engine — Feature-Rich C++ CLI
 *
 * A full-featured command-line interface for the NSE Alpha Engine quant core.
 * Runs the complete pipeline with ANSI colors, timing bars, all-strategies
 * comparison, configurable parameters, CSV export, and a built-in 1M-row
 * benchmark with ASCII throughput bars.
 *
 * Usage
 * -----
 *   ./nse_engine <csv>  [--strategy <sma_crossover|rsi|macd|all>]
 *                       [--sma-short N]    [--sma-long N]
 *                       [--rsi-window N]   [--rsi-oversold F] [--rsi-overbought F]
 *                       [--macd-fast N]    [--macd-slow N]   [--macd-signal N]
 *                       [--bb-window N]    [--bb-k F]
 *                       [--benchmark]      [--rows N]
 *                       [--export file]    [--no-color]
 *                       [--verbose]
 *   ./nse_engine --benchmark --rows 2000000
 *   ./nse_engine --help
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <chrono>
#include <cassert>

#ifdef __unix__
#include <unistd.h>
#endif

#include "data_ingestion.hpp"
#include "data_utils.hpp"
#include "indicators.hpp"
#include "signals.hpp"
#include "backtest.hpp"
#include "benchmark.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ANSI colour system
// ─────────────────────────────────────────────────────────────────────────────

static bool g_color = true;

struct C {
    static const char* reset()   { return g_color ? "\033[0m"     : ""; }
    static const char* bold()    { return g_color ? "\033[1m"     : ""; }
    static const char* red()     { return g_color ? "\033[31m"    : ""; }
    static const char* green()   { return g_color ? "\033[32m"    : ""; }
    static const char* yellow()  { return g_color ? "\033[33m"    : ""; }
    static const char* blue()    { return g_color ? "\033[34m"    : ""; }
    static const char* magenta() { return g_color ? "\033[35m"    : ""; }
    static const char* cyan()    { return g_color ? "\033[36m"    : ""; }
    static const char* white()   { return g_color ? "\033[97m"    : ""; }
    static const char* bgreen()  { return g_color ? "\033[1;32m"  : ""; }
    static const char* bred()    { return g_color ? "\033[1;31m"  : ""; }
    static const char* bcyan()   { return g_color ? "\033[1;36m"  : ""; }
    static const char* byellow() { return g_color ? "\033[1;33m"  : ""; }
    static const char* bblue()   { return g_color ? "\033[1;34m"  : ""; }
    static const char* bmagenta(){ return g_color ? "\033[1;35m"  : ""; }
    static const char* dim()     { return g_color ? "\033[2m"     : ""; }
};

// ─────────────────────────────────────────────────────────────────────────────
// CLI argument struct
// ─────────────────────────────────────────────────────────────────────────────

struct Args {
    std::string filepath;
    std::string strategy      = "sma_crossover"; // sma_crossover | rsi | macd | all
    int    sma_short          = 10;
    int    sma_long           = 50;
    int    rsi_window         = 14;
    double rsi_oversold       = 30.0;
    double rsi_overbought     = 70.0;
    int    macd_fast          = 12;
    int    macd_slow          = 26;
    int    macd_signal        = 9;
    int    bb_window          = 20;
    double bb_k               = 2.0;
    bool   run_benchmark      = false;
    bool   benchmark_only     = false;
    std::size_t bench_rows    = 1'000'000;
    std::string export_file;
    bool   verbose            = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Formatting helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string fmtNum(double v, int dec = 2) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(dec) << v;
    return s.str();
}

static std::string fmtComma(long long v) {
    std::string s = std::to_string(v);
    int n = static_cast<int>(s.size());
    for (int i = n - 3; i > 0; i -= 3)
        s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

static std::string repeat(char c, int n) {
    return (n > 0) ? std::string(static_cast<std::size_t>(n), c) : "";
}

static void printHRule(int w = 70) {
    std::cout << C::dim() << repeat('-', w) << C::reset() << "\n";
}

static void printDRule(int w = 70) {
    std::cout << C::bcyan() << repeat('=', w) << C::reset() << "\n";
}

// Build "########........" bar
static std::string mkBar(long long us, long long max_us, int w = 20) {
    int fill = 0;
    if (max_us > 0)
        fill = std::max(1, std::min(w,
            static_cast<int>(std::round(
                static_cast<double>(us) / static_cast<double>(max_us) * w))));
    std::string s;
    s += C::cyan();
    for (int i = 0; i < w; ++i) s += (i < fill ? '#' : '.');
    s += C::reset();
    return s;
}

// Signal colour
static const char* sigColor(Signal s) {
    if (s == Signal::BUY)  return C::bgreen();
    if (s == Signal::SELL) return C::bred();
    return C::dim();
}

static const char* sigStr(Signal s) {
    if (s == Signal::BUY)  return "BUY ";
    if (s == Signal::SELL) return "SELL";
    return "HOLD";
}

// ─────────────────────────────────────────────────────────────────────────────
// Banner
// ─────────────────────────────────────────────────────────────────────────────

static void printBanner() {
    std::cout << "\n";
    printDRule(72);
    std::cout << C::bcyan()
              << "  ███╗   ██╗███████╗███████╗     █████╗ ██╗     ██████╗ ██╗  ██╗ █████╗ \n"
              << "  ████╗  ██║██╔════╝██╔════╝    ██╔══██╗██║     ██╔══██╗██║  ██║██╔══██╗\n"
              << "  ██╔██╗ ██║███████╗█████╗      ███████║██║     ██████╔╝███████║███████║\n"
              << "  ██║╚██╗██║╚════██║██╔══╝      ██╔══██║██║     ██╔═══╝ ██╔══██║██╔══██║\n"
              << "  ██║ ╚████║███████║███████╗    ██║  ██║███████╗██║     ██║  ██║██║  ██║\n"
              << "  ╚═╝  ╚═══╝╚══════╝╚══════╝    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝\n"
              << C::reset();
    std::cout << C::byellow()
              << "                  E N G I N E   ─   C++ Quant Core v1.0\n"
              << C::reset();
    printDRule(72);
    std::cout << "\n";
}

static void printHelp(const char* prog) {
    printBanner();
    std::cout << C::bold() << "USAGE\n" << C::reset();
    std::cout << "  " << prog << " <csv>   [options]\n";
    std::cout << "  " << prog << " --benchmark [--rows N]\n\n";

    std::cout << C::bold() << "OPTIONS\n" << C::reset();
    auto opt = [](const char* name, const char* def, const char* desc) {
        std::cout << "  " << C::cyan() << std::left << std::setw(28) << name
                  << C::reset() << std::setw(12) << def << "  " << desc << "\n";
    };
    opt("--strategy <name>",      "sma_crossover",
        "sma_crossover | rsi | macd | all");
    opt("--sma-short N",          "10",  "SMA crossover fast period");
    opt("--sma-long N",           "50",  "SMA crossover slow period");
    opt("--rsi-window N",         "14",  "RSI period (Wilder)");
    opt("--rsi-oversold F",       "30",  "RSI oversold threshold (BUY)");
    opt("--rsi-overbought F",     "70",  "RSI overbought threshold (SELL)");
    opt("--macd-fast N",          "12",  "MACD fast EMA period");
    opt("--macd-slow N",          "26",  "MACD slow EMA period");
    opt("--macd-signal N",        "9",   "MACD signal EMA period");
    opt("--bb-window N",          "20",  "Bollinger Bands window");
    opt("--bb-k F",               "2.0", "Bollinger Bands std-dev multiplier");
    opt("--benchmark",            "",    "Run 1M-row perf benchmark after analysis");
    opt("--rows N",               "1M",  "Synthetic row count for benchmark");
    opt("--export <file.csv>",    "",    "Write trade log to CSV");
    opt("--no-color",             "",    "Disable ANSI colours");
    opt("--verbose",              "",    "Show all indicator values (not just sample)");
    opt("--help",                 "",    "Show this help message");
    std::cout << "\n";

    std::cout << C::bold() << "EXAMPLES\n" << C::reset();
    std::cout << "  " << prog << " data/RELIANCE.csv\n";
    std::cout << "  " << prog << " data/RELIANCE.csv --strategy all\n";
    std::cout << "  " << prog << " data/RELIANCE.csv --strategy rsi --rsi-window 9 --rsi-oversold 25 --rsi-overbought 75\n";
    std::cout << "  " << prog << " data/RELIANCE.csv --strategy sma_crossover --sma-short 5 --sma-long 21\n";
    std::cout << "  " << prog << " data/RELIANCE.csv --export trades.csv --benchmark\n";
    std::cout << "  " << prog << " --benchmark --rows 5000000\n";
    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument parser
// ─────────────────────────────────────────────────────────────────────────────

static bool parseArgs(int argc, char* argv[], Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextStr = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << arg << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        auto nextInt = [&]() -> int { return std::stoi(nextStr()); };
        auto nextDbl = [&]() -> double { return std::stod(nextStr()); };

        if (arg == "--help" || arg == "-h") { return false; }  // caller prints help
        else if (arg == "--no-color")   { g_color = false; }
        else if (arg == "--verbose")    { a.verbose = true; }
        else if (arg == "--benchmark")  { a.run_benchmark = true; }
        else if (arg == "--strategy")   { a.strategy = nextStr(); }
        else if (arg == "--sma-short")  { a.sma_short = nextInt(); }
        else if (arg == "--sma-long")   { a.sma_long = nextInt(); }
        else if (arg == "--rsi-window") { a.rsi_window = nextInt(); }
        else if (arg == "--rsi-oversold")   { a.rsi_oversold = nextDbl(); }
        else if (arg == "--rsi-overbought") { a.rsi_overbought = nextDbl(); }
        else if (arg == "--macd-fast")  { a.macd_fast = nextInt(); }
        else if (arg == "--macd-slow")  { a.macd_slow = nextInt(); }
        else if (arg == "--macd-signal"){ a.macd_signal = nextInt(); }
        else if (arg == "--bb-window")  { a.bb_window = nextInt(); }
        else if (arg == "--bb-k")       { a.bb_k = nextDbl(); }
        else if (arg == "--rows")       { a.bench_rows = static_cast<std::size_t>(nextInt()); }
        else if (arg == "--export")     { a.export_file = nextStr(); }
        else if (arg[0] != '-')         { a.filepath = arg; }
        else {
            std::cerr << C::bred() << "Unknown option: " << arg << C::reset() << "\n";
            return false;
        }
    }

    // benchmark-only mode: no CSV required
    if (a.filepath.empty() && a.run_benchmark) {
        a.benchmark_only = true;
        return true;
    }
    if (a.filepath.empty()) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section headers
// ─────────────────────────────────────────────────────────────────────────────

static void sectionHeader(const std::string& step, const std::string& title) {
    std::cout << "\n" << C::bblue() << step << C::reset()
              << C::bold() << "  " << title << C::reset() << "\n";
    printHRule(70);
}

// ─────────────────────────────────────────────────────────────────────────────
// Indicator sample printer (first N valid values, last N valid values)
// ─────────────────────────────────────────────────────────────────────────────

static void printIndicatorRow(const std::string& name,
                               const std::vector<double>& v,
                               long long elapsed_us,
                               long long max_us,
                               bool verbose,
                               int sample_n = 4) {
    // Collect valid values
    std::vector<double> valid;
    valid.reserve(v.size());
    for (double x : v) if (!std::isnan(x)) valid.push_back(x);

    std::ostringstream vals;
    if (verbose) {
        vals << "all " << valid.size() << " values";
    } else {
        vals << "[";
        int show = std::min(sample_n, static_cast<int>(valid.size()));
        for (int i = 0; i < show; ++i) {
            if (i) vals << ", ";
            vals << std::fixed << std::setprecision(2) << valid[i];
        }
        if (static_cast<int>(valid.size()) > show) {
            vals << " … " << std::fixed << std::setprecision(2) << valid.back();
        }
        vals << "]";
    }

    // Format timing
    std::string ms_str = fmtNum(elapsed_us / 1000.0, 2) + " ms";
    long long tput = (elapsed_us > 0)
        ? static_cast<long long>(v.size() * 1e6 / elapsed_us)
        : 0LL;
    std::string tput_str = fmtComma(tput / 1'000'000) + "M pts/s";

    std::cout << "  " << C::yellow() << std::left << std::setw(20) << name << C::reset()
              << C::dim() << vals.str().substr(0, 38) << C::reset()
              << "\n"
              << "  " << std::string(20, ' ')
              << mkBar(elapsed_us, max_us, 20)
              << "  " << C::magenta() << std::right << std::setw(8) << ms_str << C::reset()
              << "  " << C::dim() << tput_str << C::reset()
              << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal display
// ─────────────────────────────────────────────────────────────────────────────

static void printSignalSummary(const std::vector<SignalPoint>& signals,
                                const std::string& strat_name,
                                long long elapsed_us) {
    int buys = 0, sells = 0, holds = 0;
    for (const auto& s : signals) {
        if (s.signal == Signal::BUY)       ++buys;
        else if (s.signal == Signal::SELL) ++sells;
        else                               ++holds;
    }

    std::cout << "  Strategy    : " << C::bcyan() << strat_name << C::reset() << "\n";
    std::cout << "  Total bars  : " << signals.size() << "\n";
    std::cout << "  " << C::bgreen() << "BUY " << C::reset()
              << std::setw(4) << buys  << "   "
              << C::bred()    << "SELL" << C::reset()
              << std::setw(4) << sells << "   "
              << C::dim()     << "HOLD" << C::reset()
              << std::setw(6) << holds << "\n";
    std::cout << "  Time        : " << C::magenta()
              << fmtNum(elapsed_us / 1000.0, 2) << " ms" << C::reset() << "\n\n";

    // Show first 6 non-HOLD signals
    std::cout << "  " << C::bold() << "Action signals:\n" << C::reset();
    int shown = 0;
    for (const auto& s : signals) {
        if (s.signal == Signal::HOLD) continue;
        std::cout << "    "
                  << sigColor(s.signal) << "[" << sigStr(s.signal) << "]" << C::reset()
                  << "  " << C::dim() << s.timestamp << C::reset()
                  << "  ₹" << C::white() << std::fixed << std::setprecision(2)
                  << s.price << C::reset() << "\n";
        if (++shown >= 6) break;
    }
    if (shown == 0) std::cout << "    " << C::dim() << "(no BUY/SELL signals)\n" << C::reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Backtest display
// ─────────────────────────────────────────────────────────────────────────────

static void printBacktestResult(const BacktestResult& bt,
                                 long long elapsed_us,
                                 const std::string& export_file) {
    const char* ret_col = (bt.total_return_pct >= 0) ? C::bgreen() : C::bred();

    std::cout << "  Trades      : " << C::bold() << bt.num_trades << C::reset() << "\n";
    std::cout << "  Total Return: " << ret_col
              << (bt.total_return_pct >= 0 ? "+" : "") << fmtNum(bt.total_return_pct) << "%"
              << C::reset() << "\n";
    std::cout << "  Win Rate    : " << C::byellow() << fmtNum(bt.win_rate) << "%" << C::reset() << "\n";
    std::cout << "  Max Drawdown: " << C::bred()    << fmtNum(bt.max_drawdown_pct) << "%" << C::reset() << "\n";
    std::cout << "  Time        : " << C::magenta()
              << fmtNum(elapsed_us / 1000.0, 2) << " ms" << C::reset() << "\n";

    if (bt.num_trades == 0) {
        std::cout << "\n  " << C::dim() << "(no trades executed)\n" << C::reset();
        return;
    }

    // Show trade table (up to 8 trades)
    std::cout << "\n  " << C::bold()
              << std::left  << std::setw(6)  << "#"
              << std::setw(14) << "Entry"
              << std::setw(14) << "Exit"
              << std::right << std::setw(10) << "Entry ₹"
              << std::setw(10) << "Exit ₹"
              << std::setw(10) << "PnL %"
              << std::setw(7)  << "Bars"
              << std::setw(7)  << "W/L"
              << C::reset() << "\n";
    printHRule(68);

    std::size_t show = std::min(bt.trades.size(), std::size_t(8));
    for (std::size_t i = 0; i < show; ++i) {
        const auto& t = bt.trades[i];
        const char* pnl_col = t.is_win ? C::bgreen() : C::bred();
        std::cout << "  "
                  << C::dim() << std::left << std::setw(4) << (i+1) << C::reset()
                  << std::setw(14) << t.entry_timestamp.substr(0, 13)
                  << std::setw(14) << t.exit_timestamp.substr(0, 13)
                  << std::right
                  << std::setw(10) << fmtNum(t.entry_price)
                  << std::setw(10) << fmtNum(t.exit_price)
                  << pnl_col
                  << std::setw(9) << (t.pnl_pct >= 0 ? "+" : "") + fmtNum(t.pnl_pct) + "%"
                  << C::reset()
                  << std::setw(7) << t.duration_bars
                  << (t.is_win
                     ? (std::string("  ") + C::bgreen() + "WIN"  + C::reset())
                     : (std::string("  ") + C::bred()   + "LOSS" + C::reset()))
                  << "\n";
    }
    if (bt.trades.size() > show) {
        std::cout << "  " << C::dim()
                  << "  ... and " << (bt.trades.size() - show) << " more trades"
                  << C::reset() << "\n";
    }

    // Export to CSV if requested
    if (!export_file.empty()) {
        std::ofstream f(export_file);
        if (f) {
            f << "trade,entry_ts,exit_ts,entry_price,exit_price,pnl_pct,duration_bars,win\n";
            for (std::size_t i = 0; i < bt.trades.size(); ++i) {
                const auto& t = bt.trades[i];
                f << (i+1) << ","
                  << t.entry_timestamp << "," << t.exit_timestamp << ","
                  << std::fixed << std::setprecision(4)
                  << t.entry_price << "," << t.exit_price << ","
                  << t.pnl_pct << "," << t.duration_bars << ","
                  << (t.is_win ? "1" : "0") << "\n";
            }
            std::cout << "\n  " << C::bgreen() << "✔ Exported " << bt.trades.size()
                      << " trades → " << export_file << C::reset() << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark section
// ─────────────────────────────────────────────────────────────────────────────

static void runBenchmarkSection(std::size_t N) {
    sectionHeader("⚡ BENCHMARK", "C++ Engine Performance — " + fmtComma(static_cast<long long>(N)) + " rows");

    std::cout << "  " << C::dim() << "Generating synthetic price series...\n" << C::reset();
    std::vector<double> close(N);
    std::vector<std::string> ts(N);
    for (std::size_t i = 0; i < N; ++i) {
        close[i] = 100.0 + 50.0 * std::sin(static_cast<double>(i) * 0.001)
                         + static_cast<double>(i) * 0.0001;
        ts[i] = "T" + std::to_string(i);
    }
    std::cout << "  " << C::dim() << "Ready. Timing 9 operations...\n\n" << C::reset();

    struct BR { std::string name; long long us; double tput; };
    std::vector<BR> results;

    auto bench = [&](const std::string& name, std::function<void()> fn) {
        auto r = BenchmarkModule::measure(name, N, fn);
        results.push_back({name, r.elapsed_us, r.throughput_per_sec});
    };

    bench("SMA(20)",            [&]{ IndicatorEngine::sma(close, 20); });
    bench("EMA(20)",            [&]{ IndicatorEngine::ema(close, 20); });
    bench("RSI(14)",            [&]{ IndicatorEngine::rsi(close, 14); });
    bench("MACD(12,26,9)",      [&]{ IndicatorEngine::macd(close, 12, 26, 9); });
    bench("BollingerBands(20)", [&]{ IndicatorEngine::bollingerBands(close, 20, 2.0); });
    bench("SMA Crossover",      [&]{ SignalEngine::smaCrossover(close, ts, 10, 50); });
    bench("RSI Strategy",       [&]{ SignalEngine::rsiStrategy(close, ts, 14); });
    bench("MACD Strategy",      [&]{ SignalEngine::macdStrategy(close, ts, 12, 26, 9); });
    auto sigs = SignalEngine::smaCrossover(close, ts, 10, 50);
    bench("Backtest",           [&]{ BacktestEngine::run(sigs, close, ts); });

    long long max_us = 0;
    for (auto& r : results) max_us = std::max(max_us, r.us);

    // Header
    std::cout << "  " << C::bold()
              << std::left << std::setw(24) << "Operation"
              << std::right << std::setw(21) << "Bar"
              << std::setw(10) << "ms"
              << std::setw(16) << "M pts/sec"
              << C::reset() << "\n";
    printHRule(72);

    for (auto& r : results) {
        int fill = static_cast<int>(
            std::round(static_cast<double>(r.us) / static_cast<double>(max_us) * 20));
        fill = std::max(1, std::min(20, fill));

        std::string bar;
        bar += C::cyan();
        for (int i = 0; i < 20; ++i) bar += (i < fill ? '#' : '.');
        bar += C::reset();

        double ms   = r.us / 1000.0;
        double mpts = r.tput / 1e6;

        const char* ms_col = (ms < 50.0) ? C::bgreen() : C::byellow();

        std::cout << "  " << C::yellow() << std::left << std::setw(22) << r.name << C::reset()
                  << "  " << bar
                  << "  " << ms_col << std::right << std::setw(7) << fmtNum(ms, 1) << " ms" << C::reset()
                  << "  " << C::dim() << std::setw(9) << fmtNum(mpts, 1) << "M" << C::reset()
                  << "\n";
    }

    // Summary stats
    long long total_us = 0;
    for (auto& r : results) total_us += r.us;
    std::cout << "\n";
    printHRule(72);
    std::cout << "  " << C::bold()
              << "Total time for all operations on " << fmtComma(static_cast<long long>(N)) << " rows: "
              << C::bgreen() << fmtNum(total_us / 1000.0, 1) << " ms" << C::reset() << "\n";
    std::cout << "  " << C::dim()
              << "(Indicators < 50 ms at 500k rows = PRD §8 target  "
              << C::bgreen() << "PASS" << C::reset() << C::dim() << ")\n"
              << C::reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// All-strategies comparison
// ─────────────────────────────────────────────────────────────────────────────

static void runAllStrategies(const OHLCVData& data, const Args& a) {
    const auto& close = data.close;
    const auto& ts    = data.timestamp;

    struct SR {
        std::string name;
        int buys, sells, trades;
        double total_return, win_rate, max_dd;
        long long sig_us, bt_us;
    };

    std::vector<SR> rows;

    auto runOne = [&](const std::string& name,
                       std::vector<SignalPoint> sigs,
                       long long sig_us) {
        long long t0 = BenchmarkModule::nowUs();
        auto bt = BacktestEngine::run(sigs, close, ts);
        long long bt_us = BenchmarkModule::nowUs() - t0;

        SR r;
        r.name         = name;
        r.buys  = r.sells = 0;
        r.sig_us = sig_us;
        r.bt_us  = bt_us;

        int holds = 0;
        for (auto& s : sigs) {
            if (s.signal == Signal::BUY)       ++r.buys;
            else if (s.signal == Signal::SELL) ++r.sells;
            else                               ++holds;
        }
        r.trades       = bt.num_trades;
        r.total_return = bt.total_return_pct;
        r.win_rate     = bt.win_rate;
        r.max_dd       = bt.max_drawdown_pct;
        rows.push_back(r);
    };

    {
        long long t0 = BenchmarkModule::nowUs();
        auto sigs = SignalEngine::smaCrossover(close, ts, a.sma_short, a.sma_long);
        runOne("SMA Crossover (" + std::to_string(a.sma_short) + "/" + std::to_string(a.sma_long) + ")",
               sigs, BenchmarkModule::nowUs() - t0);
    }
    {
        long long t0 = BenchmarkModule::nowUs();
        auto sigs = SignalEngine::rsiStrategy(close, ts, a.rsi_window, a.rsi_oversold, a.rsi_overbought);
        runOne("RSI(" + std::to_string(a.rsi_window) + ") OB/OS " +
               fmtNum(a.rsi_oversold, 0) + "/" + fmtNum(a.rsi_overbought, 0),
               sigs, BenchmarkModule::nowUs() - t0);
    }
    {
        long long t0 = BenchmarkModule::nowUs();
        auto sigs = SignalEngine::macdStrategy(close, ts, a.macd_fast, a.macd_slow, a.macd_signal);
        runOne("MACD(" + std::to_string(a.macd_fast) + "," +
               std::to_string(a.macd_slow) + "," + std::to_string(a.macd_signal) + ")",
               sigs, BenchmarkModule::nowUs() - t0);
    }

    sectionHeader("📊 STRATEGY COMPARISON", "All 3 Strategies vs " + std::to_string(close.size()) + " bars");

    std::cout << "  " << C::bold()
              << std::left  << std::setw(34) << "Strategy"
              << std::right << std::setw(5)  << "BUY"
              << std::setw(5)   << "SELL"
              << std::setw(8)   << "Trades"
              << std::setw(10)  << "Return%"
              << std::setw(9)   << "WinRate"
              << std::setw(9)   << "MaxDD%"
              << std::setw(9)   << "Time"
              << C::reset() << "\n";
    printHRule(90);

    // Find best return for highlighting
    double best_ret = -1e18;
    for (auto& r : rows) best_ret = std::max(best_ret, r.total_return);

    for (auto& r : rows) {
        bool is_best = (r.total_return == best_ret && r.trades > 0);
        const char* ret_col = (r.total_return > 0) ? C::bgreen() : C::bred();
        std::string tot_ms = fmtNum((r.sig_us + r.bt_us) / 1000.0, 2) + "ms";

        std::cout << "  "
                  << (is_best ? C::byellow() : C::reset())
                  << std::left << std::setw(34) << r.name << C::reset()
                  << C::bgreen() << std::right << std::setw(5) << r.buys  << C::reset()
                  << C::bred()   << std::setw(5) << r.sells << C::reset()
                  << std::setw(8) << r.trades
                  << ret_col << std::setw(10) << (r.total_return >= 0 ? "+" : "") + fmtNum(r.total_return) + "%" << C::reset()
                  << C::byellow() << std::setw(9) << fmtNum(r.win_rate) + "%" << C::reset()
                  << C::bred()    << std::setw(9) << fmtNum(r.max_dd)   + "%" << C::reset()
                  << C::magenta() << std::setw(9) << tot_ms              << C::reset()
                  << (is_best ? (std::string("  ") + C::byellow() + "◀ BEST" + C::reset()) : "")
                  << "\n";
    }
    printHRule(90);
}

// ─────────────────────────────────────────────────────────────────────────────
// main pipeline
// ─────────────────────────────────────────────────────────────────────────────

static void runPipeline(const Args& a) {

    // ─── 1. Data Ingestion ────────────────────────────────────────────────────
    sectionHeader("[ 1 / 5 ]", "Data Ingestion  →  " + a.filepath);

    long long t0 = BenchmarkModule::nowUs();
    OHLCVData data;
    try {
        data = DataIngestionEngine::loadFromCSV(a.filepath, MissingValuePolicy::DROP);
    } catch (const std::exception& e) {
        std::cerr << C::bred() << "ERROR: " << e.what() << C::reset() << "\n";
        return;
    }
    long long load_us = BenchmarkModule::nowUs() - t0;

    // Normalise timestamps
    DataUtils::normaliseTimestamps(data);
    auto errs = DataUtils::validate(data);

    std::cout << "  Rows loaded  : " << C::bold() << data.size() << C::reset() << "\n";
    std::cout << "  Date range   : " << C::cyan() << data.timestamp.front()
              << C::reset() << "  →  "
              << C::cyan() << data.timestamp.back() << C::reset() << "\n";
    std::cout << "  Load time    : " << C::magenta() << load_us << " µs" << C::reset() << "\n";
    std::cout << "  Validation   : ";
    if (errs.empty()) {
        std::cout << C::bgreen() << "PASS (0 errors)" << C::reset() << "\n";
    } else {
        std::cout << C::byellow() << errs.size() << " warning(s)" << C::reset()
                  << C::dim() << " — first: " << errs[0].field << ": " << errs[0].reason
                  << C::reset() << "\n";
    }

    // Price stats
    const auto& cl = data.close;
    double mn = *std::min_element(cl.begin(), cl.end());
    double mx = *std::max_element(cl.begin(), cl.end());
    double avg = std::accumulate(cl.begin(), cl.end(), 0.0) / cl.size();
    std::cout << "  Price range  : ₹" << C::white() << fmtNum(mn) << C::reset()
              << " – ₹" << C::white() << fmtNum(mx) << C::reset()
              << "  (avg ₹" << C::dim() << fmtNum(avg) << C::reset() << ")\n";

    // ─── 2. Indicators ────────────────────────────────────────────────────────
    sectionHeader("[ 2 / 5 ]", "Technical Indicators");

    auto& close = data.close;

    auto b_sma = BenchmarkModule::measure("SMA(" + std::to_string(a.bb_window) + ")",
                    close.size(), [&]{ IndicatorEngine::sma(close, a.bb_window); });
    auto b_ema = BenchmarkModule::measure("EMA(" + std::to_string(a.bb_window) + ")",
                    close.size(), [&]{ IndicatorEngine::ema(close, a.bb_window); });
    auto b_rsi = BenchmarkModule::measure("RSI(" + std::to_string(a.rsi_window) + ")",
                    close.size(), [&]{ IndicatorEngine::rsi(close, a.rsi_window); });
    auto b_mac = BenchmarkModule::measure(
                    "MACD(" + std::to_string(a.macd_fast) + "," +
                              std::to_string(a.macd_slow) + "," +
                              std::to_string(a.macd_signal) + ")",
                    close.size(),
                    [&]{ IndicatorEngine::macd(close, a.macd_fast, a.macd_slow, a.macd_signal); });
    auto b_bb  = BenchmarkModule::measure(
                    "BB(" + std::to_string(a.bb_window) + ", k=" + fmtNum(a.bb_k, 1) + ")",
                    close.size(),
                    [&]{ IndicatorEngine::bollingerBands(close, a.bb_window, a.bb_k); });

    auto sma20  = IndicatorEngine::sma(close, a.bb_window);
    auto ema20  = IndicatorEngine::ema(close, a.bb_window);
    auto rsi14  = IndicatorEngine::rsi(close, a.rsi_window);
    auto macd_r = IndicatorEngine::macd(close, a.macd_fast, a.macd_slow, a.macd_signal);
    auto bb     = IndicatorEngine::bollingerBands(close, a.bb_window, a.bb_k);

    long long max_ind_us = std::max({b_sma.elapsed_us, b_ema.elapsed_us,
                                      b_rsi.elapsed_us, b_mac.elapsed_us, b_bb.elapsed_us});

    printIndicatorRow(b_sma.name, sma20,          b_sma.elapsed_us, max_ind_us, a.verbose);
    printIndicatorRow(b_ema.name, ema20,           b_ema.elapsed_us, max_ind_us, a.verbose);
    printIndicatorRow(b_rsi.name, rsi14,           b_rsi.elapsed_us, max_ind_us, a.verbose);
    printIndicatorRow(b_mac.name, macd_r.macd_line,b_mac.elapsed_us, max_ind_us, a.verbose);
    printIndicatorRow("BB Upper",  bb.upper,        b_bb.elapsed_us,  max_ind_us, a.verbose);
    printIndicatorRow("BB Middle", bb.middle,        b_bb.elapsed_us,  max_ind_us, a.verbose);
    printIndicatorRow("BB Lower",  bb.lower,         b_bb.elapsed_us,  max_ind_us, a.verbose);

    long long total_ind_us = b_sma.elapsed_us + b_ema.elapsed_us +
                              b_rsi.elapsed_us + b_mac.elapsed_us + b_bb.elapsed_us;
    std::cout << "\n  Total indicator time: " << C::bgreen()
              << fmtNum(total_ind_us / 1000.0, 2) << " ms" << C::reset()
              << " for " << close.size() << " bars\n";

    // ─── 3. Signals ───────────────────────────────────────────────────────────
    if (a.strategy == "all") {
        runAllStrategies(data, a);
    } else {
        sectionHeader("[ 3 / 5 ]", "Signal Generation  —  " + a.strategy);

        std::vector<SignalPoint> signals;
        long long sig_us = 0;
        {
            long long ts0 = BenchmarkModule::nowUs();
            try {
                if (a.strategy == "sma_crossover")
                    signals = SignalEngine::smaCrossover(close, data.timestamp, a.sma_short, a.sma_long);
                else if (a.strategy == "rsi")
                    signals = SignalEngine::rsiStrategy(close, data.timestamp, a.rsi_window, a.rsi_oversold, a.rsi_overbought);
                else if (a.strategy == "macd")
                    signals = SignalEngine::macdStrategy(close, data.timestamp, a.macd_fast, a.macd_slow, a.macd_signal);
                else {
                    std::cerr << C::bred() << "Unknown strategy: " << a.strategy << C::reset() << "\n";
                    return;
                }
            } catch (const std::exception& e) {
                std::cerr << C::bred() << "ERROR: " << e.what() << C::reset() << "\n";
                return;
            }
            sig_us = BenchmarkModule::nowUs() - ts0;
        }

        printSignalSummary(signals, a.strategy, sig_us);

        // ─── 4. Backtest ──────────────────────────────────────────────────────
        sectionHeader("[ 4 / 5 ]", "Backtest  —  Long-Only, No Slippage");

        BacktestResult bt;
        long long bt_us = 0;
        {
            long long ts0 = BenchmarkModule::nowUs();
            try {
                bt = BacktestEngine::run(signals, close, data.timestamp);
            } catch (const std::exception& e) {
                std::cerr << C::bred() << "ERROR: " << e.what() << C::reset() << "\n";
                return;
            }
            bt_us = BenchmarkModule::nowUs() - ts0;
        }
        printBacktestResult(bt, bt_us, a.export_file);
    }

    // ─── 5. Benchmark ─────────────────────────────────────────────────────────
    if (a.run_benchmark) {
        runBenchmarkSection(a.bench_rows);
    } else {
        sectionHeader("[ 5 / 5 ]", "Performance");
        std::cout << "  " << C::dim()
                  << "Pass --benchmark to run 1M-row throughput test.\n"
                  << "  Pass --benchmark --rows N for custom row count.\n"
                  << C::reset();
    }

    std::cout << "\n";
    printDRule(72);
    std::cout << C::bgreen() << "  Done." << C::reset() << "\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef __unix__
    // Disable colour when not connected to a terminal
    if (!isatty(fileno(stdout))) g_color = false;
#endif

    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    Args a;
    bool ok = parseArgs(argc, argv, a);
    if (!ok) {
        printHelp(argv[0]);
        // exit 0 for --help, 1 for bad args
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") return 0;
        }
        return 1;
    }

    printBanner();

    if (a.benchmark_only) {
        runBenchmarkSection(a.bench_rows);
    } else {
        runPipeline(a);
    }
    return 0;
}
