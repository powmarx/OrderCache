# Provides the GTest::gtest and GTest::gtest_main targets in one of two modes.
#
#   ORDERCACHE_GTEST_MODE=ONLINE  (default)
#       Downloads the pinned GoogleTest release and checks its SHA-256. Tries
#       GitHub first, then the Ubuntu archive copy of the same upstream tarball.
#
#   ORDERCACHE_GTEST_MODE=OFFLINE
#       Never uses the network. Uses, in this order:
#         1. ORDERCACHE_GTEST_SOURCE pointing at a release archive (.tar.gz/.zip).
#            The archive must match the pinned SHA-256 below.
#         2. ORDERCACHE_GTEST_SOURCE pointing at an extracted source directory.
#            Not hash-checked; needs GoogleTest 1.11+ (for the GTest:: targets).
#         3. ORDERCACHE_GTEST_SOURCE empty: an installed GoogleTest 1.10+ package
#            with CMake config files, via find_package(GTest CONFIG), e.g. vcpkg,
#            apt's libgtest-dev, or an install found through CMAKE_PREFIX_PATH /
#            GTest_DIR.

set(ORDERCACHE_GTEST_MODE "ONLINE" CACHE STRING
    "How to obtain GoogleTest: ONLINE (download pinned release) or OFFLINE (local archive/source dir, or installed package)")
set_property(CACHE ORDERCACHE_GTEST_MODE PROPERTY STRINGS ONLINE OFFLINE)
set(ORDERCACHE_GTEST_SOURCE "" CACHE PATH
    "OFFLINE only: GoogleTest release archive or extracted source directory. Empty = use an installed GTest package")

# Pinned release. The SHA-256 is that of the upstream v1.14.0 source tarball as
# recorded in Ubuntu's GPG-signed noble source index (googletest 1.14.0-1).
set(_ordercache_gtest_version "1.14.0")
set(_ordercache_gtest_sha256  "b948c3ec67f81b2292dc4742f5a412d4447e157ae90bc6a1aae7f234c1057081")
set(_ordercache_gtest_urls
    "https://github.com/google/googletest/archive/refs/tags/v${_ordercache_gtest_version}.tar.gz"
    "https://archive.ubuntu.com/ubuntu/pool/universe/g/googletest/googletest_${_ordercache_gtest_version}.orig.tar.gz")

string(TOUPPER "${ORDERCACHE_GTEST_MODE}" _ordercache_gtest_mode)
if(NOT _ordercache_gtest_mode MATCHES "^(ONLINE|OFFLINE)$")
    message(FATAL_ERROR
        "ORDERCACHE_GTEST_MODE must be ONLINE or OFFLINE (got '${ORDERCACHE_GTEST_MODE}').")
endif()

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)  # extracted files get extraction-time timestamps
endif()

# Settings for a GoogleTest built from source as part of this project
# (honoured through CMP0077, which googletest's CMakeLists enables).
set(INSTALL_GTEST OFF)            # do not add googletest to our install rules
set(BUILD_GMOCK OFF)              # only gtest is needed for now
set(gtest_force_shared_crt ON)    # MSVC: use /MD like the rest of the project

if(_ordercache_gtest_mode STREQUAL "ONLINE")
    if(ORDERCACHE_GTEST_SOURCE)
        message(WARNING "ORDERCACHE_GTEST_SOURCE is ignored in ONLINE mode.")
    endif()
    message(STATUS "GoogleTest: ONLINE, downloading v${_ordercache_gtest_version} (SHA-256 verified)")
    FetchContent_Declare(googletest
        URL      ${_ordercache_gtest_urls}
        URL_HASH SHA256=${_ordercache_gtest_sha256})
    FetchContent_MakeAvailable(googletest)

elseif(ORDERCACHE_GTEST_SOURCE)
    get_filename_component(_ordercache_gtest_src "${ORDERCACHE_GTEST_SOURCE}" ABSOLUTE)
    if(NOT EXISTS "${_ordercache_gtest_src}")
        message(FATAL_ERROR "ORDERCACHE_GTEST_SOURCE does not exist: ${_ordercache_gtest_src}")
    endif()

    if(IS_DIRECTORY "${_ordercache_gtest_src}")
        if(NOT EXISTS "${_ordercache_gtest_src}/googletest/CMakeLists.txt")
            message(FATAL_ERROR
                "ORDERCACHE_GTEST_SOURCE is not a GoogleTest source tree "
                "(missing googletest/CMakeLists.txt): ${_ordercache_gtest_src}")
        endif()
        message(STATUS "GoogleTest: OFFLINE, source directory ${_ordercache_gtest_src}")
        FetchContent_Declare(googletest SOURCE_DIR "${_ordercache_gtest_src}")
    else()
        message(STATUS "GoogleTest: OFFLINE, archive ${_ordercache_gtest_src} (SHA-256 verified)")
        FetchContent_Declare(googletest
            URL      "${_ordercache_gtest_src}"
            URL_HASH SHA256=${_ordercache_gtest_sha256})
    endif()
    FetchContent_MakeAvailable(googletest)

else()
    # The tests need GTEST_SKIP and INSTANTIATE_TEST_SUITE_P (GoogleTest 1.10+).
    # CONFIG mode is required: without it, CMake's FindGTest module falls back to
    # a plain library search that reports no version, so an old installation
    # would pass the check. vcpkg, libgtest-dev, and any CMake-installed
    # GoogleTest provide the config files.
    find_package(GTest 1.10 CONFIG)
    if(NOT GTest_FOUND OR NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest: OFFLINE mode found no installed GTest package (1.10 or newer).\n"
            "Either point CMake at an installation that provides GTestConfig.cmake\n"
            "(CMAKE_PREFIX_PATH, GTest_DIR, or the vcpkg toolchain file),\n"
            "or pass -DORDERCACHE_GTEST_SOURCE=<googletest archive or source dir>.\n"
            "The pinned archive is googletest v${_ordercache_gtest_version}, available from:\n"
            "  ${_ordercache_gtest_urls}")
    endif()
    message(STATUS "GoogleTest: OFFLINE, installed package (version: ${GTest_VERSION})")
endif()

if(NOT TARGET GTest::gtest_main)
    message(FATAL_ERROR "GoogleTest setup did not provide the GTest::gtest_main target.")
endif()
