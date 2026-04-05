/**
 * @file backtest.hpp
 * @brief Event-driven backtesting engine for NSE equity signal streams.
 *
 * Simulates systematic long-only trading by replaying a vector of SignalPoints
 * against the original OHLCV price series. Execution rules (PRD §5.4):
 *   - BUY  : open a long position at the signal bar's closing price.
 *   - SELL : close the open position at the signal bar's closing price.
 *   - HOLD : no action; if in a position, continue holding.
 *   - Double-BUY (BUY while already long) is ignored.
 *   - No slippage, no transaction costs in the current version.
 *   - One position at a time (no pyramiding).
 *
 * Performance target (PRD §5.4): O(n) for any signal stream size.
 * A hash map is built once from the timestamp list so each signal lookup
 * is O(1) amortised.
 */

#pragma once

#include <vector>
#include <string>
#include "signals.hpp"

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Record for a single completed round-trip trade.
 *
 * Populated by BacktestEngine::run() for every matched BUY→SELL pair.
 * PnL is expressed as a percentage return relative to the entry price.
 */
struct Trade {
    std::string entry_timestamp; ///< Timestamp of the BUY bar.
    std::string exit_timestamp;  ///< Timestamp of the SELL bar.
    double      entry_price;     ///< Execution price at entry (closing price).
    double      exit_price;      ///< Execution price at exit  (closing price).
    int         duration_bars;   ///< Number of bars held (exit_idx − entry_idx).
    double      pnl_pct;         ///< Round-trip PnL: (exit − entry) / entry × 100.
    bool        is_win;          ///< true iff pnl_pct > 0.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Aggregate performance metrics for a completed backtest run.
 *
 * All percentage fields are expressed as raw percentages (e.g. 12.5 means 12.5%).
 */
struct BacktestResult {
    std::vector<Trade> trades;           ///< Ordered list of all closed trades.
    double             total_return_pct; ///< Compounded return over all trades.
    double             win_rate;         ///< Fraction of winning trades × 100.
    int                num_trades;       ///< Total number of closed round-trip trades.
    double             max_drawdown_pct; ///< Peak-to-trough decline of the equity curve × 100.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Deterministic, single-pass backtesting engine.
 *
 * All methods are static. The engine is stateless between calls.
 *
 * Thread safety: concurrent calls with different inputs are safe.
 */
class BacktestEngine {
public:
    /**
     * @brief Replay a signal stream against historical close prices.
     *
     * Iterates through @p signals in chronological order:
     *   1. On BUY  (not in position): record entry_price and entry_timestamp.
     *   2. On SELL (in position)    : close trade, compute PnL, append equity.
     *   3. All other cases          : skip.
     *
     * Equity curve starts at 100.0 and compounds multiplicatively after each
     * closed trade. Max drawdown is computed from this equity curve.
     *
     * @param signals     Output from SignalEngine — must be chronologically ordered.
     * @param close       Original closing price series used to look up bar indices.
     * @param timestamps  Timestamps parallel to @p close.
     * @return            BacktestResult with full trade log and summary metrics.
     * @throws std::invalid_argument  close.size() != timestamps.size().
     */
    static BacktestResult run(
        const std::vector<SignalPoint>&  signals,
        const std::vector<double>&       close,
        const std::vector<std::string>&  timestamps);

private:
    /**
     * @brief Compute maximum peak-to-trough drawdown of an equity curve.
     *
     * Scans the equity curve in a single O(n) pass tracking the running peak.
     * Drawdown at each point = (peak − value) / peak × 100.
     *
     * @param equity_curve  Compounding equity values starting at 100.
     * @return              Maximum drawdown expressed as a percentage.
     */
    static double computeMaxDrawdown(const std::vector<double>& equity_curve);
};
