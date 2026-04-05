/**
 * @file indicators.hpp
 * @brief Technical indicator engine for OHLCV price series.
 *
 * All indicators operate in O(n) time and O(n) space with no dynamic
 * allocations inside hot loops. Output vectors always match the input
 * length; indices inside the warm-up period contain std::numeric_limits
 * <double>::quiet_NaN() so callers can detect invalid positions with
 * std::isnan().
 *
 * Mathematical definitions follow standard quantitative finance conventions:
 *   SMA  : arithmetic mean over a rolling window
 *   EMA  : exponentially-weighted mean,  α = 2 / (n + 1)
 *   RSI  : Wilder's Relative Strength Index,  α = 1 / n
 *   MACD : fast-EMA minus slow-EMA, signal = EMA of MACD line
 *   BB   : SMA ± k × population standard deviation
 */

#pragma once

#include <vector>
#include <cstddef>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Output of IndicatorEngine::macd().
 *
 * All three vectors have the same length as the input close series.
 * Values before the warm-up period are NaN.
 */
struct MACDResult {
    std::vector<double> macd_line;   ///< fast-EMA(close) minus slow-EMA(close).
    std::vector<double> signal_line; ///< EMA(macd_line, signal_period).
    std::vector<double> histogram;   ///< macd_line minus signal_line (momentum bar).
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Output of IndicatorEngine::bollingerBands().
 *
 * All three vectors have the same length as the input close series.
 * Values before the warm-up period are NaN.
 */
struct BollingerBandsResult {
    std::vector<double> upper;  ///< middle + k × σ  (resistance / overbought level).
    std::vector<double> middle; ///< SMA(close, window) — centre band.
    std::vector<double> lower;  ///< middle − k × σ  (support / oversold level).
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Stateless, zero-allocation technical indicator engine.
 *
 * All methods are static. Call them directly without constructing an instance.
 *
 * Performance target (PRD §8): < 50 ms for 1 000 000 data points on
 * standard x86-64 hardware compiled with -O3.
 *
 * Thread safety: individual calls are stateless and may be called
 * concurrently on different inputs without synchronisation.
 */
class IndicatorEngine {
public:
    /**
     * @brief Simple Moving Average — rolling arithmetic mean.
     *
     * Uses an O(1)-per-step incremental sum to achieve O(n) total complexity.
     * Warm-up period: indices [0, window-2] are NaN.
     *
     * @param close   Closing price series.
     * @param window  Rolling window length (must be ≥ 1).
     * @return        Vector of length close.size(); NaN during warm-up.
     * @throws std::invalid_argument  window ≤ 0.
     */
    static std::vector<double> sma(const std::vector<double>& close, int window);

    /**
     * @brief Exponential Moving Average — alpha = 2 / (n + 1).
     *
     * Seeded with the SMA of the first @p window values, then applies the
     * standard EMA recurrence:  EMA[i] = α × close[i] + (1−α) × EMA[i−1].
     * Warm-up period: indices [0, window-2] are NaN.
     *
     * @param close   Closing price series.
     * @param window  EMA period (must be ≥ 1).
     * @return        Vector of length close.size(); NaN during warm-up.
     * @throws std::invalid_argument  window ≤ 0.
     */
    static std::vector<double> ema(const std::vector<double>& close, int window);

    /**
     * @brief Relative Strength Index — Wilder's smoothing method.
     *
     * Computes average gain and loss over the first @p window bars, then
     * applies Wilder's exponential smoothing (α = 1/window) for subsequent
     * bars.  RSI = 100 − 100 / (1 + avgGain / avgLoss).
     * Degenerate cases: avgLoss == 0 → RSI = 100 (all gains, no losses).
     * Warm-up period: indices [0, window-1] are NaN.
     *
     * @param close   Closing price series.
     * @param window  RSI period (default 14, must be ≥ 1).
     * @return        Vector of length close.size(), values in [0, 100]; NaN during warm-up.
     * @throws std::invalid_argument  window ≤ 0.
     */
    static std::vector<double> rsi(const std::vector<double>& close, int window = 14);

    /**
     * @brief Moving Average Convergence Divergence.
     *
     * MACD Line   = EMA(close, fast_period) − EMA(close, slow_period).
     * Signal Line = EMA(MACD Line, signal_period).
     * Histogram   = MACD Line − Signal Line.
     *
     * Warm-up periods:
     *   macd_line   valid from index slow_period − 1.
     *   signal_line valid from index slow_period + signal_period − 2.
     *
     * @param close         Closing price series.
     * @param fast_period   Fast EMA period (default 12, must be < slow_period).
     * @param slow_period   Slow EMA period (default 26).
     * @param signal_period Signal line EMA period (default 9).
     * @return              MACDResult with three parallel vectors.
     * @throws std::invalid_argument  fast_period ≥ slow_period, or any period ≤ 0.
     */
    static MACDResult macd(const std::vector<double>& close,
                            int fast_period   = 12,
                            int slow_period   = 26,
                            int signal_period = 9);

    /**
     * @brief Bollinger Bands — SMA ± k × population standard deviation.
     *
     * Uses O(n) incremental variance via running sum-of-squares:
     *   variance = (Σx²/n) − (Σx/n)²
     * which avoids the O(n×window) inner loop of the naive implementation.
     * Warm-up period: indices [0, window-2] are NaN.
     *
     * @param close   Closing price series.
     * @param window  Rolling window for SMA and standard deviation (default 20).
     * @param k       Standard deviation multiplier for band width (default 2.0).
     * @return        BollingerBandsResult with upper, middle, lower vectors.
     * @throws std::invalid_argument  window ≤ 0 or k ≤ 0.
     */
    static BollingerBandsResult bollingerBands(const std::vector<double>& close,
                                                int    window = 20,
                                                double k      = 2.0);

private:
    /**
     * @brief Compute EMA from a precomputed seed value starting at @p start_idx.
     *
     * Used internally by macd() to compute the signal line EMA over an already
     * computed MACD line where the warm-up seed is not a simple SMA.
     *
     * @param data      Input series (may contain leading NaN values).
     * @param window    EMA period.
     * @param seed      Seed value placed at @p start_idx.
     * @param start_idx First index to populate; all prior indices remain NaN.
     * @return          EMA vector of length data.size().
     */
    static std::vector<double> emaFromSeed(const std::vector<double>& data,
                                            int    window,
                                            double seed,
                                            std::size_t start_idx);
};
