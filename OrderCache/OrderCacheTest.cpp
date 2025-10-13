#include "OrderCacheTestUtils.h"
#include "OrderCache.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void runExample1Tests() {
    cout << "Running Example 1 Tests...\n";
    OrderCache cache;

    // Add orders from Example 1
    cache.addOrder(Order("OrdId1", "SecId1", "Sell", 100, "User10", "Company2"));
    cache.addOrder(Order("OrdId2", "SecId3", "Sell", 200, "User8", "Company2"));
    cache.addOrder(Order("OrdId3", "SecId1", "Buy", 300, "User13", "Company2"));
    cache.addOrder(Order("OrdId4", "SecId2", "Sell", 400, "User12", "Company2"));
    cache.addOrder(Order("OrdId5", "SecId3", "Sell", 500, "User7", "Company2"));
    cache.addOrder(Order("OrdId6", "SecId3", "Buy", 600, "User3", "Company1"));
    cache.addOrder(Order("OrdId7", "SecId1", "Sell", 700, "User10", "Company2"));
    cache.addOrder(Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"));
    cache.addOrder(Order("OrdId9", "SecId2", "Buy", 900, "User6", "Company2"));
    cache.addOrder(Order("OrdId10", "SecId2", "Sell", 1000, "User5", "Company1"));
    cache.addOrder(Order("OrdId11", "SecId1", "Sell", 1100, "User13", "Company2"));
    cache.addOrder(Order("OrdId12", "SecId2", "Buy", 1200, "User9", "Company2"));
    cache.addOrder(Order("OrdId13", "SecId1", "Sell", 1300, "User1", "Company"));

    // Test getMatchingSizeForSecurity
    assert(cache.getMatchingSizeForSecurity("SecId1") == 300 && "SecId1 matching size should be 300");
    assert(cache.getMatchingSizeForSecurity("SecId2") == 1000 && "SecId2 matching size should be 1000");
    assert(cache.getMatchingSizeForSecurity("SecId3") == 600 && "SecId3 matching size should be 600");

    // Test getAllOrders
    auto orders = cache.getAllOrders();
    assert(orders.size() == 13 && "Total orders should be 13");

    // Test cancelOrder
    cache.cancelOrder("OrdId1");
    orders = cache.getAllOrders();
    assert(orders.size() == 12 && "Total orders after cancel should be 12");
    bool ordId1Found = false;
    for (const auto& order : orders) {
        if (order.orderId() == "OrdId1") ordId1Found = true;
    }
    assert(!ordId1Found && "OrdId1 should be removed");

    // Test cancelOrdersForUser
    cache.cancelOrdersForUser("User10");
    orders = cache.getAllOrders();
    assert(orders.size() == 11 && "Total orders after canceling User10 orders should be 11");
    bool user10Found = false;
    for (const auto& order : orders) {
        if (order.user() == "User10") user10Found = true;
    }
    assert(!user10Found && "No orders for User10 should remain");

    // Test cancelOrdersForSecIdWithMinimumQty
    cache.cancelOrdersForSecIdWithMinimumQty("SecId2", 900);
    orders = cache.getAllOrders();
    assert(orders.size() == 8 && "Total orders after canceling SecId2 with minQty 900 should be 8");
    bool largeSecId2Found = false;
    vector<string> remainingOrderIds;
    for (const auto& order : orders) {
        if (order.securityId() == "SecId2") {
            if (order.qty() >= 900) largeSecId2Found = true;
            remainingOrderIds.push_back(order.orderId());
        }
    }
    assert(!largeSecId2Found && "No SecId2 orders with qty >= 900 should remain");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "OrdId4") != remainingOrderIds.end() &&
        "OrdId4 should remain (qty=400 < 900)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "OrdId9") == remainingOrderIds.end() &&
        "OrdId9 should be canceled (qty=900)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "OrdId10") == remainingOrderIds.end() &&
        "OrdId10 should be canceled (qty=1000)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "OrdId12") == remainingOrderIds.end() &&
        "OrdId12 should be canceled (qty=1200)");

    // Test cancelOrdersForSecIdWithMinimumQty with 3 matching orders
    OrderCache minQtyCache;
    minQtyCache.addOrder(Order("Ord1", "SecA", "Buy", 1000, "User1", "Company1"));
    minQtyCache.addOrder(Order("Ord2", "SecA", "Sell", 1500, "User2", "Company2"));
    minQtyCache.addOrder(Order("Ord3", "SecA", "Buy", 2000, "User3", "Company3"));
    minQtyCache.addOrder(Order("Ord4", "SecA", "Sell", 500, "User4", "Company4")); // Below minQty
    minQtyCache.addOrder(Order("Ord5", "SecB", "Buy", 1000, "User5", "Company5")); // Different security
    minQtyCache.cancelOrdersForSecIdWithMinimumQty("SecA", 1000);
    orders = minQtyCache.getAllOrders();
    assert(orders.size() == 2 && "Total orders after canceling SecA with minQty 1000 should be 2");
    remainingOrderIds.clear();
    for (const auto& order : orders) {
        remainingOrderIds.push_back(order.orderId());
    }
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "Ord4") != remainingOrderIds.end() &&
        "Ord4 should remain (qty < 1000)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "Ord5") != remainingOrderIds.end() &&
        "Ord5 should remain (different security)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "Ord1") == remainingOrderIds.end() &&
        "Ord1 should be canceled (qty >= 1000)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "Ord2") == remainingOrderIds.end() &&
        "Ord2 should be canceled (qty >= 1000)");
    assert(find(remainingOrderIds.begin(), remainingOrderIds.end(), "Ord3") == remainingOrderIds.end() &&
        "Ord3 should be canceled (qty >= 1000)");
    cout << "cancelOrdersForSecIdWithMinimumQty with 3 matching orders test passed!\n";

    // Test bug fix for getMatchingSizeForSecurity
    OrderCache bugCache;
    bugCache.addOrder(Order("B1", "TEST", "Buy", 200, "User1", "C1"));
    bugCache.addOrder(Order("B2", "TEST", "Buy", 200, "User2", "C2"));
    bugCache.addOrder(Order("S1", "TEST", "Sell", 100, "User3", "Csell"));
    assert(bugCache.getMatchingSizeForSecurity("TEST") == 100 && "Bug fix: max match 100");
    cout << "Bug fix test for getMatchingSizeForSecurity passed!\n";

    cout << "Example 1 Tests Passed!\n";
}

void runExample2Tests() {
    cout << "Running Example 2 Tests...\n";
    OrderCache cache;

    // Add orders from Example 2
    cache.addOrder(Order("OrdId1", "SecId3", "Sell", 100, "User1", "Company1"));
    cache.addOrder(Order("OrdId2", "SecId3", "Sell", 200, "User3", "Company2"));
    cache.addOrder(Order("OrdId3", "SecId1", "Buy", 300, "User2", "Company1"));
    cache.addOrder(Order("OrdId4", "SecId3", "Sell", 400, "User5", "Company2"));
    cache.addOrder(Order("OrdId5", "SecId2", "Sell", 500, "User2", "Company1"));
    cache.addOrder(Order("OrdId6", "SecId2", "Buy", 600, "User3", "Company2"));
    cache.addOrder(Order("OrdId7", "SecId2", "Sell", 700, "User1", "Company1"));
    cache.addOrder(Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"));
    cache.addOrder(Order("OrdId9", "SecId1", "Buy", 900, "User5", "Company2"));
    cache.addOrder(Order("OrdId10", "SecId1", "Sell", 1000, "User1", "Company1"));
    cache.addOrder(Order("OrdId11", "SecId2", "Sell", 1100, "User6", "Company2"));

    // Test getMatchingSizeForSecurity
    assert(cache.getMatchingSizeForSecurity("SecId1") == 900 && "SecId1 matching size should be 900");
    assert(cache.getMatchingSizeForSecurity("SecId2") == 600 && "SecId2 matching size should be 600");
    assert(cache.getMatchingSizeForSecurity("SecId3") == 0 && "SecId3 matching size should be 0");

    // Test getAllOrders
    auto orders = cache.getAllOrders();
    assert(orders.size() == 11 && "Total orders should be 11");

    // Test cancelOrder
    cache.cancelOrder("OrdId3");
    orders = cache.getAllOrders();
    assert(orders.size() == 10 && "Total orders after cancel should be 10");
    bool ordId3Found = false;
    for (const auto& order : orders) {
        if (order.orderId() == "OrdId3") ordId3Found = true;
    }
    assert(!ordId3Found && "OrdId3 should be removed");

    // Test cancelOrdersForUser
    cache.cancelOrdersForUser("User2");
    orders = cache.getAllOrders();
    assert(orders.size() == 8 && "Total orders after canceling User2 orders should be 8");
    bool user2Found = false;
    for (const auto& order : orders) {
        if (order.user() == "User2") user2Found = true;
    }
    assert(!user2Found && "No orders for User2 should remain");

    // Test cancelOrdersForSecIdWithMinimumQty
    cache.cancelOrdersForSecIdWithMinimumQty("SecId1", 800);
    orders = cache.getAllOrders();
    assert(orders.size() == 6 && "Total orders after canceling SecId1 with minQty 800 should be 6");
    bool largeSecId1Found = false;
    for (const auto& order : orders) {
        if (order.securityId() == "SecId1" && order.qty() >= 800) largeSecId1Found = true;
    }
    assert(!largeSecId1Found && "No SecId1 orders with qty >= 800 should remain");

    cout << "Example 2 Tests Passed!\n";
}

void runExample3Tests() {
    cout << "\nRunning Example Test 3...\n";

    OrderCache cache;

    // Add orders from the example
    vector<Order> orders = {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyA"),
        Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyC"),
        Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyB"),
        Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyD"),
        Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyE"),
        Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyE")
    };

    // Add orders to cache
    for (const auto& order : orders) {
        cache.addOrder(order);
    }

    // Verify order count
    auto allOrders = cache.getAllOrders();
    assert(allOrders.size() == 8 && "Expected 8 orders in cache");

    // Single-threaded test: Calculate total matching quantity
    unsigned int totalMatchingQty = 0;
    vector<string> securities = { "SecId1", "SecId2", "SecId3" };
    for (const auto& secId : securities) {
        unsigned int qty = cache.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }

    cout << "Total matching quantity (single-threaded): " << totalMatchingQty << "\n";
    assert(totalMatchingQty == 2700 && "Expected total matching quantity of 2700");


    cout << "Example Test 3 Passed!\n";
}

void runExample4Tests() {
    cout << "\nRunning Example Test 4...\n";

    OrderCache cache;

    // Add orders from the example
    vector<Order> orders = {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyA"),
        Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyA"),
        Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyA"),
        Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyA"),
        Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyA"),
        Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyA"),
        Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyA")
    };

    // Add orders to cache
    for (const auto& order : orders) {
        cache.addOrder(order);
    }

    // Verify order count
    auto allOrders = cache.getAllOrders();
    assert(allOrders.size() == 8 && "Expected 8 orders in cache");

    // Single-threaded test: Calculate total matching quantity
    unsigned int totalMatchingQty = 0;
    vector<string> securities = { "SecId1", "SecId2", "SecId3" };
    for (const auto& secId : securities) {
        unsigned int qty = cache.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }    
	assert(totalMatchingQty == 0 && "Expected total matching quantity of 0");

	//cancel all orders for CompanyA
	vector<string> users = { "User1", "User2", "User3", "User4", "User5", "User6", "User7", "User8" };
    for (const auto& user : users) {
        cache.cancelOrdersForUser(user);
	}
	assert(cache.getAllOrders().empty() && "Expected all orders to be canceled");


    // Add orders from the example
	orders.clear();
    orders = {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyC"),
        Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyD"),
        Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyE"),
        Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyF"),
        Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyG"),
        Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyH")
    };

    // Add orders to cache
    for (const auto& order : orders) {
        cache.addOrder(order);
    }
    
    totalMatchingQty = 0;
    for (const auto& secId : securities) {
        unsigned int qty = cache.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }
	assert(totalMatchingQty == 500+2700+0 && "Expected total matching quantity of 0");

    cout << "Total matching quantity (single-threaded): " << totalMatchingQty << "\n";
    cout << "Example Test 4 Passed!\n";
}

void runOverMatchTest() {
    std::cout << "\nRunning Over-Match Test...\n";

    OrderCache cache;

    // Add orders for over-matching case
    std::vector<Order> orders = {
        Order("OrdId1", "SecId1", "Sell", 1000, "User1", "CompanyA"),
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId1", "Buy", 1000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Buy", 1000, "User3", "CompanyC"),
        Order("OrdId4", "SecId1", "Sell", 500, "User4", "CompanyD")
    };

    // Add orders to cache
    for (const auto& order : orders) {
        cache.addOrder(order);
    }

    // Verify order count
    auto allOrders = cache.getAllOrders();
    assert(allOrders.size() == 4 && "Expected 4 orders in cache");

    // Single-threaded test: Calculate matching quantity
    unsigned int matchingQty = cache.getMatchingSizeForSecurity("SecId1");
    std::cout << "Matching size for SecId1: " << matchingQty << "\n";
    assert(matchingQty == 500 && "Expected matching quantity of 500 for SecId1");

	// Test cancelOrdersForSecIdWithMinimumQty in over-match scenario
    cache.cancelOrdersForSecIdWithMinimumQty("SecId1", 1000);
	assert(cache.getAllOrders().size() == 1 && "Expected 1 order remaining after canceling SecId1 with minQty 1000");
	cout << "Orders remaining after canceling SecId1 with minQty 1000: " << cache.getAllOrders().size() << "\n";
	assert(cache.getMatchingSizeForSecurity("SecId1") == 0 && "Expected matching quantity of 0 after cancellations");
	cout << "Matching size for SecId1 after cancellations: " << cache.getMatchingSizeForSecurity("SecId1") << "\n";

    std::cout << "Over-Match Test Passed!\n";
}