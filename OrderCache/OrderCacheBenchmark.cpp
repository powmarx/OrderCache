#include "OrderCacheTestUtils.h"
#include "OrderCache.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <chrono>
#include <iomanip>

void runBenchmark(unsigned int numOrders) {
    cout << "\nRunning Performance Benchmarks...\n";
    vector<unsigned int> testSizes = (numOrders == 0) ? vector<unsigned>{1000, 100000, 1000000} : vector<unsigned int>{ numOrders };
    // Seed random number generator
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> secDist(1, 1000); // 10 securities
    uniform_int_distribution<> qtyDist(1000, 2000);
    uniform_int_distribution<> userDist(1, 1000); // 20 users
    uniform_int_distribution<> companyDist(1, 1000); // 5 companies
    uniform_int_distribution<> sideDist(0, 1); // 0: Buy, 1: Sell

    for (int size : testSizes) {
        cout << "\nBenchmarking with " << size << " orders...\n";

        // Generate orders
        vector<Order> orders;
        vector<string> userIDs;
        vector<string> secIDs;
        orders.reserve(size);
        for (int i = 1; i <= size; ++i) {
            stringstream ordId;
            ordId << "BenchOrd" << i;
            stringstream secId;
            secId << "SecId" << secDist(gen);
			secIDs.push_back(secId.str());
            string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
            unsigned int qty = qtyDist(gen);
            stringstream user;
            user << "User" << userDist(gen);
			userIDs.push_back(user.str());
            stringstream company;
            company << "Company" << companyDist(gen);
            orders.emplace_back(ordId.str(), secId.str(), side, qty, user.str(), company.str());
        }

        // Benchmark addOrder
        OrderCache cache;
        auto start = chrono::high_resolution_clock::now();
        for (const auto& order : orders) {
            cache.addOrder(order);
        }
        auto end = chrono::high_resolution_clock::now();
        auto addOrderDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        double addOrderAvg = static_cast<double>(addOrderDuration.count()) / size;
        cout << "addOrder: Total = " << addOrderDuration.count() << " us, Avg per order = "
            << fixed << setprecision(2) << addOrderAvg << " us\n";

        // Benchmark getAllOrders
        start = chrono::high_resolution_clock::now();
        auto allOrders = cache.getAllOrders();
        end = chrono::high_resolution_clock::now();
        auto getAllOrdersDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        cout << "getAllOrders: Total = " << getAllOrdersDuration.count() << " us\n";

        // Benchmark getMatchingSizeForSecurity
        unsigned long long totalMatching = 0;
        start = chrono::high_resolution_clock::now();
        for (int sec = 1; sec <= 10; ++sec) {
            stringstream secId;
            secId << "SecId" << sec;
            totalMatching += cache.getMatchingSizeForSecurity(secId.str());
        }
        end = chrono::high_resolution_clock::now();
        auto matchSizeDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        double matchSizeAvg = static_cast<double>(matchSizeDuration.count()) / 10;
        cout << "getMatchingSizeForSecurity: Total = " << matchSizeDuration.count()
            << " us, Avg per security = " << fixed << setprecision(2) << matchSizeAvg << " us\n";
        cout << "Total Orders matching: " << totalMatching << "\n";

        // Benchmark cancelOrder
        int numToCancel = size / 10;
        int realNumCancelled = numToCancel;
        vector<string> orderIds;
        for (int i = 1; i <= size; ++i) {
            orderIds.push_back("BenchOrd" + to_string(i));
        }
        shuffle(orderIds.begin(), orderIds.end(), gen);
        start = chrono::high_resolution_clock::now();
        for (int i = 0; i < numToCancel; ++i) {
            cache.cancelOrder(orderIds[i]);
        }
        end = chrono::high_resolution_clock::now();
        auto cancelOrderDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        double cancelOrderAvg = static_cast<double>(cancelOrderDuration.count()) / realNumCancelled;
        cout << "cancelOrder: Total = " << cancelOrderDuration.count() << " us, Avg per cancel = "
            << fixed << setprecision(2) << cancelOrderAvg << " us\n";

        // Benchmark cancelOrdersForUser
         start = chrono::high_resolution_clock::now();
        try {

            cache.cancelOrdersForUser(userIDs[0]);
        } catch (const exception& e) {
            cerr << "Exception during cancelOrdersForUser: " << e.what() << "\n";
		}
        end = chrono::high_resolution_clock::now();
        auto cancelUserDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        cout << "cancelOrdersForUser: Total = " << cancelUserDuration.count() << " us\n";

        // Benchmark cancelOrdersForSecIdWithMinimumQty
        stringstream randSec;
        randSec << "SecId" << secDist(gen);
        start = chrono::high_resolution_clock::now();
        try            {
            cache.cancelOrdersForSecIdWithMinimumQty(randSec.str(), 1500);
        } catch (const exception& e) {
            cerr << "Exception during cancelOrdersForSecIdWithMinimumQty: " << e.what() << "\n";
		}
        end = chrono::high_resolution_clock::now();
        auto cancelMinQtyDuration = chrono::duration_cast<chrono::microseconds>(end - start);
        cout << "cancelOrdersForSecIdWithMinimumQty: Total = " << cancelMinQtyDuration.count() << " us\n";

        // Verify order count
        auto remainingOrders = cache.getAllOrders();
        cout << "Orders remaining after operations: " << remainingOrders.size() << "\n";
        cout << "All time tests: " << (cancelMinQtyDuration.count() + cancelUserDuration.count() + cancelOrderDuration.count() + matchSizeDuration.count() + getAllOrdersDuration.count() + addOrderDuration.count()) << " us\n";
        cout << "Benchmark for " << size << " orders completed.\n";
    }

    cout << "\nPerformance Benchmarks Completed!\n";
}