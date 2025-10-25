#ifndef STRIPED_THREAD_SAFE_MAP_H_
#define STRIPED_THREAD_SAFE_MAP_H_

#include <unordered_map>
#include <mutex>
#include <array>
#include <optional>
#include <functional>
#include <vector>
#include <shared_mutex>
#include <type_traits>
#include <algorithm>


template<typename Key, typename Value, typename Hash = std::hash<Key>, size_t StripeCount = 16>
class StripedThreadSafeMap {
private:
    struct StripedBucket {
        std::unordered_map<Key, Value, Hash> data;
        mutable std::shared_mutex mutex;
    };
    
    std::array<StripedBucket, StripeCount> buckets;

    // Improved hash distribution with bounds checking
    StripedBucket& get_bucket_for_key(const Key& key) const {
        Hash hasher;
        size_t index = hasher(key) % StripeCount;
        return const_cast<StripedBucket&>(buckets[index]);
    }

    void lock_all() {
        for (auto& bucket : buckets) {
            bucket.mutex.lock();
        }
    }

    void unlock_all() {
        for (auto& bucket : buckets) {
            bucket.mutex.unlock();
        }
    }

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = typename std::unordered_map<Key, Value, Hash>::size_type;

    // @brief Default constructor
    StripedThreadSafeMap() = default;

    // @brief Constructor with initializer list
    StripedThreadSafeMap(std::initializer_list<value_type> init) {
        for (const auto& pair : init) {
            insert(pair.first, pair.second);
        }
    }

    // @brief Insert or update a key-value pair
    void insert(const Key& key, const Value& value) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        bucket.data[key] = value;
    }

    // @brief Insert or update a key-value pair
    void insert_list(const Key& key, const Value& value, int capacity = 10000, float load_factor = 0.7F) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
		auto it = bucket.data.find(key);
        if (it != bucket.data.end()) {
            it->second.insert(value.begin(), value.end());
        }
        else
        {
			bucket.data.reserve(capacity);  
			bucket.data.max_load_factor(load_factor);
			bucket.data[key] = value;
        }
    }

    // @brief Emplace a key-value pair
    template<typename... Args>
    void emplace(const Key& key, Args&&... args) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        bucket.data.emplace(key, std::forward<Args>(args)...);
    }

    // @brief Try to emplace a key-value pair
    template<typename... Args>
    auto try_emplace(const Key& key, Args&&... args) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        return bucket.data.try_emplace(key, std::forward<Args>(args)...);
    }

    // @brief Get value for key with bounds checking (const version)
    std::optional<Value> at(const Key& key) const {
        auto& bucket = get_bucket_for_key(key);
        std::shared_lock lock(bucket.mutex);
        auto it = bucket.data.find(key);
        return it != bucket.data.end() ? std::optional<Value>(it->second) : std::nullopt;
    }
	
    // @brief Get value for key with bounds checking (non-const version)
    Value* at(const Key& key) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);

        auto it = bucket.data.find(key);

        // Returns a pointer to the value stored in the map's internal bucket. Safe.
        return it != bucket.data.end() ? &(it->second) : nullptr;
    }   
    // @brief Check if key existsb
    bool contains(const Key& key) const {
        auto& bucket = get_bucket_for_key(key);
        std::shared_lock lock(bucket.mutex);
        return bucket.data.find(key) != bucket.data.end();
    }

    // @brief Remove key from map
    bool erase(const Key& key) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        return bucket.data.erase(key) > 0;
    }

    // @brief Get size of the map
    size_type size() const {
        size_type total_size = 0;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            total_size += bucket.data.size();
        }
        return total_size;
    }

    // @brief Check if map is empty
    bool empty() const {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            if (!bucket.data.empty()) {
                return false;
            }
        }
        return true;
    }

    // @brief Clear all elements from the map
    void clear() {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            bucket.data.clear();
        }
    }

    // @brief Apply a function to each key-value pair (const version)
    template<typename Func>
    void for_each(Func func) const {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
        }
        
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                func(pair.first, pair.second);
            }
        }
    }

    // @brief Apply a function to each key-value pair (non-const version)
    template<typename Func>
    void for_each(Func func) {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
        }
        
        for (auto& bucket : buckets) {
            for (auto& pair : bucket.data) {
                func(pair.first, pair.second);
            }
        }
    }

    // @brief Find first element satisfying predicate
    template<typename Predicate>
    std::optional<value_type> find_if(Predicate pred) const {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            for (const auto& pair : bucket.data) {
                if (pred(pair.first, pair.second)) {
                    return pair;
                }
            }
        }
        return std::nullopt;
    }

    // @brief Count elements satisfying predicate
    template<typename Predicate>
    size_type count_if(Predicate pred) const {
        size_type count = 0;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            for (const auto& pair : bucket.data) {
                if (pred(pair.first, pair.second)) {
                    ++count;
                }
            }
        }
        return count;
    }

    // @brief Atomically update a value for a key
    template<typename Func>
    bool update(const Key& key, Func update_func) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        auto it = bucket.data.find(key);
        if (it != bucket.data.end()) {
            it->second = update_func(it->second);
            return true;
        }
        return false;
    }

    // @brief Get value or default if key doesn't exist
    Value get_or_default(const Key& key, const Value& default_value) const {
        auto& bucket = get_bucket_for_key(key);
        std::shared_lock lock(bucket.mutex);
        auto it = bucket.data.find(key);
        return it != bucket.data.end() ? it->second : default_value;
    }

    // @brief Insert if key doesn't exist, otherwise do nothing
    bool insert_if_absent(const Key& key, const Value& value) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        if (bucket.data.find(key) == bucket.data.end()) {
            bucket.data[key] = value;
            return true;
        }
        return false;
    }

    // @brief Thread-safe copy of all keys
    std::vector<Key> keys() const {
        std::vector<Key> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        // First pass: count total elements for reservation
        size_type total_size = 0;
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            total_size += bucket.data.size();
        }
        
        result.reserve(total_size);
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                result.push_back(pair.first);
            }
        }
        return result;
    }

    // @brief Thread-safe copy of all values
    std::vector<Value> values() const {
        std::vector<Value> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        size_type total_size = 0;
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            total_size += bucket.data.size();
        }
        
        result.reserve(total_size);
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                result.push_back(pair.second);
            }
        }
        return result;
    }

    // @brief Thread-safe copy of all key-value pairs
    std::vector<value_type> items() const {
        std::vector<value_type> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        size_type total_size = 0;
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            total_size += bucket.data.size();
        }
        
        result.reserve(total_size);
        for (auto& bucket : buckets) {
            for (const auto& pair : bucket.data) {
                result.push_back(pair);
            }
        }
        return result;
    }

    // @brief Compute size without locking all stripes (approximate)
    size_type size_approximate() const {
        size_type total_size = 0;
        for (auto& bucket : buckets) {
            // Try to get shared lock without blocking
            if (bucket.mutex.try_lock_shared()) {
                total_size += bucket.data.size();
                bucket.mutex.unlock_shared();
            } else {
                // If we can't lock, use the last known size (this is why it's approximate)
                total_size += bucket.data.size();
            }
        }
        return total_size;
    }

    // === OPERATORS ===

    // @brief Subscript operator
    Value& operator[](const Key& key) {
        auto& bucket = get_bucket_for_key(key);
        std::unique_lock lock(bucket.mutex);
        return bucket.data[key];
    }

    // Copy constructor
    StripedThreadSafeMap(const StripedThreadSafeMap& other) {
        std::vector<std::shared_lock<std::shared_mutex>> other_locks;
        other_locks.reserve(StripeCount);
        
        for (auto& bucket : other.buckets) {
            other_locks.emplace_back(bucket.mutex);
        }
        
        for (size_t i = 0; i < StripeCount; ++i) {
            buckets[i].data = other.buckets[i].data;
        }
    }

    // Copy assignment operator
    StripedThreadSafeMap& operator=(const StripedThreadSafeMap& other) {
        if (this != &other) {
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::shared_lock<std::shared_mutex>> other_locks;
            this_locks.reserve(StripeCount);
            other_locks.reserve(StripeCount);

            for (auto& bucket : buckets) {
                this_locks.emplace_back(bucket.mutex);
            }
            for (auto& bucket : other.buckets) {
                other_locks.emplace_back(bucket.mutex);
            }

            for (size_t i = 0; i < StripeCount; ++i) {
                buckets[i].data = other.buckets[i].data;
            }
        }
        return *this;
    }

    // Move constructor
    StripedThreadSafeMap(StripedThreadSafeMap&& other) noexcept {
        std::vector<std::unique_lock<std::shared_mutex>> other_locks;
        other_locks.reserve(StripeCount);
        
        for (auto& bucket : other.buckets) {
            other_locks.emplace_back(bucket.mutex);
        }
        
        for (size_t i = 0; i < StripeCount; ++i) {
            buckets[i].data = std::move(other.buckets[i].data);
        }
    }

    // Move assignment operator
    StripedThreadSafeMap& operator=(StripedThreadSafeMap&& other) noexcept {
        if (this != &other) {
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::unique_lock<std::shared_mutex>> other_locks;
            this_locks.reserve(StripeCount);
            other_locks.reserve(StripeCount);

            for (auto& bucket : buckets) {
                this_locks.emplace_back(bucket.mutex);
            }
            for (auto& bucket : other.buckets) {
                other_locks.emplace_back(bucket.mutex);
            }

            for (size_t i = 0; i < StripeCount; ++i) {
                buckets[i].data = std::move(other.buckets[i].data);
            }
        }
        return *this;
    }

    // @brief Get the current stripe count (useful for monitoring)
    constexpr size_t get_stripe_count() const noexcept {
        return StripeCount;
    }

    // @brief Get statistics about bucket distribution (useful for tuning)
    std::vector<size_type> get_bucket_sizes() const {
        std::vector<size_type> sizes;
        sizes.reserve(StripeCount);
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        locks.reserve(StripeCount);
        
        for (auto& bucket : buckets) {
            locks.emplace_back(bucket.mutex);
            sizes.push_back(bucket.data.size());
        }
        return sizes;
    }
};

#endif // STRIPED_THREAD_SAFE_MAP_H_