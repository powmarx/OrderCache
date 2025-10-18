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
    
    if (!(orders_.try_emplace(orderId, order))) {
		throw invalid_argument("Duplicate Order ID: " + orderId);
    }; // O(1) average case

    // Update indexes with collision mitigation
    auto& secSet = ordersBySecurity_[securityId];
    if (secSet.empty()) secSet.reserve(10000); // Preallocate for ~10,000 orders
    secSet.max_load_factor(0.7F); // Reduce collisions
    secSet.insert(orderId); // O(1) average case

    auto& userSet = ordersByUser_[user];
    if (userSet.empty()) userSet.reserve(5000); // Preallocate for ~10,000 orders
    userSet.max_load_factor(0.7F); // Reduce collisions
    userSet.insert(orderId); // O(1) average case
}

void OrderCache::cancelOrder(const string& orderId) {
    // Single lookup in main storage
    if(!orders_.contains(orderId)){
        throw out_of_range("Order ID not found: " + orderId);
    }

    const Order& order = orders_.at(orderId);
    const string& securityId = order.securityId();
    const string& user = order.user();

    // Optimize security index removal O(1)
    if (ordersBySecurity_.contains(securityId)) {
        auto& secOrders = ordersBySecurity_[securityId];
        // Use unordered_set for O(1) removal
        secOrders.erase(orderId);
		// For efficiency, only erase the map entry if the set is empty
        if (secOrders.empty()) {
            ordersBySecurity_.erase(securityId);
        }
    }

    // Optimize user index removal
    if (ordersByUser_.contains(user)) {
        auto& userOrders = ordersByUser_[user];
        userOrders.erase(orderId);
        if (userOrders.empty()) {
            ordersByUser_.erase(user);
        }
    }

    // Remove from main storage last (for exception safety)
    orders_.erase(orderId);
}
void OrderCache::cancelOrdersForUser(const string& user) {
    if (!ordersByUser_.contains(user)) {
        throw out_of_range("User ID not found: " + user);
    }

    // Get copy of order IDs to avoid modifying while iterating
    auto& orderIds = ordersByUser_[user];

    // Remove each order using the existing cancelOrder logic
    for (const string& orderId : orderIds) {
        // Reuse the same pattern as cancelOrder
		cancelOrder(orderId); // Reuse existing method for consistency and safety
    }

}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) {
    if (!ordersBySecurity_.contains(securityId)) {
        throw out_of_range("Security ID not found: " + securityId);
    }

    auto orderIds = ordersBySecurity_[securityId];

    for (const auto& orderId : orderIds) {
        if (orders_.contains(orderId)) {
			const auto& order = orders_[orderId];
            if (order.qty() >= minQty) {

                cancelOrder(orderId); // Reuse existing method for consistency and safety
            }
        }
    }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const string& securityId) {
	if (!ordersBySecurity_.contains(securityId) ){
		// Could be an error code or -1 for not found, but no specs about it
        return 0;
    }

	const auto& orderIds = ordersBySecurity_[securityId];

    unordered_map<string, unsigned int> buyQtyPerCompany;
    unordered_map<string, unsigned int> sellQtyPerCompany;

	// Reserve space to reduce rehashing
    buyQtyPerCompany.reserve(orderIds.size()); // Estimate companies
    sellQtyPerCompany.reserve(orderIds.size());

    // Aggregate buy and sell quantities by company
    for (const string& oid : orderIds) {
        if (orders_.contains(oid)) {
            const Order& o = orders_[oid];
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
    for (const auto& order: orders_.values()) {
        result.push_back(order);
    }
    return result;
}