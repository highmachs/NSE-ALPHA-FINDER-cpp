/**
 * @file signals.hpp
 * @brief Discrete trading signal generation engine for NSE equities.
 *
 * Generates BUY / SELL / HOLD signals from OHLCV price series using three
 * classic systematic strategies:
 *
 *   1. SMA Crossover  — trend-following; uses two moving averages of
 *                        different periods to detect regime changes.
 *   2. RSI Strategy   — mean-reversion; buys oversold and sells overbought
 *                        based on Wilder's momentum oscillator.
 *   3. MACD Strategy  — trend-following + momentum; acts on crossovers
 *                        between the MACD line and its signal line.
 *
 * Each strategy returns a vector of SignalPoint values paired with their
 * timestamps and closing prices so that downstream consumers (BacktestEngine,
 * API layer) can locate each signal in the original time series.
 *
 * All strategies are deterministic: identical inputs produce identical outputs.
 */

#pragma once

#include <vector>
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Discrete trading signal type.
 *
 * Stored as int8_t so that large signal vectors remain cache-friendly.
 */
enum class Signal : int8_t {
    BUY  =  1,  ///< Enter a long position at this bar.
    SELL = -1,  ///< Exit the current long position at this bar.
    HOLD =  0   ///< No actionable event; hold current position (or stay flat).
};

/**
 * @brief Convert a Signal to its canonical string representation.
 * @param s  Signal value.
 * @return   "BUY", "SELL", or "HOLD" (static string; no allocation).
 */
inline const char* signalToStr(Signal s) noexcept {
    switch (s) {
        case Signal::BUY:  return "BUY";
        case Signal::SELL: return "SELL";
        default:           return "HOLD";
    }
}

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A single timestamped trading signal with its associated price.
 *
 * The @p price field reflects the closing price of the bar on which the
 * signal was generated, which is the execution price assumed by BacktestEngine
 * (no slippage model in the current version).
 */
struct SignalPoint {
    std::string timestamp; ///< Date/time string matching the source OHLCVData row.
    Signal      signal;    ///< BUY, SELL, or HOLD.
    double      price;     ///< Closing price at the signal bar.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Stateless signal generation engine.
 *
 * All methods are static. The engine is a pure function of its inputs and
 * carries no mutable state between calls.
 *
 * Performance: each strategy runs in O(n) time after O(n) indicator
 * computation. No dynamic allocations occur inside the signal-scan loop.
 *
 * Thread safety: concurrent calls with different inputs are safe.
 */
class SignalEngine {
public:
    /**
     * @brief SMA Crossover strategy — trend-following.
     *
     * Computes SMA(close, short_window) and SMA(close, long_window), then
     * scans for crossings:
     *   BUY  when short SMA crosses from below to above long SMA.
     *   SELL when short SMA crosses from above to below long SMA.
     *   HOLD at all other bars after the long SMA warm-up period.
     *
     * The first bar after warm-up initialises the previous-state register
     * without emitting a signal.
     *
     * @param close        Closing price series.
     * @param timestamps   Timestamps parallel to @p close.
     * @param short_window Period of the fast SMA (must be < long_window).
     * @param long_window  Period of the slow SMA.
     * @return             Vector of SignalPoint, one per bar after warm-up.
     * @throws std::invalid_argument  Size mismatch, short_window ≥ long_window,
     *                                or either window ≤ 0.
     */
    static std::vector<SignalPoint> smaCrossover(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int short_window,
        int long_window);

    /**
     * @brief RSI threshold strategy — mean-reversion.
     *
     * Computes RSI(close, window) then applies fixed thresholds:
     *   BUY  when RSI < oversold  (default 30 — oversold condition).
     *   SELL when RSI > overbought (default 70 — overbought condition).
     *   HOLD otherwise.
     *
     * Unlike crossover strategies, this emits a signal at every bar after
     * the RSI warm-up period (including HOLD bars).
     *
     * @param close       Closing price series.
     * @param timestamps  Timestamps parallel to @p close.
     * @param window      RSI period (default 14).
     * @param oversold    RSI level below which BUY is generated (default 30).
     * @param overbought  RSI level above which SELL is generated (default 70).
     * @return            Vector of SignalPoint for every post-warm-up bar.
     * @throws std::invalid_argument  Size mismatch or window ≤ 0.
     */
    static std::vector<SignalPoint> rsiStrategy(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int    window      = 14,
        double oversold    = 30.0,
        double overbought  = 70.0);

    /**
     * @brief MACD crossover strategy — trend-following + momentum.
     *
     * Computes the full MACD result and scans for crossings between the
     * MACD line and the signal line:
     *   BUY  when MACD line crosses from below to above signal line.
     *   SELL when MACD line crosses from above to below signal line.
     *   HOLD otherwise.
     *
     * @param close          Closing price series.
     * @param timestamps     Timestamps parallel to @p close.
     * @param fast_period    Fast EMA period (default 12).
     * @param slow_period    Slow EMA period (default 26).
     * @param signal_period  Signal line EMA period (default 9).
     * @return               Vector of SignalPoint for detected crossing bars.
     * @throws std::invalid_argument  Size mismatch or invalid periods.
     */
    static std::vector<SignalPoint> macdStrategy(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int fast_period   = 12,
        int slow_period   = 26,
        int signal_period = 9);
};
