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
    std::cout << "\nRunning Performance Benchmarks...\n";
    std::vector<unsigned int> testSizes = (numOrders == 0) ? std::vector<unsigned>{100, 1000, 10000} : std::vector<unsigned int>{ numOrders };

    // Seed random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> secDist(10, 100); // 10 securities
    std::uniform_int_distribution<> qtyDist(1000, 2000);
    std::uniform_int_distribution<> userDist(100, 200); // 20 users
    std::uniform_int_distribution<> companyDist(25, 50); // 5 companies
    std::uniform_int_distribution<> sideDist(0, 1); // 0: Buy, 1: Sell

    for (int size : testSizes) {
        std::cout << "\nBenchmarking with " << size << " orders...\n";

        // Generate orders
        std::vector<Order> orders;
        orders.reserve(size);
        for (int i = 1; i <= size; ++i) {
            std::stringstream ordId;
            ordId << "BenchOrd" << i;
            std::stringstream secId;
            secId << "SecId" << secDist(gen);
            std::string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
            unsigned int qty = qtyDist(gen);
            std::stringstream user;
            user << "User" << userDist(gen);
            std::stringstream company;
            company << "Company" << companyDist(gen);
            orders.emplace_back(ordId.str(), secId.str(), side, qty, user.str(), company.str());
        }

        // Benchmark addOrder
        OrderCache cache;
        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& order : orders) {
            cache.addOrder(order);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto addOrderDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double addOrderAvg = static_cast<double>(addOrderDuration.count()) / size;
        std::cout << "addOrder: Total = " << addOrderDuration.count() << " us, Avg per order = "
            << std::fixed << std::setprecision(2) << addOrderAvg << " us\n";

        // Benchmark getAllOrders
        start = std::chrono::high_resolution_clock::now();
        auto allOrders = cache.getAllOrders();
        end = std::chrono::high_resolution_clock::now();
        auto getAllOrdersDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "getAllOrders: Total = " << getAllOrdersDuration.count() << " us\n";

        // Benchmark getMatchingSizeForSecurity
        unsigned long long totalMatching = 0;
        start = std::chrono::high_resolution_clock::now();
        for (int sec = 1; sec <= 10; ++sec) {
            std::stringstream secId;
            secId << "SecId" << sec;
            totalMatching += cache.getMatchingSizeForSecurity(secId.str());
        }
        end = std::chrono::high_resolution_clock::now();
        auto matchSizeDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double matchSizeAvg = static_cast<double>(matchSizeDuration.count()) / 10;
        std::cout << "getMatchingSizeForSecurity: Total = " << matchSizeDuration.count()
            << " us, Avg per security = " << std::fixed << std::setprecision(2) << matchSizeAvg << " us\n";

        // Benchmark cancelOrder
        int numToCancel = size / 10;
        int realNumCancelled = numToCancel;
        std::vector<std::string> orderIds;
        for (int i = 1; i <= size; ++i) {
            orderIds.push_back("BenchOrd" + std::to_string(i));
        }
        std::shuffle(orderIds.begin(), orderIds.end(), gen);
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numToCancel; ++i) {
            cache.cancelOrder(orderIds[i]);
        }
        end = std::chrono::high_resolution_clock::now();
        auto cancelOrderDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double cancelOrderAvg = static_cast<double>(cancelOrderDuration.count()) / realNumCancelled;
        std::cout << "cancelOrder: Total = " << cancelOrderDuration.count() << " us, Avg per cancel = "
            << std::fixed << std::setprecision(2) << cancelOrderAvg << " us\n";

        // Benchmark cancelOrdersForUser
        std::stringstream randUser;
        randUser << "User" << userDist(gen);
        start = std::chrono::high_resolution_clock::now();
        cache.cancelOrdersForUser(randUser.str());
        end = std::chrono::high_resolution_clock::now();
        auto cancelUserDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "cancelOrdersForUser: Total = " << cancelUserDuration.count() << " us\n";

        // Benchmark cancelOrdersForSecIdWithMinimumQty
        std::stringstream randSec;
        randSec << "SecId" << secDist(gen);
        start = std::chrono::high_resolution_clock::now();
        cache.cancelOrdersForSecIdWithMinimumQty(randSec.str(), 500);
        end = std::chrono::high_resolution_clock::now();
        auto cancelMinQtyDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "cancelOrdersForSecIdWithMinimumQty: Total = " << cancelMinQtyDuration.count() << " us\n";

        // Verify order count
        auto remainingOrders = cache.getAllOrders();
        std::cout << "Orders remaining after operations: " << remainingOrders.size() << "\n";
		std::cout << "All time tests: " << (cancelMinQtyDuration.count() + cancelUserDuration.count() + cancelOrderDuration.count() + matchSizeDuration.count() + getAllOrdersDuration.count() + addOrderDuration.count()) << " us\n";
        std::cout << "Benchmark for " << size << " orders completed.\n";
    }

    std::cout << "\nPerformance Benchmarks Completed!\n";
}