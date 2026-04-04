#include "backtest.hpp"

#include <unordered_map>
#include <stdexcept>
#include <cmath>
#include <algorithm>

double BacktestEngine::computeMaxDrawdown(const std::vector<double>& equity_curve) {
    if (equity_curve.empty()) return 0.0;
    double peak = equity_curve[0];
    double max_dd = 0.0;
    for (double val : equity_curve) {
        if (val > peak) peak = val;
        double dd = (peak - val) / peak;
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd * 100.0;
}

BacktestResult BacktestEngine::run(
    const std::vector<SignalPoint>&  signals,
    const std::vector<double>&       close,
    const std::vector<std::string>&  timestamps) {

    if (close.size() != timestamps.size())
        throw std::invalid_argument("close and timestamps size mismatch");

    BacktestResult result;
    result.total_return_pct = 0.0;
    result.win_rate         = 0.0;
    result.num_trades       = 0;
    result.max_drawdown_pct = 0.0;

    if (signals.empty() || close.empty()) return result;

    std::unordered_map<std::string, std::size_t> ts_to_idx;
    ts_to_idx.reserve(timestamps.size());
    for (std::size_t i = 0; i < timestamps.size(); ++i) {
        ts_to_idx[timestamps[i]] = i;
    }

    bool in_position = false;
    std::string entry_ts;
    double entry_price = 0.0;
    std::size_t entry_bar = 0;

    double cumulative_return = 100.0;
    std::vector<double> equity_curve;
    equity_curve.push_back(100.0);

    int wins = 0;

    for (const auto& sp : signals) {
        auto it = ts_to_idx.find(sp.timestamp);
        if (it == ts_to_idx.end()) continue;
        std::size_t bar = it->second;

        if (!in_position && sp.signal == Signal::BUY) {
            in_position  = true;
            entry_ts     = sp.timestamp;
            entry_price  = sp.price;
            entry_bar    = bar;

        } else if (in_position && sp.signal == Signal::SELL) {
            double exit_price = sp.price;
            double pnl_pct    = ((exit_price - entry_price) / entry_price) * 100.0;
            int    duration   = static_cast<int>(bar) - static_cast<int>(entry_bar);

            Trade trade;
            trade.entry_timestamp = entry_ts;
            trade.exit_timestamp  = sp.timestamp;
            trade.entry_price     = entry_price;
            trade.exit_price      = exit_price;
            trade.duration_bars   = duration;
            trade.pnl_pct         = pnl_pct;
            trade.is_win          = (pnl_pct > 0.0);

            if (trade.is_win) ++wins;

            result.trades.push_back(trade);

            cumulative_return *= (1.0 + pnl_pct / 100.0);
            equity_curve.push_back(cumulative_return);

            in_position = false;
        }
    }

    result.num_trades = static_cast<int>(result.trades.size());

    if (result.num_trades > 0) {
        result.total_return_pct = cumulative_return - 100.0;
        result.win_rate         = static_cast<double>(wins) / result.num_trades * 100.0;
        result.max_drawdown_pct = computeMaxDrawdown(equity_curve);
    }

    return result;
}
