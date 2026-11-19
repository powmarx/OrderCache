// GoogleTest port of runMultithreadingTest() in OrderCache/OrderCacheMultithreadTest.cpp
// (Tests 1-5). That file is left unchanged and still runs through
// ordercache_legacy_runner (menu option 6).
//
// Porting rules, same as the single-threaded port:
//  - Same workload (thread counts, order counts, IDs, sleeps, the 3 s duration)
//    and the same expected values, with the original assert messages.
//  - Console output is not ported, including the per-add cout inside writer
//    threads (a global lock that also serialises the threads).
//  - An exception escaping a worker thread used to call std::terminate. It is
//    now reported as a test failure (see Guarded below).
//
// All five tests are currently skipped by default: they all write to the cache
// from several threads at once, which hits the StripedThreadSafeMap data race
// (see KnownIssues.h).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "KnownIssues.h"
#include "OrderCache.h"

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

}  // namespace

// Test 1: Concurrent Add and Query
TEST(ConcurrencyTest, ConcurrentAddAndQuery) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    const int numThreads = 8;
    const int ordersPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> totalOrdersAdded(0);
    std::atomic<int> successfulQueries(0);

    // Writer threads
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(Guarded([&, i]() {
            for (int j = 0; j < ordersPerThread; j++) {
                std::string orderId = "MT_Ord_" + std::to_string(i) + "_" + std::to_string(j);
                std::string securityId = "SecId" + std::to_string(j % 3 + 1);
                std::string side = (j % 2 == 0) ? "Buy" : "Sell";
                std::string user = "User" + std::to_string(i);
                std::string company = "Company" + std::to_string(i % 3);

                cache.addOrder(Order(orderId, securityId, side, 100, user, company));
                totalOrdersAdded++;
            }
        }));
    }

    // Reader threads that query while writes are happening
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(Guarded([&]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 3);

            for (int j = 0; j < 50; j++) {
                std::string securityId = "SecId" + std::to_string(dis(gen));
                try {
                    cache.getMatchingSizeForSecurity(securityId);
                    successfulQueries++;
                } catch (...) {
                    // Ignored during concurrent access, as in the original.
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }));
    }

    JoinAll(threads);

    const auto orders = cache.getAllOrders();
    EXPECT_EQ(orders.size(), static_cast<size_t>(numThreads * ordersPerThread)) << "All orders should be added";
    EXPECT_GT(successfulQueries.load(), 0) << "Should have successful queries during concurrent access";
}

// Test 2: Concurrent Add and Cancel
TEST(ConcurrencyTest, ConcurrentAddAndCancel) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    const int numThreads = 6;
    std::vector<std::thread> threads;
    std::atomic<int> addSuccess(0);
    std::atomic<int> cancelSuccess(0);
    std::atomic<int> cancelFailures(0);

    // Pre-populate with some orders
    for (int i = 0; i < 100; i++) {
        cache.addOrder(Order("PreOrd_" + std::to_string(i), "SecId1", "Buy", 100,
                             "User" + std::to_string(i % 5), "Company1"));
    }

    // Add orders
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

    // Cancel orders
    for (int i = 0; i < numThreads / 2; i++) {
        threads.emplace_back(Guarded([&, i]() {
            for (int j = 0; j < 50; j++) {
                std::string orderId = "PreOrd_" + std::to_string((i * 10 + j) % 100);
                try {
                    cache.cancelOrder(orderId);
                    cancelSuccess++;
                } catch (const std::out_of_range&) {
                    cancelFailures++;  // Expected: already canceled by another thread
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }));
    }

    JoinAll(threads);

    // Initial 100 + 150 added - up to 100 canceled: between 150 and 250.
    const auto orders = cache.getAllOrders();
    EXPECT_GE(orders.size(), 150u) << "Order count should be in expected range";
    EXPECT_LE(orders.size(), 250u) << "Order count should be in expected range";
}

// Test 3: Complex Mixed Operations
TEST(ConcurrencyTest, ComplexMixedOperations) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> operationsCompleted(0);

    // Pre-populate
    for (int i = 0; i < 200; i++) {
        cache.addOrder(Order("Mixed_" + std::to_string(i), "SecId" + std::to_string(i % 3 + 1),
                             i % 2 == 0 ? "Buy" : "Sell", 100 + i, "User" + std::to_string(i % 10),
                             "Company" + std::to_string(i % 4)));
    }

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(Guarded([&, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> opDis(0, 4);
            std::uniform_int_distribution<> idDis(0, 199);
            std::uniform_int_distribution<> secDis(1, 3);

            for (int j = 0; j < 30; j++) {
                int operation = opDis(gen);
                try {
                    switch (operation) {
                    case 0:  // Add order
                        cache.addOrder(Order("New_" + std::to_string(i) + "_" + std::to_string(j),
                                             "SecId" + std::to_string(secDis(gen)),
                                             j % 2 == 0 ? "Buy" : "Sell",
                                             50 + j, "User" + std::to_string(i),
                                             "Company" + std::to_string(i % 4)));
                        break;
                    case 1:  // Cancel order
                        cache.cancelOrder("Mixed_" + std::to_string(idDis(gen)));
                        break;
                    case 2:  // Query matching size
                        cache.getMatchingSizeForSecurity("SecId" + std::to_string(secDis(gen)));
                        break;
                    case 3:  // Get all orders
                        cache.getAllOrders();
                        break;
                    case 4:  // Cancel orders for user
                        cache.cancelOrdersForUser("User" + std::to_string(idDis(gen) % 10));
                        break;
                    }
                    operationsCompleted++;
                } catch (const std::exception&) {
                    // Expected for some operations (order not found, etc.)
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }));
    }

    JoinAll(threads);

    // Consistency check from the original: querying every remaining order's
    // security must not throw.
    const auto orders = cache.getAllOrders();
    for (const auto& order : orders) {
        EXPECT_NO_THROW(cache.getMatchingSizeForSecurity(order.securityId()))
            << "Cache should be in consistent state after concurrent operations";
    }
}

// Test 4: High Contention Stress Test (runs for 3 seconds)
TEST(ConcurrencyTest, HighContentionStress) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    const int numThreads = 12;
    std::vector<std::thread> threads;
    std::atomic<long> totalOperations(0);
    std::atomic<int> errors(0);

    // Heavy contention on a single security
    for (int i = 0; i < 100; i++) {
        cache.addOrder(Order("Stress_" + std::to_string(i), "HotSec",
                             i % 2 == 0 ? "Buy" : "Sell", 100,
                             "User" + std::to_string(i % 5), "Company" + std::to_string(i % 3)));
    }

    const auto testDuration = std::chrono::seconds(3);

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(Guarded([&, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> opDis(0, 2);

            const auto endTime = std::chrono::steady_clock::now() + testDuration;

            while (std::chrono::steady_clock::now() < endTime) {
                int operation = opDis(gen);
                try {
                    switch (operation) {
                    case 0:  // Heavy read: matching size on the hot security
                        cache.getMatchingSizeForSecurity("HotSec");
                        break;
                    case 1:  // Add order to the hot security
                        cache.addOrder(Order("Contention_" + std::to_string(i) + "_" +
                                                 std::to_string(totalOperations.load()),
                                             "HotSec", "Buy", 100, "User" + std::to_string(i), "Company1"));
                        break;
                    case 2:  // Cancel a random pre-populated order
                        cache.cancelOrder("Stress_" + std::to_string(gen() % 100));
                        break;
                    }
                    totalOperations++;
                } catch (const std::exception&) {
                    errors++;  // Expected (already canceled, etc.); the original printed these
                }
            }
        }));
    }

    JoinAll(threads);

    // Final consistency check from the original. The assertion is always true
    // for an unsigned value; it is kept for parity.
    const auto orders = cache.getAllOrders();
    const unsigned int matchSize = cache.getMatchingSizeForSecurity("HotSec");
    EXPECT_GE(matchSize, 0u) << "Matching size should be valid";
}

// Test 5: Data Race Detection Test (designed to run under a thread sanitizer).
// As in the original, it passes if it completes without crashing.
TEST(ConcurrencyTest, DataRaceDetection) {
    ORDERCACHE_SKIP_KNOWN_ISSUE(kStripedMapRaceIssue);

    OrderCache cache;
    const int iterations = 1000;

    std::thread writer1(Guarded([&]() {
        for (int i = 0; i < iterations; i++) {
            cache.addOrder(Order("RaceTest1_" + std::to_string(i), "SecA", "Buy", 100, "User1", "Comp1"));
            if (i % 10 == 0) {
                cache.cancelOrder("RaceTest1_" + std::to_string(i / 2));
            }
        }
    }));

    std::thread writer2(Guarded([&]() {
        for (int i = 0; i < iterations; i++) {
            cache.addOrder(Order("RaceTest2_" + std::to_string(i), "SecA", "Sell", 100, "User2", "Comp2"));
            if (i % 10 == 0) {
                cache.cancelOrder("RaceTest2_" + std::to_string(i / 2));
            }
        }
    }));

    auto reader = Guarded([&]() {
        for (int i = 0; i < iterations * 2; i++) {
            try {
                cache.getMatchingSizeForSecurity("SecA");
                cache.getAllOrders();
            } catch (...) {
                // Ignored, as in the original
            }
        }
    });
    std::thread reader1(reader);
    std::thread reader2(reader);

    writer1.join();
    writer2.join();
    reader1.join();
    reader2.join();

    cache.getAllOrders();
}
