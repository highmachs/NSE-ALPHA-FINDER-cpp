#pragma once

#include <vector>
#include <string>

enum class Signal : int8_t {
    BUY  =  1,
    SELL = -1,
    HOLD =  0
};

inline const char* signalToStr(Signal s) {
    switch (s) {
        case Signal::BUY:  return "BUY";
        case Signal::SELL: return "SELL";
        default:           return "HOLD";
    }
}

struct SignalPoint {
    std::string timestamp;
    Signal      signal;
    double      price;
};

class SignalEngine {
public:
    static std::vector<SignalPoint> smaCrossover(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int short_window,
        int long_window);

    static std::vector<SignalPoint> rsiStrategy(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int    window      = 14,
        double oversold    = 30.0,
        double overbought  = 70.0);

    static std::vector<SignalPoint> macdStrategy(
        const std::vector<double>&      close,
        const std::vector<std::string>& timestamps,
        int fast_period   = 12,
        int slow_period   = 26,
        int signal_period = 9);
};
