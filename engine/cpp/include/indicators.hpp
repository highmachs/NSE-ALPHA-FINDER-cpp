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
    [[nodiscard]] static std::vector<double> sma(const std::vector<double>& close, int window);

    [[nodiscard]] static std::vector<double> ema(const std::vector<double>& close, int window);

    [[nodiscard]] static std::vector<double> rsi(const std::vector<double>& close, int window = 14);

    [[nodiscard]] static MACDResult macd(const std::vector<double>& close,
                            int fast_period   = 12,
                            int slow_period   = 26,
                            int signal_period = 9);

    [[nodiscard]] static BollingerBandsResult bollingerBands(const std::vector<double>& close,
                                                int    window = 20,
                                                double k      = 2.0);

    [[nodiscard]] static std::vector<double> atr(const std::vector<double>& high,
                                    const std::vector<double>& low,
                                    const std::vector<double>& close,
                                    int window = 14);

private:
    [[nodiscard]] static std::vector<double> emaFromSeed(const std::vector<double>& data,
                                            int    window,
                                            double seed,
                                            std::size_t start_idx);
};
