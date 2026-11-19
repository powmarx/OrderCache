cmake_minimum_required(VERSION 3.20)

project(OrderCache
    VERSION 1.0.0
    DESCRIPTION "Thread-safe order cache using striped locking"
    LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(ORDERCACHE_BUILD_LEGACY_RUNNER
    "Build the original interactive test runner (OrderCache/*Test*.cpp)" ON)
option(ORDERCACHE_LEGACY_KEEP_ASSERTS
    "Keep assert() active in the legacy runner in every configuration (undefines NDEBUG for that target only)" ON)
option(ORDERCACHE_BUILD_TESTS
    "Build the GoogleTest suite (see cmake/OrderCacheGoogleTest.cmake for ONLINE/OFFLINE modes)" ON)

# Single-config generators (Makefiles, Ninja): default to Debug so that a bare
# `cmake -S . -B build` does not silently produce an unoptimised, flag-less build.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Warning flags applied to this project's targets only (never to GoogleTest).
# MSVC flags mirror OrderCache.vcxproj: Level3, SDLCheck, ConformanceMode.
function(ordercache_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /sdl /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Library: the order cache itself
# -----------------------------------------------------------------------------
add_library(ordercache_core STATIC
    OrderCache/OrderCache.cpp
    OrderCache/OrderCache.h
    OrderCache/OrderCacheInterface.h
    OrderCache/StripedThreadsafeMap.h)
add_library(OrderCache::core ALIAS ordercache_core)

target_include_directories(ordercache_core PUBLIC "${PROJECT_SOURCE_DIR}/OrderCache")
target_compile_features(ordercache_core PUBLIC cxx_std_17)
target_link_libraries(ordercache_core PUBLIC Threads::Threads)
ordercache_set_warnings(ordercache_core)

# -----------------------------------------------------------------------------
# Legacy runner: the original menu-driven test suite, unchanged
# -----------------------------------------------------------------------------
if(ORDERCACHE_BUILD_LEGACY_RUNNER)
    add_executable(ordercache_legacy_runner
        OrderCache/OrderCacheTestRunner.cpp
        OrderCache/OrderCacheTest.cpp
        OrderCache/OrderCacheMultithreadTest.cpp
        OrderCache/OrderCacheStressTest.cpp
        OrderCache/OrderCacheBenchmark.cpp
        OrderCache/OrderCacheTestUtils.h)
    target_link_libraries(ordercache_legacy_runner PRIVATE OrderCache::core)
    ordercache_set_warnings(ordercache_legacy_runner)

    # The original suite checks everything with assert(), which NDEBUG (set by
    # Release/RelWithDebInfo/MinSizeRel) compiles away. Undefine it for this
    # target so the suite actually verifies something in every configuration.
    # The guard TU fails the build if the undefine did not take effect.
    if(ORDERCACHE_LEGACY_KEEP_ASSERTS)
        target_compile_options(ordercache_legacy_runner PRIVATE
            $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
        target_sources(ordercache_legacy_runner PRIVATE
            "${PROJECT_SOURCE_DIR}/cmake/legacy_assert_guard.cpp")
    endif()
endif()

# -----------------------------------------------------------------------------
# GoogleTest suite
# -----------------------------------------------------------------------------
if(ORDERCACHE_BUILD_TESTS)
    enable_testing()
    include(OrderCacheGoogleTest)
    add_subdirectory(tests)
endif()
