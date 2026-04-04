#pragma once

#include <vector>
#include <string>
#include <cstddef>

struct MACDResult {
    std::vector<double> macd_line;
    std::vector<double> signal_line;
    std::vector<double> histogram;
};

struct BollingerBandsResult {
    std::vector<double> upper;
    std::vector<double> middle;
    std::vector<double> lower;
};

class IndicatorEngine {
public:
    static std::vector<double> sma(const std::vector<double>& close, int window);

    static std::vector<double> ema(const std::vector<double>& close, int window);

    static std::vector<double> rsi(const std::vector<double>& close, int window = 14);

    static MACDResult macd(const std::vector<double>& close,
                            int fast_period = 12,
                            int slow_period = 26,
                            int signal_period = 9);

    static BollingerBandsResult bollingerBands(const std::vector<double>& close,
                                                int window = 20,
                                                double k = 2.0);

private:
    static std::vector<double> emaFromSeed(const std::vector<double>& data,
                                            int window,
                                            double seed,
                                            std::size_t start_idx);
};
