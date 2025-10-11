#pragma once

#include "OrderCacheInterface.h"
#include <unordered_map>
#include <vector>
#include <string>

class OrderCache : public OrderCacheInterface {
private:
    // Main storage: map order ID to Order object for quick lookup
    std::unordered_map<std::string, Order> orders_;

    // Index by security ID to vectors of order IDs for matching
    std::unordered_map<std::string, std::vector<std::string>> ordersBySecurity_;

    // Index by user for quick cancellation
    std::unordered_map<std::string, std::vector<std::string>> ordersByUser_;

public:
    void addOrder(Order order) override;
    void cancelOrder(const std::string& orderId) override;
    void cancelOrdersForUser(const std::string& user) override;
    void cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) override;
    unsigned int getMatchingSizeForSecurity(const std::string& securityId) override;
    std::vector<Order> getAllOrders() const override;
};