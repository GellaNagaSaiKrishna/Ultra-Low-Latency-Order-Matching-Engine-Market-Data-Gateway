cmake_minimum_required(VERSION 3.20)

project(
    UltraLowLatencyOME
    VERSION 1.0.0
    DESCRIPTION "Ultra-Low Latency Order Matching Engine & Market Data Gateway"
    LANGUAGES CXX
)

# ===========================================================================
# C++20 standard — required for std::span, concepts, likely/unlikely attributes
# ===========================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===========================================================================
# Default build type
# ===========================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ===========================================================================
# Compiler flags — optimise aggressively for native CPU
# ===========================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
    add_link_options(
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Release>:-flto>
    )
elseif(MSVC)
    add_compile_options(
        /W4 /std:c++20
        $<$<CONFIG:Release>:/O2 /GL /arch:AVX2>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
endif()

# Export compile commands for clangd / IDE integration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===========================================================================
# Header-only interface library — shared by all targets
# ===========================================================================
add_library(ome_headers INTERFACE)
target_include_directories(ome_headers INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

# ===========================================================================
# Core library — compilable source files
# ===========================================================================
add_library(ome_core STATIC
    src/order_book.cpp
)
target_link_libraries(ome_core PUBLIC ome_headers)
target_include_directories(ome_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ===========================================================================
# Benchmark executable
# ===========================================================================
add_executable(benchmark
    src/benchmark.cpp
)
target_link_libraries(benchmark PRIVATE ome_core ome_headers)

# Pin benchmark binary to Release optimisations regardless of global type.
target_compile_options(benchmark PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math>
)

# ===========================================================================
# Tests via GTest (fetched automatically)
# ===========================================================================
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# Prevent installing GTest alongside our project.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Windows compatibility

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(tests
    tests/test_memory_pool.cpp
    tests/test_spsc_queue.cpp
    tests/test_order_book.cpp
    tests/test_protocol.cpp
)
target_link_libraries(tests PRIVATE
    ome_core
    ome_headers
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(tests)

# ===========================================================================
# Summary
# ===========================================================================
message(STATUS "")
message(STATUS "=== Ultra-Low Latency OME ===========================")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Targets      : ome_core | benchmark | tests")
message(STATUS "=====================================================")
message(STATUS "")
