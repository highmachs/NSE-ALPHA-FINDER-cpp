#pragma once

#include <vector>
#include <string>
#include "signals.hpp"

struct Trade {
    std::string entry_timestamp;
    std::string exit_timestamp;
    double      entry_price;
    double      exit_price;
    int         duration_bars;
    double      pnl_pct;
    bool        is_win;
};

struct BacktestResult {
    std::vector<Trade> trades;
    double total_return_pct;
    double win_rate;
    int    num_trades;
    double max_drawdown_pct;
};

class BacktestEngine {
public:
    static BacktestResult run(
        const std::vector<SignalPoint>&  signals,
        const std::vector<double>&       close,
        const std::vector<std::string>&  timestamps);

private:
    static double computeMaxDrawdown(const std::vector<double>& equity_curve);
};
