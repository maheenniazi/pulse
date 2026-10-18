cmake_minimum_required(VERSION 3.20)
project(pulse LANGUAGES CXX)

# ---- Standard & default build type -----------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- Warnings as a first-class citizen --------------------------------------
add_library(pulse_warnings INTERFACE)
target_compile_options(pulse_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# ---- Optional sanitizers (CI runs the test suite with these on) -------------
#   cmake -DPULSE_SANITIZE=address,undefined   or   -DPULSE_SANITIZE=thread
set(PULSE_SANITIZE "" CACHE STRING "Comma-separated sanitizer list (address,undefined,thread)")
add_library(pulse_sanitize INTERFACE)
if(PULSE_SANITIZE)
    message(STATUS "Sanitizers enabled: ${PULSE_SANITIZE}")
    target_compile_options(pulse_sanitize INTERFACE -fsanitize=${PULSE_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(pulse_sanitize INTERFACE -fsanitize=${PULSE_SANITIZE})
endif()

# ---- The core engine, built as a static library -----------------------------
add_library(pulse_core STATIC src/order_book.cpp src/backtester.cpp)
target_include_directories(pulse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# strategy/ headers are included by apps and tests via "strategy/..."
target_include_directories(pulse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pulse_core PUBLIC pulse_warnings pulse_sanitize)

# ---- Demo + backtest applications -------------------------------------------
add_executable(pulse_demo apps/demo.cpp)
target_link_libraries(pulse_demo PRIVATE pulse_core)

add_executable(pulse_backtest apps/backtest.cpp)
target_link_libraries(pulse_backtest PRIVATE pulse_core)

# ---- Benchmark ---------------------------------------------------------------
add_executable(pulse_bench bench/bench_order_book.cpp)
target_link_libraries(pulse_bench PRIVATE pulse_core)
# Benchmarks only make sense optimized.
target_compile_options(pulse_bench PRIVATE $<$<CONFIG:Release>:-O3>)

# ---- Tests -------------------------------------------------------------------
enable_testing()
find_package(Threads REQUIRED)

add_executable(pulse_tests
    tests/test_main.cpp
    tests/test_order_book.cpp
    tests/test_spsc.cpp)
target_include_directories(pulse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(pulse_tests PRIVATE pulse_core Threads::Threads)

add_test(NAME unit_tests COMMAND pulse_tests)
