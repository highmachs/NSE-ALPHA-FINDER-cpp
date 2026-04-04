/*
 * NSE Alpha Engine — C++ Unit Test Runner
 * Runs all test suites and reports pass/fail.
 */

#include "test_runner.hpp"
#include <iostream>

void runDataIngestionTests();
void runIndicatorTests();
void runSignalTests();
void runBacktestTests();

int main() {
    std::cout << "============================================\n";
    std::cout << "  NSE Alpha Engine — C++ Unit Tests\n";
    std::cout << "============================================\n";

    runDataIngestionTests();
    runIndicatorTests();
    runSignalTests();
    runBacktestTests();

    return test::summary();
}
