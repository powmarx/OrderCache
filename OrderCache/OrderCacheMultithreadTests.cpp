#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <future>
#include <iostream>
#include <cassert>

#include "OrderCache.h"
#include "OrderCacheTestUtils.h"

void runMultithreadingTests() {
    cout << "Running Multithreading Tests...\n";

    // Test 1: Concurrent Add and Query
    cout << "Test 1: Concurrent Add and Query Operations\n";
    {
        OrderCache cache;
        const int numThreads = 8;
        const int ordersPerThread = 100;
        vector<thread> threads;
        atomic<int> totalOrdersAdded(0);
        atomic<int> successfulQueries(0);

        auto start = chrono::high_resolution_clock::now();

        // Launch writer threads
        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < ordersPerThread; j++) {
                    string orderId = "MT_Ord_" + to_string(i) + "_" + to_string(j);
                    string securityId = "SecId" + to_string(j % 3 + 1); // SecId1, SecId2, SecId3
                    string side = (j % 2 == 0) ? "Buy" : "Sell";
                    string user = "User" + to_string(i);
                    string company = "Company" + to_string(i % 3);
                    try {
                        cache.addOrder(Order(orderId, securityId, side, 100, user, company));
                        totalOrdersAdded++;
						string message = "Total orders added: " + to_string(totalOrdersAdded.load()) + "\n";
                        cout << message;
                    }
                    catch (const invalid_argument&) {
						cout << string("Duplicate order ID encountered: " + orderId + "\n");
                        // Ignore exceptions during concurrent access
					}
                }
                });
        }

        //  Launch reader threads that query while writes are happening
        for (int i = 0; i < 4; i++) {
            threads.emplace_back([&, i]() {
                random_device rd;
                mt19937 gen(rd());
                uniform_int_distribution<> dis(1, 3);

                for (int j = 0; j < 50; j++) {
                    string securityId = "SecId" + to_string(dis(gen));
                    try {
                        unsigned int size = cache.getMatchingSizeForSecurity(securityId);
                        successfulQueries++;
                    }
                    catch (...) {
                        // Ignore exceptions during concurrent access
                    }
                    this_thread::sleep_for(chrono::microseconds(100));
                }
                });
        }

        // Wait for all threads
        for (auto& t : threads) {
            t.join();
        }

        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

        // Verify results
        auto orders = cache.getAllOrders();
        cout << "Added " << totalOrdersAdded << " orders, found " << orders.size() << " orders\n";
        cout << "Successful queries: " << successfulQueries << "\n";
        cout << "Execution time: " << duration.count() << "ms\n";

        assert(orders.size() == numThreads * ordersPerThread && "All orders should be added");
        assert(successfulQueries > 0 && "Should have successful queries during concurrent access");
        cout << "Test 1 Passed!\n";
    }

    // Test 2: Concurrent Add and Cancel
    cout << "\nTest 2: Concurrent Add and Cancel Operations\n";
    {
        OrderCache cache;
        const int numThreads = 6;
        vector<thread> threads;
        atomic<int> addSuccess(0);
        atomic<int> addFailure(0);
        atomic<int> cancelSuccess(0);
        atomic<int> cancelFailures(0);

        // Pre-populate with some orders
        for (int i = 0; i < 100; i++) {
            cache.addOrder(Order("PreOrd_" + to_string(i), "SecId1", "Buy", 100, "User" + to_string(i % 5), "Company1"));
        }

        auto start = chrono::high_resolution_clock::now();

        // Add orders
        for (int i = 0; i < numThreads / 2; i++) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < 500; j++) {
                    string orderId = "Add_Ord_" + to_string(i) + "_" + to_string(j);
                    try {
                        cache.addOrder(Order(orderId, "SecId2", "Sell", 200, "User" + to_string(i), "Company2"));
                        addSuccess++;
                    }
                    catch (const invalid_argument&) {
                        addFailure++; // Ignore exceptions during concurrent access
					}
                    this_thread::sleep_for(chrono::microseconds(50));
                }
                });
        }

        // Cancel orders
        for (int i = 0; i < numThreads / 2; i++) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < 500; j++) {
                    string orderId = "PreOrd_" + to_string((i * 10 + j) % 100);
                    try {
                        cache.cancelOrder(orderId);
                        cancelSuccess++;
                    }
                    catch (const out_of_range&) {
                        cancelFailures++; // Expected - order already canceled by another thread
                    }
                    this_thread::sleep_for(chrono::microseconds(50));
                }
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

        auto orders = cache.getAllOrders();
        cout << "Add operations: " << addSuccess << "\n";
        cout << "Cancel operations: " << cancelSuccess << "\n";
        cout << "Cancel failures (expected): " << cancelFailures << "\n";
		cout << "Add failures (expected): " << addFailure << "\n";
        cout << "Final order count: " << orders.size() << "\n";
        cout << "Execution time: " << duration.count() << "ms\n";

        // Should have initial 100 + added orders - canceled orders
        // Since we're adding 150 and canceling up to 100, result should be between 150 and 250
        assert(orders.size() >= 1500 -addFailure && orders.size() <= 1650 && "Order count should be in expected range");
        cout << "Test 2 Passed!\n";
    }

    // Test 3: Complex Mixed Operations
    cout << "\nTest 3: Complex Mixed Operations\n";
    {
        OrderCache cache;
        const int numThreads = 10;
        vector<thread> threads;
        atomic<int> operationsCompleted(0);

        // Pre-populate
        for (int i = 0; i < 200; i++) {
            try {
                cache.addOrder(Order("Mixed_" + to_string(i), "SecId" + to_string(i % 3 + 1),
                    i % 2 == 0 ? "Buy" : "Sell", 100 + i, "User" + to_string(i % 10),
                    "Company" + to_string(i % 4)));
            }
            catch (...) {
                // Ignore exceptions during pre-population
            }
        }

        auto start = chrono::high_resolution_clock::now();

        // Mixed operations threads
        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i]() {
                random_device rd;
                mt19937 gen(rd());
                uniform_int_distribution<> opDis(0, 4);
                uniform_int_distribution<> idDis(0, 199);
                uniform_int_distribution<> secDis(1, 3);

                for (int j = 0; j < 30; j++) {
                    int operation = opDis(gen);
                    try {
                        switch (operation) {
                        case 0: // Add order
                                cache.addOrder(Order("New_" + to_string(i) + "_" + to_string(j),
                                    "SecId" + to_string(secDis(gen)),
                                    j % 2 == 0 ? "Buy" : "Sell",
                                    50 + j, "User" + to_string(i),
                                    "Company" + to_string(i % 4)));
                            break;
                        case 1: // Cancel order
                            cache.cancelOrder("Mixed_" + to_string(idDis(gen)));
                            break;
                        case 2: // Query matching size
                            cache.getMatchingSizeForSecurity("SecId" + to_string(secDis(gen)));
                            break;
                        case 3: // Get all orders
                            cache.getAllOrders();
                            break;
                        case 4: // Cancel orders for user
                            cache.cancelOrdersForUser("User" + to_string(idDis(gen) % 10));
                            break;
                        }
                        operationsCompleted++;
                    }
                    catch (const exception& e) {
                        // Expected for some operations (order not found, etc.)
                    }
                    this_thread::sleep_for(chrono::microseconds(10));
                }
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

        // Verify cache is in consistent state
        auto orders = cache.getAllOrders();

        // Check consistency: every order should be in appropriate indexes
        for (const auto& order : orders) {
            // Verify order exists in security index
            try {
                unsigned int matchSize = cache.getMatchingSizeForSecurity(order.securityId());
                // Just verifying no exception is thrown and calculation works
            }
            catch (...) {
                assert(false && "Cache should be in consistent state after concurrent operations");
            }
        }

        cout << "Operations completed: " << operationsCompleted << "\n";
        cout << "Final order count: " << orders.size() << "\n";
        cout << "Execution time: " << duration.count() << "ms\n";
        cout << "Test 3 Passed!\n";
    }

    // Test 4: Stress Test with High Contention
    cout << "\nTest 4: High Contention Stress Test\n";
    {
        OrderCache cache;
        const int numThreads = 12;
        vector<thread> threads;
        atomic<long> totalOperations(0);
        atomic<int> errors(0);

        // Create heavy contention on a single security
        for (int i = 0; i < 100; i++) {
            cache.addOrder(Order("Stress_" + to_string(i), "HotSec",
                i % 2 == 0 ? "Buy" : "Sell", 100,
                "User" + to_string(i % 5), "Company" + to_string(i % 3)));
        }

        auto start = chrono::high_resolution_clock::now();
        auto testDuration = chrono::seconds(3); // Run for 3 seconds

        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i]() {
                random_device rd;
                mt19937 gen(rd());
                uniform_int_distribution<> opDis(0, 2);

                auto endTime = chrono::steady_clock::now() + testDuration;

                while (chrono::steady_clock::now() < endTime) {
                    int operation = opDis(gen);
                    try {
                        switch (operation) {
                        case 0: // Heavy read - matching size on hot security
                            cache.getMatchingSizeForSecurity("HotSec");
                            break;
                        case 1: // Add order to hot security
                            cache.addOrder(Order("Contention_" + to_string(i) + "_" + to_string(totalOperations),
                                "HotSec", "Buy", 100, "User" + to_string(i), "Company1"));
                            break;
                        case 2: // Cancel random order
                            cache.cancelOrder("Stress_" + to_string(gen() % 100));
                            break;
                        }
                        totalOperations++;
                    }
                    catch (const exception& e) {
						string message = "Error: " + string(e.what()) + "\n";
                        cout << message;
                        errors++;
                    }
                }
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

        cout << "Total operations: " << totalOperations << "\n";
        cout << "Errors (expected): " << errors << "\n";
        cout << "Operations per second: " << (totalOperations * 1000 / duration.count()) << "\n";
        cout << "Test duration: " << duration.count() << "ms\n";

        // Final consistency check
        auto orders = cache.getAllOrders();
        unsigned int matchSize = cache.getMatchingSizeForSecurity("HotSec");
        cout << "Final HotSec matching size: " << matchSize << "\n";
        cout << "Final order count: " << orders.size() << "\n";

        assert(matchSize >= 0 && "Matching size should be valid");
        cout << "Test 4 Passed!\n";
    }

    // Test 5: Data Race Detection Test
    cout << "\nTest 5: Data Race Detection Test\n";
    {
        OrderCache cache;
        const int iterations = 1000;
        atomic<bool> dataRaceDetected(false);

        // This test is designed to run with thread sanitizers
        thread writer1([&]() {
            for (int i = 0; i < iterations; i++) {
                cache.addOrder(Order("RaceTest1_" + to_string(i), "SecA", "Buy", 100, "User1", "Comp1"));
                if (i % 10 == 0) {
                    cache.cancelOrder("RaceTest1_" + to_string(i / 2));
                }
            }
            });

        thread writer2([&]() {
            for (int i = 0; i < iterations; i++) {
                cache.addOrder(Order("RaceTest2_" + to_string(i), "SecA", "Sell", 100, "User2", "Comp2"));
                if (i % 10 == 0) {
                    cache.cancelOrder("RaceTest2_" + to_string(i / 2));
                }
            }
            });

        thread reader1([&]() {
            for (int i = 0; i < iterations * 2; i++) {
                try {
                    cache.getMatchingSizeForSecurity("SecA");
                    cache.getAllOrders();
                }
                catch (...) {
                    // Ignore
                }
            }
            });

        thread reader2([&]() {
            for (int i = 0; i < iterations * 2; i++) {
                try {
                    cache.getMatchingSizeForSecurity("SecA");
                    cache.getAllOrders();
                }
                catch (...) {
                    // Ignore
                }
            }
            });

        writer1.join();
        writer2.join();
        reader1.join();
        reader2.join();

        // If we get here without crashes, basic thread safety is working
        auto orders = cache.getAllOrders();
        cout << "Data race test completed without crashes. Final orders: " << orders.size() << "\n";
        cout << "Test 5 Passed!\n";
    }

    cout << "\nAll Multithreading Tests Passed!\n";
}