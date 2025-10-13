#include "OrderCache.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <thread>

void OrderCache::addOrder(Order order) {
    const string& orderId = order.orderId();
    const string& securityId = order.securityId();
    const string& user = order.user();

    // Acquire all write locks
    unique_lock<shared_mutex> ordersLock(ordersMutex_);
    unique_lock<shared_mutex> securityLock(securityIndexMutex_);
    unique_lock<shared_mutex> userLock(userIndexMutex_);

    // Add to main storage
    if (!(orders_.try_emplace(orderId, order)).second) {
        throw invalid_argument("Duplicate Order ID: " + orderId);
    }; // O(1) average case

    // Update indexes with collision mitigation
    auto& secSet = ordersBySecurity_[securityId];
    if (secSet.empty()) secSet.reserve(50000);
    secSet.max_load_factor(0.7F);
    secSet.insert(orderId);

    auto& userSet = ordersByUser_[user];
    if (userSet.empty()) userSet.reserve(50000);
    userSet.max_load_factor(0.7F);
    userSet.insert(orderId);
}

void OrderCache::cancelOrder(const string& orderId) {
    // First, find the order with read lock
    shared_lock<shared_mutex> ordersReadLock(ordersMutex_);
    auto orderIt = orders_.find(orderId);
    if (orderIt == orders_.end()) {
        throw out_of_range("Order ID not found: " + orderId);
    }

    const Order& order = orderIt->second;
    const string& securityId = order.securityId();
    const string& user = order.user();
    ordersReadLock.unlock(); // Release read lock

    // Acquire write locks for modification
    unique_lock<shared_mutex> ordersWriteLock(ordersMutex_);
    unique_lock<shared_mutex> securityLock(securityIndexMutex_);
    unique_lock<shared_mutex> userLock(userIndexMutex_);

    // Re-validate order still exists (could have been deleted by another thread)
    orderIt = orders_.find(orderId);
    if (orderIt == orders_.end()) {
        throw out_of_range("Order ID not found (concurrently deleted): " + orderId);
    }

    // Remove from security index
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt != ordersBySecurity_.end()) {
        auto& secOrders = secIt->second;
        secOrders.erase(orderId);
        if (secOrders.empty()) {
            ordersBySecurity_.erase(secIt);
        }
    }

    // Remove from user index
    auto userIt = ordersByUser_.find(user);
    if (userIt != ordersByUser_.end()) {
        auto& userOrders = userIt->second;
        userOrders.erase(orderId);
        if (userOrders.empty()) {
            ordersByUser_.erase(userIt);
        }
    }

    // Remove from main storage
    orders_.erase(orderId);
}

void OrderCache::cancelOrdersForUser(const string& user) {
    // Get user's orders with read lock
    shared_lock<shared_mutex> userReadLock(userIndexMutex_);
    auto userIt = ordersByUser_.find(user);
    if (userIt == ordersByUser_.end()) {
        throw out_of_range("User ID not found: " + user);
    }

    // Copy order IDs while holding the lock
    auto orderIds = userIt->second;
    userReadLock.unlock();

    // Cancel each order individually (they handle their own locking)
    for (const auto& orderId : orderIds) {
        try {
            cancelOrder(orderId);
        }
        catch (const out_of_range&) {
            // Order might have been concurrently deleted - continue
        }
    }

    // Final cleanup - user entry should be empty now, but double-check
    unique_lock<shared_mutex> userWriteLock(userIndexMutex_);
    userIt = ordersByUser_.find(user);
    if (userIt != ordersByUser_.end() && userIt->second.empty()) {
        ordersByUser_.erase(userIt);
    }
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) {
    // Get security's orders with read lock
    shared_lock<shared_mutex> securityReadLock(securityIndexMutex_);
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        throw out_of_range("Security ID not found: " + securityId);
    }

    // Copy order IDs while holding the lock
    auto orderIds = secIt->second;
    securityReadLock.unlock();

    // Cancel qualifying orders
    for (const auto& orderId : orderIds) {
        try {
            // Check if order meets quantity criteria
            shared_lock<shared_mutex> ordersReadLock(ordersMutex_);
            auto orderIt = orders_.find(orderId);
            if (orderIt != orders_.end() && orderIt->second.qty() >= minQty) {
                ordersReadLock.unlock();
                cancelOrder(orderId);
            }
        }
        catch (const out_of_range&) {
            // Order might have been concurrently deleted - continue
        }
    }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const string& securityId) {
    // Use read locks for this query operation
    unordered_set<string> orderIdsCopy;
    {
        shared_lock<shared_mutex> securityLock(securityIndexMutex_);
        auto secIt = ordersBySecurity_.find(securityId);
        if (secIt == ordersBySecurity_.end()) {
            return 0;
        }
        orderIdsCopy = secIt->second; // Safe copy
    }

    // Step 2: Process orders (protected by read lock)
    shared_lock<shared_mutex> ordersLock(ordersMutex_);
    unordered_map<string, unsigned int> buyQtyPerCompany;
    unordered_map<string, unsigned int> sellQtyPerCompany;

    // Aggregate buy and sell quantities by company
    for (const string& oid : orderIdsCopy) {
        auto oit = orders_.find(oid);
        if (oit != orders_.end()) {
            const Order& o = oit->second;
            if (o.side() == "Buy") {
                buyQtyPerCompany[o.company()] += o.qty();
            }
            else if (o.side() == "Sell") {
                sellQtyPerCompany[o.company()] += o.qty();
            }
        }
    }

    // Calculate total sell quantity
    unsigned int totalSell = 0;
    for (const auto& sell : sellQtyPerCompany) {
        totalSell += sell.second;
    }

    // For each buying company, match against external sells
    unsigned int potentialMatch = 0;
    for (const auto& buy : buyQtyPerCompany) {
        const string& comp = buy.first;
        unsigned int buyQ = buy.second;

        unsigned int selfSell = sellQtyPerCompany.count(comp) ? sellQtyPerCompany.at(comp) : 0;
        unsigned int externalSell = (totalSell > selfSell) ? (totalSell - selfSell) : 0;

        potentialMatch += min(buyQ, externalSell);
    }

    unsigned int result = min(potentialMatch, totalSell);
    return result;
}

vector<Order> OrderCache::getAllOrders() const {
    vector<Order> result;
    shared_lock<shared_mutex> ordersLock(ordersMutex_); // Read lock for queries
    result.reserve(orders_.size());
    for (const auto& pair : orders_) {
        result.push_back(pair.second);
    }
    return result;
}

