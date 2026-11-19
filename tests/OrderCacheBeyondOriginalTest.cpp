// =============================================================================
// BEYOND THE ORIGINAL SUITE
//
// Nothing in this file is a port of OrderCache/*Test*.cpp. These tests go beyond
// the original suite: they add expectations the original never checked. The
// ports, which keep the original expected values, are in
// OrderCacheSingleThreadTest.cpp, OrderCacheStressTest.cpp and
// OrderCacheConcurrencyTest.cpp.
//
// Contents:
//  1. Cancel semantics (agreed rule): the first cancel of an order succeeds, any
//     later cancel of the same order throws std::out_of_range, both sequentially
//     and when several threads cancel the same order at the same time.
//  2. Exact counts for the workloads of original multithreaded Tests 2 and 5,
//     which the original only checked loosely (a range) or not at all.
//  3. A seeded stress test: the original stress workload with a printed,
//     overridable seed (ORDERCACHE_TEST_SEED), so a failure can be reproduced.
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "KnownIssues.h"
#include "OrderCache.h"
#include "TestEnvironment.h"

using ordercache_test::kCancelNotAtomicIssue;
using ordercache_test::kStripedMapRaceIssue;

namespace {

// Wraps a worker-thread body so that an unexpected exception becomes a test
// failure instead of std::terminate (GoogleTest assertions are thread-safe).
template <typename Body>
auto Guarded(Body body) {
    return [body]() mutable {
        try {
            body();
        } catch (const std::exception& e) {
            ADD_FAILURE() << "Unexpected exception in worker thread: " << e.what();
        } catch (...) {
            ADD_FAILURE() << "Unexpected non-std exception in worker thread";
        }
    };
}

void JoinAll(std::vector<std::thread>& threads) {
    for (auto& t : threads) {
        t.join();
    }
}

// Holds every participating thread until all have arrived, then releases them
// together so their operations really overlap (C++17 has no std::latch).
class StartGate {
public:
    explicit StartGate(int participants) : waiting_(participants) {}

    void ArriveAndWait() {
        waiting_.fetch_sub(1);
        while (waiting_.load() > 0) {
            std::this_thread::yield();
        }
    }

private:
    std::atomic<int> waiting_;
};

bool ContainsOrder(const std::vector<Order>& orders, const std::string& orderId) {
    for (const auto& order : orders) {
        if (order.orderId() == orderId) return true;
    }
    return false;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. Cancel semantics: first cancel succeeds, later cancels throw
// -----------------------------------------------------------------------------
TEST(CancelSemanticsTest, SecondCancelOfSameOrderThrows) {
    OrderCache cache;
    cache.addOrder(Order("Ord1", "SecA", "Buy", 100, "User1", "Comp1"));
    cache.addOrder(Order("Ord2", "SecA", "Sell", 100, "User2", "Comp2"));

    EXPECT_NO_THROW(cache.cancelOrder("Ord1")) << "First cancel should succeed";
    EXPECT_THROW(cache.cancelOrder("Ord1"), std::out_of_range) << "Second cancel of the same order should fail";

    // The failed second cancel must not disturb the rest of the cache.
    const auto orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_TRUE(ContainsOrder(orders, "Ord2"));
}

TEST(CancelSemanticsTest, CancelAfterCancelOrdersForUserThrows) {
    OrderCache cache;
    cache.addOrder(Order("Ord1", "SecA", "Buy", 100, "User1", "Comp1"));
    cache.addOrder(Order("Ord2", "SecB", "Sell", 200, "User1", "Comp1"));

    cache.cancelOrdersForUser("User1");

    EXPECT_THROW(cache.cancelOrder("Ord1"), std::out_of_range) << "Ord1 was already cancelled via its user";
    EXPECT_THROW(cache.cancelOrder("Ord2"), std::out_of_range) << "Ord2 was already cancelled via its user";
    EXPECT_TRUE(cache.getAllOrders().empty());
}

TEST(CancelSemanticsTest, CancelAfterCancelOrdersForSecIdWithMinimumQtyThrows) {
    OrderCache cache;
    cache.addOrder(Order("Big", "SecA", "Buy", 1000, "User1", "Comp1"));
    cache.addOrder(Order("Small", "SecA", "Sell", 100, "User2", "Comp2"));

    cache.cancelOrdersForSecIdWithMinimumQty("SecA", 500);

    EXPECT_THROW(cache.cancelOrder("Big"), std::out_of_range) << "Big was already cancelled by the min-qty cancel";
    EXPECT_NO_THROW(cache.cancelOrder("Small")) << "Small was below minQty, so its first cancel should succeed";
    EXPECT_THROW(cache.cancelOrder("Small"), std::out_of_range) << "Second cancel of Small should fail";
    EXPECT_TRUE(cache.getAllOrders().empty());
}

TEST(CancelSemanticsConcurrentTest, SameOrderCancelledFromManyThreadsSucceedsExactlyOnce) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kCancelNotAtomicIssue);

    // Each order is raced by kThreads threads released together. All threads
    // use the same order, security and user keys, so they contend on the same
    // stripes; this isolates the cancel semantics from the striped-map race.
    constexpr int kThreads = 8;
    constexpr int kOrders = 200;

    OrderCache cache;
    for (int i = 0; i < kOrders; ++i) {
        cache.addOrder(Order("Ord" + std::to_string(i), "SecA", "Buy", 100, "User1", "Comp1"));
    }

    int ordersCancelledMoreThanOnce = 0;
    int ordersNeverCancelled = 0;
    for (int i = 0; i < kOrders; ++i) {
        const std::string orderId = "Ord" + std::to_string(i);
        StartGate gate(kThreads);
        std::atomic<int> succeeded(0);
        std::atomic<int> notFound(0);

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back(Guarded([&]() {
                gate.ArriveAndWait();
                try {
                    cache.cancelOrder(orderId);
                    succeeded++;
                } catch (const std::out_of_range&) {
                    notFound++;
                }
            }));
        }
        JoinAll(threads);

        EXPECT_EQ(succeeded.load() + notFound.load(), kThreads) << orderId;
        if (succeeded.load() > 1) ++ordersCancelledMoreThanOnce;
        if (succeeded.load() == 0) ++ordersNeverCancelled;
    }

    EXPECT_EQ(ordersCancelledMoreThanOnce, 0)
        << ordersCancelledMoreThanOnce << " of " << kOrders
        << " orders were cancelled successfully by more than one thread";
    EXPECT_EQ(ordersNeverCancelled, 0)
        << ordersNeverCancelled << " of " << kOrders << " orders were not cancelled by any thread";
    EXPECT_TRUE(cache.getAllOrders().empty());
    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecA"), 0u);
}

// -----------------------------------------------------------------------------
// 2. Exact counts for original multithreaded Tests 2 and 5
// -----------------------------------------------------------------------------

// Same workload as original Test 2 (ConcurrencyTest.ConcurrentAddAndCancel),
// which only checked that the final count is in [150, 250]. The exact values:
//  - 3 adder threads x 50 orders: 150 adds, all unique IDs, so all succeed.
//  - Canceller i cancels PreOrd_((i*10 + j) % 100) for j = 0..49:
//      i=0 -> 0..49, i=1 -> 10..59, i=2 -> 20..69: 150 attempts on 70 distinct IDs.
//    With "first cancel succeeds, later ones fail": 70 successes, 80 failures.
//  - Final count: 100 + 150 - 70 = 180.
TEST(ExactCountsTest, ConcurrentAddAndCancelWorkload) {
    // Both known issues apply: the race (concurrent writes on different
    // stripes) and the cancel semantics (the 70/80 split).
    ORDERCACHE_SKIP_KNOWN_ISSUE(std::string(kStripedMapRaceIssue) + " Also: " + kCancelNotAtomicIssue);

    OrderCache cache;
    const int numThreads = 6;
    std::vector<std::thread> threads;
    std::atomic<int> addSuccess(0);
    std::atomic<int> cancelSuccess(0);
    std::atomic<int> cancelFailures(0);

    for (int i = 0; i < 100; i++) {
        cache.addOrder(Order("PreOrd_" + std::to_string(i), "SecId1", "Buy", 100,
                             "User" + std::to_string(i % 5), "Company1"));
    }

    for (int i = 0; i < numThreads / 2; i++) {
        threads.emplace_back(Guarded([&, i]() {
            for (int j = 0; j < 50; j++) {
                std::string orderId = "Add_Ord_" + std::to_string(i) + "_" + std::to_string(j);
                cache.addOrder(Order(orderId, "SecId2", "Sell", 200, "User" + std::to_string(i), "Company2"));
                addSuccess++;
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }));
    }
    for (int i = 0; i < numThreads / 2; i++) {
        threads.emplace_back(Guarded([&, i]() {
            for (int j = 0; j < 50; j++) {
                std::string orderId = "PreOrd_" + std::to_string((i * 10 + j) % 100);
                try {
                    cache.cancelOrder(orderId);
                    cancelSuccess++;
                } catch (const std::out_of_range&) {
                    cancelFailures++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }));
    }
    JoinAll(threads);

    EXPECT_EQ(addSuccess.load(), 150);
    EXPECT_EQ(cancelSuccess.load(), 70) << "Each of the 70 distinct pre-populated IDs should be cancelled once";
    EXPECT_EQ(cancelFailures.load(), 80) << "The other 80 attempts target already-cancelled IDs";
    EXPECT_EQ(cache.getAllOrders().size(), 180u) << "100 pre-populated + 150 added - 70 cancelled";
}

// Same workload as original Test 5 (ConcurrencyTest.DataRaceDetection), which
// only checked that it completes without crashing. The exact values:
//  - Each writer adds 1000 orders and, at i = 0, 10, ..., 990, cancels order
//    i/2 = 0, 5, ..., 495 of its own series: 100 cancels, each of an order it
//    has already added and not yet cancelled, so none may fail.
//  - Final count: 2 x (1000 - 100) = 1800.
TEST(ExactCountsTest, DataRaceDetectionWorkload) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    constexpr int iterations = 1000;

    auto writer = [&](const std::string& prefix, const std::string& side, const std::string& user,
                      const std::string& company) {
        return Guarded([&cache, prefix, side, user, company]() {
            for (int i = 0; i < iterations; i++) {
                cache.addOrder(Order(prefix + std::to_string(i), "SecA", side, 100, user, company));
                if (i % 10 == 0) {
                    cache.cancelOrder(prefix + std::to_string(i / 2));  // throws -> test failure
                }
            }
        });
    };
    auto reader = Guarded([&]() {
        for (int i = 0; i < iterations * 2; i++) {
            try {
                cache.getMatchingSizeForSecurity("SecA");
                cache.getAllOrders();
            } catch (...) {
            }
        }
    });

    std::thread writer1(writer("RaceTest1_", "Buy", "User1", "Comp1"));
    std::thread writer2(writer("RaceTest2_", "Sell", "User2", "Comp2"));
    std::thread reader1(reader);
    std::thread reader2(reader);
    writer1.join();
    writer2.join();
    reader1.join();
    reader2.join();

    EXPECT_EQ(cache.getAllOrders().size(), 1800u) << "2 writers x (1000 added - 100 cancelled)";
}

// -----------------------------------------------------------------------------
// 3. Seeded stress test
// -----------------------------------------------------------------------------

// The original stress workload and checks (see OrderCacheStressTest.cpp), with
// the generator seeded from ordercache_test::TestSeed(). The seed is printed and
// attached to every failure message; rerun a failure with
//   ORDERCACHE_TEST_SEED=<seed> ctest --test-dir <build> -R SeededStressTest
// The run is single-threaded, so the same seed reproduces it exactly.
class SeededStressTest : public ::testing::TestWithParam<unsigned int> {};

TEST_P(SeededStressTest, RandomOrdersThenCancellations) {
    const unsigned int numOrders = GetParam();
    const std::uint32_t seed = ordercache_test::TestSeed();
    std::cout << "[   SEED   ] ORDERCACHE_TEST_SEED=" << seed << std::endl;
    RecordProperty("seed", std::to_string(seed));
    SCOPED_TRACE("ORDERCACHE_TEST_SEED=" + std::to_string(seed));

    OrderCache cache;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> secDist(1, 10);
    std::uniform_int_distribution<> qtyDist(100, 2000);
    std::uniform_int_distribution<> userDist(100, 200);
    std::uniform_int_distribution<> companyDist(100, 500);
    std::uniform_int_distribution<> sideDist(0, 1);

    for (unsigned int i = 1; i <= numOrders; ++i) {
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

    const int numToCancel = numOrders / 10;
    int realNumToCancel = numToCancel;
    for (int i = 0; i < numToCancel; ++i) {
        std::uniform_int_distribution<> ordDist(1, numOrders - i);
        try {
            cache.cancelOrder("StressOrd" + std::to_string(ordDist(gen)));
        } catch (const std::out_of_range&) {
            realNumToCancel--;  // repeated random ID: already cancelled
        }
    }
    orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), static_cast<size_t>(numOrders - realNumToCancel))
        << "Orders after single cancellations match";

    cache.cancelOrdersForUser("User" + std::to_string(userDist(gen)));
    cache.cancelOrdersForSecIdWithMinimumQty("SecId" + std::to_string(secDist(gen)), 500);

    orders = cache.getAllOrders();
    EXPECT_LT(orders.size(), static_cast<size_t>(numOrders)) << "Orders decreased after cancellations";
}

// 100,000 is left out: it is blocked by the index-set copy issue (see
// OrderCacheStressTest.cpp), and the two sizes below already exercise the seed.
INSTANTIATE_TEST_SUITE_P(OrderCounts, SeededStressTest,
                         ::testing::Values(1000u, 10000u),
                         [](const ::testing::TestParamInfo<unsigned int>& info) {
                             return "Orders" + std::to_string(info.param);
                         });
