#include "signals.hpp"
#include "indicators.hpp"

#include <cmath>
#include <stdexcept>

std::vector<SignalPoint> SignalEngine::smaCrossover(
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int short_window,
    int long_window) {

    if (close.size() != timestamps.size())
        throw std::invalid_argument("close and timestamps size mismatch");
    if (short_window <= 0 || long_window <= 0)
        throw std::invalid_argument("SMA windows must be positive");
    if (short_window >= long_window)
        throw std::invalid_argument("short_window must be less than long_window");

    auto short_sma = IndicatorEngine::sma(close, short_window);
    auto long_sma  = IndicatorEngine::sma(close, long_window);

    const std::size_t n = close.size();
    std::vector<SignalPoint> signals;

    bool prev_short_above = false;
    bool initialized = false;

    std::size_t start = static_cast<std::size_t>(long_window);

    for (std::size_t i = start; i < n; ++i) {
        if (std::isnan(short_sma[i]) || std::isnan(long_sma[i])) continue;

        bool short_above = short_sma[i] > long_sma[i];

        if (!initialized) {
            prev_short_above = short_above;
            initialized = true;
            continue;
        }

        if (!prev_short_above && short_above) {
            signals.push_back({timestamps[i], Signal::BUY, close[i]});
        } else if (prev_short_above && !short_above) {
            signals.push_back({timestamps[i], Signal::SELL, close[i]});
        } else {
            signals.push_back({timestamps[i], Signal::HOLD, close[i]});
        }

        prev_short_above = short_above;
    }

    return signals;
}

std::vector<SignalPoint> SignalEngine::rsiStrategy(
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int    window,
    double oversold,
    double overbought) {

    if (close.size() != timestamps.size())
        throw std::invalid_argument("close and timestamps size mismatch");

    auto rsi_vals = IndicatorEngine::rsi(close, window);
    const std::size_t n = close.size();
    std::vector<SignalPoint> signals;

    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(rsi_vals[i])) continue;

        Signal sig = Signal::HOLD;
        if (rsi_vals[i] < oversold)    sig = Signal::BUY;
        else if (rsi_vals[i] > overbought) sig = Signal::SELL;

        signals.push_back({timestamps[i], sig, close[i]});
    }

    return signals;
}

std::vector<SignalPoint> SignalEngine::macdStrategy(
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int fast_period,
    int slow_period,
    int signal_period) {

    if (close.size() != timestamps.size())
        throw std::invalid_argument("close and timestamps size mismatch");

    auto macd_result = IndicatorEngine::macd(close, fast_period, slow_period, signal_period);
    const std::size_t n = close.size();
    std::vector<SignalPoint> signals;

    bool prev_macd_above = false;
    bool initialized = false;

    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(macd_result.macd_line[i]) || std::isnan(macd_result.signal_line[i])) continue;

        bool macd_above = macd_result.macd_line[i] > macd_result.signal_line[i];

        if (!initialized) {
            prev_macd_above = macd_above;
            initialized = true;
            continue;
        }

        Signal sig = Signal::HOLD;
        if (!prev_macd_above && macd_above)  sig = Signal::BUY;
        else if (prev_macd_above && !macd_above) sig = Signal::SELL;

        signals.push_back({timestamps[i], sig, close[i]});
        prev_macd_above = macd_above;
    }

    return signals;
}
