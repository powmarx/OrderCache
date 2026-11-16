# OrderCache

A C++ order cache implementation that stores orders and supports efficient cancellation and matching-size queries by security.

## Features

- Add and cancel orders by order ID.
- Cancel all orders for a user.
- Cancel orders for a security when quantity meets a minimum threshold.
- Calculate the matching quantity for a security from buy and sell orders.
- Retrieve all currently stored orders.
- Includes benchmark, stress-test, and example test sources.

## Requirements

- Visual Studio 2022 with the C++ desktop workload.
- C++17 support.
- Windows 10 SDK.

## Build and run

1. Open `OrderCache.sln` in Visual Studio.
2. Select the `Debug` or `Release` configuration and the `x64` platform.
3. Build the solution.
4. Run the resulting console application to execute the included tests.

The project file is `OrderCache/OrderCache.vcxproj`. The public interface is defined in `OrderCache/OrderCacheInterface.h`, and the implementation is in `OrderCache/OrderCache.cpp`.

## API overview

`OrderCache` implements `OrderCacheInterface` with these operations:

```cpp
void addOrder(Order order);
void cancelOrder(const std::string& orderId);
void cancelOrdersForUser(const std::string& user);
void cancelOrdersForSecIdWithMinimumQty(
    const std::string& securityId,
    unsigned int minQty);
unsigned int getMatchingSizeForSecurity(const std::string& securityId);
std::vector<Order> getAllOrders() const;
```

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for the full text.
