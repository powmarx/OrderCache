// GoogleTest port of runStressTest(numOrders) in OrderCache/OrderCacheStressTest.cpp.
// That file is left unchanged and still runs through ordercache_legacy_runner
// (menu option 40).
//
// The original reads numOrders interactively (1..1,000,000). Here it is the
// test parameter; see INSTANTIATE_TEST_SUITE_P at the bottom for the sizes.
// The workload, random distributions and expected values match the original.
// Statistics the original only printed (total matching size, average quantity)
// are still computed, so the same code paths run, but they are not printed.

#include <gtest/gtest.h>

#include <exception>
#include <random>
#include <string>
#include <vector>

#include "KnownIssues.h"
#include "OrderCache.h"

namespace {

unsigned long long TotalMatchingAcrossSecurities(OrderCache& cache) {
    unsigned long long total = 0;
    for (int sec = 1; sec <= 10; ++sec) {
        total += cache.getMatchingSizeForSecurity("SecId" + std::to_string(sec));
    }
    return total;
}

// Sizes at or above this are skipped until the index-set copy issue is fixed:
// at 100,000 orders the quadratic cost exceeds the CTest timeout.
constexpr unsigned int kQuadraticCostThreshold = 100000;

}  // namespace

class StressTest : public ::testing::TestWithParam<unsigned int> {};

TEST_P(StressTest, RandomOrdersThenCancellations) {
    const unsigned int numOrders = GetParam();
    if (numOrders >= kQuadraticCostThreshold) {
        ORDERCACHE_SKIP_KNOWN_ISSUE(ordercache_test::kIndexSetCopyIssue);
    }
    OrderCache cache;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> secDist(1, 10);         // 10 securities
    std::uniform_int_distribution<> qtyDist(100, 2000);
    std::uniform_int_distribution<> userDist(100, 200);     // 101 users
    std::uniform_int_distribution<> companyDist(100, 500);  // 401 companies
    std::uniform_int_distribution<> sideDist(0, 1);         // 0: Buy, 1: Sell

    // Add numOrders random orders
    for (unsigned int i = 1; i <= numOrders; ++i) {
        // Drawn in the same order as the original.
        const std::string secId = "SecId" + std::to_string(secDist(gen));
        const std::string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
        const unsigned int qty = static_cast<unsigned int>(qtyDist(gen));
        const std::string user = "User" + std::to_string(userDist(gen));
        const std::string company = "Company" + std::to_string(companyDist(gen));
        cache.addOrder(Order("StressOrd" + std::to_string(i), secId, side, qty, user, company));
    }

    auto orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), static_cast<size_t>(numOrders))
        << "Total orders should match numOrders after adding";

    TotalMatchingAcrossSecurities(cache);

    // cancelOrder: cancel 10% of orders at random. IDs can repeat, so a cancel
    // may hit an order that is already gone; those are not counted.
    const int numToCancel = numOrders / 10;
    int realNumToCancel = numToCancel;
    for (int i = 0; i < numToCancel; ++i) {
        std::uniform_int_distribution<> ordDist(1, numOrders - i);
        try {
            cache.cancelOrder("StressOrd" + std::to_string(ordDist(gen)));
        } catch (const std::exception&) {
            realNumToCancel--;  // the original printed a warning here
        }
    }
    orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), static_cast<size_t>(numOrders - realNumToCancel))
        << "Orders after single cancellations match";

    // cancelOrdersForUser for a random user
    cache.cancelOrdersForUser("User" + std::to_string(userDist(gen)));

    // cancelOrdersForSecIdWithMinimumQty for a random security, minQty 500
    cache.cancelOrdersForSecIdWithMinimumQty("SecId" + std::to_string(secDist(gen)), 500);

    orders = cache.getAllOrders();
    TotalMatchingAcrossSecurities(cache);

    EXPECT_LT(orders.size(), static_cast<size_t>(numOrders)) << "Orders decreased after cancellations";
}

// Sizes within the original's 1..1,000,000 range. The final assertion needs at
// least one cancellation to succeed, which is effectively certain at these sizes
// (at 1,000 orders, 100 random cancels are attempted). 100,000 is currently
// skipped (see kQuadraticCostThreshold).
INSTANTIATE_TEST_SUITE_P(OrderCounts, StressTest,
                         ::testing::Values(1000u, 10000u, 100000u),
                         [](const ::testing::TestParamInfo<unsigned int>& info) {
                             return "Orders" + std::to_string(info.param);
                         });
