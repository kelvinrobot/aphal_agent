cmake_minimum_required(VERSION 3.21)

project(exotic
    VERSION 0.1.0
    DESCRIPTION "Autonomous multi-agent runtime for AI agents"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

option(EXOTIC_BUILD_TESTS "Build unit tests" ON)
option(EXOTIC_BUILD_BENCHMARKS "Build benchmark suite" ON)
option(EXOTIC_BUILD_EXAMPLES "Build example programs" ON)
set(EXOTIC_SANITIZER "" CACHE STRING "Sanitizer to build with: address, thread, undefined, or empty")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

find_package(Threads REQUIRED)

if (EXOTIC_BUILD_TESTS OR EXOTIC_BUILD_BENCHMARKS)
    include(Dependencies)
endif()

add_subdirectory(src/exotic)

if (EXOTIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (EXOTIC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if (EXOTIC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
