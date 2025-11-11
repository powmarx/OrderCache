#ifndef STRIPED_THREAD_SAFE_MAP_H_
#define STRIPED_THREAD_SAFE_MAP_H_

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <array>
#include <optional>
#include <functional>
#include <vector>
#include <shared_mutex>
#include <type_traits>
#include <algorithm>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <cassert>

/*
  NOTE:
    This header declares StripedThreadSafeMap. Implementation is separated into
    StripedThreadSafeMap_impl.inl which is included at the end of this file.
    This keeps the template header tidy while preserving header-only use.
    All original comments are preserved.
*/

template<typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename CrossMutexType = std::shared_mutex,
    size_t StripeCount = 16>
class StripedThreadSafeMap {
public:
    using SharedLock = std::shared_lock<CrossMutexType>;
    using UniqueLock = std::unique_lock<CrossMutexType>;
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = typename std::unordered_map<Key, Value, Hash>::size_type;

private:
    // raw pointer mode (backwards compatibility)
    CrossMutexType* shared_cross_mutex_ = nullptr;
    // optional shared_ptr-backed mode for safe lifetime
    std::weak_ptr<CrossMutexType> shared_cross_mutex_wp_;
    bool use_shared_mutex_ = false;

    struct StripedBucket {
        std::unordered_map<Key, Value, Hash> data;
        // tombstones / deleted markers (soft delete)
        std::unordered_set<Key, Hash> deleted_keys;
        mutable CrossMutexType mutex;
    };

    std::array<StripedBucket, StripeCount> buckets;

    // tombstone counter for fast atomic queries
    std::atomic<size_type> tombstone_count_{ 0 };

    // purge policy & auto-purge support
    std::atomic<bool> auto_purge_running_{ false };
    std::thread auto_purge_thread_;
    std::chrono::milliseconds auto_purge_interval_{ 0 };
    std::mutex auto_purge_control_mutex_;
    std::condition_variable auto_purge_cv_;
    // threshold-driven purge
    std::atomic<size_type> purge_threshold_{ 0 };
    std::atomic<bool> auto_purge_only_when_threshold_exceeded_{ false };

    // notification callback
    std::function<void(size_type)> tombstone_notify_callback_{ nullptr };
    std::atomic<size_type> notification_threshold_{ 0 };
    std::mutex callback_mutex_;

    // detection trait for insert_list (Phase A)
    template <typename V, typename = void>
    struct stsm_has_range_insert : std::false_type {};
    template <typename V>
    struct stsm_has_range_insert<V,
        std::void_t<
        decltype(std::declval<V&>().insert(std::declval<const V&>().begin(),
            std::declval<const V&>().end()))
        >
    > : std::true_type {
    };

    // Improved hash distribution with bounds checking
    StripedBucket& get_bucket_for_key(const Key& key) const;

    // Cross-mutex helpers
    UniqueLock get_cross_unique_lock() const;
    SharedLock get_cross_shared_lock() const;

    // Get appropriate mutex for operations
    template<typename LockType>
    auto get_lock(StripedBucket& bucket) const;

    // Helper: whether a key is logically deleted (assumes caller holds lock)
    bool is_marked_locked(StripedBucket& bucket, const Key& key) const;

    // DEADLOCK-SAFE HELPER: lock all buckets in deterministic index order and keep locks alive
    template <typename LockType>
    std::vector<LockType> lock_all_buckets() const;

    // internal purge function (assumes caller handles locking strategy if needed)
    void purge_marked_internal();

    // thread loop for auto purge
    void auto_purge_loop();

    // notify callback if registered and threshold reached
    void maybe_notify();

public:
    // @brief For multithreaded safety, operator[] is deleted
    Value& operator[](const Key& key) = delete;
    StripedThreadSafeMap(StripedThreadSafeMap& other) = delete;
    StripedThreadSafeMap(StripedThreadSafeMap&& other) = delete;
    StripedThreadSafeMap& operator=(StripedThreadSafeMap&& other) = delete;
    StripedThreadSafeMap(const StripedThreadSafeMap& other) = delete;

    ~StripedThreadSafeMap();

    // @brief Default constructor
    StripedThreadSafeMap() = default;

    // @brief Constructor with initializer list
    // @param init Initializer list of key-value pairs to insert
    StripedThreadSafeMap(std::initializer_list<value_type> init);

    // @brief Enable/disable cross mutex
    // @param shared_mutex Reference to external cross mutex
    explicit StripedThreadSafeMap(CrossMutexType& shared_mutex);

    // @brief Method to set shared mutex (raw reference overload)
    // @param shared_mutex Reference to external cross mutex
    void set_shared_cross_mutex(CrossMutexType& shared_mutex);

    // @brief Method to set shared mutex (shared_ptr overload)
    void set_shared_cross_mutex(std::shared_ptr<CrossMutexType> shared_mutex);

    // @brief Clear shared cross mutex usage (guarded)
    void clear_shared_cross_mutex();

    // @brief Check if shared cross mutex is enabled
    bool is_shared_cross_mutex_enabled() const;

    // ----------------------------
    // Purge policy & notification control
    // ----------------------------
    // @brief Set the threshold for purging. If >0 you may enable threshold-driven auto-purge.
    // @param threshold Number of tombstones to trigger purge
    void set_purge_threshold(size_type threshold);

    // @brief Configure auto-purge behavior: interval (ms) and whether purge only when threshold exceeded.
    // interval_ms == 0 disables auto-purge.
    // @param interval Purge interval in milliseconds
    // @param only_when_threshold_exceeded If true, purge only when threshold exceeded
    void start_auto_purge(std::chrono::milliseconds interval, bool only_when_threshold_exceeded = true);

    // @brief Stop auto-purge thread if running
    void stop_auto_purge();

    // @brief Configure a notification callback and threshold. If threshold==0 any positive tombstone_count triggers notification.
    // The callback is invoked asynchronously (detached thread).
    // @param cb Callback function taking current tombstone count
    // @param notify_threshold Threshold to trigger notification
    void set_tombstone_notify(std::function<void(size_type)> cb, size_type notify_threshold = 0);

    // @brief Clear notification callback
    void clear_tombstone_notify();

    // ----------------------------
    // Core operations (soft-delete support)
    // ----------------------------

    // @brief Insert or update a key-value pair (real insertion/unmark if previously marked).
    // On insert we remove tombstone if present -> resurrect/replace the entry
    // @param key Key to insert/update
    // @param value Value to insert/update
    void insert(const Key& key, const Value& value);

    // @brief Insert or update a key-value pair specialized for container-like values (merging), unmarks if present.
    //  Assumes Value supports insert(begin, end) method (e.g., std::vector, std::unordered_set)
    // @param key Key to insert/update
    // @param value Container-like Value to merge/insert
    // @param capacity Initial capacity for container if newly created
    // @param load_factor Load factor for container if newly created to avoid rehashes
    // Default capacity and load factor can be specified for initial allocation
    void insert_list(const Key& key, const Value& value, int capacity = 10000, float load_factor = 0.7F);

    // @brief Emplace a key-value pair (unmarks if present)
    // @param key Key to emplace/update
    // @param args Arguments to forward to Value constructor
    template<typename... Args>
    void emplace(const Key& key, Args&&... args);

    // @brief Try to emplace a key-value pair or update if mark deleted
    // @param key Key to emplace/update
    // @param args Arguments to forward to Value constructor
    // @return true if newly inserted, false if key already existed and was not marked deleted
    template<typename... Args>
    bool try_emplace(const Key& key, Args&&... args);

    // @brief Get value for key with bounds checking (const version)
    // @param key Key to lookup
    // @return Returns Value{} when not found or marked deleted.
    const Value at(const Key& key) const;

    // Optional-returning lookup (Phase A)
    std::optional<Value> at_opt(const Key& key) const;

    // @brief Check if key exists and is not marked deleted
    // @param key Key to check
    // @return true if key exists and is not marked deleted
    bool contains(const Key& key) const;

    // @brief Remove key from map (real deletion). If mark_only==true will only mark deleted (soft-delete)
    // IMPORTANT (concurrency): when mark_only==true, we return true ONLY if we newly marked the key (tombstone inserted).
    // If the key was already tombstoned by another thread, this returns false, preventing double counting.
    bool erase(const Key& key, bool mark_only = true);

    // @brief Mark a key as deleted (soft delete). Does not remove the value; returns true if key existed and was marked.
    bool mark_deleted(const Key& key);

    // @brief Unmark a key as deleted (logical undelete). Returns true if key was previously marked.
    bool unmark_deleted(const Key& key);

    // @brief Get size of the map excluding soft-deleted entries
    size_type size() const;

    // @brief Check if map is empty (excluding soft-deleted entries)
    bool empty() const;

    // @brief Clear all elements from the map (real deletion). If mark_only==true will only mark all existing keys deleted.
    void clear(bool mark_only = true);

    // @brief Permanently remove all keys previously marked as deleted (real deletion of tombstones)
    void purge_marked();

    // ----------------------------
    // Metrics/API for tombstones
    // ----------------------------
    // Return total number of tombstoned keys across all buckets (fast atomic)
    size_type total_tombstones() const;

    // Return per-bucket tombstone counts (requires locking)
    std::vector<size_type> get_bucket_tombstone_counts() const;

    // @brief Apply a function to each key-value pair (const version), skipping soft-deleted entries
    template<typename Func>
    void for_each(Func func) const;

    // @brief Find first element satisfying predicate, skipping soft-deleted elements
    template<typename Predicate>
    std::optional<value_type> find_if(Predicate pred) const;

    // @brief Count elements satisfying predicate, skipping soft-deleted elements
    template<typename Predicate>
    size_type count_if(Predicate pred) const;

    // @brief Atomically update a value for a key (does NOT update if key is soft-deleted)
    template<typename Func>
    bool update(const Key& key, Func update_func);

    // @brief Thread-safe copy of all keys (skips soft-deleted)
    std::vector<Key> keys() const;

    // Safe variadic erase_map: checks existence of all keys first, then erases them
    template<typename... Args>
    bool erase_map(const Key& key, Args&&... args);

    // @brief Thread-safe copy of all values (skips soft-deleted)
    std::vector<Value> values() const;

    // @brief Thread-safe copy of all key-value pairs (skips soft-deleted)
    std::vector<value_type> items() const;

    // @brief Compute size without locking all stripes (approximate), accounting for tombstones if accessible
    size_type size_approximate() const;

    // @brief Get the current stripe count (useful for monitoring)
    constexpr size_t get_stripe_count() const noexcept { return StripeCount; }

    // @brief Get statistics about bucket distribution (useful for tuning). Returns sizes excluding soft-deleted.
    std::vector<size_type> get_bucket_sizes() const;
};

// Include implementation
#include "StripedThreadSafeMap_impl.inl"

#endif // STRIPED_THREAD_SAFE_MAP_H_