include(GoogleTest)

# GoogleTest executable: ports of the original suites (one file per suite), plus
# OrderCacheBeyondOriginalTest.cpp, which goes beyond the original suite.
add_executable(ordercache_gtests
    OrderCacheSingleThreadTest.cpp
    OrderCacheStressTest.cpp
    OrderCacheConcurrencyTest.cpp
    OrderCacheBeyondOriginalTest.cpp  # beyond the original suite
    KnownIssues.h
    TestEnvironment.h)
target_link_libraries(ordercache_gtests PRIVATE OrderCache::core GTest::gtest_main)
ordercache_set_warnings(ordercache_gtests)

# Each TEST becomes its own CTest entry. The per-test TIMEOUT turns a deadlock
# into a test failure instead of a hung run.
gtest_discover_tests(ordercache_gtests
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 60)
