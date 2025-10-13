#pragma once

#include "OrderCacheInterface.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>

using namespace std;

class OrderCache : public OrderCacheInterface {
private:
    // Main storage and indexes
    unordered_map<string, Order> orders_;
    unordered_map<string, unordered_set<string>> ordersBySecurity_;
    unordered_map<string, unordered_set<string>> ordersByUser_;

    // Synchronization primitives
    mutable shared_mutex ordersMutex_;          // For orders_ map
    mutable shared_mutex securityIndexMutex_;   // For ordersBySecurity_
    mutable shared_mutex userIndexMutex_;       // For ordersByUser_

public:
    void addOrder(Order order) override;
    void cancelOrder(const string& orderId) override;
    void cancelOrdersForUser(const string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const string& securityId) override;
    vector<Order> getAllOrders() const override;

};