#pragma once

#include <string>
#include <vector>

class Order {
public:
	Order() = default;
    // do not alter signature of this constructor
    Order(const std::string& ordId, const std::string& secId, const std::string& side, const unsigned int qty, const std::string& user,
          const std::string& company)
        : m_orderId(ordId), m_securityId(secId), m_side(side), m_qty(qty), m_user(user), m_company(company) {}

    // do not alter these accessor methods
    std::string orderId() const { return m_orderId; }
    std::string securityId() const { return m_securityId; }
    std::string side() const { return m_side; }
    std::string user() const { return m_user; }
    std::string company() const { return m_company; }
    unsigned int qty() const { return m_qty; }

private:
    // use the below to hold the order data
    // do not remove these member variables
    std::string m_orderId    = "";  // unique order id
    std::string m_securityId = "";  // security identifier
    std::string m_side       = "";  // side of the order, eg Buy or Sell
    unsigned int m_qty       = 0;   // qty for this order
    std::string m_user       = "";  // user name who owns this order
    std::string m_company    = "";  // company for user
};

class OrderCacheInterface {
public:
    // implement the 6 methods below, do not alter signatures
    virtual void addOrder(Order order) = 0;
    virtual void cancelOrder(const std::string& orderId) = 0;
    virtual void cancelOrdersForUser(const std::string& user) = 0;
    virtual void cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) = 0;
    virtual unsigned int getMatchingSizeForSecurity(const std::string& securityId) = 0;
    virtual std::vector<Order> getAllOrders() const = 0;
};