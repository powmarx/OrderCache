#pragma once

// Test settings read from environment variables:
//   ORDERCACHE_RUN_KNOWN_ISSUE_TESTS=1   run tests skipped for known issues (KnownIssues.h)
//   ORDERCACHE_TEST_SEED=<uint32>        fix the seed of the seeded random tests

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace ordercache_test {

inline std::optional<std::string> GetEnvVar(const char* name) {
#ifdef _MSC_VER
    // getenv raises C4996, which /sdl turns into an error.
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

// Seed for the seeded random tests: ORDERCACHE_TEST_SEED if set, otherwise a
// fresh random value. Fixed for the lifetime of the process (CTest runs each
// test in its own process). Tests report it in failure messages so a failing
// run can be reproduced with ORDERCACHE_TEST_SEED=<seed>.
inline std::uint32_t TestSeed() {
    static const std::uint32_t seed = [] {
        const auto text = GetEnvVar("ORDERCACHE_TEST_SEED");
        if (!text) {
            return static_cast<std::uint32_t>(std::random_device{}());
        }
        char* end = nullptr;
        errno = 0;
        const unsigned long long value = std::strtoull(text->c_str(), &end, 10);
        if (text->empty() || *end != '\0' || errno == ERANGE || value > UINT32_MAX) {
            throw std::invalid_argument("ORDERCACHE_TEST_SEED must be an unsigned 32-bit integer, got '" +
                                        *text + "'");
        }
        return static_cast<std::uint32_t>(value);
    }();
    return seed;
}

}  // namespace ordercache_test
