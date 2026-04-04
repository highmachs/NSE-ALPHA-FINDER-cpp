#include "indicators.hpp"

#include <stdexcept>
#include <cmath>
#include <numeric>

std::vector<double> IndicatorEngine::sma(const std::vector<double>& close, int window) {
    if (window <= 0) throw std::invalid_argument("SMA window must be positive");
    const std::size_t n = close.size();
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    if (static_cast<std::size_t>(window) > n) return result;

    double rolling_sum = 0.0;
    for (int i = 0; i < window; ++i) rolling_sum += close[static_cast<std::size_t>(i)];
    result[static_cast<std::size_t>(window - 1)] = rolling_sum / window;

    for (std::size_t i = static_cast<std::size_t>(window); i < n; ++i) {
        rolling_sum += close[i] - close[i - static_cast<std::size_t>(window)];
        result[i] = rolling_sum / window;
    }
    return result;
}

std::vector<double> IndicatorEngine::ema(const std::vector<double>& close, int window) {
    if (window <= 0) throw std::invalid_argument("EMA window must be positive");
    const std::size_t n = close.size();
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    if (static_cast<std::size_t>(window) > n) return result;

    const double alpha = 2.0 / (window + 1.0);
    const double alpha_inv = 1.0 - alpha;

    double seed = 0.0;
    for (int i = 0; i < window; ++i) seed += close[static_cast<std::size_t>(i)];
    seed /= window;

    std::size_t start = static_cast<std::size_t>(window - 1);
    result[start] = seed;

    for (std::size_t i = start + 1; i < n; ++i) {
        result[i] = alpha * close[i] + alpha_inv * result[i - 1];
    }
    return result;
}

std::vector<double> IndicatorEngine::emaFromSeed(const std::vector<double>& data,
                                                   int window,
                                                   double seed,
                                                   std::size_t start_idx) {
    const std::size_t n = data.size();
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    if (start_idx >= n) return result;

    const double alpha = 2.0 / (window + 1.0);
    const double alpha_inv = 1.0 - alpha;

    result[start_idx] = seed;
    for (std::size_t i = start_idx + 1; i < n; ++i) {
        result[i] = alpha * data[i] + alpha_inv * result[i - 1];
    }
    return result;
}

std::vector<double> IndicatorEngine::rsi(const std::vector<double>& close, int window) {
    if (window <= 0) throw std::invalid_argument("RSI window must be positive");
    const std::size_t n = close.size();
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    if (static_cast<std::size_t>(window) >= n) return result;

    const double alpha = 1.0 / window;
    const double alpha_inv = 1.0 - alpha;

    double avg_gain = 0.0;
    double avg_loss = 0.0;

    for (int i = 1; i <= window; ++i) {
        double diff = close[static_cast<std::size_t>(i)] - close[static_cast<std::size_t>(i - 1)];
        if (diff > 0.0) avg_gain += diff;
        else            avg_loss -= diff;
    }
    avg_gain /= window;
    avg_loss /= window;

    auto calcRSI = [](double ag, double al) -> double {
        if (al == 0.0) return 100.0;
        return 100.0 - (100.0 / (1.0 + ag / al));
    };

    result[static_cast<std::size_t>(window)] = calcRSI(avg_gain, avg_loss);

    for (std::size_t i = static_cast<std::size_t>(window) + 1; i < n; ++i) {
        double diff = close[i] - close[i - 1];
        double gain = (diff > 0.0) ? diff : 0.0;
        double loss = (diff < 0.0) ? -diff : 0.0;
        avg_gain = alpha * gain + alpha_inv * avg_gain;
        avg_loss = alpha * loss + alpha_inv * avg_loss;
        result[i] = calcRSI(avg_gain, avg_loss);
    }
    return result;
}

MACDResult IndicatorEngine::macd(const std::vector<double>& close,
                                  int fast_period,
                                  int slow_period,
                                  int signal_period) {
    if (fast_period <= 0 || slow_period <= 0 || signal_period <= 0)
        throw std::invalid_argument("MACD periods must be positive");
    if (fast_period >= slow_period)
        throw std::invalid_argument("MACD fast period must be less than slow period");

    const std::size_t n = close.size();
    MACDResult result;
    result.macd_line.assign(n, std::numeric_limits<double>::quiet_NaN());
    result.signal_line.assign(n, std::numeric_limits<double>::quiet_NaN());
    result.histogram.assign(n, std::numeric_limits<double>::quiet_NaN());

    auto fast_ema = ema(close, fast_period);
    auto slow_ema = ema(close, slow_period);

    std::size_t macd_start = static_cast<std::size_t>(slow_period - 1);
    if (macd_start >= n) return result;

    for (std::size_t i = macd_start; i < n; ++i) {
        result.macd_line[i] = fast_ema[i] - slow_ema[i];
    }

    double seed = 0.0;
    int count = 0;
    for (std::size_t i = macd_start; i < macd_start + static_cast<std::size_t>(signal_period) && i < n; ++i) {
        seed += result.macd_line[i];
        ++count;
    }
    if (count < signal_period) return result;
    seed /= signal_period;

    std::size_t signal_start = macd_start + static_cast<std::size_t>(signal_period - 1);
    if (signal_start >= n) return result;

    auto signal_ema = emaFromSeed(result.macd_line, signal_period, seed, signal_start);

    for (std::size_t i = signal_start; i < n; ++i) {
        result.signal_line[i] = signal_ema[i];
        result.histogram[i]   = result.macd_line[i] - result.signal_line[i];
    }

    return result;
}

BollingerBandsResult IndicatorEngine::bollingerBands(const std::vector<double>& close,
                                                       int window,
                                                       double k) {
    if (window <= 0) throw std::invalid_argument("Bollinger Bands window must be positive");
    const std::size_t n = close.size();
    BollingerBandsResult result;
    result.upper.assign(n, std::numeric_limits<double>::quiet_NaN());
    result.middle.assign(n, std::numeric_limits<double>::quiet_NaN());
    result.lower.assign(n, std::numeric_limits<double>::quiet_NaN());

    if (static_cast<std::size_t>(window) > n) return result;

    // O(n) incremental variance using running sum and sum-of-squares
    double sum    = 0.0;
    double sum_sq = 0.0;
    const std::size_t w = static_cast<std::size_t>(window);

    for (std::size_t i = 0; i < w; ++i) {
        sum    += close[i];
        sum_sq += close[i] * close[i];
    }

    for (std::size_t i = w - 1; i < n; ++i) {
        if (i >= w) {
            double removed = close[i - w];
            sum    += close[i] - removed;
            sum_sq += close[i] * close[i] - removed * removed;
        }
        double mean     = sum / window;
        double variance = (sum_sq / window) - (mean * mean);
        if (variance < 0.0) variance = 0.0;
        double sd = std::sqrt(variance);
        result.middle[i] = mean;
        result.upper[i]  = mean + k * sd;
        result.lower[i]  = mean - k * sd;
    }

    return result;
}
