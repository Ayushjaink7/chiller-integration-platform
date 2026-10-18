cmake_minimum_required(VERSION 3.16)
project(ChillerPlatform VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Multi-Model Chiller Integration Platform
#   chiller_core   : HAL interface, simulated models, PID, faults, black-box logger
#   chiller_sim    : command-line runner (run / benchmark / faults / list)
#   chiller_server : REST API + live simulation, serves the React dashboard
#   chiller_tests  : GoogleTest unit tests
# All third-party code is vendored in third_party/, so the build needs no
# internet access and no extra installs beyond a C++17 compiler and CMake.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CHILLER_BUILD_TESTS  "Build the GoogleTest unit tests" ON)
option(CHILLER_BUILD_SERVER "Build the REST API server"       ON)

# Put every executable in build/bin, also for Visual Studio (multi-config) builds.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Threads REQUIRED)

# --- third-party: SQLite (amalgamation, public domain) ----------------------
add_library(sqlite3_vendored STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3_vendored PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3_vendored PRIVATE
  SQLITE_THREADSAFE=1 SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(sqlite3_vendored PRIVATE Threads::Threads)
if(UNIX AND NOT APPLE)
  target_link_libraries(sqlite3_vendored PRIVATE m)
endif()
if(MSVC)
  target_compile_options(sqlite3_vendored PRIVATE /w)
else()
  target_compile_options(sqlite3_vendored PRIVATE -w)
endif()

# --- third-party: nlohmann/json (header-only, MIT) --------------------------
add_library(nlohmann_json_vendored INTERFACE)
target_include_directories(nlohmann_json_vendored SYSTEM INTERFACE third_party)

# --- warnings for our own code ----------------------------------------------
function(chiller_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# --- core library -------------------------------------------------------------
add_library(chiller_core STATIC
  src/types.cpp
  src/config.cpp
  src/scenario.cpp
  src/pid_controller.cpp
  src/simulated_chiller.cpp
  src/screw_chiller.cpp
  src/centrifugal_chiller.cpp
  src/scroll_chiller.cpp
  src/model_factory.cpp
  src/control_app.cpp
  src/blackbox_logger.cpp
  src/metrics.cpp
  src/runner.cpp
)
target_include_directories(chiller_core PUBLIC include)
target_link_libraries(chiller_core PUBLIC nlohmann_json_vendored sqlite3_vendored Threads::Threads)
chiller_warnings(chiller_core)
if(MINGW)
  # std::filesystem needs this on older MinGW toolchains; harmless on newer ones.
  target_link_libraries(chiller_core PUBLIC stdc++fs)
endif()

# --- command-line simulator ----------------------------------------------------
add_executable(chiller_sim apps/chiller_sim.cpp)
target_link_libraries(chiller_sim PRIVATE chiller_core)
chiller_warnings(chiller_sim)

# --- REST API server -------------------------------------------------------------
if(CHILLER_BUILD_SERVER)
  add_executable(chiller_server apps/chiller_server.cpp apps/live_simulation.cpp)
  target_include_directories(chiller_server SYSTEM PRIVATE third_party/httplib)
  target_link_libraries(chiller_server PRIVATE chiller_core Threads::Threads)
  chiller_warnings(chiller_server)
  if(WIN32)
    target_compile_definitions(chiller_server PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(chiller_server PRIVATE ws2_32)
  endif()
endif()

# --- unit tests ------------------------------------------------------------------
if(CHILLER_BUILD_TESTS)
  enable_testing()
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

  add_executable(chiller_tests
    tests/test_pid.cpp
    tests/test_config.cpp
    tests/test_models.cpp
    tests/test_faults.cpp
    tests/test_logger.cpp
    tests/test_metrics.cpp
    tests/test_closed_loop.cpp
  )
  target_link_libraries(chiller_tests PRIVATE chiller_core GTest::gtest_main)
  target_compile_definitions(chiller_tests PRIVATE
    CHILLER_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
  chiller_warnings(chiller_tests)

  include(GoogleTest)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    # Discover tests when ctest runs, not at build time (also works when cross-compiling).
    gtest_discover_tests(chiller_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
  else()
    gtest_discover_tests(chiller_tests DISCOVERY_TIMEOUT 60)
  endif()
endif()
