#pragma once

#include "OrderCacheInterface.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <unordered_set>
#include "StripedThreadsafeMap.h"

using namespace std;

template<typename Key, typename Value>
using StripedMap = StripedThreadSafeMap<Key, Value, std::hash<Key>, 2>;

class OrderCache : public OrderCacheInterface {
private:
    // Main storage: map order ID to Order object for quick lookup
    StripedMap <string, Order> orders_;

	// Indexes for efficient queries
    StripedMap<string, unordered_set<string>> ordersBySecurity_;
    StripedMap<string, unordered_set<string>> ordersByUser_;
public:
    void addOrder(Order order) override;
    void cancelOrder(const string& orderId) override;
    void cancelOrdersForUser(const string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const string& securityId) override;
    vector<Order> getAllOrders() const override;
};