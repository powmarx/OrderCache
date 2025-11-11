#ifndef STRIPED_THREAD_SAFE_MAP_IMPL_INL_
#define STRIPED_THREAD_SAFE_MAP_IMPL_INL_

#include <utility>

// Implementation for StripedThreadSafeMap declared in StripedThreadSafeMap.h.
// This file is included at the end of the header to keep the template visible to TUs.

/* Private helpers and method implementations */

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::StripedBucket&
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_bucket_for_key(const Key& key) const {
    Hash hasher;
    size_t index = hasher(key) % StripeCount;
    return const_cast<StripedBucket&>(buckets[index]);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::UniqueLock
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_cross_unique_lock() const {
    if (auto sp = shared_cross_mutex_wp_.lock()) {
        return UniqueLock(*sp);
    }
    if (shared_cross_mutex_) {
        return UniqueLock(*shared_cross_mutex_);
    }
    return UniqueLock(buckets[0].mutex);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::SharedLock
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_cross_shared_lock() const {
    if (auto sp = shared_cross_mutex_wp_.lock()) {
        return SharedLock(*sp);
    }
    if (shared_cross_mutex_) {
        return SharedLock(*shared_cross_mutex_);
    }
    return SharedLock(buckets[0].mutex);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename LockType>
auto StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_lock(
    StripedBucket& bucket) const {
    if (auto sp = shared_cross_mutex_wp_.lock()) {
        return LockType(*sp);
    }
    if (shared_cross_mutex_) {
        return LockType(*shared_cross_mutex_);
    }
    return LockType(bucket.mutex);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::is_marked_locked(
    StripedBucket& bucket, const Key& key) const {
    return bucket.deleted_keys.find(key) != bucket.deleted_keys.end();
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template <typename LockType>
std::vector<LockType> StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::lock_all_buckets() const {
    std::vector<LockType> locks;
    locks.reserve(StripeCount);
    for (size_t i = 0; i < StripeCount; ++i) {
        locks.emplace_back(buckets[i].mutex);
    }
    return locks;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::purge_marked_internal() {
    if (is_shared_cross_mutex_enabled()) {
        UniqueLock lock = get_cross_unique_lock();
        for (auto& bucket : buckets) {
            size_type removed = bucket.deleted_keys.size();
            for (const auto& k : bucket.deleted_keys) {
                bucket.data.erase(k);
            }
            bucket.deleted_keys.clear();
            if (removed) tombstone_count_.fetch_sub(removed, std::memory_order_relaxed);
        }
    }
    else {
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
    maybe_notify();
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::auto_purge_loop() {
    std::unique_lock<std::mutex> ctl_lock(auto_purge_control_mutex_);
    while (auto_purge_running_.load()) {
        if (auto_purge_only_when_threshold_exceeded_.load()) {
            if (tombstone_count_.load(std::memory_order_relaxed) < purge_threshold_.load()) {
                if (auto_purge_interval_.count() == 0) break;
                auto_purge_cv_.wait_for(ctl_lock, auto_purge_interval_, [this] { return !auto_purge_running_.load(); });
                continue;
            }
        }
        purge_marked_internal();
        if (auto_purge_interval_.count() == 0) break;
        auto_purge_cv_.wait_for(ctl_lock, auto_purge_interval_, [this] { return !auto_purge_running_.load(); });
    }
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::maybe_notify() {
    std::function<void(size_type)> cb_copy;
    size_type current = tombstone_count_.load(std::memory_order_relaxed);
    size_type threshold = notification_threshold_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(callback_mutex_);
        if (!tombstone_notify_callback_) return;
        cb_copy = tombstone_notify_callback_;
    }
    if (threshold == 0) {
        if (current > 0) {
            std::thread([cb_copy, current] { cb_copy(current); }).detach();
        }
    }
    else {
        if (current >= threshold) {
            std::thread([cb_copy, current] { cb_copy(current); }).detach();
        }
    }
}

/* Public API implementations */

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::~StripedThreadSafeMap() {
    stop_auto_purge();
    clear_shared_cross_mutex();
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::StripedThreadSafeMap(
    std::initializer_list<value_type> init) {
    for (const auto& pair : init) {
        insert(pair.first, pair.second);
    }
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::StripedThreadSafeMap(
    CrossMutexType& shared_mutex)
    : shared_cross_mutex_(&shared_mutex), use_shared_mutex_(true) {
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::set_shared_cross_mutex(
    CrossMutexType& shared_mutex) {
    auto locks = lock_all_buckets<UniqueLock>();
    shared_cross_mutex_wp_.reset();
    shared_cross_mutex_ = &shared_mutex;
    use_shared_mutex_ = true;
    (void)locks;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::set_shared_cross_mutex(
    std::shared_ptr<CrossMutexType> shared_mutex) {
    auto locks = lock_all_buckets<UniqueLock>();
    shared_cross_mutex_wp_ = shared_mutex;
    shared_cross_mutex_ = nullptr;
    use_shared_mutex_ = true;
    (void)locks;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::clear_shared_cross_mutex() {
    auto locks = lock_all_buckets<UniqueLock>();
    use_shared_mutex_ = false;
    shared_cross_mutex_ = nullptr;
    shared_cross_mutex_wp_.reset();
    (void)locks;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::is_shared_cross_mutex_enabled() const {
    if (!use_shared_mutex_) return false;
    if (shared_cross_mutex_) return true;
    if (!shared_cross_mutex_wp_.expired()) return true;
    return false;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::set_purge_threshold(size_type threshold) {
    purge_threshold_.store(threshold, std::memory_order_relaxed);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::start_auto_purge(
    std::chrono::milliseconds interval, bool only_when_threshold_exceeded) {
    stop_auto_purge();
    if (interval.count() == 0) return;
    auto_purge_interval_ = interval;
    auto_purge_only_when_threshold_exceeded_.store(only_when_threshold_exceeded);
    auto_purge_running_.store(true);
    auto_purge_thread_ = std::thread([this] { this->auto_purge_loop(); });
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::stop_auto_purge() {
    if (!auto_purge_running_.load()) return;
    {
        std::lock_guard<std::mutex> lk(auto_purge_control_mutex_);
        auto_purge_running_.store(false);
    }
    auto_purge_cv_.notify_all();
    if (auto_purge_thread_.joinable()) auto_purge_thread_.join();
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::set_tombstone_notify(
    std::function<void(size_type)> cb, size_type notify_threshold) {
    std::lock_guard<std::mutex> lk(callback_mutex_);
    tombstone_notify_callback_ = std::move(cb);
    notification_threshold_.store(notify_threshold, std::memory_order_relaxed);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::clear_tombstone_notify() {
    std::lock_guard<std::mutex> lk(callback_mutex_);
    tombstone_notify_callback_ = nullptr;
    notification_threshold_.store(0, std::memory_order_relaxed);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::insert(
    const Key& key, const Value& value) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);
    size_type erased = bucket.deleted_keys.erase(key);
    if (erased) {
        tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    bucket.data[key] = value;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::insert_list(
    const Key& key, const Value& value, int capacity, float load_factor) {
    static_assert(
        stsm_has_range_insert<Value>::value,
        "insert_list requires Value to support insert(InputIt, InputIt)"
        );
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
        size_type erased = bucket.deleted_keys.erase(key);
        if (erased) {
            tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
        }
        bucket.data[key] = value;
    }
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename... Args>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::emplace(const Key& key, Args&&... args) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);
    size_type erased = bucket.deleted_keys.erase(key);
    if (erased) {
        tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
        bucket.data[key] = Value(std::forward<Args>(args)...);
        return;
    }
    (void)bucket.data.emplace(key, std::forward<Args>(args)...);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename... Args>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::try_emplace(
    const Key& key, Args&&... args) {
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
const Value StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::at(const Key& key) const {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<SharedLock>(bucket);
    auto it = bucket.data.find(key);
    if (it != bucket.data.end() && !is_marked_locked(bucket, key)) {
        return it->second;
    }
    return Value{};
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::optional<Value> StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::at_opt(const Key& key) const {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<SharedLock>(bucket);
    auto it = bucket.data.find(key);
    if (it != bucket.data.end() && !is_marked_locked(bucket, key)) {
        return it->second;
    }
    return std::nullopt;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::contains(const Key& key) const {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<SharedLock>(bucket);
    auto it = bucket.data.find(key);
    return (it != bucket.data.end()) && !is_marked_locked(bucket, key);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::erase(const Key& key, bool mark_only) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);

    auto it = bucket.data.find(key);
    if (it == bucket.data.end()) {
        return false;
    }

    if (mark_only) {
        auto p = bucket.deleted_keys.insert(key);
        if (p.second) {
            tombstone_count_.fetch_add(1, std::memory_order_relaxed);
            maybe_notify();
        }
        return p.second;
    }
    else {
        size_type erased_tomb = bucket.deleted_keys.erase(key);
        if (erased_tomb) tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
        bool removed = bucket.data.erase(key) > 0;
        return removed;
    }
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::mark_deleted(const Key& key) {
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::unmark_deleted(const Key& key) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);

    size_type erased = bucket.deleted_keys.erase(key);
    if (erased) {
        tombstone_count_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size() const {
    size_type total_size = 0;
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
        for (auto& bucket : buckets) {
            total_size += (bucket.data.size() > bucket.deleted_keys.size())
                ? (bucket.data.size() - bucket.deleted_keys.size()) : 0;
        }
    }
    else {
        auto locks = lock_all_buckets<SharedLock>();
        for (size_t i = 0; i < StripeCount; ++i) {
            total_size += (buckets[i].data.size() > buckets[i].deleted_keys.size())
                ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
        }
    }
    return total_size;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::empty() const {
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
        for (auto& bucket : buckets) {
            for (const auto& kv : bucket.data) {
                if (bucket.deleted_keys.find(kv.first) == bucket.deleted_keys.end()) {
                    return false;
                }
            }
        }
    }
    else {
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::clear(bool mark_only) {
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_unique_lock();
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::purge_marked() {
    purge_marked_internal();
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::total_tombstones() const {
    return tombstone_count_.load(std::memory_order_relaxed);
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::vector<typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_bucket_tombstone_counts() const {
    std::vector<size_type> out;
    out.reserve(StripeCount);
    auto locks = lock_all_buckets<SharedLock>();
    for (size_t i = 0; i < StripeCount; ++i) {
        out.push_back(buckets[i].deleted_keys.size());
    }
    return out;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename Func>
void StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::for_each(Func func) const {
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end()) {
                    func(pair.first, pair.second);
                }
            }
        }
    }
    else {
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename Predicate>
std::optional<typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::value_type>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::find_if(Predicate pred) const {
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end() && pred(pair.first, pair.second)) {
                    return pair;
                }
            }
        }
        return std::nullopt;
    }
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename Predicate>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::count_if(Predicate pred) const {
    size_type count = 0;
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                if (bucket.deleted_keys.find(pair.first) == bucket.deleted_keys.end() && pred(pair.first, pair.second)) {
                    ++count;
                }
            }
        }
    }
    else {
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename Func>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::update(const Key& key, Func update_func) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);
    auto it = bucket.data.find(key);
    if (it != bucket.data.end() && !is_marked_locked(bucket, key)) {
        it->second = update_func(it->second);
        return true;
    }
    return false;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::vector<Key> StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::keys() const {
    std::vector<Key> result;
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
template<typename... Args>
bool StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::erase_map(const Key& key, Args&&... args) {
    auto& bucket = get_bucket_for_key(key);
    auto lock = get_lock<UniqueLock>(bucket);

    auto it = bucket.data.find(key);
    if (it == bucket.data.end() || is_marked_locked(bucket, key)) {
        return false;
    }

    bool all_found = true;
    using expander = int[];
    (void)expander {
        0, (all_found = all_found && (it->second.erase(std::forward<Args>(args)) > 0), 0)...
    };

    if (!all_found) return false;

    if (it->second.empty()) {
        bucket.data.erase(key);
        bucket.deleted_keys.erase(key);
    }
    return true;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::vector<Value> StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::values() const {
    std::vector<Value> result;
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::vector<typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::value_type>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::items() const {
    std::vector<value_type> result;
    if (is_shared_cross_mutex_enabled()) {
        auto lock = get_cross_shared_lock();
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

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_approximate() const {
    size_type total_size = 0;
    for (auto& bucket : buckets) {
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
            size_t actual = bucket.data.size();
            size_t tomb = bucket.deleted_keys.size();
            if (actual > tomb) total_size += (actual - tomb);
        }
    }
    return total_size;
}

template<typename Key, typename Value, typename Hash, typename CrossMutexType, size_t StripeCount>
std::vector<typename StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::size_type>
StripedThreadSafeMap<Key, Value, Hash, CrossMutexType, StripeCount>::get_bucket_sizes() const {
    std::vector<size_type> sizes;
    sizes.reserve(StripeCount);
    auto locks = lock_all_buckets<SharedLock>();
    for (size_t i = 0; i < StripeCount; ++i) {
        size_type sz = (buckets[i].data.size() > buckets[i].deleted_keys.size())
            ? (buckets[i].data.size() - buckets[i].deleted_keys.size()) : 0;
        sizes.push_back(sz);
    }
    return sizes;
}

#endif // STRIPED_THREAD_SAFE_MAP_IMPL_INL_