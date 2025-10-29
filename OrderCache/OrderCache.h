#pragma once

#include "OrderCacheInterface.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <unordered_set>
#include <shared_mutex>
#include "StripedThreadsafeMap.h"

using namespace std;

template<typename Key, typename Value>
using StripedMap = StripedThreadSafeMap<Key, Value, std::hash<Key>, shared_mutex, 2>;

class OrderCache : public OrderCacheInterface {
private:
    std::shared_mutex mutex_; // For any additional synchronization if needed
    
    // Main storage: map order ID to Order object for quick lookup
    StripedMap <string, Order> orders_;

	// Indexes for efficient queries
    StripedMap<string, unordered_set<string>> ordersBySecurity_;
    StripedMap<string, unordered_set<string>> ordersByUser_;
public:
    OrderCache() {
		orders_.set_shared_cross_mutex(mutex_);
		ordersBySecurity_.set_shared_cross_mutex(mutex_);
		ordersByUser_.set_shared_cross_mutex(mutex_);
    }
    void addOrder(Order order) override;
    void cancelOrder(const string& orderId) override;
    void cancelOrdersForUser(const string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const string& securityId) override;
    vector<Order> getAllOrders() const override;
};