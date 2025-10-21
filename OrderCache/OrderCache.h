#pragma once

#include "OrderCacheInterface.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <unordered_set>
#include "StripedThreadsafeMap.h"

using namespace std;

class OrderCache : public OrderCacheInterface {
private:
    // Main storage: map order ID to Order object for quick lookup
    StripedThreadSafeMap <string, Order> orders_;

	// Indexes for efficient queries
    StripedThreadSafeMap<string, unordered_set<string>, hash<string>, 2> ordersBySecurity_;
    StripedThreadSafeMap<string, unordered_set<string>, hash<string>, 2> ordersByUser_;
    unordered_set<string>& updateIndexSet(unordered_set<string>& currentSet, const string& orderId, size_t capacity, bool is_insertion);

public:
    void addOrder(Order order) override;
    void cancelOrder(const string& orderId) override;
    void cancelOrdersForUser(const string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const string& securityId) override;
    vector<Order> getAllOrders() const override;
};