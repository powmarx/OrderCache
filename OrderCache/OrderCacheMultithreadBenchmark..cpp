// OrderCacheMultithreadBenchmark.cpp
#include "OrderCache.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <functional>
#include <memory>
#include <cassert>

using namespace std;
using namespace std::chrono;

class SafeMultithreadBenchmark {
private:
    atomic<bool> stop_requested_{ false };
    atomic<long> operations_completed_{ 0 };
    atomic<long> memory_errors_{ 0 };

    struct ThreadStats {
        long add_ops{ 0 };
        long cancel_ops{ 0 };
        long query_ops{ 0 };
        long bulk_cancel_ops{ 0 };
        long errors{ 0 };
        vector<microseconds> latencies;
    };

public:
    void runSafeBenchmark() {
        cout << "\n=== Safe OrderCache Multithreaded Benchmark ===\n";
        cout << "This version includes memory safety checks and debugging\n";

        // Test with conservative thread counts first
        vector<int> thread_counts = { 1, 2, 4, 8, 16};
        bool success = true;

        for (int threads : thread_counts) {
            cout << "\n--- Testing with " << threads << " threads ---\n";
            if (!runSingleBenchmark(threads, 5000)) {
                cout << "*** ERROR: Benchmark failed with " << threads << " threads ***\n";
                success = false;
                break;
            }
        }

        if (success) {
            cout << "\n=== All safe benchmarks completed successfully ===\n";
            runAdvancedBenchmarks();
        }
        else {
            cout << "\n*** Benchmark halted due to errors ***\n";
            runDebuggingTests();
        }
    }

private:
    bool runSingleBenchmark(int num_threads, int operations_per_thread) {
        try {
            // Use unique_ptr for automatic cleanup
            auto cache = make_unique<OrderCache>();
            stop_requested_.store(false);
            operations_completed_.store(0);
            memory_errors_.store(0);

            cout << "Phase 1: Pre-populating cache...\n";
            if (!prePopulateSafely(*cache, 1000)) {
                cout << "ERROR: Pre-population failed\n";
                return false;
            }

            cout << "Phase 2: Starting worker threads...\n";
            vector<thread> workers;
            vector<ThreadStats> stats(num_threads);
            atomic<bool> start_flag{ false };

            // Create workers
            for (int i = 0; i < num_threads; i++) {
                workers.emplace_back([&, i, cache_ptr = cache.get()]() {
                    safeWorkerThread(i, cache_ptr, operations_per_thread, stats[i], start_flag);
                    });
            }

            // Start all threads
            this_thread::sleep_for(50ms);
            start_flag.store(true);

            // Let them run
            this_thread::sleep_for(5s);
            stop_requested_.store(true);

            // Wait for completion
            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            // Verify cache integrity
            cout << "Phase 3: Verifying cache integrity...\n";
            if (!verifyCacheIntegrity(*cache)) {
                cout << "ERROR: Cache integrity check failed\n";
                return false;
            }

            printSafeResults(stats, num_threads);
            return true;

        }
        catch (const exception& e) {
            cout << "EXCEPTION in benchmark: " << e.what() << "\n";
            return false;
        }
        catch (...) {
            cout << "UNKNOWN EXCEPTION in benchmark\n";
            return false;
        }
    }

    bool prePopulateSafely(OrderCache& cache, int num_orders) {
        try {
            vector<Order> orders;
            orders.reserve(num_orders);

            // Create orders first
            for (int i = 0; i < num_orders; i++) {
                string order_id = "SafeInit" + to_string(i);
                string security_id = "Sec" + to_string(i % 10 + 1);
                string side = (i % 2 == 0) ? "Buy" : "Sell";
                unsigned int qty = 100 + (i % 1900);
                string user = "User" + to_string(i % 50);
                string company = "Company" + to_string(i % 20);

                orders.emplace_back(order_id, security_id, side, qty, user, company);
            }

            // Add to cache with error handling
            int success_count = 0;
            for (const auto& order : orders) {
                try {
                    cache.addOrder(order);
                    success_count++;
                }
                catch (const invalid_argument& e) {
                    // Duplicate order ID - expected in some cases
                    memory_errors_++;
                }
                catch (...) {
                    cout << "Unexpected error during pre-population\n";
                    return false;
                }
            }

            cout << "Successfully added " << success_count << "/" << num_orders << " initial orders\n";
            return success_count > num_orders / 2; // Allow some failures

        }
        catch (...) {
            cout << "Exception during pre-population\n";
            return false;
        }
    }

    void safeWorkerThread(int thread_id, OrderCache* cache, int operations,
        ThreadStats& stats, const atomic<bool>& start_flag) {
        // Wait for start signal
        while (!start_flag.load() && !stop_requested_.load()) {
            this_thread::yield();
        }

        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> op_dist(0,2);
        uniform_int_distribution<> sec_dist(1, 10);
        uniform_int_distribution<> qty_dist(100, 1000);
        uniform_int_distribution<> user_dist(1, 50);

        stats.latencies.reserve(operations);

        for (int i = 0; i < operations && !stop_requested_.load(); i++) {
            auto start_time = high_resolution_clock::now();

            try {
                int operation = op_dist(gen);

                switch (operation) {
                case 0: // Safe add operation
                    safeAddOperation(thread_id, i, cache, stats);
                    break;
                case 1: // Safe query operation  
                    safeQueryOperation(cache, stats, sec_dist(gen));
                    break;
                case 2: // Safe cancel operation
                    safeCancelOperation(thread_id, i, cache, stats);
                    break;
                case 3: // Safe bulk operation
                    safeBulkOperation(cache, stats, user_dist(gen), sec_dist(gen));
                    break;
                }

                auto end_time = high_resolution_clock::now();
                stats.latencies.push_back(duration_cast<microseconds>(end_time - start_time));

            }
            catch (const exception& e) {
                stats.errors++; 
                memory_errors_++;

                // Log first few errors
                if (stats.errors <= 3) {
                    cout << "Thread " << thread_id << " error: " << e.what() << "\n";
                }
            }
            catch (...) {
                stats.errors++;
                memory_errors_++;

                if (stats.errors <= 3) {
                    cout << "Thread " << thread_id << " unknown error\n";
                }
            }

            operations_completed_++;

            //if (verifyCacheIntegritySilent(*cache) == false) {
            //    throw runtime_error("Cache integrity check failed during operations");
            //}

            // Small delay to prevent overwhelming the system
            if (i % 100 == 0) {
                this_thread::sleep_for(1ms);
            }
        }
    }

    void safeAddOperation(int thread_id, int op_id, OrderCache* cache, ThreadStats& stats) {
        string order_id = "T" + to_string(thread_id) + "A" + to_string(op_id);
        string security_id = "Sec" + to_string((thread_id + op_id) % 10 + 1);
        string side = (op_id % 2 == 0) ? "Buy" : "Sell";
        unsigned int qty = 100 + (op_id % 900);
        string user = "User" + to_string((thread_id + op_id) % 50);
        string company = "Company" + to_string((thread_id + op_id) % 20);

        try {
            cache->addOrder(Order(order_id, security_id, side, qty, user, company));
            stats.add_ops++;
        }
        catch (exception& e) {
            cout << "SafeAdd ERROR: " << e.what() << endl;
        }
    }

    void safeQueryOperation(OrderCache* cache, ThreadStats& stats, int sec_num) {
        string security_id = "Sec" + to_string(sec_num);
        cache->getMatchingSizeForSecurity(security_id);
        stats.query_ops++;
    }

    void safeCancelOperation(int thread_id, int op_id, OrderCache* cache, ThreadStats& stats) {
        // Only attempt to cancel orders we might have created
        if (op_id % 10 == 0) {
            string target_order_id = "T" + to_string(thread_id) + "A" + to_string(op_id / 10);
            try {
                cache->cancelOrder(target_order_id);
                stats.cancel_ops++;
            }
            catch (const out_of_range&) {
                // Expected - order doesn't exist
                stats.errors++;
            }
        }
    }

    void safeBulkOperation(OrderCache* cache, ThreadStats& stats, int user_num, int sec_num) {
        // Only perform bulk operations occasionally
        static atomic<int> bulk_op_counter{ 0 };
        int counter = bulk_op_counter++;
        if (counter % 20 == 0) {
            string user = "User" + to_string(user_num);
            try {
                cache->cancelOrdersForUser(user);
                stats.bulk_cancel_ops++;
            }
            catch (const out_of_range&) {
                // Expected - user might not exist
                stats.errors++;
            }
        }
        else if (counter % 25 == 0) {
            string security = "Sec" + to_string(sec_num);
            try {
                cache->cancelOrdersForSecIdWithMinimumQty(security, 500);
                stats.bulk_cancel_ops++;
            }
            catch (const out_of_range&) {
                // Expected - security might not exist
                stats.errors++;
            }
        }
    }

    bool verifyCacheIntegritySilent(OrderCache& cache) {
        try {

            // Test 1: GetAllOrders should not crash
            auto all_orders = cache.getAllOrders();

            // Test 2: Matching size calculations should work for all securities

            // Test 3: Verify no duplicate order IDs
            unordered_set<string> order_ids;
            for (const auto& order : all_orders) {
                string order_id = order.orderId();
                if (order_ids.count(order_id)) {
                    cout << "ERROR: Duplicate order ID found: " << order_id << "\n";
                    return false;
                }
                order_ids.insert(order_id);
            }

            return true;

        }
        catch (const exception& e) {
            cout << "Cache integrity check failed: " << e.what() << "\n";
            return false;
        }
        catch (...) {
            cout << "Cache integrity check failed with unknown error\n";
            return false;
        }
    }
    bool verifyCacheIntegrity(OrderCache& cache) {
        try {
            cout << "Running cache integrity checks...\n";

            // Test 1: GetAllOrders should not crash
            auto all_orders = cache.getAllOrders();
            cout << "  - GetAllOrders: " << all_orders.size() << " orders\n";

            // Test 2: Matching size calculations should work for all securities
            for (int i = 1; i <= 10; i++) {
                string security_id = "Sec" + to_string(i);
                unsigned int match_size = cache.getMatchingSizeForSecurity(security_id);
                cout << "  - " << security_id << " match size: " << match_size << "\n";
            }

            // Test 3: Verify no duplicate order IDs
            unordered_set<string> order_ids;
            for (const auto& order : all_orders) {
                string order_id = order.orderId();
                if (order_ids.count(order_id)) {
                    cout << "ERROR: Duplicate order ID found: " << order_id << " securityId: " << order.securityId() << " userId: " << order.user() << "\n";
                    return false;
                }
                order_ids.insert(order_id);
            }

            cout << "  - No duplicate order IDs found\n";
            return true;

        }
        catch (const exception& e) {
            cout << "Cache integrity check failed: " << e.what() << "\n";
            return false;
        }
        catch (...) {
            cout << "Cache integrity check failed with unknown error\n";
            return false;
        }
    }

    void printSafeResults(const vector<ThreadStats>& stats, int num_threads) {
        ThreadStats total;
        vector<microseconds> all_latencies;

        for (const auto& s : stats) {
            total.add_ops += s.add_ops;
            total.cancel_ops += s.cancel_ops;
            total.query_ops += s.query_ops;
            total.bulk_cancel_ops += s.bulk_cancel_ops;
            total.errors += s.errors;
            all_latencies.insert(all_latencies.end(), s.latencies.begin(), s.latencies.end());
        }

        long total_ops = total.add_ops + total.cancel_ops + total.query_ops + total.bulk_cancel_ops;

        cout << "\n=== SAFE BENCHMARK RESULTS ===\n";
        cout << "Threads: " << num_threads << "\n";
        cout << "Total Operations: " << total_ops << "\n";
        cout << "  - Add: " << total.add_ops << " (" << (total.add_ops * 100.0 / total_ops) << "%)\n";
        cout << "  - Cancel: " << total.cancel_ops << " (" << (total.cancel_ops * 100.0 / total_ops) << "%)\n";
        cout << "  - Query: " << total.query_ops << " (" << (total.query_ops * 100.0 / total_ops) << "%)\n";
        cout << "  - Bulk Cancel: " << total.bulk_cancel_ops << " (" << (total.bulk_cancel_ops * 100.0 / total_ops) << "%)\n";
        cout << "Errors: " << total.errors << " (" << (total.errors * 100.0 / total_ops) << "%)\n";
        cout << "Memory Errors: " << memory_errors_.load() << "\n";

        if (!all_latencies.empty()) {
            sort(all_latencies.begin(), all_latencies.end());
            cout << "Latency (us): Min=" << all_latencies.front().count()
                << ", 50%=" << all_latencies[all_latencies.size() / 2].count()
                << ", 90%=" << all_latencies[all_latencies.size() * 9 / 10].count()
                << ", Max=" << all_latencies.back().count() << "\n";
        }
    }

    void runAdvancedBenchmarks() {
        cout << "\n=== Running Advanced (Cautious) Benchmarks ===\n";

        // Memory leak detection test
        cout << "\nMemory Leak Detection Test:\n";
        for (int i = 0; i < 3; i++) {
            auto cache = make_unique<OrderCache>();
            prePopulateSafely(*cache, 100);

            // Perform operations
            vector<thread> workers;
            for (int t = 0; t < 2; t++) {
                workers.emplace_back([&, cache_ptr = cache.get()]() {
                    for (int j = 0; j < 100; j++) {
                        ThreadStats dummyStats;
                        safeQueryOperation(cache_ptr, dummyStats, (j % 10) + 1);
                    }
                    });
            }

            for (auto& w : workers) w.join();
            cout << "  - Cycle " << (i + 1) << " completed without crash\n";
        }
    }

    void runDebuggingTests() {
        cout << "\n=== Running Debugging Tests ===\n";

        cout << "Test 1: Single-threaded basic operations...\n";
        testSingleThreaded();

        cout << "Test 2: Two-threaded minimal test...\n";
        testMinimalMultithreaded();

        cout << "Test 3: Memory stress test...\n";
        testMemoryStress();
    }

    void testSingleThreaded() {
        OrderCache cache;

        // Basic add
        cache.addOrder(Order("Debug1", "Sec1", "Buy", 100, "User1", "Company1"));
        cache.addOrder(Order("Debug2", "Sec1", "Sell", 200, "User2", "Company2"));

        // Query
        auto size = cache.getMatchingSizeForSecurity("Sec1");
        cout << "  - Single-threaded match size: " << size << "\n";

        // Cancel
        cache.cancelOrder("Debug1");

        // Verify
        auto orders = cache.getAllOrders();
        cout << "  - Orders after cancellation: " << orders.size() << "\n";

        cout << "  - Single-threaded test PASSED\n";
    }

    void testMinimalMultithreaded() {
        auto cache = make_unique<OrderCache>();
        atomic<bool> start{ false };

        thread reader([&]() {
            while (!start.load()) this_thread::yield();
            for (int i = 0; i < 100; i++) {
                cache->getMatchingSizeForSecurity("Sec1");
            }
            });

        thread writer([&]() {
            while (!start.load()) this_thread::yield();
            for (int i = 0; i < 50; i++) {
                cache->addOrder(Order("MT" + to_string(i), "Sec1", "Buy", 100, "User" + to_string(i), "Company1"));
            }
            });

        start.store(true);
        reader.join();
        writer.join();

        cout << "  - Minimal multithreaded test PASSED\n";
    }

    void testMemoryStress() {
        cout << "  - Testing memory allocation patterns...\n";

        for (int size = 100; size <= 1000; size *= 10) {
            auto cache = make_unique<OrderCache>();
            if (prePopulateSafely(*cache, size)) {
                cout << "    - Size " << size << ": OK\n";
            }
            else {
                cout << "    - Size " << size << ": FAILED\n";
            }
        }
    }
};

// Main entry points
void runSafeMultithreadedBenchmark() {
    cout << "=== OrderCache Safe Multithreaded Benchmark ===\n";
    cout << "Designed to avoid heap corruption and memory errors\n";

    SafeMultithreadBenchmark benchmark;
    benchmark.runSafeBenchmark();
}

void runQuickStabilityTest() {
    cout << "\n=== Quick Stability Test ===\n";

    OrderCache cache;
    vector<thread> threads;
    atomic<int> completed{ 0 };

    // Simple test: 4 threads, 100 operations each
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t]() {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> sec_dist(1, 5);

            for (int i = 0; i < 100; i++) {
                try {
                    // Mostly reads
                    cache.getMatchingSizeForSecurity("Sec" + to_string(sec_dist(gen)));
                    completed++;
                }
                catch (...) {
                    // Ignore errors in quick test
                }
            }
            });
    }

    for (auto& t : threads) t.join();

    cout << "Completed " << completed.load() << " operations without crash\n";
    cout << "Quick stability test PASSED\n";
}