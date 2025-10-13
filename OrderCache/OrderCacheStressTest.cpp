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
    cout << "Running Stress Test with " << numOrders << " orders...\n";
    OrderCache cache;

    // Seed random number generator
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> secDist(1, 10); // 10 securities
    uniform_int_distribution<> qtyDist(100, 2000);
    uniform_int_distribution<> userDist(100, 200); // 20 users
    uniform_int_distribution<> companyDist(100, 500); // 5 companies
    uniform_int_distribution<> sideDist(0, 1); // 0: Buy, 1: Sell

    // Add numOrders random orders
    for (unsigned int i = 1; i <= numOrders; ++i) {
        stringstream ordId;
        ordId << "StressOrd" << i;
        stringstream secId;
        secId << "SecId" << secDist(gen);
        string side = (sideDist(gen) == 0) ? "Buy" : "Sell";
        unsigned int qty = qtyDist(gen);
        stringstream user;
        user << "User" << userDist(gen);
        stringstream company;
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
        stringstream secId;
        secId << "SecId" << sec;
        totalMatching += cache.getMatchingSizeForSecurity(secId.str());
    }
    cout << "Total matching size across all securities: " << totalMatching << "\n";

    // Test cancelOrder: cancel 10% of orders randomly
    int numToCancel = numOrders / 10;
	int realNumToCancel = numToCancel; // To track actual cancellations
    for (int i = 0; i < numToCancel; ++i) {
        uniform_int_distribution<> ordDist(1, numOrders - i); // Adjust for removed
        int randOrd = ordDist(gen);
        stringstream ordId;
        ordId << "StressOrd" << randOrd;
        try
        {
            cache.cancelOrder(ordId.str());
        }
        catch (const exception& e)
        {
			realNumToCancel--; // If order not found, don't count it
			cout << "Warning: " << e.what() << "\n";
		}
    }
    orders = cache.getAllOrders();
    cout << "Cancelled " << realNumToCancel << " orders.\n";
	cout << "Orders remaining after cancellations: " << orders.size() << "\n";
    assert(orders.size() == static_cast<size_t>(numOrders - realNumToCancel) && "Orders after single cancellations match");

    // Test cancelOrdersForUser: cancel for a random user
    stringstream randUser;
    randUser << "User" << userDist(gen);
    cache.cancelOrdersForUser(randUser.str());

    // Test cancelOrdersForSecIdWithMinimumQty: cancel for a random security with minQty 500
    stringstream randSec;
    randSec << "SecId" << secDist(gen);
    cache.cancelOrdersForSecIdWithMinimumQty(randSec.str(), 500);

    // Final getAllOrders and statistics
    orders = cache.getAllOrders();
    cout << "Final number of orders after all cancellations: " << orders.size() << "\n";

    // Recompute total matching after operations
    totalMatching = 0;
    for (int sec = 1; sec <= 10; ++sec) {
        stringstream secId;
        secId << "SecId" << sec;
        totalMatching += cache.getMatchingSizeForSecurity(secId.str());
    }
    cout << "Total matching size after operations: " << totalMatching << "\n";

    // Compute average quantity (statistic)
    unsigned long long totalQty = 0;
    for (const auto& order : orders) {
        totalQty += order.qty();
    }
    double avgQty = orders.empty() ? 0 : static_cast<double>(totalQty) / orders.size();
    cout << "Average order quantity remaining: " << avgQty << "\n";

    assert(orders.size() < static_cast<size_t>(numOrders) && "Orders decreased after cancellations");
    cout << "Stress Test Passed!\n";
}