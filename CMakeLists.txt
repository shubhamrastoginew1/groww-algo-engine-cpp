cmake_minimum_required(VERSION 3.20)
project(groww_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Some macOS Command Line Tools installs ship an empty usr/include/c++/v1 (even
# <string> is "not found"); the SDK still has libc++'s headers. No-op elsewhere.
if(APPLE AND CMAKE_OSX_SYSROOT)
  include(CheckIncludeFileCXX)
  check_include_file_cxx(string GE_HAVE_CXX_STRING)
  if(NOT GE_HAVE_CXX_STRING AND EXISTS "${CMAKE_OSX_SYSROOT}/usr/include/c++/v1/string")
    add_compile_options("-stdlib++-isystem${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
  endif()
endif()

# ── dependencies (downloaded once into build/_deps) ─────────────────────────
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json yaml-cpp googletest)
find_package(Threads REQUIRED)

# ── trading core ───────────────────────────────────────────────────────────
add_library(ge_core STATIC
  src/core/time.cpp
  src/core/models.cpp
  src/core/logger.cpp
  src/core/config.cpp
  src/core/event_bus.cpp
  src/core/engine.cpp
  src/data/instruments.cpp
  src/data/candle_builder.cpp
  src/data/simulator.cpp
  src/orders/paper_broker.cpp
  src/risk/risk_manager.cpp
  src/portfolio/position_manager.cpp
  src/strategies/strategy.cpp
  src/strategies/ema_crossover.cpp
  src/backtest/backtester.cpp
)
target_include_directories(ge_core PUBLIC include)
target_link_libraries(ge_core PUBLIC nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp Threads::Threads)
target_compile_options(ge_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(groww_engine apps/groww_engine.cpp)
target_link_libraries(groww_engine PRIVATE ge_core)

# ── tests ──────────────────────────────────────────────────────────────────
enable_testing()
add_executable(ge_tests tests/test_engine.cpp)
target_link_libraries(ge_tests PRIVATE ge_core GTest::gtest_main)
target_compile_definitions(ge_tests PRIVATE GE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(GoogleTest)
gtest_discover_tests(ge_tests)
