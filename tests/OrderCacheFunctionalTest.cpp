// GoogleTest port of the single-threaded tests in OrderCache/OrderCacheTest.cpp
// (runExample1Tests .. runExample4Tests, runOverMatchTest).
//
// Expected values and messages are the originals. Steps whose expected values
// depend on earlier steps stay in one TEST, with a SCOPED_TRACE per step.
// Independent scenarios are separate TESTs.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "OrderCache.h"
#include "TestHelpers.h"

using ordercache_test::addOrders;
using ordercache_test::containsOrderId;
using ordercache_test::countOrders;
using ordercache_test::totalMatchingSize;

namespace {

const std::vector<std::string> kSecurities = { "SecId1", "SecId2", "SecId3" };

// -----------------------------------------------------------------------------
// Example 1 (runExample1Tests)
// -----------------------------------------------------------------------------
class Example1 : public ::testing::Test {
protected:
    void SetUp() override
    {
        addOrders(cache_, {
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

TEST_F(Example1, MatchingSizePerSecurity)
{
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId1"), 300u) << "SecId1 matching size should be 300";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId2"), 1000u) << "SecId2 matching size should be 1000";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId3"), 600u) << "SecId3 matching size should be 600";
}

// The original queries every security twice in a row; a query must not change
// the result of the next one.
TEST_F(Example1, MatchingSizeIsStableAcrossRepeatedQueries)
{
    for (int round = 1; round <= 2; ++round) {
        SCOPED_TRACE("query round " + std::to_string(round));
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId1"), 300u) << "SecId1 matching size should be 300";
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId2"), 1000u) << "SecId2 matching size should be 1000";
        EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId3"), 600u) << "SecId3 matching size should be 600";
    }
}

TEST_F(Example1, GetAllOrdersReturnsEveryOrder)
{
    EXPECT_EQ(cache_.getAllOrders().size(), 13u) << "Total orders should be 13";
}

// Each expected count depends on the cancellations before it (e.g. OrdId1
// belongs to User10), so the sequence stays in a single test.
TEST_F(Example1, CancellationSequence)
{
    {
        SCOPED_TRACE("cancelOrder(OrdId1)");
        cache_.cancelOrder("OrdId1");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 12u) << "Total orders after cancel should be 12";
        EXPECT_FALSE(containsOrderId(orders, "OrdId1")) << "OrdId1 should be removed";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser(User10)");
        cache_.cancelOrdersForUser("User10");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 11u) << "Total orders after canceling User10 orders should be 11";
        EXPECT_EQ(countOrders(orders, [](const Order& o) { return o.user() == "User10"; }), 0u)
            << "No orders for User10 should remain";
    }
    {
        SCOPED_TRACE("cancelOrdersForSecIdWithMinimumQty(SecId2, 900)");
        cache_.cancelOrdersForSecIdWithMinimumQty("SecId2", 900);
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 8u) << "Total orders after canceling SecId2 with minQty 900 should be 8";
        EXPECT_EQ(countOrders(orders, [](const Order& o) { return o.securityId() == "SecId2" && o.qty() >= 900; }), 0u)
            << "No SecId2 orders with qty >= 900 should remain";
        EXPECT_TRUE(containsOrderId(orders, "OrdId4")) << "OrdId4 should remain (qty=400 < 900)";
        EXPECT_FALSE(containsOrderId(orders, "OrdId9")) << "OrdId9 should be canceled (qty=900)";
        EXPECT_FALSE(containsOrderId(orders, "OrdId10")) << "OrdId10 should be canceled (qty=1000)";
        EXPECT_FALSE(containsOrderId(orders, "OrdId12")) << "OrdId12 should be canceled (qty=1200)";
    }
}

// "Test cancelOrdersForSecIdWithMinimumQty with 3 matching orders" (Example 1)
TEST(CancelOrdersForSecIdWithMinimumQty, CancelsAllMatchingOrdersOnly)
{
    OrderCache cache;
    addOrders(cache, {
        Order("Ord1", "SecA", "Buy", 1000, "User1", "Company1"),
        Order("Ord2", "SecA", "Sell", 1500, "User2", "Company2"),
        Order("Ord3", "SecA", "Buy", 2000, "User3", "Company3"),
        Order("Ord4", "SecA", "Sell", 500, "User4", "Company4"),  // Below minQty
        Order("Ord5", "SecB", "Buy", 1000, "User5", "Company5"),  // Different security
    });

    cache.cancelOrdersForSecIdWithMinimumQty("SecA", 1000);

    const auto orders = cache.getAllOrders();
    ASSERT_EQ(orders.size(), 2u) << "Total orders after canceling SecA with minQty 1000 should be 2";
    EXPECT_TRUE(containsOrderId(orders, "Ord4")) << "Ord4 should remain (qty < 1000)";
    EXPECT_TRUE(containsOrderId(orders, "Ord5")) << "Ord5 should remain (different security)";
    EXPECT_FALSE(containsOrderId(orders, "Ord1")) << "Ord1 should be canceled (qty >= 1000)";
    EXPECT_FALSE(containsOrderId(orders, "Ord2")) << "Ord2 should be canceled (qty >= 1000)";
    EXPECT_FALSE(containsOrderId(orders, "Ord3")) << "Ord3 should be canceled (qty >= 1000)";
}

// "Test bug fix for getMatchingSizeForSecurity" (Example 1)
TEST(GetMatchingSizeForSecurity, TotalMatchIsCappedBySellQuantity)
{
    OrderCache cache;
    addOrders(cache, {
        Order("B1", "TEST", "Buy", 200, "User1", "C1"),
        Order("B2", "TEST", "Buy", 200, "User2", "C2"),
        Order("S1", "TEST", "Sell", 100, "User3", "Csell"),
    });

    EXPECT_EQ(cache.getMatchingSizeForSecurity("TEST"), 100u) << "Bug fix: max match 100";
}

// -----------------------------------------------------------------------------
// Example 2 (runExample2Tests)
// -----------------------------------------------------------------------------
class Example2 : public ::testing::Test {
protected:
    void SetUp() override
    {
        addOrders(cache_, {
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

TEST_F(Example2, MatchingSizePerSecurity)
{
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId1"), 900u) << "SecId1 matching size should be 900";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId2"), 600u) << "SecId2 matching size should be 600";
    EXPECT_EQ(cache_.getMatchingSizeForSecurity("SecId3"), 0u) << "SecId3 matching size should be 0";
}

TEST_F(Example2, GetAllOrdersReturnsEveryOrder)
{
    EXPECT_EQ(cache_.getAllOrders().size(), 11u) << "Total orders should be 11";
}

TEST_F(Example2, CancellationSequence)
{
    {
        SCOPED_TRACE("cancelOrder(OrdId3)");
        cache_.cancelOrder("OrdId3");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 10u) << "Total orders after cancel should be 10";
        EXPECT_FALSE(containsOrderId(orders, "OrdId3")) << "OrdId3 should be removed";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser(User2)");
        cache_.cancelOrdersForUser("User2");
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 8u) << "Total orders after canceling User2 orders should be 8";
        EXPECT_EQ(countOrders(orders, [](const Order& o) { return o.user() == "User2"; }), 0u)
            << "No orders for User2 should remain";
    }
    {
        SCOPED_TRACE("cancelOrdersForSecIdWithMinimumQty(SecId1, 800)");
        cache_.cancelOrdersForSecIdWithMinimumQty("SecId1", 800);
        const auto orders = cache_.getAllOrders();
        ASSERT_EQ(orders.size(), 6u) << "Total orders after canceling SecId1 with minQty 800 should be 6";
        EXPECT_EQ(countOrders(orders, [](const Order& o) { return o.securityId() == "SecId1" && o.qty() >= 800; }), 0u)
            << "No SecId1 orders with qty >= 800 should remain";
    }
}

// -----------------------------------------------------------------------------
// Example 3 (runExample3Tests)
// -----------------------------------------------------------------------------
TEST(Example3, TotalMatchingQuantityAcrossSecurities)
{
    OrderCache cache;
    addOrders(cache, {
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
    EXPECT_EQ(totalMatchingSize(cache, kSecurities), 2700u) << "Expected total matching quantity of 2700";
}

// -----------------------------------------------------------------------------
// Example 4 (runExample4Tests): only one company, then re-added with distinct companies
// -----------------------------------------------------------------------------
TEST(Example4, SingleCompanyThenDistinctCompanies)
{
    OrderCache cache;
    const std::vector<std::string> users = {
        "User1", "User2", "User3", "User4", "User5", "User6", "User7", "User8" };

    {
        SCOPED_TRACE("all orders from CompanyA");
        addOrders(cache, {
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
        EXPECT_EQ(totalMatchingSize(cache, kSecurities), 0u) << "Expected total matching quantity of 0";
    }
    {
        SCOPED_TRACE("cancelOrdersForUser for every user");
        for (const auto& user : users) {
            cache.cancelOrdersForUser(user);
        }
        ASSERT_TRUE(cache.getAllOrders().empty()) << "Expected all orders to be canceled";
    }
    {
        SCOPED_TRACE("same order IDs re-added, one company per order");
        addOrders(cache, {
            Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
            Order("OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"),
            Order("OrdId3", "SecId1", "Sell", 500, "User3", "CompanyC"),
            Order("OrdId4", "SecId2", "Buy", 600, "User4", "CompanyD"),
            Order("OrdId5", "SecId2", "Buy", 100, "User5", "CompanyE"),
            Order("OrdId6", "SecId3", "Buy", 1000, "User6", "CompanyF"),
            Order("OrdId7", "SecId2", "Buy", 2000, "User7", "CompanyG"),
            Order("OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyH"),
        });
        EXPECT_EQ(totalMatchingSize(cache, kSecurities), 500u + 2700u + 0u)
            << "Expected total matching quantity of 3200";
    }
}

// -----------------------------------------------------------------------------
// Over-match (runOverMatchTest)
// -----------------------------------------------------------------------------
const std::vector<Order> kOverMatchBuy = {
    Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"),
    Order("OrdId2", "SecId1", "Buy", 1000, "User2", "CompanyB"),
    Order("OrdId3", "SecId1", "Buy", 1000, "User3", "CompanyC"),
    Order("OrdId4", "SecId1", "Sell", 500, "User4", "CompanyD"),
};

const std::vector<Order> kOverMatchSell = {
    Order("OrdId1", "SecId1", "Sell", 1000, "User1", "CompanyA"),
    Order("OrdId2", "SecId1", "Sell", 1000, "User2", "CompanyB"),
    Order("OrdId3", "SecId1", "Sell", 1000, "User3", "CompanyC"),
    Order("OrdId4", "SecId1", "Buy", 500, "User4", "CompanyD"),
};

TEST(OverMatch, BuySideIsCappedBySellQuantity)
{
    OrderCache cache;
    addOrders(cache, kOverMatchBuy);

    ASSERT_EQ(cache.getAllOrders().size(), 4u) << "Expected 4 orders in cache";
    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 500u) << "Expected matching quantity of 500 for SecId1";
}

TEST(OverMatch, SellSideIsCappedByBuyQuantity)
{
    OrderCache cache;
    addOrders(cache, kOverMatchSell);

    EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 500u) << "Expected matching quantity of 500 for SecId1";
}

TEST(OverMatch, CancelWithMinimumQtyThenRejectDuplicate)
{
    OrderCache cache;
    addOrders(cache, kOverMatchBuy);

    {
        SCOPED_TRACE("cancelOrdersForSecIdWithMinimumQty(SecId1, 1000)");
        cache.cancelOrdersForSecIdWithMinimumQty("SecId1", 1000);
        ASSERT_EQ(cache.getAllOrders().size(), 1u)
            << "Expected 1 order remaining after canceling SecId1 with minQty 1000";
        EXPECT_EQ(cache.getMatchingSizeForSecurity("SecId1"), 0u)
            << "Expected matching quantity of 0 after cancellations";
    }
    {
        SCOPED_TRACE("add duplicate OrdId4");
        // The original catches this and prints "Expected error on adding
        // duplicate order"; here the expected exception is asserted.
        EXPECT_THROW(cache.addOrder(Order("OrdId4", "SecId1", "Buy", 500, "User1", "CompanyA")),
                     std::invalid_argument);
        EXPECT_EQ(cache.getAllOrders().size(), 1u) << "Expected 1 remaining order";
    }
}

} // namespace
