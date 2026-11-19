// Compiled into ordercache_legacy_runner only when ORDERCACHE_LEGACY_KEEP_ASSERTS
// is ON. The legacy suite relies entirely on assert(); if NDEBUG is still defined
// here, the /UNDEBUG or -UNDEBUG option did not take effect and the suite would
// pass without checking anything, so fail the build instead.
#ifdef NDEBUG
#error "NDEBUG is defined for ordercache_legacy_runner: assert() would be compiled out."
#endif
