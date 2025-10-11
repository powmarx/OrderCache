#include "OrderCache.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <sstream>
using namespace std;

void OrderCache::addOrder(Order order) {
    const std::string& orderId = order.orderId();

    // Use emplace to construct Order in-place, avoiding default constructor
    orders_.emplace(orderId, order);

    // Add to security index
    ordersBySecurity_[order.securityId()].push_back(orderId);

    // Add to user index
    ordersByUser_[order.user()].push_back(orderId);
}

void OrderCache::cancelOrder(const std::string& orderId) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
		stringstream ss;
		ss << "Order ID not found: " << orderId;
        throw std::out_of_range(ss.str().c_str());
    }

    const Order& order = it->second;

    // Remove from security index
    auto& secOrders = ordersBySecurity_[order.securityId()];
    secOrders.erase(std::remove(secOrders.begin(), secOrders.end(), orderId), secOrders.end());
    if (secOrders.empty()) {
        ordersBySecurity_.erase(order.securityId());
    }

    // Remove from user index
    auto& userOrders = ordersByUser_[order.user()];
    userOrders.erase(std::remove(userOrders.begin(), userOrders.end(), orderId), userOrders.end());
    if (userOrders.empty()) {
        ordersByUser_.erase(order.user());
    }

    // Remove from main storage
    orders_.erase(orderId);
}

void OrderCache::cancelOrdersForUser(const std::string& user) {
    auto it = ordersByUser_.find(user);
    if (it == ordersByUser_.end()) return;

    // For each order ID of this user
    for (const auto& orderId : it->second) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end()) {
            // Remove from security index
            auto& secOrders = ordersBySecurity_[orderIt->second.securityId()];
            secOrders.erase(std::remove(secOrders.begin(), secOrders.end(), orderId), secOrders.end());
            if (secOrders.empty()) {
                ordersBySecurity_.erase(orderIt->second.securityId());
            }

            // Remove from main storage
            orders_.erase(orderId);
        }
    }

    // Clear user index
    ordersByUser_.erase(user);
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) {
    auto it = ordersBySecurity_.find(securityId);
    if (it == ordersBySecurity_.end()) return;

    // Collect orders to remove
    std::vector<std::string> toRemove;
    for (const auto& orderId : it->second) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end() && orderIt->second.qty() >= minQty) {
            toRemove.push_back(orderId);
        }
    }   

    // Remove collected orders
    for (const auto& orderId : toRemove) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end()) {
            // Remove from user index
            auto& userOrders = ordersByUser_[orderIt->second.user()];
            userOrders.erase(std::remove(userOrders.begin(), userOrders.end(), orderId), userOrders.end());
            if (userOrders.empty()) {
                ordersByUser_.erase(orderIt->second.user());
            }

            // Remove from main storage
            orders_.erase(orderId);
        }
    }

    // Update security index
    it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
        [&](const std::string& id) {
            return std::find(toRemove.begin(), toRemove.end(), id) != toRemove.end();
        }),
        it->second.end());

    if (it->second.empty()) {
        ordersBySecurity_.erase(securityId);
    }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const std::string& securityId) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        return 0;
    }

    const auto& orderIds = secIt->second;

    std::unordered_map<std::string, unsigned long long> buyQtyPerCompany;
    std::unordered_map<std::string, unsigned long long> sellQtyPerCompany;

    // Aggregate buy and sell quantities by company
    for (const std::string& oid : orderIds) {
        auto oit = orders_.find(oid);
        if (oit != orders_.end()) {
            const Order& o = oit->second;
            if (o.side() == "Buy") {
                buyQtyPerCompany[o.company()] += static_cast<unsigned long long>(o.qty());
            }
            else if (o.side() == "Sell") {
                sellQtyPerCompany[o.company()] += static_cast<unsigned long long>(o.qty());
            }
        }
    }

    // Calculate total sell quantity
    unsigned long long totalSell = 0;
    for (const auto& p : sellQtyPerCompany) {
        totalSell += p.second;
    }

    // For each buying company, match against external sells
    unsigned long long potentialMatch = 0;
    for (const auto& p : buyQtyPerCompany) {
        const std::string& comp = p.first;
        unsigned long long buyQ = p.second;
        unsigned long long selfSell = sellQtyPerCompany.count(comp) ? sellQtyPerCompany.at(comp) : 0ULL;
        unsigned long long externalSell = (totalSell > selfSell) ? (totalSell - selfSell) : 0ULL;
        potentialMatch += std::min(buyQ, externalSell);
    }

    // Cap by total sell to handle shared sell quantities
    unsigned long long result = std::min(potentialMatch, totalSell);
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