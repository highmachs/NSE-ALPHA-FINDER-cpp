#include "benchmark.hpp"

#include <chrono>

long long BenchmarkModule::nowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()).count();
}

BenchmarkResult BenchmarkModule::measure(
    const std::string&    name,
    std::size_t           data_points,
    std::function<void()> fn) {

    using namespace std::chrono;

    auto t0 = high_resolution_clock::now();
    fn();
    auto t1 = high_resolution_clock::now();

    long long us = duration_cast<microseconds>(t1 - t0).count();

    double throughput = 0.0;
    if (us > 0) {
        throughput = static_cast<double>(data_points) / (static_cast<double>(us) / 1e6);
    }

    return BenchmarkResult{name, us, throughput, data_points};
}
