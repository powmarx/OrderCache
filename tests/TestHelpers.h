#pragma once

// Small helpers shared by the GoogleTest suites.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "OrderCache.h"

namespace ordercache_test {

inline void addOrders(OrderCache& cache, const std::vector<Order>& orders)
{
    for (const auto& order : orders) {
        cache.addOrder(order);
    }
}

inline std::string joinOrderIds(const std::vector<Order>& orders)
{
    std::string out = "[";
    for (std::size_t i = 0; i < orders.size(); ++i) {
        out += (i == 0 ? "" : ", ") + orders[i].orderId();
    }
    return out + "]";
}

// Works with both EXPECT_TRUE and EXPECT_FALSE. A failure message lists the
// orders that are actually present.
inline ::testing::AssertionResult containsOrderId(const std::vector<Order>& orders,
                                                  const std::string& orderId)
{
    const bool found = std::any_of(orders.begin(), orders.end(),
        [&](const Order& o) { return o.orderId() == orderId; });
    if (found) {
        return ::testing::AssertionSuccess() << orderId << " is present in " << joinOrderIds(orders);
    }
    return ::testing::AssertionFailure() << orderId << " is not present in " << joinOrderIds(orders);
}

template <typename Predicate>
std::size_t countOrders(const std::vector<Order>& orders, Predicate predicate)
{
    return static_cast<std::size_t>(std::count_if(orders.begin(), orders.end(), predicate));
}

inline unsigned int totalMatchingSize(OrderCache& cache, const std::vector<std::string>& securityIds)
{
    unsigned int total = 0;
    for (const auto& securityId : securityIds) {
        total += cache.getMatchingSizeForSecurity(securityId);
    }
    return total;
}

// Seed for tests that use random data. It is fixed so failures can be
// reproduced; set ORDERCACHE_TEST_SEED to try other sequences.
inline unsigned int testSeed()
{
    constexpr unsigned int defaultSeed = 20240601u;
    std::string value;
#ifdef _MSC_VER
    // std::getenv triggers C4996, which /sdl turns into an error.
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, "ORDERCACHE_TEST_SEED") == 0 && buffer != nullptr) {
        value = buffer;
    }
    std::free(buffer);
#else
    if (const char* env = std::getenv("ORDERCACHE_TEST_SEED")) {
        value = env;
    }
#endif
    return value.empty() ? defaultSeed
                         : static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 10));
}

} // namespace ordercache_test
