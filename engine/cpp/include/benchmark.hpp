#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <cstddef>

struct BenchmarkResult {
    std::string name;
    long long   elapsed_us;
    double      throughput_per_sec;
    std::size_t data_points;
};

class BenchmarkModule {
public:
    static BenchmarkResult measure(
        const std::string&        name,
        std::size_t               data_points,
        std::function<void()>     fn);

    static long long nowUs();
};
