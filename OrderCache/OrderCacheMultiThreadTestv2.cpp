#include "OrderCache.h"
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <random>
#include <sstream>
#include <cassert>
#include <chrono>
#include <unordered_set>
#include <mutex>

using namespace std;

class OrderCacheMultiThreadTest {
private:
    OrderCache cache;
    atomic<bool> stopFlag{ false };
    atomic<int> operationsCompleted{ 0 };
    mutex coutMutex; // To synchronize console output

    vector<string> securityIds = { "SEC1", "SEC2", "SEC3", "SEC4", "SEC5" };
    vector<string> users = { "USER1", "USER2", "USER3", "USER4", "USER5", "USER6", "USER7", "USER8" };
    vector<string> companies = { "COMP1", "COMP2", "COMP3", "COMP4" };

    random_device rd;
    mt19937 gen{ rd() };

    uniform_int_distribution<> secDist{ 0, static_cast<int>(securityIds.size() - 1) };
    uniform_int_distribution<> userDist{ 0, static_cast<int>(users.size() - 1) };
    uniform_int_distribution<> companyDist{ 0, static_cast<int>(companies.size() - 1) };
    uniform_int_distribution<> qtyDist{ 100, 2000 };
    uniform_int_distribution<> sideDist{ 0, 1 };
    uniform_int_distribution<> opDist{ 0, 5 }; // For operation type selection

public:
    void runConcurrentAddTest(int numThreads, int durationSeconds) {
        cout << "Running Concurrent Add Test with " << numThreads << " threads for " << durationSeconds << " seconds\n";

        vector<thread> threads;
        atomic<int> totalOrdersAdded{ 0 };
        atomic<int> duplicateErrors{ 0 };

        auto startTime = chrono::steady_clock::now();

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([this, &totalOrdersAdded, &duplicateErrors, startTime, durationSeconds, threadId = i]() {
                int localCounter = 0;
                while (chrono::duration_cast<chrono::seconds>(
                    chrono::steady_clock::now() - startTime).count() < durationSeconds) {

                    string orderId = generateOrderId(threadId, localCounter++);
                    Order order = generateRandomOrder(orderId);

                    try {
                        cache.addOrder(order);
                        totalOrdersAdded++;
                    }
                    catch (const invalid_argument& e) {
                        duplicateErrors++;
                    }

                    // Small random delay to increase contention
                    this_thread::sleep_for(chrono::microseconds(10));
                }
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto finalOrders = cache.getAllOrders();

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Concurrent Add Test Results:\n";
            cout << "  Total orders added: " << totalOrdersAdded << "\n";
            cout << "  Duplicate errors: " << duplicateErrors << "\n";
            cout << "  Final cache size: " << finalOrders.size() << "\n";
            cout << "  Expected unique orders: " << (totalOrdersAdded - duplicateErrors) << "\n";
        }

        // Verify no duplicates in final cache
        unordered_set<string> orderIds;
        for (const auto& order : finalOrders) {
            bool inserted = orderIds.insert(order.orderId()).second;
            assert(inserted && "Duplicate order ID found in cache!");
        }

        assert(orderIds.size() == finalOrders.size() && "Cache size mismatch with unique order IDs");
    }

    void runMixedOperationsTest(int numThreads, int durationSeconds) {
        cout << "Running Mixed Operations Test with " << numThreads << " threads for " << durationSeconds << " seconds\n";

        // Pre-populate cache with some orders
        prePopulateCache(1000);

        vector<thread> threads;
        atomic<int> adds{ 0 }, cancels{ 0 }, userCancels{ 0 }, secCancels{ 0 }, matches{ 0 }, getAlls{ 0 };

        auto startTime = chrono::steady_clock::now();

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([this, &adds, &cancels, &userCancels, &secCancels, &matches, &getAlls,
                startTime, durationSeconds, threadId = i]() {
                    int operationCount = 0;
                    while (chrono::duration_cast<chrono::seconds>(
                        chrono::steady_clock::now() - startTime).count() < durationSeconds) {

                        int opType = opDist(gen);
                        try {
                            switch (opType) {
                            case 0: // Add order
                            {
                                string orderId = "MixedOp_" + to_string(threadId) + "_" + to_string(operationCount);
                                cache.addOrder(generateRandomOrder(orderId));
                                adds++;
                                break;
                            }
                            case 1: // Cancel order (if exists)
                            {
                                string orderId = generateExistingOrderId();
                                if (!orderId.empty()) {
                                    cache.cancelOrder(orderId);
                                    cancels++;
                                }
                                break;
                            }
                            case 2: // Cancel orders for user
                            {
                                string user = users[userDist(gen)];
                                try {
                                    cache.cancelOrdersForUser(user);
                                    userCancels++;
                                }
                                catch (const out_of_range&) {
                                    // User might not exist, that's OK
                                }
                                break;
                            }
                            case 3: // Cancel orders for security with min qty
                            {
                                string secId = securityIds[secDist(gen)];
                                try {
                                    cache.cancelOrdersForSecIdWithMinimumQty(secId, 500);
                                    secCancels++;
                                }
                                catch (const out_of_range&) {
                                    // Security might not exist, that's OK
                                }
                                break;
                            }
                            case 4: // Get matching size
                            {
                                string secId = securityIds[secDist(gen)];
                                cache.getMatchingSizeForSecurity(secId);
                                matches++;
                                break;
                            }
                            case 5: // Get all orders
                            {
                                cache.getAllOrders();
                                getAlls++;
                                break;
                            }
                            }
                            operationCount++;
                        }
                        catch (const exception& e) {
                            // Log unexpected exceptions
                            lock_guard<mutex> lock(coutMutex);
                            cerr << "Thread " << threadId << " unexpected exception: " << e.what() << "\n";
                        }

                        this_thread::sleep_for(chrono::microseconds(5));
                    }

                    operationsCompleted += operationCount;
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Mixed Operations Test Results:\n";
            cout << "  Total operations: " << operationsCompleted.load() << "\n";
            cout << "  Add operations: " << adds.load() << "\n";
            cout << "  Cancel operations: " << cancels.load() << "\n";
            cout << "  User cancel operations: " << userCancels.load() << "\n";
            cout << "  Security cancel operations: " << secCancels.load() << "\n";
            cout << "  Match operations: " << matches.load() << "\n";
            cout << "  Get all operations: " << getAlls.load() << "\n";
            cout << "  Final cache size: " << cache.getAllOrders().size() << "\n";
        }

        // Verify cache consistency
        verifyCacheConsistency();
    }

    void runProducerConsumerTest(int producers, int consumers, int durationSeconds) {
        cout << "Running Producer-Consumer Test with " << producers << " producers and "
            << consumers << " consumers for " << durationSeconds << " seconds\n";

        vector<thread> producerThreads;
        vector<thread> consumerThreads;
        atomic<int> produced{ 0 }, consumed{ 0 };

        auto startTime = chrono::steady_clock::now();

        // Producer threads - only add orders
        for (int i = 0; i < producers; ++i) {
            producerThreads.emplace_back([this, &produced, startTime, durationSeconds, producerId = i]() {
                int localProduced = 0;
                while (chrono::duration_cast<chrono::seconds>(
                    chrono::steady_clock::now() - startTime).count() < durationSeconds) {

                    string orderId = "Prod_" + to_string(producerId) + "_" + to_string(localProduced);
                    cache.addOrder(generateRandomOrder(orderId));
                    localProduced++;
                    produced++;

                    this_thread::sleep_for(chrono::microseconds(20));
                }
                });
        }

        // Consumer threads - mixed operations (mostly reads and cancellations)
        for (int i = 0; i < consumers; ++i) {
            consumerThreads.emplace_back([this, &consumed, startTime, durationSeconds, consumerId = i]() {
                int localConsumed = 0;
                while (chrono::duration_cast<chrono::seconds>(
                    chrono::steady_clock::now() - startTime).count() < durationSeconds) {

                    // Mostly read operations with occasional cancellations
                    int opType = uniform_int_distribution<>{ 0, 10 }(gen);
                    if (opType < 7) { // 70% read operations
                        if (opType % 2 == 0) {
                            cache.getMatchingSizeForSecurity(securityIds[secDist(gen)]);
                        }
                        else {
                            cache.getAllOrders();
                        }
                    }
                    else { // 30% cancellation operations
                        string orderId = generateExistingOrderId();
                        if (!orderId.empty()) {
                            try {
                                cache.cancelOrder(orderId);
                                consumed++;
                            }
                            catch (const exception&) {
                                // Order might have been already cancelled
                            }
                        }
                    }
                    localConsumed++;

                    this_thread::sleep_for(chrono::microseconds(15));
                }
                });
        }

        for (auto& t : producerThreads) {
            t.join();
        }
        for (auto& t : consumerThreads) {
            t.join();
        }

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Producer-Consumer Test Results:\n";
            cout << "  Orders produced: " << produced.load() << "\n";
            cout << "  Orders consumed: " << consumed.load() << "\n";
            cout << "  Final cache size: " << cache.getAllOrders().size() << "\n";
            cout << "  Net orders: " << (produced - consumed) << "\n";
        }

        verifyCacheConsistency();
    }

    void runStressConsistencyTest(int numThreads, int initialOrders, int durationSeconds) {
        cout << "Running Stress Consistency Test with " << numThreads << " threads, "
            << initialOrders << " initial orders for " << durationSeconds << " seconds\n";

        // Pre-populate with known orders
        prePopulateCache(initialOrders);

        vector<thread> threads;
        atomic<bool> consistencyCheckPassed{ true };

        auto startTime = chrono::steady_clock::now();

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([this, &consistencyCheckPassed, startTime, durationSeconds, threadId = i]() {
                int iterations = 0;
                while (chrono::duration_cast<chrono::seconds>(
                    chrono::steady_clock::now() - startTime).count() < durationSeconds) {

                    // Perform random operations
                    int opType = opDist(gen);
                    try {
                        switch (opType) {
                        case 0:
                            cache.addOrder(generateRandomOrder("Stress_" + to_string(threadId) + "_" + to_string(iterations)));
                            break;
                        case 1:
                            cache.cancelOrder(generateExistingOrderId());
                            break;
                        case 2:
                            cache.cancelOrdersForUser(users[userDist(gen)]);
                            break;
                        case 3:
                            cache.cancelOrdersForSecIdWithMinimumQty(securityIds[secDist(gen)], 750);
                            break;
                        case 4:
                            cache.getMatchingSizeForSecurity(securityIds[secDist(gen)]);
                            break;
                        case 5:
                            cache.getAllOrders();
                            break;
                        }
                    }
                    catch (const exception& e) {
                        // Expected exceptions for non-existent orders/users
                    }

                    // Periodically verify consistency
                    if (iterations % 100 == 0) {
                        if (!verifyCacheConsistencySilent()) {
                            consistencyCheckPassed = false;
                            return;
                        }
                    }

                    iterations++;
                    this_thread::sleep_for(chrono::microseconds(2));
                }
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        bool finalConsistency = verifyCacheConsistency();

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Stress Consistency Test Results:\n";
            cout << "  Thread-level consistency: " << (consistencyCheckPassed ? "PASSED" : "FAILED") << "\n";
            cout << "  Final consistency: " << (finalConsistency ? "PASSED" : "FAILED") << "\n";
            cout << "  Final cache size: " << cache.getAllOrders().size() << "\n";
        }

        assert(consistencyCheckPassed && "Thread-level consistency check failed!");
        assert(finalConsistency && "Final consistency check failed!");
    }

private:
    string generateOrderId(int threadId, int counter) {
        return "Order_" + to_string(threadId) + "_" + to_string(counter);
    }

    Order generateRandomOrder(const string& orderId) {
        string securityId = securityIds[secDist(gen)];
        string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
        unsigned int qty = qtyDist(gen);
        string user = users[userDist(gen)];
        string company = companies[companyDist(gen)];

        return Order(orderId, securityId, side, qty, user, company);
    }

    void prePopulateCache(int numOrders) {
        for (int i = 0; i < numOrders; ++i) {
            string orderId = "PrePop_" + to_string(i);
            cache.addOrder(generateRandomOrder(orderId));
        }
    }

    string generateExistingOrderId() {
        // Try to find an existing order ID
        auto orders = cache.getAllOrders();
        if (orders.empty()) {
            return "";
        }

        uniform_int_distribution<> dist(0, static_cast<int>(orders.size() - 1));
        return orders[dist(gen)].orderId();
    }

    bool verifyCacheConsistency() {
        lock_guard<mutex> lock(coutMutex);
        cout << "Verifying cache consistency...\n";

        bool consistent = verifyCacheConsistencySilent();

        if (consistent) {
            cout << "Cache consistency: PASSED\n";
        }
        else {
            cout << "Cache consistency: FAILED\n";
        }

        return consistent;
    }

    bool verifyCacheConsistencySilent() {
        try {
            auto allOrders = cache.getAllOrders();

            // Verify no duplicate order IDs
            unordered_set<string> orderIds;
            for (const auto& order : allOrders) {
                if (!orderIds.insert(order.orderId()).second) {
                    cerr << "Duplicate order ID found: " << order.orderId() << "\n";
                    return false;
                }
            }

            // For each order, verify it exists in the appropriate indexes
            for (const auto& order : allOrders) {
                // Verify we can retrieve by order ID
                try {
                    // This would require adding a getOrder method to OrderCacheInterface
                    // For now, we'll rely on getAllOrders being consistent
                }
                catch (const exception& e) {
                    cerr << "Failed to verify order " << order.orderId() << ": " << e.what() << "\n";
                    return false;
                }
            }

            return true;
        }
        catch (const exception& e) {
            cerr << "Exception during consistency check: " << e.what() << "\n";
            return false;
        }
    }
};

void runAllMultiThreadedTestsv2() {
    OrderCacheMultiThreadTest test;

    cout << "=== ORDER CACHE MULTITHREADED TESTS ===\n\n";

    // Test 1: Concurrent additions
    test.runConcurrentAddTest(4, 5);
    cout << "---\n";

    // Test 2: Mixed operations
    test.runMixedOperationsTest(6, 5);
    cout << "---\n";

    // Test 3: Producer-consumer pattern
    test.runProducerConsumerTest(3, 3, 5);
    cout << "---\n";

    // Test 4: Stress with consistency checks
    test.runStressConsistencyTest(8, 500, 5);

    cout << "\n=== ALL MULTITHREADED TESTS COMPLETED ===\n";
}

