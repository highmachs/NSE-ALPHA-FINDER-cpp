#include "test_runner.hpp"
#include "indicators.hpp"

#include <vector>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <limits>

static std::vector<double> makeClose() {
    // 50 deterministic values
    return {
        100, 102, 101, 105, 107, 106, 108, 110, 109, 111,
        113, 112, 115, 117, 116, 118, 120, 119, 122, 124,
        123, 125, 127, 126, 129, 131, 130, 132, 134, 133,
        136, 138, 137, 139, 141, 140, 143, 145, 144, 146,
        148, 147, 150, 152, 151, 153, 155, 154, 157, 159
    };
}

// Reference SMA computed naively
static double naiveSMA(const std::vector<double>& v, int w, int idx) {
    double s = 0;
    for (int i = idx - w + 1; i <= idx; ++i) s += v[static_cast<std::size_t>(i)];
    return s / w;
}

void runIndicatorTests() {
    test::suite("IndicatorEngine — SMA");

    auto close = makeClose();

    {
        auto sma = IndicatorEngine::sma(close, 5);
        test::check(sma.size() == close.size(), "SMA: output size matches input");
        for (int i = 0; i < 4; ++i) {
            test::isnan_check(sma[static_cast<std::size_t>(i)],
                              "SMA: warmup index " + std::to_string(i) + " is NaN");
        }
        for (int i = 4; i < static_cast<int>(close.size()); ++i) {
            double ref = naiveSMA(close, 5, i);
            test::near(sma[static_cast<std::size_t>(i)], ref, 1e-9,
                       "SMA(5) at index " + std::to_string(i));
        }
    }

    {
        auto sma1 = IndicatorEngine::sma(close, 1);
        for (std::size_t i = 0; i < close.size(); ++i) {
            test::near(sma1[i], close[i], 1e-9, "SMA(1) == close at " + std::to_string(i));
        }
    }

    {
        bool threw = false;
        try { IndicatorEngine::sma(close, 0); } catch (...) { threw = true; }
        test::check(threw, "SMA(0) throws");
    }

    test::suite("IndicatorEngine — EMA");

    {
        auto ema = IndicatorEngine::ema(close, 5);
        const double alpha = 2.0 / 6.0;
        test::check(ema.size() == close.size(), "EMA: output size matches input");

        // Seed = mean of first 5
        double seed = (close[0]+close[1]+close[2]+close[3]+close[4]) / 5.0;
        test::near(ema[4], seed, 1e-9, "EMA: seed at index 4");

        double prev = seed;
        for (std::size_t i = 5; i < close.size(); ++i) {
            double expected = alpha * close[i] + (1.0 - alpha) * prev;
            test::near(ema[i], expected, 1e-9,
                       "EMA(5) at index " + std::to_string(i));
            prev = expected;
        }
    }

    {
        bool threw = false;
        try { IndicatorEngine::ema(close, -1); } catch (...) { threw = true; }
        test::check(threw, "EMA(-1) throws");
    }

    test::suite("IndicatorEngine — RSI");

    {
        // Flat price: no change → avg_gain=0, avg_loss=0 → RSI=100 by convention
        std::vector<double> flat(30, 100.0);
        auto rsi = IndicatorEngine::rsi(flat, 14);
        test::near(rsi[14], 100.0, 1e-9, "RSI: flat prices → 100.0");
    }

    {
        // Strictly increasing: all gains, no loss → RSI = 100
        std::vector<double> up(30);
        for (int i = 0; i < 30; ++i) up[static_cast<std::size_t>(i)] = 100.0 + i;
        auto rsi = IndicatorEngine::rsi(up, 14);
        for (std::size_t i = 14; i < up.size(); ++i) {
            test::near(rsi[i], 100.0, 1e-9,
                       "RSI: strictly up at index " + std::to_string(i));
        }
    }

    {
        // Strictly decreasing: all losses, no gain → RSI = 0
        std::vector<double> dn(30);
        for (int i = 0; i < 30; ++i) dn[static_cast<std::size_t>(i)] = 100.0 - i;
        auto rsi = IndicatorEngine::rsi(dn, 14);
        for (std::size_t i = 14; i < dn.size(); ++i) {
            test::near(rsi[i], 0.0, 1e-9,
                       "RSI: strictly down at index " + std::to_string(i));
        }
    }

    {
        auto rsi = IndicatorEngine::rsi(close, 14);
        test::check(rsi.size() == close.size(), "RSI: output size matches input");
        for (int i = 0; i < 14; ++i) {
            test::isnan_check(rsi[static_cast<std::size_t>(i)],
                              "RSI: warmup index " + std::to_string(i) + " is NaN");
        }
        for (std::size_t i = 14; i < close.size(); ++i) {
            bool bounded = rsi[i] >= 0.0 && rsi[i] <= 100.0;
            test::check(bounded, "RSI[" + std::to_string(i) + "] in [0,100]");
        }
    }

    test::suite("IndicatorEngine — MACD");

    {
        auto macd = IndicatorEngine::macd(close, 12, 26, 9);
        test::check(macd.macd_line.size()   == close.size(), "MACD: macd_line size");
        test::check(macd.signal_line.size() == close.size(), "MACD: signal_line size");
        test::check(macd.histogram.size()   == close.size(), "MACD: histogram size");

        // Warmup: first valid MACD at index slow-1=25
        for (int i = 0; i < 25; ++i) {
            test::isnan_check(macd.macd_line[static_cast<std::size_t>(i)],
                              "MACD: warmup macd_line[" + std::to_string(i) + "]");
        }

        // Histogram = macd - signal where both are valid
        for (std::size_t i = 34; i < close.size(); ++i) {
            double expected_hist = macd.macd_line[i] - macd.signal_line[i];
            test::near(macd.histogram[i], expected_hist, 1e-9,
                       "MACD: histogram[" + std::to_string(i) + "] == macd-signal");
        }
    }

    {
        bool threw = false;
        try { IndicatorEngine::macd(close, 26, 12, 9); } catch (...) { threw = true; }
        test::check(threw, "MACD: fast >= slow throws");
    }

    test::suite("IndicatorEngine — Bollinger Bands");

    {
        auto bb = IndicatorEngine::bollingerBands(close, 5, 2.0);
        test::check(bb.upper.size()  == close.size(), "BB: upper size");
        test::check(bb.middle.size() == close.size(), "BB: middle size");
        test::check(bb.lower.size()  == close.size(), "BB: lower size");

        // upper >= middle >= lower (when both are non-NaN)
        for (std::size_t i = 4; i < close.size(); ++i) {
            test::check(bb.upper[i] >= bb.middle[i], "BB: upper >= middle at " + std::to_string(i));
            test::check(bb.middle[i] >= bb.lower[i], "BB: middle >= lower at " + std::to_string(i));
        }

        // middle should match SMA
        auto sma = IndicatorEngine::sma(close, 5);
        for (std::size_t i = 4; i < close.size(); ++i) {
            test::near(bb.middle[i], sma[i], 1e-6,
                       "BB: middle[" + std::to_string(i) + "] == SMA");
        }
    }

    {
        // Flat prices: SD=0 → upper == middle == lower
        std::vector<double> flat(30, 100.0);
        auto bb = IndicatorEngine::bollingerBands(flat, 5, 2.0);
        for (std::size_t i = 4; i < flat.size(); ++i) {
            test::near(bb.upper[i],  100.0, 1e-9, "BB flat: upper==100");
            test::near(bb.middle[i], 100.0, 1e-9, "BB flat: middle==100");
            test::near(bb.lower[i],  100.0, 1e-9, "BB flat: lower==100");
        }
    }
}
