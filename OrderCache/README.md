# OrderCache - multithreadv3

A thread-safe C++ order cache implementation using striped locking to support concurrent order updates, cancellations, and queries with reduced lock contention.

## Design

This branch uses `StripedThreadSafeMap` rather than a single globally locked container:

- Orders are stored in a striped concurrent map keyed by order ID.
- Security and user indexes are also stored in striped concurrent maps.
- Each key is assigned to a stripe, so operations on unrelated keys can proceed concurrently.
- Reads return safe copies of stored values, allowing the cache to release locks before longer calculations or multi-step cleanup.
- `getAllOrders()` obtains a consistent snapshot through the map's values operation.

`OrderCache` maintains three concurrent maps:

```text
orders_           order ID      -> Order
ordersBySecurity_ security ID   -> set of order IDs
ordersByUser_     user          -> set of order IDs
```

The security and user indexes use two stripes in this implementation. The primary order map uses its default stripe configuration.

## Features

- Add and cancel orders by order ID.
- Cancel all orders for a user.
- Cancel orders for a security when quantity meets a minimum threshold.
- Calculate matching quantity for a security while excluding same-company trades.
- Retrieve a consistent snapshot of all currently stored orders.
- Includes single-threaded tests, multithreaded tests, benchmarks, and stress tests.

## Requirements

- Visual Studio 2022 with the C++ desktop workload.
- C++17 support.
- Windows 10 SDK.

## Build and run

1. Open `OrderCache.sln` in Visual Studio.
2. Select the `Debug` or `Release` configuration and the `x64` platform.
3. Build the solution.
4. Run the resulting console application to execute the included tests.

### Build with CMake

CMake 3.20 or newer works with any C++17 compiler (MSVC, GCC, Clang). Visual Studio 2022 can open the folder directly. The `.sln`/`.vcxproj` build is unchanged.

```sh
cmake -S . -B build                 # add -DCMAKE_BUILD_TYPE=Release for an optimised build
cmake --build build                 # multi-config generators (VS): add --config Debug|Release
ctest --test-dir build              # GoogleTest suite; add -C Debug|Release on multi-config generators
```

| Target | What it is |
|---|---|
| `ordercache_core` | Static library: `OrderCache.cpp` plus headers |
| `ordercache_legacy_runner` | The original interactive test runner. `assert()` stays active in every configuration; set `-DORDERCACHE_LEGACY_KEEP_ASSERTS=OFF` to match the Visual Studio Release build |
| `ordercache_gtests` | GoogleTest executable (no tests yet) |

GoogleTest v1.14.0 is obtained in one of two modes:

- **Online** (default, `-DORDERCACHE_GTEST_MODE=ONLINE`): downloads the pinned release and verifies its SHA-256.
- **Offline** (`-DORDERCACHE_GTEST_MODE=OFFLINE`): never uses the network. Choose one source:
  - `-DORDERCACHE_GTEST_SOURCE=/path/googletest-1.14.0.tar.gz`: a local copy of the release archive, SHA-256 verified;
  - `-DORDERCACHE_GTEST_SOURCE=/path/googletest`: an extracted GoogleTest source tree;
  - no `ORDERCACHE_GTEST_SOURCE`: an installed package found by `find_package(GTest)` (vcpkg, `libgtest-dev`, or `CMAKE_PREFIX_PATH`).

Other options: `ORDERCACHE_BUILD_TESTS` and `ORDERCACHE_BUILD_LEGACY_RUNNER` (both `ON` by default).

The project file is `OrderCache/OrderCache.vcxproj`. The public interface is defined in `OrderCache/OrderCacheInterface.h`. The cache implementation is in `OrderCache/OrderCache.cpp`, and the striped map implementation is in `OrderCache/StripedThreadsafeMap.h`.

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

## Concurrency notes

Individual map operations are protected by the stripe associated with their key. Cache methods update indexes before removing the corresponding order from primary storage so readers do not observe an order whose index entries have already been removed. Multi-step operations work from copied order or index values and reacquire only the stripes needed for each update.

The striped map reduces contention but does not make an arbitrary sequence of separate cache calls atomic. Callers that require a larger transaction boundary must provide their own coordination.

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for the full text.
