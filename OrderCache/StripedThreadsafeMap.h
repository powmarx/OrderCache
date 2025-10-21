#ifndef _STRIPED_THREAD_SAFE_MAP_H_
#define _STRIPED_THREAD_SAFE_MAP_H_

#include <unordered_map>
#include <mutex>
#include <array>
#include <optional>
#include <functional>
#include <vector>
#include <shared_mutex>
#include <algorithm>

template<typename Key, typename Value, typename Hash = std::hash<Key>, size_t StripeCount = 16>
class StripedThreadSafeMap
{
private:
    std::unordered_map<Key, Value, Hash> data;
    // Mutexes must be mutable to be locked by const methods using std::shared_lock
    mutable std::array<std::shared_mutex, StripeCount> mutexes;

    // @brief Helper function to get the mutex for a specific key
    std::shared_mutex& get_mutex_for_key(const Key& key) const
    {
        Hash hasher;
        // The mutex index is determined by the hash of the key modulo the number of stripes.
        return mutexes[hasher(key) % StripeCount];
    }

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = typename std::unordered_map<Key, Value, Hash>::size_type;

    // @brief Default constructor
    StripedThreadSafeMap() = default;

    // @brief Initializer list constructor
    StripedThreadSafeMap(std::initializer_list<value_type> init)
    {
        for (const auto& pair : init) {
            insert(pair.first, pair.second);
        }
    }

    // @brief Insert or update a key-value pair
    void insert(const Key& key, const Value& value)
    {
        // Exclusive (write) lock for the key's stripe
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        data[key] = value;
    }

    // @brief Emplace a key-value pair
    template<typename... Args>
    void emplace(const Key& key, Args&&... args)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        data.emplace(key, std::forward<Args>(args)...);
    }

    // @brief Try to emplace a key-value pair
    template<typename... Args>
    bool try_emplace(const Key& key, Args&&... args)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.try_emplace(key, std::forward<Args>(args)...).second;
    }

    // @brief Get value for key with bounds checking (const version)
    // @return Optional containing the value if the key exists
    std::optional<Value> at(const Key& key) const
    {
        // Shared (read) lock
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    // @brief Get value for key without bounds checking (non-const version)
    // @return Reference to the value
    // @throws std::out_of_range if key doesn't exist
    // CRITICAL CORRECTION: Must use unique_lock (exclusive) because returning a non-const
    // reference allows external code to WRITE to the value, which must be protected.
    Value& at(const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.at(key);
    }

    // @brief Check if key exists
    bool contains(const Key& key) const
    {
        // Shared (read) lock
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.find(key) != data.end();
    }

    // @brief Remove key from map
    bool erase(const Key& key)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.erase(key) > 0;
    }

    // @brief Get the size of the map
    // Global operation: requires locking all mutexes (read).
    size_type size() const
    {
        // Sequentially lock all mutexes in the same consistent order (0 to N-1)
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        return data.size();
    }

    // @brief Check if the map is empty
    // Global operation: requires locking all mutexes (read).
    bool empty() const
    {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        return data.empty();
    }

    // @brief Clear all elements from the map
    // Global operation: requires locking all mutexes (write).
    void clear()
    {
        // Exclusive lock on all mutexes.
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        data.clear();
    }

    // @brief Apply a function to every key-value pair (const version)
    // Global operation: requires locking all mutexes (read).
    template<typename Func>
    void for_each(Func func) const
    {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        for (const auto& pair : data)
        {
            func(pair.first, pair.second);
        }
    }

    // @brief Apply a function to every key-value pair (non-const version)
    // Global operation: requires locking all mutexes (write).
    template<typename Func>
    void for_each(Func func)
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        for (auto& pair : data)
        {
            func(pair.first, pair.second);
        }
    }

    // @brief Enumerate keys
    // Global operation: requires locking all mutexes (read).
    std::vector<Key> keys() const
    {
        std::vector<Key> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        result.reserve(data.size());
        for (const auto& pair : data)
        {
            result.push_back(pair.first);
        }
        return result;
    }

    // @brief Enumerate values
    // Global operation: requires locking all mutexes (read).
    std::vector<Value> values() const
    {
        std::vector<Value> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        result.reserve(data.size());
        for (const auto& pair : data)
        {
            result.push_back(pair.second);
        }
        return result;
    }

    // @brief Enumerate items (key-value pairs)
    // Global operation: requires locking all mutexes (read).
    std::vector<value_type> items() const
    {
        std::vector<value_type> result;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        result.reserve(data.size());
        for (const auto& pair : data)
        {
            result.push_back(pair);
        }
        return result;
    }

    // @brief Approximate size (potentially non-thread-safe for speed)
    size_type size_approximate() const
    {
        // Use with caution in multithreaded scenarios where a completely accurate count is needed.
        return data.size();
    }

    // @brief Update a value atomically if key exists
    template<typename Func>
    bool update(const Key& key, Func update_func)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            it->second = update_func(it->second);
            return true;
        }
        return false;
    }

    // @brief Update a value atomically if key exists
    template<typename Func>
    void update_or_insert(const Key& key, Func update_func)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);

        if (it == data.end()) {
            // Insere um Value padrão (unordered_set vazio)
            it = data.emplace(key, Value{}).first;
        }
       update_func(it->second);
    }

    // @brief Get value wrapped in optional
    std::optional<Value> get(const Key& key) const
    {
        // Shared (read) lock
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    // @brief Get value or provided default
    Value get_or_default(const Key& key, const Value& default_value) const
    {
        // Shared (read) lock
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return default_value;
    }

    // @brief Insert only if key is absent
    bool insert_if_absent(const Key& key, const Value& value)
    {
        // Exclusive (write) lock
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        if (data.find(key) == data.end())
        {
            data[key] = value;
            return true;
        }
        return false;
    }

    // === OPERATORS ===

    // @brief Subscript operator (may insert a default value)
    Value& operator[](const Key& key)
    {
        // Exclusive (write) lock, as it may create a default value if the key doesn't exist.
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data[key];
    }

    // @brief Copy Constructor
    StripedThreadSafeMap(const StripedThreadSafeMap& other)
    {
        // Lock all of 'other's mutexes (read) before accessing data.
        std::vector<std::shared_lock<std::shared_mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = other.data;
    }

    // @brief Copy Assignment Operator (Deadlock-safe locking for two objects)
    StripedThreadSafeMap& operator=(const StripedThreadSafeMap& other)
    {
        if (this != &other)
        {
            // Lock all stripes of 'this' (exclusive) and 'other' (shared)
            // in the same sequential order to prevent cross-object deadlocks.
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::shared_lock<std::shared_mutex>> other_locks;

            // Exclusive lock all stripes of 'this'
            for (auto& mutex : mutexes)
            {
                this_locks.emplace_back(mutex);
            }
            // Shared lock all stripes of 'other'
            for (auto& mutex : other.mutexes)
            {
                other_locks.emplace_back(mutex);
            }

            data = other.data;
        }
        return *this;
    }

    // @brief Move Constructor
    StripedThreadSafeMap(StripedThreadSafeMap&& other) noexcept
    {
        // Lock all of 'other's mutexes (exclusive) to ensure nobody is using it before moving.
        std::vector<std::unique_lock<std::shared_mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = std::move(other.data);
    }

    // @brief Move Assignment Operator (Deadlock-safe locking for two objects)
    StripedThreadSafeMap& operator=(StripedThreadSafeMap&& other) noexcept
    {
        if (this != &other)
        {
            // Lock all stripes of 'this' and 'other' exclusively
            // in the same sequential order to prevent deadlocks.
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::unique_lock<std::shared_mutex>> other_locks;

            // Exclusive lock all stripes of 'this'
            for (auto& mutex : mutexes)
            {
                this_locks.emplace_back(mutex);
            }
            // Exclusive lock all stripes of 'other'
            for (auto& mutex : other.mutexes)
            {
                other_locks.emplace_back(mutex);
            }

            data = std::move(other.data);
        }
        return *this;
    }
};

#endif // _STRIPED_THREAD_SAFE_MAP_H_