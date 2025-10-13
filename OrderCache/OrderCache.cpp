#include "OrderCache.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <sstream>

void OrderCache::addOrder(Order order) {
    const string& orderId = order.orderId();
    const string& securityId = order.securityId();
    const string& user = order.user();// Add to main storage
    
    if (!(orders_.try_emplace(orderId, order)).second) {
		throw invalid_argument("Duplicate Order ID: " + orderId);
    }; // O(1) average case

    // Update indexes with collision mitigation
    auto& secSet = ordersBySecurity_[securityId];
    if (secSet.empty()) secSet.reserve(50000); // Preallocate for ~50,000 orders
    secSet.max_load_factor(0.7F); // Reduce collisions
    secSet.insert(orderId); // O(1) average case

    auto& userSet = ordersByUser_[user];
    if (userSet.empty()) userSet.reserve(50000); // Preallocate for ~50,000 orders
    userSet.max_load_factor(0.7F); // Reduce collisions
    userSet.insert(orderId); // O(1) average case
}

void OrderCache::cancelOrder(const string& orderId) {
    // Single lookup in main storage
    auto orderIt = orders_.find(orderId);
    if (orderIt == orders_.end()) {
        throw out_of_range("Order ID not found: " + orderId);
    }

    const Order& order = orderIt->second;
    const string& securityId = order.securityId();
    const string& user = order.user();

    // Optimize security index removal O(1)
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt != ordersBySecurity_.end()) {
        auto& secOrders = secIt->second;
        // Use unordered_set for O(1) removal
        secOrders.erase(orderId);
		// For efficiency, only erase the map entry if the set is empty
        if (secOrders.empty()) {
            ordersBySecurity_.erase(secIt);
        }
    }

    // Optimize user index removal
    auto userIt = ordersByUser_.find(user);
    if (userIt != ordersByUser_.end()) {
        auto& userOrders = userIt->second;
        userOrders.erase(orderId);
        if (userOrders.empty()) {
            ordersByUser_.erase(userIt);
        }
    }

    // Remove from main storage last (for exception safety)
    orders_.erase(orderIt);
}
void OrderCache::cancelOrdersForUser(const string& user) {
    auto userIt = ordersByUser_.find(user);
    if (userIt == ordersByUser_.end()) {
        throw out_of_range("User ID not found: " + user);
    }

    // Get copy of order IDs to avoid modifying while iterating
    auto orderIds = userIt->second; // copy of the set

    // Remove each order using the existing cancelOrder logic
    for (const auto& orderId : orderIds) {
        // Reuse the same pattern as cancelOrder
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end()) {
            const Order& order = orderIt->second;
            const string& securityId = order.securityId();

            // Remove from security index
            auto secIt = ordersBySecurity_.find(securityId);
            if (secIt != ordersBySecurity_.end()) {
                auto& secOrders = secIt->second;
                secOrders.erase(orderId);
                if (secOrders.empty()) {
                    ordersBySecurity_.erase(secIt);
                }
            }

            // Remove from main storage
            orders_.erase(orderIt);
        }
    }

    // Remove the user index entry
    ordersByUser_.erase(userIt);
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        throw out_of_range("Security ID not found: " + securityId);
    }

    auto orderIds = secIt->second;

    for (const auto& orderId : orderIds) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end() && orderIt->second.qty() >= minQty) {
            const Order& order = orderIt->second;
            const string& user = order.user();

            auto userIt = ordersByUser_.find(user);
            if (userIt != ordersByUser_.end()) {
                auto& userOrders = userIt->second;
                userOrders.erase(orderId);
                if (userOrders.empty()) {
                    ordersByUser_.erase(userIt);
                }
            }

            // Remove from main storage
            orders_.erase(orderIt);

            // Remove from the security index (current set we're iterating on copy)
            secIt->second.erase(orderId);
        }
    }

    // Clean up empty security entry
    if (secIt->second.empty()) {
        ordersBySecurity_.erase(secIt);
    }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const string& securityId) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        return 0;
    }

    const auto& orderIds = secIt->second;

    unordered_map<string, unsigned int> buyQtyPerCompany;
    unordered_map<string, unsigned int> sellQtyPerCompany;

	// Reserve space to reduce rehashing
    buyQtyPerCompany.reserve(orderIds.size()); // Estimate companies
    sellQtyPerCompany.reserve(orderIds.size());

    // Aggregate buy and sell quantities by company
    for (const string& oid : orderIds) {
        auto oit = orders_.find(oid);
        if (oit != orders_.end()) {
            const Order& o = oit->second;
            const string& company = o.company(); // Reference, not copy
            unsigned int qty = o.qty();

            if (o.side() == "Buy") {
                buyQtyPerCompany[company] += qty;
            }
            else if (o.side() == "Sell") {
                sellQtyPerCompany[company] += qty;
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

		// For matching requirements, only consider sells from other companies
        unsigned int selfSell = sellQtyPerCompany.count(comp) ? sellQtyPerCompany.at(comp) : 0;
        unsigned int externalSell = (totalSell > selfSell) ? (totalSell - selfSell) : 0;
        
        // Sels only with buyers needs!
        potentialMatch += min(buyQ, externalSell);
    }

    // Cap by total sell to handle shared sell quantities
    unsigned int result = min(potentialMatch, totalSell);
    return result;
}

vector<Order> OrderCache::getAllOrders() const {
    vector<Order> result;
    result.reserve(orders_.size());
    for (const auto& pair : orders_) {
        result.push_back(pair.second);
    }
    return result;
}