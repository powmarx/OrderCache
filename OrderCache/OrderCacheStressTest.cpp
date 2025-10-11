#include "OrderCacheTestUtils.h"
#include "OrderCache.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <chrono>

using namespace std;

void runStressTest(unsigned int numOrders) {
    std::cout << "Running Stress Test with " << numOrders << " orders...\n";
    OrderCache cache;

    // Seed random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> secDist(1, 10); // 10 securities
    std::uniform_int_distribution<> qtyDist(100, 2000);
    std::uniform_int_distribution<> userDist(1, 20); // 20 users
    std::uniform_int_distribution<> companyDist(1, 5); // 5 companies
    std::uniform_int_distribution<> sideDist(0, 1); // 0: Buy, 1: Sell

    // Add numOrders random orders
    for (int i = 1; i <= numOrders; ++i) {
        std::stringstream ordId;
        ordId << "StressOrd" << i;
        std::stringstream secId;
        secId << "SecId" << secDist(gen);
        std::string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
        unsigned int qty = qtyDist(gen);
        std::stringstream user;
        user << "User" << userDist(gen);
        std::stringstream company;
        company << "Company" << companyDist(gen);
        cache.addOrder(Order(ordId.str(), secId.str(), side, qty, user.str(), company.str()));
    }
	cout << "Added " << numOrders << " orders.\n";

    // Test getAllOrders after adding
    auto orders = cache.getAllOrders();
    assert(orders.size() == static_cast<size_t>(numOrders) && "Total orders should match numOrders after adding");

    // Compute statistics: total matching size across all securities
    unsigned long long totalMatching = 0;
    for (int sec = 1; sec <= 10; ++sec) {
        std::stringstream secId;
        secId << "SecId" << sec;
        totalMatching += cache.getMatchingSizeForSecurity(secId.str());
    }
    std::cout << "Total matching size across all securities: " << totalMatching << "\n";

    // Test cancelOrder: cancel 10% of orders randomly
    int numToCancel = numOrders / 10;
	int realNumToCancel = numToCancel; // To track actual cancellations
    for (int i = 0; i < numToCancel; ++i) {
        std::uniform_int_distribution<> ordDist(1, numOrders - i); // Adjust for removed
        int randOrd = ordDist(gen);
        std::stringstream ordId;
        ordId << "StressOrd" << randOrd;
        try
        {
            cache.cancelOrder(ordId.str());
        }
        catch (const std::exception& e)
        {
			realNumToCancel--; // If order not found, don't count it
			cout << "Warning: " << e.what() << "\n";
		}
    }
    orders = cache.getAllOrders();
    std::cout << "Cancelled " << realNumToCancel << " orders.\n";
	std::cout << "Orders remaining after cancellations: " << orders.size() << "\n";
    assert(orders.size() == static_cast<size_t>(numOrders - realNumToCancel) && "Orders after single cancellations match");

    // Test cancelOrdersForUser: cancel for a random user
    std::stringstream randUser;
    randUser << "User" << userDist(gen);
    cache.cancelOrdersForUser(randUser.str());

    // Test cancelOrdersForSecIdWithMinimumQty: cancel for a random security with minQty 500
    std::stringstream randSec;
    randSec << "SecId" << secDist(gen);
    cache.cancelOrdersForSecIdWithMinimumQty(randSec.str(), 500);

    // Final getAllOrders and statistics
    orders = cache.getAllOrders();
    std::cout << "Final number of orders after all cancellations: " << orders.size() << "\n";

    // Recompute total matching after operations
    totalMatching = 0;
    for (int sec = 1; sec <= 10; ++sec) {
        std::stringstream secId;
        secId << "SecId" << sec;
        totalMatching += cache.getMatchingSizeForSecurity(secId.str());
    }
    std::cout << "Total matching size after operations: " << totalMatching << "\n";

    // Compute average quantity (statistic)
    unsigned long long totalQty = 0;
    for (const auto& order : orders) {
        totalQty += order.qty();
    }
    double avgQty = orders.empty() ? 0 : static_cast<double>(totalQty) / orders.size();
    std::cout << "Average order quantity remaining: " << avgQty << "\n";

    assert(orders.size() < static_cast<size_t>(numOrders) && "Orders decreased after cancellations");
    std::cout << "Stress Test Passed!\n";
}