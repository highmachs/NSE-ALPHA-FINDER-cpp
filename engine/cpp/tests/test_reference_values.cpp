/**
 * @file test_reference_values.cpp
 * @brief Reference-value validation tests — PRD §9.
 *
 * Each group computes an indicator on a fixed, hand-verified dataset and
 * compares the output to a known correct value computed independently using
 * the standard textbook formulas (Wilder 1978, Murphy 1999).
 *
 * Fixed reference dataset (10 bars):
 *   close = {44, 43, 44, 43, 45, 46, 47, 46, 48, 50}
 */

#include "test_runner.hpp"
#include "indicators.hpp"
#include "data_ingestion.hpp"
#include "data_utils.hpp"

#include <cmath>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────

static bool aeq(double a, double b, double tol = 1e-3) {
    return std::abs(a - b) <= tol;
}

// Reference dataset: 10 bars that match classic textbook examples.
static const std::vector<double> REF = {44, 43, 44, 43, 45, 46, 47, 46, 48, 50};

// ─────────────────────────────────────────────────────────────────────────────

void runReferenceValueTests() {

    // ── SMA reference values ─────────────────────────────────────────────────
    test::suite("Reference Values — SMA");
    {
        auto out = IndicatorEngine::sma(REF, 3);
        // SMA(3)[2] = (44+43+44)/3 = 43.6667
        test::near(out[2], 43.6667, 1e-3, "SMA(3) index 2 = 43.6667");
        // SMA(3)[9] = (46+48+50)/3 = 48.0
        test::near(out[9], 48.0, 1e-6, "SMA(3) index 9 = 48.0");
        // Warm-up
        test::check(std::isnan(out[0]), "SMA(3) index 0 is NaN");
        test::check(std::isnan(out[1]), "SMA(3) index 1 is NaN");
    }
    {
        auto out = IndicatorEngine::sma(REF, 5);
        // SMA(5)[4] = (44+43+44+43+45)/5 = 43.8
        test::near(out[4], 43.8, 1e-6, "SMA(5) index 4 = 43.8");
        // SMA(5)[9] = (46+47+46+48+50)/5 = 237/5 = 47.4
        test::near(out[9], 47.4, 1e-6, "SMA(5) index 9 = 47.4");
    }

    // ── EMA reference values ─────────────────────────────────────────────────
    test::suite("Reference Values — EMA");
    {
        auto out = IndicatorEngine::ema(REF, 3);
        // alpha = 2/(3+1) = 0.5
        // EMA[2] (seed) = SMA(3)[2] = 43.6667
        test::near(out[2], 43.6667, 1e-3, "EMA(3) seed index 2 = 43.6667");
        // EMA[3] = 0.5*43 + 0.5*43.6667 = 43.3333
        test::near(out[3], 43.3333, 1e-3, "EMA(3) index 3 = 43.3333");
        // EMA[4] = 0.5*45 + 0.5*43.3333 = 44.1667
        test::near(out[4], 44.1667, 1e-3, "EMA(3) index 4 = 44.1667");
        // Chain to index 9 = 48.5052 (computed iteratively)
        // EMA[5]=45.0833, [6]=46.0417, [7]=46.0208, [8]=47.0104, [9]=48.5052
        test::near(out[9], 48.5052, 1e-2, "EMA(3) index 9 ≈ 48.5052");
        // Warm-up
        test::check(std::isnan(out[0]), "EMA(3) index 0 is NaN");
        test::check(std::isnan(out[1]), "EMA(3) index 1 is NaN");
    }

    // ── RSI reference values (Wilder, window=5) ──────────────────────────────
    test::suite("Reference Values — RSI");
    {
        auto out = IndicatorEngine::rsi(REF, 5);
        // Changes: −1,+1,−1,+2,+1,+1,−1,+2,+2
        // First 5 changes (indices 1..5): −1,+1,−1,+2,+1
        // avgGain = (1+2+1)/5=0.8, avgLoss=(1+1)/5=0.4
        // RSI[5] = 100 − 100/(1+2.0) = 66.6667
        test::near(out[5], 66.6667, 5e-2, "RSI(5) at index 5 ≈ 66.67");

        // Wilder smooth from index 6: change=+1
        // avgGain[6]=0.8*0.8+0.2*1=0.84, avgLoss[6]=0.8*0.4+0.2*0=0.32
        // RS=2.625, RSI=100−100/3.625=72.41
        test::near(out[6], 72.41, 2e-1, "RSI(5) at index 6 ≈ 72.41");

        // Warm-up: indices 0..4 all NaN
        for (int i = 0; i < 5; ++i) {
            test::check(std::isnan(out[i]),
                        "RSI(5) warm-up index " + std::to_string(i) + " is NaN");
        }
    }

    // ── MACD structural identities ────────────────────────────────────────────
    test::suite("Reference Values — MACD");
    {
        // Extend REF to 40 bars so warm-up completes
        std::vector<double> prices;
        for (int rep = 0; rep < 4; ++rep)
            for (double v : REF) prices.push_back(v);

        auto res = IndicatorEngine::macd(prices, 3, 5, 3);

        // Identity: histogram == macd_line - signal_line
        bool hist_ok = true;
        for (std::size_t i = 0; i < prices.size(); ++i) {
            if (!std::isnan(res.macd_line[i]) && !std::isnan(res.signal_line[i])) {
                double expected = res.macd_line[i] - res.signal_line[i];
                if (std::abs(res.histogram[i] - expected) > 1e-9) {
                    hist_ok = false; break;
                }
            }
        }
        test::check(hist_ok, "MACD histogram == macd_line - signal_line for all valid bars");

        // Identity: macd_line == fast_ema - slow_ema
        auto fast_ema = IndicatorEngine::ema(prices, 3);
        auto slow_ema = IndicatorEngine::ema(prices, 5);
        bool line_ok  = true;
        for (std::size_t i = 4; i < prices.size(); ++i) {
            double expected = fast_ema[i] - slow_ema[i];
            if (std::abs(res.macd_line[i] - expected) > 1e-6) {
                line_ok = false; break;
            }
        }
        test::check(line_ok, "MACD line == fast_ema - slow_ema for all post-warmup bars");
    }

    // ── Bollinger Bands structural invariants ─────────────────────────────────
    test::suite("Reference Values — Bollinger Bands");
    {
        auto out = IndicatorEngine::bollingerBands(REF, 3, 2.0);
        auto sma = IndicatorEngine::sma(REF, 3);

        bool upper_ok  = true;
        bool middle_ok = true;
        bool lower_ok  = true;

        for (std::size_t i = 2; i < REF.size(); ++i) {
            if (out.upper[i]  < out.middle[i] - 1e-9)  upper_ok  = false;
            if (!aeq(out.middle[i], sma[i], 1e-9))      middle_ok = false;
            if (out.middle[i] < out.lower[i]  - 1e-9)  lower_ok  = false;
        }
        test::check(upper_ok,  "BB: upper >= middle for all valid bars");
        test::check(middle_ok, "BB: middle == SMA for all valid bars");
        test::check(lower_ok,  "BB: middle >= lower for all valid bars");

        // Flat prices → SD=0 → upper == middle == lower
        std::vector<double> flat(10, 100.0);
        auto bb_flat = IndicatorEngine::bollingerBands(flat, 3, 2.0);
        bool flat_ok = true;
        for (std::size_t i = 2; i < flat.size(); ++i) {
            if (!aeq(bb_flat.upper[i], 100.0, 1e-9))  flat_ok = false;
            if (!aeq(bb_flat.middle[i],100.0, 1e-9))  flat_ok = false;
            if (!aeq(bb_flat.lower[i], 100.0, 1e-9))  flat_ok = false;
        }
        test::check(flat_ok, "BB: flat price series → upper == middle == lower == price");
    }

    // ── DataUtils validation reference values ─────────────────────────────────
    test::suite("Reference Values — DataUtils validation");
    {
        // Valid row — no errors
        OHLCVData good;
        good.timestamp = {"2023-01-01"};
        good.open      = {100.0};
        good.high      = {105.0};
        good.low       = {98.0};
        good.close     = {102.0};
        good.volume    = {100000.0};
        auto errs = DataUtils::validate(good);
        test::check(errs.empty(), "DataUtils: valid OHLCV row → 0 errors");

        // high < close — must flag "high" field
        OHLCVData bad_high;
        bad_high.timestamp = {"2023-01-01"};
        bad_high.open      = {100.0};
        bad_high.high      = {99.0};   // below close
        bad_high.low       = {97.0};
        bad_high.close     = {100.0};
        bad_high.volume    = {100000.0};
        auto errs2 = DataUtils::validate(bad_high);
        bool found_high = false;
        for (auto& e : errs2) if (e.field == "high") found_high = true;
        test::check(found_high, "DataUtils: high < close → 'high' error flagged");

        // Negative open — must flag "open" field
        OHLCVData neg;
        neg.timestamp = {"2023-01-01"};
        neg.open      = {-10.0};
        neg.high      = {5.0};
        neg.low       = {-12.0};
        neg.close     = {3.0};
        neg.volume    = {0.0};
        auto errs3 = DataUtils::validate(neg);
        bool found_open = false;
        for (auto& e : errs3) if (e.field == "open") found_open = true;
        test::check(found_open, "DataUtils: negative open → 'open' error flagged");

        // Negative volume — must flag "volume"
        OHLCVData neg_vol;
        neg_vol.timestamp = {"2023-01-01"};
        neg_vol.open      = {100.0};
        neg_vol.high      = {102.0};
        neg_vol.low       = {99.0};
        neg_vol.close     = {101.0};
        neg_vol.volume    = {-500.0};
        auto errs4 = DataUtils::validate(neg_vol);
        bool found_vol = false;
        for (auto& e : errs4) if (e.field == "volume") found_vol = true;
        test::check(found_vol, "DataUtils: negative volume → 'volume' error flagged");

        // dropInvalidRows: keep only the valid row
        OHLCVData mixed;
        mixed.timestamp = {"2023-01-01", "2023-01-02"};
        mixed.open      = {100.0, -1.0};
        mixed.high      = {102.0, 5.0};
        mixed.low       = {99.0,  -3.0};
        mixed.close     = {101.0, 3.0};
        mixed.volume    = {1000.0, 0.0};
        auto clean = DataUtils::dropInvalidRows(mixed);
        test::check(clean.size() == 1, "DataUtils: dropInvalidRows keeps only 1 valid row");
        test::check(clean.timestamp[0] == std::string("2023-01-01"),
                    "DataUtils: retained row has correct timestamp");
    }

    // ── Timestamp normalisation reference values ──────────────────────────────
    test::suite("Reference Values — Timestamp normalisation");
    {
        test::check(
            DataUtils::normaliseOneTimestamp("2023-06-15") == std::string("2023-06-15"),
            "Norm: ISO date unchanged");
        test::check(
            DataUtils::normaliseOneTimestamp("2023-06-15 09:30:00") == std::string("2023-06-15"),
            "Norm: ISO datetime → date only");
        test::check(
            DataUtils::normaliseOneTimestamp("2023-06-15T09:30:00+05:30") == std::string("2023-06-15"),
            "Norm: ISO 8601 with TZ → date only");
        test::check(
            DataUtils::normaliseOneTimestamp("06/15/2023") == std::string("2023-06-15"),
            "Norm: US MM/DD/YYYY converted");
        test::check(
            DataUtils::normaliseOneTimestamp("15-Jun-2023") == std::string("2023-06-15"),
            "Norm: Bloomberg DD-Mon-YYYY converted");
    }
}
