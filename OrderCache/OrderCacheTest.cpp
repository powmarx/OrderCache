#include "OrderCacheTestUtils.h"
#include "OrderCache.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void runExample1Tests() {
    cout << "Running Example 1 Tests...\n";
    OrderCache cacheBuy;

    // Add orders from Example 1
    cacheBuy.addOrder(Order("OrdId1", "SecId1", "Sell", 100, "User10", "Company2"));
    cacheBuy.addOrder(Order("OrdId2", "SecId3", "Sell", 200, "User8", "Company2"));
    cacheBuy.addOrder(Order("OrdId3", "SecId1", "Buy", 300, "User13", "Company2"));
    cacheBuy.addOrder(Order("OrdId4", "SecId2", "Sell", 400, "User12", "Company2"));
    cacheBuy.addOrder(Order("OrdId5", "SecId3", "Sell", 500, "User7", "Company2"));
    cacheBuy.addOrder(Order("OrdId6", "SecId3", "Buy", 600, "User3", "Company1"));
    cacheBuy.addOrder(Order("OrdId7", "SecId1", "Sell", 700, "User10", "Company2"));
    cacheBuy.addOrder(Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"));
    cacheBuy.addOrder(Order("OrdId9", "SecId2", "Buy", 900, "User2", "Company2"));
    cacheBuy.addOrder(Order("OrdId10", "SecId2", "Sell", 1000, "User5", "Company1"));
    cacheBuy.addOrder(Order("OrdId11", "SecId1", "Sell", 1100, "User13", "Company2"));
    cacheBuy.addOrder(Order("OrdId12", "SecId2", "Buy", 1200, "User99", "Company2"));
    cacheBuy.addOrder(Order("OrdId13", "SecId1", "Sell", 1300, "User99", "Company"));

    // Test getMatchingSizeForSecurity
    assert(cacheBuy.getMatchingSizeForSecurity("SecId1") == 300 && "SecId1 matching size should be 300");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId2") == 1000 && "SecId2 matching size should be 1000");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId3") == 600 && "SecId3 matching size should be 600");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId1") == 300 && "SecId1 matching size should be 300");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId2") == 1000 && "SecId2 matching size should be 1000");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId3") == 600 && "SecId3 matching size should be 600");

    // Test getAllOrders
    auto ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 13 && "Total orders should be 13");

    // Test cancelOrder
    cacheBuy.cancelOrder("OrdId1");
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 12 && "Total orders after cancel should be 12");
    bool ordId1Found = false;
    for (const auto& order : ordersBuy) {
        if (order.orderId() == "OrdId1") ordId1Found = true;
    }
    assert(!ordId1Found && "OrdId1 should be removed");

    // Test cancelOrdersForUser
    cacheBuy.cancelOrdersForUser("User10");
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 11 && "Total orders after canceling User10 orders should be 11");
    bool user10Found = false;
    for (const auto& order : ordersBuy) {
        if (order.user() == "User10") user10Found = true;
    }
    assert(!user10Found && "No orders for User10 should remain");

    // Test cancelOrdersForSecIdWithMinimumQty
    cacheBuy.cancelOrdersForSecIdWithMinimumQty("SecId2", 900);
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 8 && "Total orders after canceling SecId2 with minQty 900 should be 8");
    bool largeSecId2Found = false;
    vector<string> remainingOrderIds;
    for (const auto& order : ordersBuy) {
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
    ordersBuy = minQtyCache.getAllOrders();
    assert(ordersBuy.size() == 2 && "Total orders after canceling SecA with minQty 1000 should be 2");
    remainingOrderIds.clear();
    for (const auto& order : ordersBuy) {
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
    OrderCache cacheBuy;

    // Add orders from Example 2
    cacheBuy.addOrder(Order("OrdId1", "SecId3", "Sell", 100, "User1", "Company1"));
    cacheBuy.addOrder(Order("OrdId2", "SecId3", "Sell", 200, "User3", "Company2"));
    cacheBuy.addOrder(Order("OrdId3", "SecId1", "Buy", 300, "User2", "Company1"));
    cacheBuy.addOrder(Order("OrdId4", "SecId3", "Sell", 400, "User5", "Company2"));
    cacheBuy.addOrder(Order("OrdId5", "SecId2", "Sell", 500, "User2", "Company1"));
    cacheBuy.addOrder(Order("OrdId6", "SecId2", "Buy", 600, "User3", "Company2"));
    cacheBuy.addOrder(Order("OrdId7", "SecId2", "Sell", 700, "User1", "Company1"));
    cacheBuy.addOrder(Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"));
    cacheBuy.addOrder(Order("OrdId9", "SecId1", "Buy", 900, "User5", "Company2"));
    cacheBuy.addOrder(Order("OrdId10", "SecId1", "Sell", 1000, "User1", "Company1"));
    cacheBuy.addOrder(Order("OrdId11", "SecId2", "Sell", 1100, "User6", "Company2"));

    // Test getMatchingSizeForSecurity
    assert(cacheBuy.getMatchingSizeForSecurity("SecId1") == 900 && "SecId1 matching size should be 900");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId2") == 600 && "SecId2 matching size should be 600");
    assert(cacheBuy.getMatchingSizeForSecurity("SecId3") == 0 && "SecId3 matching size should be 0");

    // Test getAllOrders
    auto ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 11 && "Total orders should be 11");

    // Test cancelOrder
    cacheBuy.cancelOrder("OrdId3");
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 10 && "Total orders after cancel should be 10");
    bool ordId3Found = false;
    for (const auto& order : ordersBuy) {
        if (order.orderId() == "OrdId3") ordId3Found = true;
    }
    assert(!ordId3Found && "OrdId3 should be removed");

    // Test cancelOrdersForUser
    cacheBuy.cancelOrdersForUser("User2");
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 8 && "Total orders after canceling User2 orders should be 8");
    bool user2Found = false;
    for (const auto& order : ordersBuy) {
        if (order.user() == "User2") user2Found = true;
    }
    assert(!user2Found && "No orders for User2 should remain");

    // Test cancelOrdersForSecIdWithMinimumQty
    cacheBuy.cancelOrdersForSecIdWithMinimumQty("SecId1", 800);
    ordersBuy = cacheBuy.getAllOrders();
    assert(ordersBuy.size() == 6 && "Total orders after canceling SecId1 with minQty 800 should be 6");
    bool largeSecId1Found = false;
    for (const auto& order : ordersBuy) {
        if (order.securityId() == "SecId1" && order.qty() >= 800) largeSecId1Found = true;
    }
    assert(!largeSecId1Found && "No SecId1 orders with qty >= 800 should remain");

    cout << "Example 2 Tests Passed!\n";
}

void runExample3Tests() {
    cout << "\nRunning Example Test 3...\n";

    OrderCache cacheBuy;

    // Add orders from the example
    vector<Order> ordersBuy = {
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
    for (const auto& order : ordersBuy) {
        cacheBuy.addOrder(order);
    }

    // Verify order count
    auto allOrders = cacheBuy.getAllOrders();
    assert(allOrders.size() == 8 && "Expected 8 orders in cache");

    // Single-threaded test: Calculate total matching quantity
    unsigned int totalMatchingQty = 0;
    vector<string> securities = { "SecId1", "SecId2", "SecId3" };
    for (const auto& secId : securities) {
        unsigned int qty = cacheBuy.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }

    cout << "Total matching quantity (single-threaded): " << totalMatchingQty << "\n";
    assert(totalMatchingQty == 2700 && "Expected total matching quantity of 2700");


    cout << "Example Test 3 Passed!\n";
}

void runExample4Tests() {
    cout << "\nRunning Example Test 4...\n";

    OrderCache cacheBuy;

    // Add orders from the example
    vector<Order> ordersBuy = {
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
    for (const auto& order : ordersBuy) {
        cacheBuy.addOrder(order);
    }

    // Verify order count
    auto allOrders = cacheBuy.getAllOrders();
    assert(allOrders.size() == 8 && "Expected 8 orders in cache");

    // Single-threaded test: Calculate total matching quantity
    unsigned int totalMatchingQty = 0;
    vector<string> securities = { "SecId1", "SecId2", "SecId3" };
    for (const auto& secId : securities) {
        unsigned int qty = cacheBuy.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }    
	assert(totalMatchingQty == 0 && "Expected total matching quantity of 0");

	//cancel all orders for CompanyA
	vector<string> users = { "User1", "User2", "User3", "User4", "User5", "User6", "User7", "User8" };
    for (const auto& user : users) {
        cacheBuy.cancelOrdersForUser(user);
	}
	assert(cacheBuy.getAllOrders().empty() && "Expected all orders to be canceled");


    // Add orders from the example
	ordersBuy.clear();
    ordersBuy = {
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
    for (const auto& order : ordersBuy) {
        cacheBuy.addOrder(order);
    }
    
    totalMatchingQty = 0;
    for (const auto& secId : securities) {
        unsigned int qty = cacheBuy.getMatchingSizeForSecurity(secId);
        cout << "Matching size for " << secId << ": " << qty << "\n";
        totalMatchingQty += qty;
    }
	assert(totalMatchingQty == 500+2700+0 && "Expected total matching quantity of 0");

    cout << "Total matching quantity (single-threaded): " << totalMatchingQty << "\n";
    cout << "Example Test 4 Passed!\n";
}

void addOrders(vector<Order> orders, OrderCache& cacheBuy)
{
    for (const auto& order : orders) {
        try {
            cacheBuy.addOrder(order);
        }
        catch (const std::exception& ex) {
            std::cerr << "Error adding order ->  " << ex.what() << "\n";
        }
    }

}
void runOverMatchTest() {
    std::cout << "\nRunning Over-Match Test...\n";

    OrderCache cacheBuy;
    OrderCache cacheSell;

    // Add orders for over-matching case
    std::vector<Order> ordersBuy = {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId1", "Buy", 1000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Buy", 1000, "User3", "CompanyC"),
        Order("OrdId4", "SecId1", "Sell", 500, "User4", "CompanyD")
    };
    std::vector<Order> ordersSell = {
        Order("OrdId1", "SecId1", "Sell", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId1", "Sell", 1000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Sell", 1000, "User3", "CompanyC"),
        Order("OrdId4", "SecId1", "Buy", 500, "User4", "CompanyD")
    };

    // Add orders to cache
    addOrders(ordersBuy, cacheBuy);
	addOrders(ordersSell, cacheSell);

    // Verify order count
    auto allOrders = cacheBuy.getAllOrders();
    assert(allOrders.size() == 4 && "Expected 4 orders in cache");

    // Single-threaded test: Calculate matching quantity
    unsigned int matchingQty = cacheBuy.getMatchingSizeForSecurity("SecId1");
    std::cout << "Matching size for SecId1: " << matchingQty << "\n";
    assert(matchingQty == 500 && "Expected matching quantity of 500 for SecId1");

	// Test cancelOrdersForSecIdWithMinimumQty in over-match scenario
    cacheBuy.cancelOrdersForSecIdWithMinimumQty("SecId1", 1000);
	assert(cacheBuy.getAllOrders().size() == 1 && "Expected 1 order remaining after canceling SecId1 with minQty 1000");
	cout << "Orders remaining after canceling SecId1 with minQty 1000: " << cacheBuy.getAllOrders().size() << "\n";
	assert(cacheBuy.getMatchingSizeForSecurity("SecId1") == 0 && "Expected matching quantity of 0 after cancellations");
	cout << "Matching size for SecId1 after cancellations: " << cacheBuy.getMatchingSizeForSecurity("SecId1") << "\n";

	// Try to add duplicate order
    try {
        cacheBuy.addOrder(Order("OrdId4", "SecId1", "Buy", 500, "User1", "CompanyA"));
    }
    catch (const std::exception& ex) {
        std::cerr << "Expected error on adding duplicate order ->  " << ex.what() << "\n";
	}

	vector<Order> remainingOrders = cacheBuy.getAllOrders();
	assert(remainingOrders.size() == 1 && "Expected 1 remaining order");
    cout << "Remaining orders: " << remainingOrders.size()  << endl;
    cout << "Order ID: " << remainingOrders[0].orderId() << ", Security ID: " << remainingOrders[0].securityId() 
         << ", Side: " << remainingOrders[0].side() << ", Qty: " << remainingOrders[0].qty() 
		 << ", User: " << remainingOrders[0].user() << ", Company: " << remainingOrders[0].company() << "\n";

    // Single-threaded test: Calculate matching quantity
    matchingQty = cacheSell.getMatchingSizeForSecurity("SecId1");
    std::cout << "Matching size for SecId1: " << matchingQty << "\n";
    assert(matchingQty == 500 && "Expected matching quantity of 500 for SecId1");

    std::cout << "Over-Match Test Passed!\n";
}