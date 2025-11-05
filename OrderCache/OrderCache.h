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
		const StripedMap<string, Order>::size_type initial_tombstones = 10000;

		orders_.start_auto_purge(std::chrono::milliseconds(60000)); // Purge every 60s if tombstones exceed 10000
		orders_.set_purge_threshold(initial_tombstones);

		ordersBySecurity_.start_auto_purge(std::chrono::milliseconds(60000));
		ordersBySecurity_.set_purge_threshold(initial_tombstones);

		ordersByUser_.start_auto_purge(std::chrono::milliseconds(60000));
		ordersByUser_.set_purge_threshold(initial_tombstones);
    }
    void addOrder(Order order) override;
    void cancelOrder(const string& orderId) override;
    void cancelOrdersForUser(const string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const string& securityId) override;
    vector<Order> getAllOrders() const override;
};