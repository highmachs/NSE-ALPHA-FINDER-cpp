/**
 * @file test_main.cpp
 * @brief NSE Alpha Engine C++ test runner entry point.
 *
 * Invokes every registered test suite in order and prints a final
 * pass/fail summary. Exit code is 0 on all-pass, 1 on any failure.
 *
 * Test suite registry:
 *   - DataIngestionEngine  (test_data_ingestion.cpp)
 *   - IndicatorEngine      (test_indicators.cpp)
 *   - SignalEngine         (test_signals.cpp)
 *   - BacktestEngine       (test_backtest.cpp)
 *   - Reference Values     (test_reference_values.cpp)  — PRD §9
 */

#include "test_runner.hpp"
#include <iostream>

void runDataIngestionTests();
void runIndicatorTests();
void runSignalTests();
void runBacktestTests();
void runReferenceValueTests();

int main() {
    std::cout << "============================================\n";
    std::cout << "  NSE Alpha Engine — C++ Unit Tests\n";
    std::cout << "============================================\n";

    runDataIngestionTests();
    runIndicatorTests();
    runSignalTests();
    runBacktestTests();
    runReferenceValueTests();

    return test::summary();
}
