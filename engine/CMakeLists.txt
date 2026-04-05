cmake_minimum_required(VERSION 3.18)
project(nse_alpha_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 REQUIRED)

# ── Shared include path ───────────────────────────────────────────────────────
set(ENGINE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/cpp/include)

# ── Core engine sources (shared by all targets) ───────────────────────────────
set(ENGINE_SOURCES
    cpp/src/data_ingestion.cpp
    cpp/src/indicators.cpp
    cpp/src/signals.cpp
    cpp/src/backtest.cpp
    cpp/src/benchmark.cpp
    cpp/src/data_utils.cpp
)

# ── 1. Python extension module (pybind11) ─────────────────────────────────────
pybind11_add_module(nse_engine_cpp
    cpp/bindings/bindings.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(nse_engine_cpp PRIVATE ${ENGINE_INCLUDE_DIR})
target_compile_options(nse_engine_cpp PRIVATE -Wall -Wextra -O3)

set_target_properties(nse_engine_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_output
)
if(WIN32)
    target_link_options(nse_engine_cpp PRIVATE -static -static-libstdc++ -static-libgcc)
endif()

# ── 2. Standalone CLI executable ──────────────────────────────────────────────
add_executable(nse_engine
    cpp/src/main.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(nse_engine PRIVATE ${ENGINE_INCLUDE_DIR})
target_compile_options(nse_engine PRIVATE -Wall -Wextra -O3)

set_target_properties(nse_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_output
)

# ── 3. C++ Unit Test runner ───────────────────────────────────────────────────
add_executable(nse_tests
    cpp/tests/test_runner.cpp
    cpp/tests/test_main.cpp
    cpp/tests/test_data_ingestion.cpp
    cpp/tests/test_indicators.cpp
    cpp/tests/test_signals.cpp
    cpp/tests/test_backtest.cpp
    cpp/tests/test_reference_values.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(nse_tests PRIVATE
    ${ENGINE_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/cpp/tests
)
target_compile_options(nse_tests PRIVATE -Wall -Wextra -O2)

set_target_properties(nse_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_output
)

# ── CTest integration ─────────────────────────────────────────────────────────
enable_testing()
add_test(NAME UnitTests COMMAND nse_tests)
