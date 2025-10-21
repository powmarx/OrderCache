#include "OrderCache.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <sstream>

using namespace std;

// --- Private Helper Functions for Index Management ---

// @brief Updates the capacity and load factor for a newly created index set.
// This is moved out of the lambda to simplify the closure structure.
unordered_set<string>& OrderCache::updateIndexSet(
    unordered_set<string>& currentSet,
    const string& orderId,
    size_t capacity,
    bool is_insertion)
{
    // These calls are thread-safe as they run under the exclusive stripe lock provided by update().
    if (is_insertion) {
        // Performance optimization: Reserve only if empty.
        if (currentSet.empty()) currentSet.reserve(capacity);
        currentSet.max_load_factor(0.7F);
        currentSet.insert(orderId);
    }
    else {
        currentSet.erase(orderId);
    }
    return currentSet;
}

// --- CORE METHODS ---

void OrderCache::addOrder(Order order) {
    const string& orderId = order.orderId();
    const string& securityId = order.securityId();
    const string& user = order.user();

    // 1. Add to main storage (Safe: uses try_emplace, which locks the stripe)
    if (!(orders_.try_emplace(orderId, order))) {
        throw invalid_argument("Duplicate Order ID: " + orderId);
    };

    // --- Local Lambda for Insertion (Safe and Simplified) ---
    // This simple, local lambda returns by value (unordered_set) for the StripedThreadSafeMap::update assignment.
    auto insertion_lambda = [&](unordered_set<string>& currentSet, size_t capacity) {
        // Calls the stable, non-lambda helper function to perform the actual work.
        updateIndexSet(currentSet, orderId, capacity, true);
        return currentSet;
        };

    // 2. Update ordersBySecurity_ index (SAFE and Optimized)
    ordersBySecurity_.update_or_insert(securityId, [&](unordered_set<string>& currentSet) {
        return insertion_lambda(currentSet, 1000); // Max 10,000 orders per security
        });

    // 3. Update ordersByUser_ index (SAFE and Optimized)
    ordersByUser_.update_or_insert(user, [&](unordered_set<string>& currentSet) {
        return insertion_lambda(currentSet, 500); // Max 5,000 orders per user
        });
}

void OrderCache::cancelOrder(const string& orderId) {
    // 1. Single lookup in main storage
    auto order = orders_.get(orderId);
    if (!order.has_value()) {
        throw out_of_range("Order ID not found: " + orderId);
    }

    // We now have a thread-safe COPY of the order details.
    const string& securityId = order.value().securityId();
    const string& user = order.value().user();

    // --- Local Lambda for Erase (Safe and Simplified) ---
    auto erase_lambda = [&](const string& idToErase) {
        return [&](unordered_set<string>& currentSet) {
            updateIndexSet(currentSet, idToErase, 0, false); // false means erase
            return currentSet;
            };
        };

    // 2. Update ordersBySecurity_ index (Index cleanup BEFORE main data removal)
    ordersBySecurity_.update(securityId, erase_lambda(orderId));

    // 3. Update ordersByUser_ index (Index cleanup BEFORE main data removal)
    ordersByUser_.update(user, erase_lambda(orderId));

    // 4. Remove from main storage (Final step: ensures readers see a valid order until indexes are clean)
    orders_.erase(orderId);
}

void OrderCache::cancelOrdersForUser(const string& user) {
    // 1. Get a COPY of the set of order IDs for the user (Safe: shared_lock on user stripe)
    auto orderIds = ordersByUser_.get(user);

    if (orderIds.has_value()) {

        // 2. Remove the user's index entry (Immediate cleanup of user index for consistency)
        ordersByUser_.erase(user);

        // 3. Loop over the copied set and cancel each order
        // --- Local Lambda for Erase ---
        auto erase_lambda = [&](const string& idToErase) {
            return [&](unordered_set<string>& currentSet) {
                updateIndexSet(currentSet, idToErase, 0, false);
                return currentSet;
                };
            };

        for (const string& orderId : orderIds.value()) {
            auto order = orders_.get(orderId);
            if (order.has_value()) {
                const string& securityId = order.value().securityId();

                // 4. Update ordersBySecurity_ index (Index cleanup BEFORE main data removal)
                ordersBySecurity_.update(securityId, erase_lambda(orderId));

                // 5. Remove from main storage (Final step for this individual order)
                orders_.erase(orderId);
            }
        }
    }
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) {
    // 1. Get a COPY of the set of order IDs for the security
    auto orderIds = ordersBySecurity_.get(securityId);

    if (orderIds.has_value()) {
        vector<string> ordersToCancel;

        // 2. Identify orders to cancel
        for (const string& orderId : orderIds.value()) {
            auto order = orders_.get(orderId);
            if (order.has_value() && order.value().qty() >= minQty) {
                ordersToCancel.push_back(orderId);
            }
        }

        // 3. Clean up security index atomically for all orders (Index cleanup BEFORE main data removal)
        ordersBySecurity_.update(securityId, [&](unordered_set<string>& currentSet) {
            // Bulk erase within the single lock
            for (const string& id : ordersToCancel) {
                currentSet.erase(id);
            }
            return currentSet;
            });

        // 4. Perform individual order cancellations
        for (const string& orderId : ordersToCancel) {
            auto order = orders_.get(orderId);
            if (order.has_value()) {
                const string& user = order.value().user();

                // --- Local Lambda for Erase ---
                auto erase_lambda = [&](const string& idToErase) {
                    return [&](unordered_set<string>& currentSet) {
                        updateIndexSet(currentSet, idToErase, 0, false);
                        return currentSet;
                        };
                    };

                // a. Update ordersByUser_ index (Index cleanup BEFORE main data removal)
                ordersByUser_.update(user, erase_lambda(orderId));

                // b. Remove from main storage (Final step)
                orders_.erase(orderId);
            }
        }
    }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const string& securityId) {
    // 1. Get a COPY of the set of order IDs for the security
    auto orderIds = ordersBySecurity_.get(securityId);

    unordered_map<string, unsigned int> buyQtyPerCompany;
    unordered_map<string, unsigned int> sellQtyPerCompany;

    if (orderIds.has_value()) {
        for (const string& oid : orderIds.value()) {
            // Lookup order details (Safe: shared_lock on oid stripe)
            auto order = orders_.get(oid);
            if (order.has_value()) {
                const Order& o = order.value();
                const string& company = o.company();
                unsigned int qty = o.qty();

                if (o.side() == "Buy") {
                    buyQtyPerCompany[company] += qty;
                }
                else if (o.side() == "Sell") {
                    sellQtyPerCompany[company] += qty;
                }
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

        potentialMatch += min(buyQ, externalSell);
    }

    // Cap by total sell to handle shared sell quantities
    unsigned int result = min(potentialMatch, totalSell);
    return result;
}

vector<Order> OrderCache::getAllOrders() const {
    // Safe: Calls StripedThreadSafeMap::values(), which locks all stripes for a consistent snapshot
    return orders_.values();
}