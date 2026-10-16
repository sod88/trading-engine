cmake_minimum_required(VERSION 3.20)
project(QuantEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Core engine library
add_library(qe
  src/data_feed.cpp
  src/portfolio.cpp
  src/broker.cpp
  src/engine.cpp
  src/metrics.cpp
)
target_include_directories(qe PUBLIC include)

if(MSVC)
  target_compile_options(qe PUBLIC /W4 /permissive-)
else()
  target_compile_options(qe PUBLIC -Wall -Wextra -Wpedantic)
endif()

# Command-line backtester
add_executable(backtest app/main.cpp)
target_link_libraries(backtest PRIVATE qe)

# Tests
enable_testing()
add_executable(qe_tests tests/tests.cpp)
target_link_libraries(qe_tests PRIVATE qe)
add_test(NAME qe_tests COMMAND qe_tests)
