#ifndef _STRIPED_THREAD_SAFE_MAP_H_
#define _STRIPED_THREAD_SAFE_MAP_H_

#include <unordered_map>
#include <mutex>
#include <array>
#include <optional>
#include <functional>
#include <vector>
#include <shared_mutex>

template<typename Key, typename Value, typename Hash = std::hash<Key>, size_t StripeCount = 16>
class StripedThreadSafeMap
{
private:
    std::unordered_map<Key, Value, Hash> data;
    mutable std::array<std::shared_mutex, StripeCount> mutexes;

    // Helper function to get the mutex for a specific key
    std::shared_mutex& get_mutex_for_key(const Key& key) const
    {
        Hash hasher;
        return mutexes[hasher(key) % StripeCount];
    }

    // Lock all mutexes in order to avoid deadlocks
    void lock_all()
    {
        for (auto& mutex : mutexes) {
            mutex.lock();
        }
    }

    // Unlock all mutexes
    void unlock_all()
    {
        for (auto& mutex : mutexes) {
            mutex.unlock();
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
    StripedThreadSafeMap(std::initializer_list<value_type> init)
    {
        for (const auto& pair : init) {
            insert(pair.first, pair.second);
        }
    }

    // @brief Insert or update a key-value pair
    // @param key The key to insert/update
    // @param value The value to associate with the key
    void insert(const Key& key, const Value& value)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        data[key] = value;
    }

    // @brief Emplace a key-value pair
    // @param key The key to insert/update
    // @param args Arguments to construct the value
    template<typename... Args>
    void emplace(const Key& key, Args&&... args)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        data.emplace(key, std::forward<Args>(args)...);
    }

    // @brief Emplace a key-value pair
    // @param key The key to insert/update
    // @param args Arguments to construct the value
    template<typename... Args>
    bool try_emplace(const Key& key, Args&&... args)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.try_emplace(key, std::forward<Args>(args)...).second;
    }
    // @brief Get value for key with bounds checking
    // @param key The key to look up
    // @return Optional containing value if key exists
    std::optional<Value> at(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    // @brief Get value for key without bounds checking
    // @param key The key to look up
    // @return Reference to the value
    // @throws std::out_of_range if key doesn't exist
    Value& at(const Key& key)
    {
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.at(key);
    }

    // @brief Check if key exists
    // @param key The key to check
    // @return True if key exists
    bool contains(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.find(key) != data.end();
    }

    // @brief Remove key from map
    // @param key The key to remove
    // @return True if key was removed, false if it didn't exist
    bool erase(const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data.erase(key) > 0;
    }

    // @brief Get size of the map
    // @return Number of elements in the map
    size_type size() const
    {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        return data.size();
    }

    // @brief Check if map is empty
    // @return True if map is empty
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
    void clear()
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        data.clear();
    }

    // @brief Apply a function to each key-value pair
    // @param func Function to apply to each pair
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

    // @brief Apply a function to each key-value pair (non-const version)
    // @param func Function to apply to each pair
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

    // @brief Find first element satisfying predicate
    // @param pred Predicate function
    // @return Optional containing key-value pair if found
    template<typename Predicate>
    std::optional<value_type> find_if(Predicate pred) const
    {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        for (const auto& pair : data)
        {
            if (pred(pair.first, pair.second))
            {
                return pair;
            }
        }
        return std::nullopt;
    }

    // @brief Count elements satisfying predicate
    // @param pred Predicate function
    // @return Number of elements matching the condition
    template<typename Predicate>
    size_type count_if(Predicate pred) const
    {
        size_type count = 0;
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        for (const auto& pair : data)
        {
            if (pred(pair.first, pair.second))
            {
                ++count;
            }
        }
        return count;
    }

    // @brief Atomically update a value for a key
    // @param key The key to update
    // @param update_func Function that takes the current value and returns the new value
    // @return True if key was found and updated
    template<typename Func>
    bool update(const Key& key, Func update_func)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            it->second = update_func(it->second);
            return true;
        }
        return false;
    }

    // @brief Get value or default if key doesn't exist
    // @param key The key to look up
    // @param default_value Default value to return if key doesn't exist
    // @return Value for key or default value
    Value get_or_default(const Key& key, const Value& default_value) const
    {
        std::shared_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return default_value;
    }

    // @brief Insert if key doesn't exist, otherwise do nothing
    // @param key The key to insert
    // @param value The value to insert
    // @return True if key was inserted, false if it already existed
    bool insert_if_absent(const Key& key, const Value& value)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        if (data.find(key) == data.end())
        {
            data[key] = value;
            return true;
        }
        return false;
    }

    // @brief Thread-safe copy of all keys
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

    // @brief Thread-safe copy of all values
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

    // @brief Thread-safe copy of all key-value pairs
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

    // @brief Compute size without locking all stripes (approximate)
    // @return Approximate size (useful when exact size isn't critical)
    size_type size_approximate() const
    {
        // This gives an approximate size without full locking
        // Useful for monitoring purposes when exact size isn't critical
        return data.size(); // This is technically unsafe but fast
    }

    // === OPERATORS ===

    // @brief Subscript operator
    // @param key The key to access
    // @return Reference to the value (creates default value if key doesn't exist)
    Value& operator[](const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(get_mutex_for_key(key));
        return data[key];
    }

    // Copy constructor
    StripedThreadSafeMap(const StripedThreadSafeMap& other)
    {
        std::vector<std::shared_lock<std::shared_mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = other.data;
    }

    // Copy assignment operator
    StripedThreadSafeMap& operator=(const StripedThreadSafeMap& other)
    {
        if (this != &other)
        {
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::shared_lock<std::shared_mutex>> other_locks;

            for (auto& mutex : mutexes)
            {
                this_locks.emplace_back(mutex);
            }
            for (auto& mutex : other.mutexes)
            {
                other_locks.emplace_back(mutex);
            }

            data = other.data;
        }
        return *this;
    }

    // Move constructor
    StripedThreadSafeMap(StripedThreadSafeMap&& other) noexcept
    {
        std::vector<std::unique_lock<std::shared_mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = std::move(other.data);
    }

    // Move assignment operator
    StripedThreadSafeMap& operator=(StripedThreadSafeMap&& other) noexcept
    {
        if (this != &other)
        {
            std::vector<std::unique_lock<std::shared_mutex>> this_locks;
            std::vector<std::unique_lock<std::shared_mutex>> other_locks;

            for (auto& mutex : mutexes)
            {
                this_locks.emplace_back(mutex);
            }
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