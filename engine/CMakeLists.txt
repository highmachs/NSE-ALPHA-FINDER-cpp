cmake_minimum_required(VERSION 3.18)
project(nse_alpha_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 REQUIRED)

set(ENGINE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/cpp/include)

set(ENGINE_SOURCES
    cpp/src/data_ingestion.cpp
    cpp/src/indicators.cpp
    cpp/src/signals.cpp
    cpp/src/backtest.cpp
    cpp/src/benchmark.cpp
)

pybind11_add_module(nse_engine_cpp
    cpp/bindings/bindings.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(nse_engine_cpp PRIVATE ${ENGINE_INCLUDE_DIR})

target_compile_options(nse_engine_cpp PRIVATE
    -Wall
    -Wextra
    -O3
)

set_target_properties(nse_engine_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build_output
)
