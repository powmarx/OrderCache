#include "OrderCache.h"
#include "OrderCacheInterface.h"
#include "OrderCacheTestUtils.h" // Assuming this file exists for test helper declarations
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <iomanip>

using namespace std;

// --- Test Configuration ---
const int NUM_THREADS = 8;        // Number of concurrent threads
const int OPERATIONS_PER_THREAD = 100000; // Total operations each thread will execute
const int TOTAL_OPERATIONS = NUM_THREADS * OPERATIONS_PER_THREAD;

// --- Helper Functions for Data Generation ---

// Creates a pseudo-random number generator for each thread to avoid contention on a single shared generator.
mt19937 create_prng() {
    // Uses a truly random seed (random_device) to ensure test variety on each run
    return mt19937(random_device{}());
}

// @brief Simulates a worker thread performing mixed operations on the cache.
void threadWorker(OrderCache& cache, int threadId, mt19937& gen) {
    // Use a small set of securities and users to maximize lock contention on the stripes
    uniform_int_distribution<> secDist(1, 20);      // 20 securities (high contention)
    uniform_int_distribution<> userDist(1, 200);     // 200 users (high contention)
    uniform_int_distribution<> qtyDist(100, 2000);   // Quantity
    uniform_int_distribution<> sideDist(0, 1);       // Side (Buy/Sell)

    // Operation distribution: 60% Add, 20% Cancel, 20% Read
    uniform_int_distribution<> opDist(0, 99);

    for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        int op = opDist(gen);

        stringstream ordId;
        // Create a unique OrderID for this thread: T[ThreadID]O[Iteration]
        ordId << "T" << threadId << "O" << i;
        string currentOrderId = ordId.str();

        // WRITE: Insertion Operation (Add: 0-59)
        if (op < 60) {
            stringstream secId, user, company;
            secId << "SecId" << secDist(gen);
            user << "User" << userDist(gen);
            company << "Comp" << (userDist(gen) % 5 + 1); // 5 companies
            string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
            unsigned int qty = qtyDist(gen);

            Order order(currentOrderId, secId.str(), side, qty, user.str(), company.str());

            try {
                cache.addOrder(order);
            }
            catch (const exception& e) {
                // Duplicate Order ID exceptions are expected/safe and should be caught here
            }
        }
        // WRITE: Cancellation Operation (Cancel: 60-79)
        else if (op < 80) {
            // Attempt to cancel an order that should have been added recently by this thread.
            // This tests the concurrent modification of the main map and indexes.
            stringstream ordIdToCancel;
            // Target an order ID from the last 200 operations (T_ID_max(0, i - 200))
            ordIdToCancel << "T" << threadId << "O" << max(0, i - 200);

            try {
                cache.cancelOrder(ordIdToCancel.str());
            }
            catch (const out_of_range&) {
                // If the order was already canceled or aged out, it's fine.
            }
        }
        // READ: Query Operation (getMatchingSizeForSecurity: 80-99)
        else {
            stringstream secId;
            secId << "SecId" << secDist(gen);
            // This call uses a SHARED lock, testing read performance under contention
            cache.getMatchingSizeForSecurity(secId.str());
        }
    }
}

// @brief Runs the main multi-threaded test and measures performance.
void runMultithreadTest() {
    cout << "\n========================================\n";
    cout << "    Running Multithread Performance Test\n";
    cout << "========================================\n";
    cout << "Threads: " << NUM_THREADS << ", Operations/Thread: " << OPERATIONS_PER_THREAD
        << ", Total Ops: " << TOTAL_OPERATIONS << "\n";

    OrderCache cache;
    vector<thread> workers;

    auto start = chrono::high_resolution_clock::now();

    // Create and start the worker threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        mt19937 workerGen = create_prng();
        workers.emplace_back(threadWorker, ref(cache), i, ref(workerGen));
    }

    // Wait for all threads to finish
    for (auto& worker : workers) {
        worker.join();
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

    // --- Results and Verification ---

    // A simple sanity check on the final number of orders
    auto finalOrders = cache.getAllOrders();
    cout << "\nTest Finished.\n";
    cout << "Final Order Count (getAllOrders): " << finalOrders.size() << "\n";
    cout << "Total Execution Time: " << duration.count() << " ms\n";
    cout << "Operations Per Second: " << fixed << setprecision(2)
        << (double)TOTAL_OPERATIONS / (duration.count() / 1000.0) << " Ops/sec\n";
    cout << "========================================\n";
}