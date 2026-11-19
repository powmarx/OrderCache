// GoogleTest port of the single-threaded tests in OrderCache/OrderCacheTest.cpp.
// That file is left unchanged and still runs through ordercache_legacy_runner.
//
// Porting rules:
//  - Every original assert() is ported with the same expected value, and its
//    message text is kept so each check can be traced back to the original.
//  - Steps that depend on earlier cancellations stay in order inside one TEST.
//    ASSERT_* guards the order counts that later steps rely on.
//  - Independent sub-scenarios from the same original function get their own TEST.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "OrderCache.h"

namespace {

std::vector<std::string> OrderIds(const std::vector<Order>& orders) {
    std::vector<std::string> ids;
    ids.reserve(orders.size());
    for (const auto& order : orders) {
        ids.push_back(order.orderId());
    }
    return ids;
}

bool Contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

template <typename Predicate>
bool AnyOrder(const std::vector<Order>& orders, Predicate predicate) {
    return std::any_of(orders.begin(), orders.end(), predicate);
}

void AddAll(OrderCache& cache, const std::vector<Order>& orders) {
    for (const auto& order : orders) {
        cache.addOrder(order);
    }
}

unsigned int TotalMatching(OrderCache& cache, const std::vector<std::string>& securities) {
    unsigned int total = 0;
    for (const auto& secId : securities) {
        total += cache.getMatchingSizeForSecurity(secId);
    }
    return total;
}

const std::vector<std::string> kSecurities = {"SecId1", "SecId2", "SecId3"};

}  // namespace

// -----------------------------------------------------------------------------
// Example 1 (runExample1Tests)
// -----------------------------------------------------------------------------
class Example1Test : public ::testing::Test {
protected:
    void SetUp() override {
        AddAll(cache_, {
            Order("OrdId1", "SecId1", "Sell", 100, "User10", "Company2"),
            Order("OrdId2", "SecId3", "Sell", 200, "User8", "Company2"),
            Order("OrdId3", "SecId1", "Buy", 300, "User13", "Company2"),
            Order("OrdId4", "SecId2", "Sell", 400, "User12", "Company2"),
            Order("OrdId5", "SecId3", "Sell", 500, "User7", "Company2"),
            Order("OrdId6", "SecId3", "Buy", 600, "User3", "Company1"),
            Order("OrdId7", "SecId1", "Sell", 700, "User10", "Company2"),
            Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"),
            Order("OrdId9", "SecId2", "Buy", 900, "User2", "Company2"),
            Order("OrdId10", "SecId2", "Sell", 1000, "User5", "Company1"),
            Order("OrdId11", "SecId1", "Sell", 1100, "User13", "Company2"),
            Order("OrdId12", "SecId2", "Buy", 1200, "User99", "Company2"),
            Order("OrdId13", "SecId1", "Sell", 1300, "User99", "Company"),
        });
    }

    OrderCache cache_;
};

TEST_F(Example1Test, MatchingSizePerSecurity) {
    // The original queries every security twice in a row; the second round
    // checks that a query does not modify the cache.
    for (int round = 1; round <= 2; ++round) {
        SCOPED_TRACE("query round " + std::to_string(round));
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId1"), 300u) << "SecId1 matching size should be 300";
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId2"), 1000u) << "SecId2 matching size should be 1000";
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId3"), 600u) << "SecId3 matching size should be 600";
    }
}

TEST_F(Example1Test, GetAllOrdersReturnsEveryOrder) {
    EXPECT_EQ(cache_.getAllOrders().size(), 13u) << "Total orders should be 13";
}

TEST_F(Example1Test, CancellationSequence) {
    {
        SCOPED_TRACE("cancelOrder(OrdId1)");
        cache_.cancelOrder("OrdId1");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 12u) << "Total orders after cancel should be 12";
        EXPECT_FALSE(Contains(OrderIds(orders), "OrdId1")) << "OrdId1 should be removed";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser(User10)");
        cache_.cancelOrdersForUser("User10");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 11u) << "Total orders after canceling User10 orders should be 11";
        EXPECT_FALSE(AnyOrder(orders, [](const Order& o) { return o.user() == "User10"; }))
            << "No orders for User10 should remain";
    }
    {
        SCOPED_TRACE("cancelOrdersForSecIdWithMinimumQty(SecId2, 900)");
        cache_.cancelOrdersForSecIdWithMinimumQty("SecId2", 900);
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 8u) << "Total orders after canceling SecId2 with minQty 900 should be 8";
        EXPECT_FALSE(AnyOrder(orders, [](const Order& o) { return o.securityId() == "SecId2" && o.qty() >= 900; }))
            << "No SecId2 orders with qty >= 900 should remain";

        std::vector<std::string> remainingSecId2;
        for (const auto& order : orders) {
            if (order.securityId() == "SecId2") remainingSecId2.push_back(order.orderId());
        }
        EXPECT_TRUE(Contains(remainingSecId2, "OrdId4")) << "OrdId4 should remain (qty=400 < 900)";
        EXPECT_FALSE(Contains(remainingSecId2, "OrdId9")) << "OrdId9 should be canceled (qty=900)";
        EXPECT_FALSE(Contains(remainingSecId2, "OrdId10")) << "OrdId10 should be canceled (qty=1000)";
        EXPECT_FALSE(Contains(remainingSecId2, "OrdId12")) << "OrdId12 should be canceled (qty=1200)";
    }
}

// Original: "cancelOrdersForSecIdWithMinimumQty with 3 matching orders test" (minQtyCache).
TEST(CancelOrdersForSecIdWithMinimumQty, CancelsThreeMatchingOrdersOnly) {
    OrderCache cache;
    AddAll(cache, {
        Order("Ord1", "SecA", "Buy", 1000, "User1", "Company1"),
        Order("Ord2", "SecA", "Sell", 1500, "User2", "Company2"),
        Order("Ord3", "SecA", "Buy", 2000, "User3", "Company3"),
        Order("Ord4", "SecA", "Sell", 500, "User4", "Company4"),   // below minQty
        Order("Ord5", "SecB", "Buy", 1000, "User5", "Company5"),   // different security
    });

    cache.cancelOrdersForSecIdWithMinimumQty("SecA", 1000);

    const auto orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), 2u) << "Total orders after canceling SecA with minQty 1000 should be 2";
    const auto ids = OrderIds(orders);
    EXPECT_TRUE(Contains(ids, "Ord4")) << "Ord4 should remain (qty < 1000)";
    EXPECT_TRUE(Contains(ids, "Ord5")) << "Ord5 should remain (different security)";
    EXPECT_FALSE(Contains(ids, "Ord1")) << "Ord1 should be canceled (qty >= 1000)";
    EXPECT_FALSE(Contains(ids, "Ord2")) << "Ord2 should be canceled (qty >= 1000)";
    EXPECT_FALSE(Contains(ids, "Ord3")) << "Ord3 should be canceled (qty >= 1000)";
}

// Original: "Bug fix test for getMatchingSizeForSecurity" (bugCache).
TEST(GetMatchingSizeForSecurity, TwoBuyersShareOneSmallerSell) {
    OrderCache cache;
    AddAll(cache, {
        Order("B1", "TEST", "Buy", 200, "User1", "C1"),
        Order("B2", "TEST", "Buy", 200, "User2", "C2"),
        Order("S1", "TEST", "Sell", 100, "User3", "Csell"),
    });

    EXPECT_EQ(cache.getMatchingSizeForSecurity("TEST"), 100u) << "Bug fix: max match 100";
}

// -----------------------------------------------------------------------------
// Example 2 (runExample2Tests)
// -----------------------------------------------------------------------------
class Example2Test : public ::testing::Test {
protected:
    void SetUp() override {
        AddAll(cache_, {
            Order("OrdId1", "SecId3", "Sell", 100, "User1", "Company1"),
            Order("OrdId2", "SecId3", "Sell", 200, "User3", "Company2"),
            Order("OrdId3", "SecId1", "Buy", 300, "User2", "Company1"),
            Order("OrdId4", "SecId3", "Sell", 400, "User5", "Company2"),
            Order("OrdId5", "SecId2", "Sell", 500, "User2", "Company1"),
            Order("OrdId6", "SecId2", "Buy", 600, "User3", "Company2"),
            Order("OrdId7", "SecId2", "Sell", 700, "User1", "Company1"),
            Order("OrdId8", "SecId1", "Sell", 800, "User2", "Company1"),
            Order("OrdId9", "SecId1", "Buy", 900, "User5", "Company2"),
            Order("OrdId10", "SecId1", "Sell", 1000, "User1", "Company1"),
            Order("OrdId11", "SecId2", "Sell", 1100, "User6", "Company2"),
        });
    }

    OrderCache cache_;
};

TEST_F(Example2Test, MatchingSizePerSecurity) {
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId1"), 900u) << "SecId1 matching size should be 900";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId2"), 600u) << "SecId2 matching size should be 600";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId3"), 0u) << "SecId3 matching size should be 0";
}

TEST_F(Example2Test, GetAllOrdersReturnsEveryOrder) {
    EXPECT_EQ(cache_.getAllOrders().size(), 11u) << "Total orders should be 11";
}

TEST_F(Example2Test, CancellationSequence) {
    {
        SCOPED_TRACE("cancelOrder(OrdId3)");
        cache_.cancelOrder("OrdId3");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 10u) << "Total orders after cancel should be 10";
        EXPECT_FALSE(Contains(OrderIds(orders), "OrdId3")) << "OrdId3 should be removed";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser(User2)");
        cache_.cancelOrdersForUser("User2");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 8u) << "Total orders after canceling User2 orders should be 8";
        EXPECT_FALSE(AnyOrder(orders, [](const Order& o) { return o.user() == "User2"; }))
            << "No orders for User2 should remain";
    }
    {
        SCOPED_TRACE("cancelOrdersForSecIdWithMinimumQty(SecId1, 800)");
        cache_.cancelOrdersForSecIdWithMinimumQty("SecId1", 800);
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 6u) << "Total orders after canceling SecId1 with minQty 800 should be 6";
        EXPECT_FALSE(AnyOrder(orders, [](const Order& o) { return o.securityId() == "SecId1" && o.qty() >= 800; }))
            << "No SecId1 orders with qty >= 800 should remain";
    }
}

// -----------------------------------------------------------------------------
// Example 3 (runExample3Tests)
// -----------------------------------------------------------------------------
TEST(Example3Test, TotalMatchingQuantityAcrossSecurities) {
    OrderCache cache;
    AddAll(cache, {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyA"),
        Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyC"),
        Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyB"),
        Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyD"),
        Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyE"),
        Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyE"),
    });

    ASSERT_EQ(cache.getAllOrders().size(), 8u) << "Expected 8 orders in cache";
    EXPECT_EQ(TotalMatching(cache, kSecurities), 2700u) << "Expected total matching quantity of 2700";
}

// -----------------------------------------------------------------------------
// Example 4 (runExample4Tests): a single company, then the same IDs reused
// -----------------------------------------------------------------------------
TEST(Example4Test, SingleCompanyThenReuseIdsAfterCancellingEveryUser) {
    OrderCache cache;
    {
        SCOPED_TRACE("all orders from CompanyA");
        AddAll(cache, {
            Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
            Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyA"),
            Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyA"),
            Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyA"),
            Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyA"),
            Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyA"),
            Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyA"),
            Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyA"),
        });
        ASSERT_EQ(cache.getAllOrders().size(), 8u) << "Expected 8 orders in cache";
        EXPECT_EQ(TotalMatching(cache, kSecurities), 0u) << "Expected total matching quantity of 0";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser for User1..User8");
        for (const std::string user : {"User1", "User2", "User3", "User4", "User5", "User6", "User7", "User8"}) {
            cache.cancelOrdersForUser(user);
        }
        ASSERT_TRUE(cache.getAllOrders().empty()) << "Expected all orders to be canceled";
    }
    {
        SCOPED_TRACE("same order IDs re-added, one company each");
        AddAll(cache, {
            Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
            Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"),
            Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyC"),
            Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyD"),
            Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyE"),
            Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyF"),
            Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyG"),
            Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyH"),
        });
        // The original message reads "...of 0"; the expected value is 500+2700+0.
        EXPECT_EQ(TotalMatching(cache, kSecurities), 500u + 2700u + 0u)
            << "Expected total matching quantity of 3200 (500+2700+0)";
    }
}

// -----------------------------------------------------------------------------
// Over-match (runOverMatchTest)
// -----------------------------------------------------------------------------
TEST(OverMatchTest, ThreeLargeBuysAgainstOneSmallSell) {
    OrderCache cache;  // "cacheBuy" in the original
    AddAll(cache, {
        Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId1", "Buy", 1000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Buy", 1000, "User3", "CompanyC"),
        Order("OrdId4", "SecId1", "Sell", 500, "User4", "CompanyD"),
    });

    ASSERT_EQ(cache.getAllOrders().size(), 4u) << "Expected 4 orders in cache";
    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 500u) << "Expected matching quantity of 500 for SecId1";

    cache.cancelOrdersForSecIdWithMinimumQty("SecId1", 1000);
    ASSERT_EQ(cache.getAllOrders().size(), 1u)
        << "Expected 1 order remaining after canceling SecId1 with minQty 1000";
    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 0u) << "Expected matching quantity of 0 after cancellations";

    // The original catches and prints this as "Expected error on adding duplicate order".
    EXPECT_THROW(cache.addOrder(Order("OrdId4", "SecId1", "Buy", 500, "User1", "CompanyA")), std::invalid_argument)
        << "Expected error on adding duplicate order";
    EXPECT_EQ(cache.getAllOrders().size(), 1u) << "Expected 1 remaining order";
}

TEST(OverMatchTest, ThreeLargeSellsAgainstOneSmallBuy) {
    OrderCache cache;  // "cacheSell" in the original
    AddAll(cache, {
        Order("OrdId1", "SecId1", "Sell", 1000, "User1", "CompanyA"),
        Order("OrdId2", "SecId1", "Sell", 1000, "User2", "CompanyB"),
        Order("OrdId3", "SecId1", "Sell", 1000, "User3", "CompanyC"),
        Order("OrdId4", "SecId1", "Buy", 500, "User4", "CompanyD"),
    });

    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 500u) << "Expected matching quantity of 500 for SecId1";
}
