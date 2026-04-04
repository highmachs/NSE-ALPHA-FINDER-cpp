#include "test_runner.hpp"
#include "backtest.hpp"
#include "signals.hpp"

#include <vector>
#include <string>
#include <cmath>

static std::vector<std::string> makeTimes(std::size_t n) {
    std::vector<std::string> ts;
    ts.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        ts.push_back("T" + std::to_string(i));
    return ts;
}

void runBacktestTests() {
    test::suite("BacktestEngine — Basic Correctness");

    {
        // Empty signals → zero trades
        auto ts = makeTimes(10);
        std::vector<double> close(10, 100.0);
        std::vector<SignalPoint> signals;
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 0,        "empty signals: 0 trades");
        test::near(result.total_return_pct, 0.0, 1e-9, "empty signals: 0% return");
        test::near(result.win_rate,         0.0, 1e-9, "empty signals: 0% win rate");
        test::near(result.max_drawdown_pct, 0.0, 1e-9, "empty signals: 0% max drawdown");
    }

    {
        // One BUY at 100, one SELL at 110 → +10% return, 1 trade, 100% win rate
        auto ts = makeTimes(10);
        std::vector<double> close = {100,101,102,103,104,105,106,107,108,110};
        std::vector<SignalPoint> signals = {
            {ts[0], Signal::BUY,  100.0},
            {ts[9], Signal::SELL, 110.0}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 1, "single trade: 1 trade");
        test::near(result.total_return_pct, 10.0, 1e-6, "single trade: +10% return");
        test::near(result.win_rate, 100.0, 1e-9, "single trade: 100% win rate");
        test::near(result.max_drawdown_pct, 0.0, 1e-9, "single winning trade: 0% drawdown");
    }

    {
        // One BUY at 100, one SELL at 90 → -10% return, 1 trade, 0% win rate
        auto ts = makeTimes(10);
        std::vector<double> close = {100,99,98,97,96,95,94,93,92,90};
        std::vector<SignalPoint> signals = {
            {ts[0], Signal::BUY,  100.0},
            {ts[9], Signal::SELL,  90.0}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 1, "losing trade: 1 trade");
        test::near(result.total_return_pct, -10.0, 1e-6, "losing trade: -10% return");
        test::near(result.win_rate, 0.0, 1e-9, "losing trade: 0% win rate");
    }

    {
        // Two trades: +10%, then -5% → compounded return = (1.10 * 0.95 - 1)*100 = 4.5%
        auto ts = makeTimes(20);
        std::vector<double> close(20, 100.0);
        close[0]  = 100.0;
        close[9]  = 110.0;
        close[10] = 110.0;
        close[19] = 104.5;
        std::vector<SignalPoint> signals = {
            {ts[0],  Signal::BUY,  100.0},
            {ts[9],  Signal::SELL, 110.0},
            {ts[10], Signal::BUY,  110.0},
            {ts[19], Signal::SELL, 104.5}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 2, "two trades: num_trades == 2");
        test::near(result.total_return_pct, 4.5, 0.01, "two trades: ~4.5% compounded");
        test::near(result.win_rate, 50.0, 1e-9, "two trades: 50% win rate");
    }

    test::suite("BacktestEngine — One Position At A Time");

    {
        // Second BUY while already in position must be ignored
        auto ts = makeTimes(10);
        std::vector<double> close(10, 100.0);
        close[9] = 120.0;
        std::vector<SignalPoint> signals = {
            {ts[0], Signal::BUY,  100.0},
            {ts[3], Signal::BUY,  105.0},  // should be ignored
            {ts[9], Signal::SELL, 120.0}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 1, "double BUY: only 1 trade created");
        test::near(result.total_return_pct, 20.0, 1e-6, "double BUY: 20% return from first entry");
    }

    test::suite("BacktestEngine — Max Drawdown");

    {
        // Sequence: +50%, then -40% cumulative from peak
        // equity: 100 → 150 → 90
        // max drawdown from 150 to 90 = (150-90)/150 * 100 = 40%
        auto ts = makeTimes(30);
        std::vector<double> close(30, 100.0);
        close[0]  = 100.0;
        close[9]  = 150.0;
        close[10] = 150.0;
        close[29] = 90.0;
        std::vector<SignalPoint> signals = {
            {ts[0],  Signal::BUY,  100.0},
            {ts[9],  Signal::SELL, 150.0},
            {ts[10], Signal::BUY,  150.0},
            {ts[29], Signal::SELL,  90.0}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(result.num_trades == 2, "drawdown test: 2 trades");
        test::near(result.max_drawdown_pct, 40.0, 0.01, "max drawdown: ~40%");
    }

    {
        // Duration bars: BUY at T0, SELL at T4 → duration = 4
        auto ts = makeTimes(10);
        std::vector<double> close(10, 100.0);
        close[4] = 110.0;
        std::vector<SignalPoint> signals = {
            {ts[0], Signal::BUY,  100.0},
            {ts[4], Signal::SELL, 110.0}
        };
        auto result = BacktestEngine::run(signals, close, ts);
        test::check(!result.trades.empty(), "duration: trade exists");
        test::check(result.trades[0].duration_bars == 4, "duration: duration_bars == 4");
    }
}
