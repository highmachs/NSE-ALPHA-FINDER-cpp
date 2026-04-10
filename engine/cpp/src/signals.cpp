#define NOMINMAX
#include "signals.hpp"
#include "indicators.hpp"

#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>

std::vector<SignalPoint> SignalEngine::smaCrossover(
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int short_window,
    int long_window) {

    if (close.size() != timestamps.size())
        throw std::invalid_argument("close and timestamps size mismatch");
    
    if (short_window >= long_window)
        throw std::invalid_argument("short_window must be < long_window");
    
    auto short_sma = IndicatorEngine::sma(close, short_window);
    auto long_sma  = IndicatorEngine::sma(close, long_window);

    const std::size_t n = close.size();
    std::vector<SignalPoint> signals(n);
    std::size_t start = static_cast<std::size_t>(long_window - 1);

    // Initialize initial range with HOLD/NaN
    for(std::size_t i=0; i<start; ++i) {
        signals[i] = {timestamps[i], Signal::HOLD, close[i]};
    }

    #pragma omp parallel for schedule(static)
    for (std::size_t i = start; i < n; ++i) {
        if (std::isnan(short_sma[i]) || std::isnan(long_sma[i])) {
            signals[i] = {timestamps[i], Signal::HOLD, close[i]};
            continue;
        }

        bool currently_above = short_sma[i] > long_sma[i];
        
        // Check crossover from previous (if not start)
        if (i > start) {
            bool previously_above = short_sma[i-1] > long_sma[i-1];
            if (!previously_above && currently_above) {
                signals[i] = {timestamps[i], Signal::BUY, close[i]};
            } else if (previously_above && !currently_above) {
                signals[i] = {timestamps[i], Signal::SELL, close[i]};
            } else {
                signals[i] = {timestamps[i], Signal::HOLD, close[i]};
            }
        } else {
             signals[i] = {timestamps[i], Signal::HOLD, close[i]};
        }
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

std::vector<SignalPoint> SignalEngine::bollingerStrategy(
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int    window,
    double k) {

    auto bb = IndicatorEngine::bollingerBands(close, window, k);
    const std::size_t n = close.size();
    std::vector<SignalPoint> signals;

    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(bb.upper[i]) || std::isnan(bb.lower[i])) continue;

        Signal sig = Signal::HOLD;
        if (close[i] < bb.lower[i])     sig = Signal::BUY;
        else if (close[i] > bb.upper[i]) sig = Signal::SELL;

        if (sig != Signal::HOLD) {
            signals.push_back({timestamps[i], sig, close[i]});
        }
    }
    return signals;
}

std::vector<SignalPoint> SignalEngine::supertrendStrategy(
    const std::vector<double>&      high,
    const std::vector<double>&      low,
    const std::vector<double>&      close,
    const std::vector<std::string>& timestamps,
    int    period,
    double multiplier) {

    const std::size_t n = close.size();
    auto atr = IndicatorEngine::atr(high, low, close, period);
    std::vector<SignalPoint> signals;

    std::vector<double> upper_band(n, 0.0);
    std::vector<double> lower_band(n, 0.0);
    std::vector<int> trend(n, 1); // 1 for UP, -1 for DOWN

    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(atr[i])) continue;

        double mid = (high[i] + low[i]) / 2.0;
        double basic_upper = mid + (multiplier * atr[i]);
        double basic_lower = mid - (multiplier * atr[i]);

        if (i == 0) {
            upper_band[i] = basic_upper;
            lower_band[i] = basic_lower;
        } else {
            upper_band[i] = (basic_upper < upper_band[i-1] || close[i-1] > upper_band[i-1]) ? basic_upper : upper_band[i-1];
            lower_band[i] = (basic_lower > lower_band[i-1] || close[i-1] < lower_band[i-1]) ? basic_lower : lower_band[i-1];
            
            trend[i] = trend[i-1];
            if (trend[i-1] == 1 && close[i] < lower_band[i]) trend[i] = -1;
            else if (trend[i-1] == -1 && close[i] > upper_band[i]) trend[i] = 1;

            if (trend[i] != trend[i-1]) {
                Signal sig = (trend[i] == 1) ? Signal::BUY : Signal::SELL;
                signals.push_back({timestamps[i], sig, close[i]});
            }
        }
    }
    return signals;
}
