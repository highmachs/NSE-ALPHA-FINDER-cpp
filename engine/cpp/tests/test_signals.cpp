#include "test_runner.hpp"
#include "signals.hpp"
#include "indicators.hpp"

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

void runSignalTests() {
    test::suite("SignalEngine — SMA Crossover");

    {
        // Price series: start flat/down (short < long), then sharp rise (short > long = BUY),
        // then sharp drop (short < long = SELL)
        // short=3, long=6
        std::vector<double> close = {
            110, 108, 106, 104, 102, 100,   // downtrend: short SMA < long SMA
            101, 103, 106, 110, 115, 120,   // sharp uptrend: short crosses above → BUY
            119, 116, 112, 107, 101, 94     // sharp drop: short crosses below → SELL
        };
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::smaCrossover(close, ts, 3, 6);

        int buys = 0, sells = 0;
        for (const auto& s : signals) {
            if (s.signal == Signal::BUY)  ++buys;
            if (s.signal == Signal::SELL) ++sells;
        }
        test::check(buys  >= 1, "SMA crossover: at least 1 BUY crossover");
        test::check(sells >= 1, "SMA crossover: at least 1 SELL crossover");
    }

    {
        // Short window >= long window → throws
        std::vector<double> c(50, 100.0);
        auto ts = makeTimes(c.size());
        bool threw = false;
        try { SignalEngine::smaCrossover(c, ts, 10, 5); } catch (...) { threw = true; }
        test::check(threw, "SMA crossover: short >= long throws");
    }

    {
        // Mismatched sizes → throws
        std::vector<double> c(10, 100.0);
        std::vector<std::string> ts(5, "T");
        bool threw = false;
        try { SignalEngine::smaCrossover(c, ts, 3, 6); } catch (...) { threw = true; }
        test::check(threw, "SMA crossover: size mismatch throws");
    }

    test::suite("SignalEngine — RSI Strategy");

    {
        // Strictly decreasing → RSI < 30 → BUY signals
        std::vector<double> close(50);
        for (int i = 0; i < 50; ++i) close[static_cast<std::size_t>(i)] = 200.0 - i * 3.0;
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::rsiStrategy(close, ts, 14, 30.0, 70.0);

        int buys = 0;
        for (const auto& s : signals) {
            if (s.signal == Signal::BUY) ++buys;
            // RSI strategy never emits BUY when RSI >= 30 or SELL when RSI <= 70
        }
        test::check(buys >= 1, "RSI: strictly down series produces BUY signals");
    }

    {
        // Strictly increasing → RSI > 70 → SELL signals
        std::vector<double> close(50);
        for (int i = 0; i < 50; ++i) close[static_cast<std::size_t>(i)] = 100.0 + i * 3.0;
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::rsiStrategy(close, ts, 14, 30.0, 70.0);

        int sells = 0;
        for (const auto& s : signals) {
            if (s.signal == Signal::SELL) ++sells;
        }
        test::check(sells >= 1, "RSI: strictly up series produces SELL signals");
    }

    {
        // price in signal matches close at that index
        std::vector<double> close = {
            100, 102, 101, 105, 107, 106, 108, 110, 109, 111,
            100, 98,  95,  92,  88,  83,  77,  70
        };
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::rsiStrategy(close, ts, 14, 30.0, 70.0);
        for (const auto& s : signals) {
            std::size_t idx = std::stoul(s.timestamp.substr(1));
            test::near(s.price, close[idx], 1e-9,
                       "RSI: signal price matches close at " + s.timestamp);
        }
    }

    test::suite("SignalEngine — MACD Strategy");

    {
        // Generate a sinusoidal price that has multiple crossovers
        std::vector<double> close(200);
        for (int i = 0; i < 200; ++i)
            close[static_cast<std::size_t>(i)] = 100.0 + 20.0 * std::sin(i * 0.15);
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::macdStrategy(close, ts, 12, 26, 9);

        int buys = 0, sells = 0;
        for (const auto& s : signals) {
            if (s.signal == Signal::BUY)  ++buys;
            if (s.signal == Signal::SELL) ++sells;
        }
        test::check(buys  >= 1, "MACD: sine wave produces BUY signals");
        test::check(sells >= 1, "MACD: sine wave produces SELL signals");
    }

    {
        // All signal types have valid prices
        std::vector<double> close(100);
        for (int i = 0; i < 100; ++i)
            close[static_cast<std::size_t>(i)] = 100.0 + 10.0 * std::sin(i * 0.3);
        auto ts = makeTimes(close.size());
        auto signals = SignalEngine::macdStrategy(close, ts, 12, 26, 9);
        for (const auto& s : signals) {
            test::check(!std::isnan(s.price), "MACD: signal price is not NaN");
        }
    }
}
