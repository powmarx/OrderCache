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
    CrossMutexType* shared_cross_mutex_ = nullptr;
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

    // Improved hash distribution with bounds checking
    StripedBucket& get_bucket_for_key(const Key& key) const {
        Hash hasher;
        size_t index = hasher(key) % StripeCount;
        return const_cast<StripedBucket&>(buckets[index]);
    }

    // Get appropriate mutex for operations
    template<typename LockType>
    auto get_lock(StripedBucket& bucket) const {
        if (shared_cross_mutex_) {
            return LockType(*shared_cross_mutex_);  // Lock cross mutex
        }
        else {
            return LockType(bucket.mutex);  // Lock bucket mutex
        }
    }

    // Helper: whether a key is logically deleted (assumes caller holds at least shared/exclusive lock per get_lock policy)
    bool is_marked_locked(StripedBucket& bucket, const Key& key) const {
        return bucket.deleted_keys.find(key) != bucket.deleted_keys.end();
    }

    // DEADLOCK-SAFE HELPER: lock all buckets in deterministic index order and keep locks alive
    // Using a fixed lock acquisition order prevents cyclic waits among threads.
    template <typename LockType>
    std::vector<LockType> lock_all_buckets() const {
        std::vector<LockType> locks;
        locks.reserve(StripeCount);
        for (size_t i = 0; i < StripeCount; ++i) {
            locks.emplace_back(buckets[i].mutex);
        }
        return locks;
    }

    // internal purge function (assumes caller handles locking strategy if needed)
    void purge_marked_internal() {
        if (is_shared_cross_mutex_enabled()) {
            // Single cross mutex protects entire structure
            UniqueLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                size_type removed = bucket.deleted_keys.size();
                for (const auto& k : bucket.deleted_keys) {
                    bucket.data.erase(k);
                }
                bucket.deleted_keys.clear();
                // adjust tombstone_count_
                if (removed) tombstone_count_.fetch_sub(removed, std::memory_order_relaxed);
            }
        }
        else {
            // DEADLOCK-SAFE: acquire all bucket locks in index order
            auto locks = lock_all_buckets<UniqueLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                auto& bucket = buckets[i];
                size_type removed = bucket.deleted_keys.size();
                for (const auto& k : bucket.deleted_keys) {
                    bucket.data.erase(k);
                }
                bucket.deleted_keys.clear();
                if (removed) tombstone_count_.fetch_sub(removed, std::memory_order_relaxed);
            }
        }
        // Optionally call notification callback to indicate number changed
        maybe_notify();
    }

    // thread loop for auto purge
    void auto_purge_loop() {
        std::unique_lock<std::mutex> ctl_lock(auto_purge_control_mutex_);
        while (auto_purge_running_.load()) {
            // If we are only purging when threshold exceeded, check first
            if (auto_purge_only_when_threshold_exceeded_.load()) {
                if (tombstone_count_.load(std::memory_order_relaxed) < purge_threshold_.load()) {
                    // wait interval or stop
                    if (auto_purge_interval_.count() == 0) break;
                    auto_purge_cv_.wait_for(ctl_lock, auto_purge_interval_, [this] { return !auto_purge_running_.load(); });
                    continue;
                }
            }
            // purge now
            purge_marked_internal();

            // wait for next interval or stop
            if (auto_purge_interval_.count() == 0) break;
            auto_purge_cv_.wait_for(ctl_lock, auto_purge_interval_, [this] { return !auto_purge_running_.load(); });
        }
    }

    // notify callback if registered and threshold reached
    void maybe_notify() {
        std::function<void(size_type)> cb_copy;
        size_type current = tombstone_count_.load(std::memory_order_relaxed);
        size_type threshold = notification_threshold_.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(callback_mutex_);
            if (!tombstone_notify_callback_) return;
            // copy callback to invoke outside of lock
            cb_copy = tombstone_notify_callback_;
        }
        if (threshold == 0) {
            // immediate notify for any positive count
            if (current > 0) {
                // call async to avoid blocking map operations
                std::thread([cb_copy, current] { cb_copy(current); }).detach();
            }
        }
        else {
            if (current >= threshold) {
                std::thread([cb_copy, current] { cb_copy(current); }).detach();
            }
        }
    }

public:
    // @brief For multithreaded safety, operator[] is deleted
    Value& operator[](const Key& key) = delete;
    StripedThreadSafeMap(StripedThreadSafeMap& other) = delete;
    StripedThreadSafeMap(StripedThreadSafeMap&& other) = delete;
    StripedThreadSafeMap& operator=(StripedThreadSafeMap&& other) = delete;
    StripedThreadSafeMap(const StripedThreadSafeMap& other) = delete;

    ~StripedThreadSafeMap() {
        stop_auto_purge();
        clear_shared_cross_mutex();
    }

    // @brief Default constructor
    StripedThreadSafeMap() = default;

    // @brief Constructor with initializer list
	// @param init Initializer list of key-value pairs to insert
    StripedThreadSafeMap(std::initializer_list<value_type> init) {
        for (const auto& pair : init) {
            insert(pair.first, pair.second);
        }
    }

    // @brief Enable/disable cross mutex
	// @param shared_mutex Reference to external cross mutex
    explicit StripedThreadSafeMap(CrossMutexType& shared_mutex)
        : shared_cross_mutex_(&shared_mutex), use_shared_mutex_(true) {
    }

    // @brief Method to set shared mutex
	// @param shared_mutex Reference to external cross mutex
    void set_shared_cross_mutex(CrossMutexType& shared_mutex) {
        // NOTE: switching modes while other threads are active can be sensitive;
        // if needed, acquire all bucket locks here before flipping (omitted to match original semantics).
        shared_cross_mutex_ = &shared_mutex;
        use_shared_mutex_ = true;
    }

	// @brief Clear shared cross mutex usage
    void clear_shared_cross_mutex() {
        // See note above about switching modes; we keep original semantics here.
        use_shared_mutex_ = false;
        shared_cross_mutex_ = nullptr;
    }

	// @brief Check if shared cross mutex is enabled
    bool is_shared_cross_mutex_enabled() const {
        return use_shared_mutex_ && shared_cross_mutex_ != nullptr;
    }

    // ----------------------------
    // Purge policy & notification control
    // ----------------------------
    // @brief Set the threshold for purging. If >0 you may enable threshold-driven auto-purge.
	// @param threshold Number of tombstones to trigger purge
    void set_purge_threshold(size_type threshold) {
        purge_threshold_.store(threshold, std::memory_order_relaxed);
    }

    // @brief Configure auto-purge behavior: interval (ms) and whether purge only when threshold exceeded.
    // interval_ms == 0 disables auto-purge.
	// @param interval Purge interval in milliseconds
	// @param only_when_threshold_exceeded If true, purge only when threshold exceeded
    void start_auto_purge(std::chrono::milliseconds interval, bool only_when_threshold_exceeded = true) {
        stop_auto_purge();
        if (interval.count() == 0) return;
        auto_purge_interval_ = interval;
        auto_purge_only_when_threshold_exceeded_.store(only_when_threshold_exceeded);
        auto_purge_running_.store(true);
        auto_purge_thread_ = std::thread([this] { this->auto_purge_loop(); });
    }

	// @brief Stop auto-purge thread if running
    void stop_auto_purge() {
        if (!auto_purge_running_.load()) return;
        {
            std::lock_guard<std::mutex> lk(auto_purge_control_mutex_);
            auto_purge_running_.store(false);
        }
        auto_purge_cv_.notify_all();
        if (auto_purge_thread_.joinable()) auto_purge_thread_.join();
    }

    // @brief Configure a notification callback and threshold. If threshold==0 any positive tombstone_count triggers notification.
    // The callback is invoked asynchronously (detached thread).
	// @param cb Callback function taking current tombstone count
	// @param notify_threshold Threshold to trigger notification
    void set_tombstone_notify(std::function<void(size_type)> cb, size_type notify_threshold = 0) {
        std::lock_guard<std::mutex> lk(callback_mutex_);
        tombstone_notify_callback_ = std::move(cb);
        notification_threshold_.store(notify_threshold, std::memory_order_relaxed);
    }

	// @brief Clear notification callback
    void clear_tombstone_notify() {
        std::lock_guard<std::mutex> lk(callback_mutex_);
        tombstone_notify_callback_ = nullptr;
        notification_threshold_.store(0, std::memory_order_relaxed);
    }

    // ----------------------------
    // Core operations (soft-delete support)
    // ----------------------------

    // @brief Insert or update a key-value pair (real insertion/unmark if previously marked).
	// On insert we remove tombstone if present -> resurrect/replace the entry
	// @param key Key to insert/update
	// @param value Value to insert/update
    void insert(const Key& key, const Value& value) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);
        // On insert we remove tombstone if present -> resurrect/replace the entry
        size_type erased = bucket.deleted_keys.erase(key);
        if (erased) {
            tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
        }
        bucket.data[key] = value;
    }

    // @brief Insert or update a key-value pair specialized for container-like values (merging), unmarks if present.
    //  Assumes Value supports insert(begin, end) method (e.g., std::vector, std::unordered_set)
	// @param key Key to insert/update
	// @param value Container-like Value to merge/insert
	// @param capacity Initial capacity for container if newly created
	// @param load_factor Load factor for container if newly created to avoid rehashes
	// Default capacity and load factor can be specified for initial allocation
    void insert_list(const Key& key, const Value& value, int capacity = 10000, float load_factor = 0.7F) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);

        if (bucket.data.empty() && capacity > 0) {
            bucket.data.reserve(capacity);
            bucket.data.max_load_factor(load_factor);
        }
        auto it = bucket.data.find(key);
        if (it != bucket.data.end()) {
            it->second.insert(value.begin(), value.end());
        }
        else {
            // remove tombstone if present
            size_type erased = bucket.deleted_keys.erase(key);
            if (erased) {
                tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
            }
            bucket.data[key] = value;
        }
    }

    // @brief Emplace a key-value pair (unmarks if present)
	// @param key Key to emplace/update
	// @param args Arguments to forward to Value constructor
    template<typename... Args>
    void emplace(const Key& key, Args&&... args) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);
        size_type erased = bucket.deleted_keys.erase(key);
        if (erased) {
            tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
            bucket.data[key] = Value(std::forward<Args>(args)...);
            return;
        }
        auto result = bucket.data.emplace(key, std::forward<Args>(args)...);
        (void)result;
    }

    // @brief Try to emplace a key-value pair or update if mark deleted
	// @param key Key to emplace/update
	// @param args Arguments to forward to Value constructor
	// @return true if newly inserted, false if key already existed and was not marked deleted
    template<typename... Args>
    bool try_emplace(const Key& key, Args&&... args) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);
        size_type erased = bucket.deleted_keys.erase(key);
        if (erased) {
            tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
            bucket.data[key] = Value(std::forward<Args>(args)...);
            return true;
        }
        auto result = bucket.data.try_emplace(key, std::forward<Args>(args)...);
        return result.second;
    }

    // @brief Get value for key with bounds checking (const version)
	// @param key Key to lookup
    // @return Returns Value{} when not found or marked deleted.
    const Value at(const Key& key) const {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<std::shared_lock<CrossMutexType>>(bucket);
        auto it = bucket.data.find(key);
        if (it != bucket.data.end() && !is_marked_locked(bucket, key)) {
            return it->second;
        }
        return Value{};
    }

    // @brief Check if key exists and is not marked deleted
	// @param key Key to check
	// @return true if key exists and is not marked deleted
    bool contains(const Key& key) const {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<std::shared_lock<CrossMutexType>>(bucket);
        auto it = bucket.data.find(key);
        return (it != bucket.data.end()) && !is_marked_locked(bucket, key);
    }

    // @brief Remove key from map (real deletion). If mark_only==true will only mark deleted (soft-delete)
    // IMPORTANT (concurrency): when mark_only==true, we return true ONLY if we newly marked the key (tombstone inserted).
    // If the key was already tombstoned by another thread, this returns false, preventing double counting.
    bool erase(const Key& key, bool mark_only = true) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);

        auto it = bucket.data.find(key);
        if (it == bucket.data.end()) {
            // Nothing to delete or mark
            return false;
        }

        if (mark_only) {
            // soft delete: add to tombstones if not already present
            auto p = bucket.deleted_keys.insert(key);
            if (p.second) {
                tombstone_count_.fetch_add(1, std::memory_order_relaxed);
                maybe_notify();
            }
            // Return true ONLY if we actually transitioned to marked-deleted
            return p.second;
        }
        else {
            // real deletion: remove from data and tombstones (if present)
            size_type erased_tomb = bucket.deleted_keys.erase(key);
            if (erased_tomb) tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
            bool removed = bucket.data.erase(key) > 0;
            return removed;
        }
    }

    // @brief Mark a key as deleted (soft delete). Does not remove the value; returns true if key existed and was marked.
    bool mark_deleted(const Key& key) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);
        auto it = bucket.data.find(key);
        if (it == bucket.data.end()) return false;
        auto p = bucket.deleted_keys.insert(key);
        if (p.second) {
            tombstone_count_.fetch_add(1, std::memory_order_relaxed);
            maybe_notify();
            return true;
        }
        return false;
    }

    // @brief Unmark a key as deleted (logical undelete). Returns true if key was previously marked.
    bool unmark_deleted(const Key& key) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);

        size_type erased = bucket.deleted_keys.erase(key);
        if (erased) {
            tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    // @brief Get size of the map excluding soft-deleted entries
    size_type size() const {
        size_type total_size = 0;
        // choose locking strategy
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                total_size += (bucket.data.size() > bucket.deleted_keys.size()) ? (bucket.data.size() - bucket.deleted_keys.size()) : 0;
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                total_size += (buckets[i].data.size() > buckets[i].deleted_keys.size()) ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
            }
        }
        return total_size;
    }

    // @brief Check if map is empty (excluding soft-deleted entries)
    bool empty() const {
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                for (const auto& kv : bucket.data) {
                    if (bucket.deleted_keys.find(kv.first) == bucket.deleted_keys.end()) {
                        return false;
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& kv : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(kv.first) == buckets[i].deleted_keys.end()) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // @brief Clear all elements from the map (real deletion). If mark_only==true will only mark all existing keys deleted.
    void clear(bool mark_only = true) {
        if (is_shared_cross_mutex_enabled()) {
            UniqueLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                if (mark_only) {
                    for (const auto& kv : bucket.data) {
                        auto p = bucket.deleted_keys.insert(kv.first);
                        if (p.second) tombstone_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else {
                    bucket.data.clear();
                    size_type removed = bucket.deleted_keys.size();
                    bucket.deleted_keys.clear();
                    if (removed) {
                        tombstone_count_.fetch_sub(removed, std::memory_order_relaxed);
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<UniqueLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                auto& bucket = buckets[i];
                if (mark_only) {
                    for (const auto& kv : bucket.data) {
                        auto p = bucket.deleted_keys.insert(kv.first);
                        if (p.second) tombstone_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else {
                    bucket.data.clear();
                    size_type removed = bucket.deleted_keys.size();
                    bucket.deleted_keys.clear();
                    if (removed) {
                        tombstone_count_.fetch_sub(removed, std::memory_order_relaxed);
                    }
                }
            }
        }
        maybe_notify();
    }

    // @brief Permanently remove all keys previously marked as deleted (real deletion of tombstones)
    void purge_marked() {
        purge_marked_internal();
    }

    // ----------------------------
    // Metrics/API for tombstones
    // ----------------------------
    // Return total number of tombstoned keys across all buckets (fast atomic)
    size_type total_tombstones() const {
        return tombstone_count_.load(std::memory_order_relaxed);
    }

    // Return per-bucket tombstone counts (requires locking)
    std::vector<size_type> get_bucket_tombstone_counts() const {
        std::vector<size_type> out;
        out.reserve(StripeCount);
        // DEADLOCK-SAFE: lock all buckets in index order
        auto locks = lock_all_buckets<SharedLock>();
        for (size_t i = 0; i < StripeCount; ++i) {
            out.push_back(buckets[i].deleted_keys.size());
        }
        return out;
    }

    // @brief Apply a function to each key-value pair (const version), skipping soft-deleted entries
    template<typename Func>
    void for_each(Func func) const {
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end()) {
                        func(pair.first, pair.second);
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& pair : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end()) {
                        func(pair.first, pair.second);
                    }
                }
            }
        }
    }

    // @brief Find first element satisfying predicate, skipping soft-deleted elements
    template<typename Predicate>
    std::optional<value_type> find_if(Predicate pred) const {
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end() && pred(pair.first, pair.second)) {
                        return pair;
                    }
                }
            }
            return std::nullopt;
        }
        // DEADLOCK-SAFE: lock all buckets in index order
        auto locks = lock_all_buckets<SharedLock>();
        for (size_t i = 0; i < StripeCount; ++i) {
            for (const auto& pair : buckets[i].data) {
                if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end() && pred(pair.first, pair.second)) {
                    return pair;
                }
            }
        }
        return std::nullopt;
    }

    // @brief Count elements satisfying predicate, skipping soft-deleted elements
    template<typename Predicate>
    size_type count_if(Predicate pred) const {
        size_type count = 0;
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end() && pred(pair.first, pair.second)) {
                        ++count;
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& pair : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end() && pred(pair.first, pair.second)) {
                        ++count;
                    }
                }
            }
        }
        return count;
    }

    // @brief Atomically update a value for a key (does NOT update if key is soft-deleted)
    template<typename Func>
    bool update(const Key& key, Func update_func) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);
        auto it = bucket.data.find(key);
        if (it != bucket.data.end() && !is_marked_locked(bucket, key)) {
            it->second = update_func(it->second);
            return true;
        }
        return false;
    }

    // @brief Thread-safe copy of all keys (skips soft-deleted)
    std::vector<Key> keys() const {
        std::vector<Key> result;
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            size_type total_size = 0;
            for (auto& bucket : buckets) {
                total_size += (bucket.data.size() > bucket.deleted_keys.size()) ? (bucket.data.size() - bucket.deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end()) {
                        result.push_back(pair.first);
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            size_type total_size = 0;
            for (size_t i = 0; i < StripeCount; ++i) {
                total_size += (buckets[i].data.size() > buckets[i].deleted_keys.size()) ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& pair : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end()) {
                        result.push_back(pair.first);
                    }
                }
            }
        }
        return result;
    }

    // Safe variadic erase_map: checks existence of all keys first, then erases them
    // Note: This operates on the container Value associated with a key: it uses erase on the contained container.
    // If you use soft-delete for the parent key, behavior remains consistent (we operate only if key exists and not soft-deleted).
    template<typename... Args>
    bool erase_map(const Key& key, Args&&... args) {
        auto& bucket = get_bucket_for_key(key);
        auto lock = get_lock<UniqueLock>(bucket);

        auto it = bucket.data.find(key);
        if (it == bucket.data.end() || is_marked_locked(bucket, key)) {
            return false;
        }

        // Helper: expand parameter pack with index sequence
        bool all_found = true;

        // Create an array of bools to capture results
        using expander = int[];
        (void)expander {
            0, (
                all_found = all_found && (it->second.erase(std::forward<Args>(args)) > 0), 0
                )...
        };

        if (!all_found) {
            return false;
        }

        if (it->second.empty()) {
            bucket.data.erase(key);
            bucket.deleted_keys.erase(key);
        }

        return true;
    }

    // @brief Thread-safe copy of all values (skips soft-deleted)
    std::vector<Value> values() const {
        std::vector<Value> result;
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            size_type total_size = 0;
            for (auto& bucket : buckets) {
                total_size += (bucket.data.size() > bucket.deleted_keys.size()) ? (bucket.data.size() - bucket.deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end()) {
                        result.push_back(pair.second);
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            size_type total_size = 0;
            for (size_t i = 0; i < StripeCount; ++i) {
                total_size += (buckets[i].data.size() > buckets[i].deleted_keys.size()) ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& pair : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end()) {
                        result.push_back(pair.second);
                    }
                }
            }
        }
        return result;
    }

    // @brief Thread-safe copy of all key-value pairs (skips soft-deleted)
    std::vector<value_type> items() const {
        std::vector<value_type> result;
        if (is_shared_cross_mutex_enabled()) {
            SharedLock lock(*shared_cross_mutex_);
            size_type total_size = 0;
            for (auto& bucket : buckets) {
                total_size += (bucket.data.size() > bucket.deleted_keys.size()) ? (bucket.data.size() - bucket.deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (auto& bucket : buckets) {
                for (const auto& pair : bucket.data) {
                    if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end()) {
                        result.push_back(pair);
                    }
                }
            }
        }
        else {
            // DEADLOCK-SAFE: lock all buckets in index order
            auto locks = lock_all_buckets<SharedLock>();
            size_type total_size = 0;
            for (size_t i = 0; i < StripeCount; ++i) {
                total_size += (buckets[i].data.size() > buckets[i].deleted_keys.size()) ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
            }
            result.reserve(total_size);
            for (size_t i = 0; i < StripeCount; ++i) {
                for (const auto& pair : buckets[i].data) {
                    if (buckets[i].deleted_keys.find(pair.first) == buckets[i].deleted_keys.end()) {
                        result.push_back(pair);
                    }
                }
            }
        }
        return result;
    }

    // @brief Compute size without locking all stripes (approximate), accounting for tombstones if accessible
    size_type size_approximate() const {
        size_type total_size = 0;
        for (auto& bucket : buckets) {
            // Try to get shared lock without blocking
            if constexpr (std::is_same<CrossMutexType, std::shared_mutex>::value) {
                if (bucket.mutex.try_lock_shared()) {
                    size_t actual = bucket.data.size();
                    size_t tomb = bucket.deleted_keys.size();
                    if (actual > tomb) total_size += (actual - tomb);
                    bucket.mutex.unlock_shared();
                }
                else {
                    size_t actual = bucket.data.size();
                    size_t tomb = bucket.deleted_keys.size();
                    if (actual > tomb) total_size += (actual - tomb);
                }
            }
            else {
                // Fallback when CrossMutexType does not support shared locking
                size_t actual = bucket.data.size();
                size_t tomb = bucket.deleted_keys.size();
                if (actual > tomb) total_size += (actual - tomb);
            }
        }
        return total_size;
    }

    // @brief Get the current stripe count (useful for monitoring)
    constexpr size_t get_stripe_count() const noexcept {
        return StripeCount;
    }

    // @brief Get statistics about bucket distribution (useful for tuning). Returns sizes excluding soft-deleted.
    std::vector<size_type> get_bucket_sizes() const {
        std::vector<size_type> sizes;
        sizes.reserve(StripeCount);
        // DEADLOCK-SAFE: lock all buckets in index order
        auto locks = lock_all_buckets<SharedLock>();
        for (size_t i = 0; i < StripeCount; ++i) {
            size_type sz = (buckets[i].data.size() > buckets[i].deleted_keys.size()) ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
            sizes.push_back(sz);
        }
        return sizes;
    }
};

#endif // STRIPED_THREAD_SAFE_MAP_H_