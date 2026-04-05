/**
 * @file benchmark.hpp
 * @brief High-resolution performance measurement module.
 *
 * Wraps std::chrono::high_resolution_clock to measure wall-clock elapsed
 * time for any callable with nanosecond resolution. Results are reported
 * in microseconds and as a throughput rate (data points per second).
 *
 * PRD §5.5 requirements satisfied:
 *   - Time in microseconds.
 *   - Throughput (data points per second).
 *   - Uses high-resolution clock.
 *   - Output reproducible across runs (deterministic input → deterministic output).
 *
 * Usage example:
 * @code
 *   auto result = BenchmarkModule::measure("SMA(20)", close.size(), [&] {
 *       IndicatorEngine::sma(close, 20);
 *   });
 *   std::cout << result.elapsed_us << " us\n";
 * @endcode
 */

#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Result of a single benchmark measurement.
 */
struct BenchmarkResult {
    std::string name;               ///< Human-readable label for the measured operation.
    long long   elapsed_us;         ///< Wall-clock elapsed time in microseconds.
    double      throughput_per_sec; ///< data_points / (elapsed_us / 1e6), or 0 if elapsed == 0.
    std::size_t data_points;        ///< Number of data points passed to the operation.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Stateless, high-resolution performance measurement utility.
 *
 * All methods are static. No instance is required.
 *
 * Thread safety: measure() and nowUs() may be called concurrently; they
 * access only the system clock and their function argument.
 */
class BenchmarkModule {
public:
    /**
     * @brief Measure the wall-clock time of an arbitrary callable.
     *
     * Calls @p fn exactly once, records start and end timestamps via
     * std::chrono::high_resolution_clock, and computes throughput.
     *
     * The callable may perform any work; no constraints on allocations
     * or exceptions (exceptions propagate normally).
     *
     * @param name        Label stored in the returned BenchmarkResult.
     * @param data_points Number of logical data points processed by @p fn.
     *                    Used only for the throughput calculation — not validated.
     * @param fn          Zero-argument callable to time. Called exactly once.
     * @return            BenchmarkResult with timing and throughput.
     */
    static BenchmarkResult measure(
        const std::string&        name,
        std::size_t               data_points,
        std::function<void()>     fn);

    /**
     * @brief Return the current wall-clock time as microseconds since epoch.
     *
     * Useful for manual multi-section timing without wrapping each section
     * in a lambda.
     *
     * @return  Microseconds since the clock epoch (not UTC necessarily).
     */
    static long long nowUs();
};
