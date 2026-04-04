#pragma once

#include <iostream>
#include <string>
#include <cmath>
#include <vector>

namespace test {

// Declarations — defined in test_runner.cpp (single TU)
extern int  g_pass;
extern int  g_fail;
extern std::string g_current_suite;

inline void suite(const std::string& name) {
    g_current_suite = name;
    std::cout << "\n[SUITE] " << name << "\n";
}

inline void check(bool condition,
                  const std::string& name,
                  const std::string& msg = "") {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
        ++g_pass;
    } else {
        std::cout << "  [FAIL] " << name;
        if (!msg.empty()) std::cout << " — " << msg;
        std::cout << "\n";
        ++g_fail;
    }
}

inline void near(double actual, double expected, double tol,
                  const std::string& name) {
    bool ok = std::abs(actual - expected) <= tol;
    std::string msg;
    if (!ok) {
        msg = "expected " + std::to_string(expected) +
              " got "      + std::to_string(actual)  +
              " (tol="     + std::to_string(tol) + ")";
    }
    check(ok, name, msg);
}

inline void isnan_check(double val, const std::string& name) {
    check(std::isnan(val), name,
          "expected NaN, got " + std::to_string(val));
}

inline void notnan(double val, const std::string& name) {
    check(!std::isnan(val), name, "expected non-NaN");
}

inline int summary() {
    std::cout << "\n============================================\n";
    std::cout << " TESTS: " << (g_pass + g_fail)
              << "  PASS: " << g_pass
              << "  FAIL: " << g_fail << "\n";
    std::cout << "============================================\n";
    return g_fail > 0 ? 1 : 0;
}

} // namespace test
