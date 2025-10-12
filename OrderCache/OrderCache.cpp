#include "OrderCache.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <sstream>
using namespace std;

void OrderCache::addOrder(Order order) {
    const std::string& orderId = order.orderId();
    const std::string& securityId = order.securityId();
    const std::string& user = order.user();// Add to main storage
    orders_.emplace(orderId, order); // O(1) average case

    // Update indexes with collision mitigation
    auto& secSet = ordersBySecurity_[securityId];
    if (secSet.empty()) secSet.reserve(10000); // Preallocate for ~10,000 orders
    secSet.max_load_factor(0.7F); // Reduce collisions
    secSet.insert(orderId); // O(1) average case

    auto& userSet = ordersByUser_[user];
    if (userSet.empty()) userSet.reserve(5000); // Preallocate for ~5,000 orders
    userSet.max_load_factor(0.7F); // Reduce collisions
    userSet.insert(orderId); // O(1) average case
}

void OrderCache::cancelOrder(const std::string& orderId) {
    // Single lookup in main storage
    auto orderIt = orders_.find(orderId);
    if (orderIt == orders_.end()) {
        // Use pre-allocated exception or simpler construction
        throw std::out_of_range("Order ID not found: " + orderId);
    }

    const Order& order = orderIt->second;
    const std::string& securityId = order.securityId();
    const std::string& user = order.user();

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
void OrderCache::cancelOrdersForUser(const std::string& user) {
    auto userIt = ordersByUser_.find(user);
    if (userIt == ordersByUser_.end()) return;

    // Get copy of order IDs to avoid modifying while iterating
    auto orderIds = userIt->second; // copy of the set

    // Remove each order using the existing cancelOrder logic
    for (const auto& orderId : orderIds) {
        // Reuse the same pattern as cancelOrder
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end()) {
            const Order& order = orderIt->second;
            const std::string& securityId = order.securityId();

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

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) return;

    // Get copy to avoid modifying while iterating
    auto orderIds = secIt->second; // copy of the set

    for (const auto& orderId : orderIds) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end() && orderIt->second.qty() >= minQty) {
            const Order& order = orderIt->second;
            const std::string& user = order.user();

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

unsigned int OrderCache::getMatchingSizeForSecurity(const std::string& securityId) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        return 0;
    }

    const auto& orderIds = secIt->second;

    std::unordered_map<std::string, unsigned int> buyQtyPerCompany;
    std::unordered_map<std::string, unsigned int> sellQtyPerCompany;

    // Aggregate buy and sell quantities by company
    for (const std::string& oid : orderIds) {
        auto oit = orders_.find(oid);
        if (oit != orders_.end()) {
            const Order& o = oit->second;
            if (o.side() == "Buy") {
                buyQtyPerCompany[o.company()] += static_cast<unsigned int>(o.qty());
            }
            else if (o.side() == "Sell") {
                sellQtyPerCompany[o.company()] += static_cast<unsigned int>(o.qty());
            }
        }
    }

    // Calculate total sell quantity
    unsigned int totalSell = 0;
    for (const auto& p : sellQtyPerCompany) {
        totalSell += p.second;
    }

    // For each buying company, match against external sells
    unsigned int potentialMatch = 0;
    for (const auto& p : buyQtyPerCompany) {
        const std::string& comp = p.first;
        unsigned int buyQ = p.second;
        unsigned int selfSell = sellQtyPerCompany.count(comp) ? sellQtyPerCompany.at(comp) : 0ULL;
        unsigned int externalSell = (totalSell > selfSell) ? (totalSell - selfSell) : 0ULL;
        potentialMatch += std::min(buyQ, externalSell);
    }

    // Cap by total sell to handle shared sell quantities
    unsigned int result = std::min(potentialMatch, totalSell);
    return static_cast<unsigned int>(result);
}

std::vector<Order> OrderCache::getAllOrders() const {
    std::vector<Order> result;
    result.reserve(orders_.size());
    for (const auto& pair : orders_) {
        result.push_back(pair.second);
    }
    return result;
}