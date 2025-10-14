#ifndef _STRIPED_THRAD_SAFE_ARRAY_H_
#define _STRIPED_THRAD_SAFE_ARRAY_H_


#include <vector>
#include <mutex>
#include <array>


// Fine-graining multithreading array control
// Be careful for changing the number of stripes, it must be a power of 2 for better performance
template<typename T, size_t StripeCount = 2>
class StripedThreadSafeArray 
{
private:
    std::vector<T> data;
    mutable std::array<std::mutex, StripeCount> mutexes;

    // Helper function to get the mutex for a specific index
    std::mutex& get_mutex_for_index(size_t index) const 
    {
        return mutexes[index % StripeCount];
    }
    // Lock all mutexes in order to avoid deadlocks
    void lock_all() const
    {
        for (auto& mutex : mutexes) {
            mutex.lock();
        }
    }

    // Unlock all mutexes
    void unlock_all() const
    {
        for (auto& mutex : mutexes) {
            mutex.unlock();
        }
    }
public:
	//@brief Default constructor
	//@param size Initial size of the array
    StripedThreadSafeArray(size_t size = 0) : data(size) 
    {
    }
	
    //@brief gets the element at the specified index with bounds checking
	//param index Index of the element to access
    T at(size_t index) const 
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        if (index >= data.size()) 
        {
            throw std::out_of_range("Index out of range");
        }
        return data[index];
    }

	//@brief gets the reference of element at the specified index with bounds checking
	//@param index Index of the element to access
    T& at(size_t index) 
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        if (index >= data.size()) 
        {
            throw std::out_of_range("Index out of range");
        }
        return data[index];
    }

    // Other methods would need to lock all mutexes for operations that affect the entire array
    
	//@brief Resizes the array to the new size (be careful with this operation as it locks all stripes)
	//@param new_size New size of the array
    void resize(size_t new_size) 
    {
        std::vector<std::unique_lock<std::mutex>> locks;
        for (auto& mutex : mutexes) 
        {
            locks.emplace_back(mutex);
        }
        data.resize(new_size);
    }


	//@bief Access reference of the first element
    T& front()
    {
        return at(0);
    }

	//@brief Access value of the first element
    T front() const
    {
        return at(0);
    }

    //brief Access reference of the last element
    T& back()
    {
        return at(data.size() - 1);
    }

	//@brief Access value of the last element
    T back() const
    {
        return at(data.size() - 1);
    }

    //@brief Get size (thread-safe, no locking needed for read-only size access if resize is the only mutating operation)
    size_t size() const
    {
        return data.size();
    }

    //@brief Check if empty
    bool empty() const
    {
        return data.empty();
    }

    //@brief Apply a function to each element with appropriate locking
	//@param func Function to apply to each element
	//Usage example:
    // Increment all elements by 1
    //numbers.for_each([](int& value) {
    //    value += 1;
    //    });
    // Print results: 2, 3, 4, 5, 6
    //numbers.for_each([](const int& value) {
    //    std::cout << value << " ";
    //    });
    template<typename Func>
    void for_each(Func func)
    {
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            func(data[i]);
        }
    }

    //@brief Const version of for_each
	//@param func Function to apply to each element
    template<typename Func>
    void for_each(Func func) const
    {
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            func(data[i]);
        }
    }

    //@brief Apply a function to each element with index
	//@param func Function to apply to each element, receives index and element reference
	//Usage example:
    // Use for_each_indexed when you need the index
    //   messages.for_each_indexed([](size_t index, std::string& message) {
    //       message = "Message #" + std::to_string(index) + " processed by thread";
    //    });
	// Concurrently process messages with their indices example:
    //   data.for_each_indexed([&futures](size_t index, const int& value) {
    //       // Launch async task for each element (read-only)
    //       futures.push_back(std::async(std::launch::async, [index, value]() {
    //           // Simulate heavy computation
    //           std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //           return value * value; // Return computed result
    //           }));
    template<typename Func>
    void for_each_indexed(Func func)
    {
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            func(i, data[i]);
        }
    }

    //@brief Batch update multiple elements (more efficient than individual locks)
	//@param start Starting index
	//@param count Number of elements to update
	//@param func Function to apply to each element in the range
	//Usage example:
    // More efficient than individual for_each for contiguous blocks
    //  size_t batch_size = 1000;
    //  for (size_t start = 0; start < large_dataset.size(); start += batch_size) {
    //        large_dataset.update_range(start, batch_size, [](double& value) {
    //            value = std::sin(value) * std::cos(value); // Some complex computation
    //          });
    //}
    template<typename Func>
    void update_range(size_t start, size_t count, Func func)
    {
        if (start >= data.size()) return;

        size_t end = std::min(start + count, data.size());
        for (size_t i = start; i < end; ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            func(data[i]);
        }
    }

    //@brief  Fill with value
	//@param value Value to fill the array with
    void fill(const T& value)
    {
        for_each([&value](T& element) { element = value; });
    }

    //@brief Swap with another striped array (must have same StripeCount)
	//@param other Other array to swap with
    void swap(StripedThreadSafeArray& other)
    {
        if (this == &other) return;

        // Lock both arrays completely to avoid deadlocks
        lock_all();
        other.lock_all();

        data.swap(other.data);

        unlock_all();
        other.unlock_all();
    }

    //@brief Thread-safe copy of entire array
    std::vector<T> to_vector() const
    {
        std::vector<T> result;
        result.reserve(data.size());

        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            result.push_back(data[i]);
        }

        return result;
    }

    // @brief Find first element satisfying predicate
	// @param pred Predicate function
	// @return True if any element matches the condition
    //Usage sample:
	//  Simple value check
    //   bool hasCharlie = employees.contains([](const Person& p) {
    //       return p.name == "Charlie";
    //       });
    template<typename Predicate>
    bool contains(Predicate pred) const
    {
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            if (pred(data[i]))
            {
                return true;
            }
        }
        return false;
    }

    //@bried Count elements satisfying predicate
	//@param pred Predicate function
	//@return Number of elements matching the condition
	//Usage sample:
	// Count elements matching a condition
	//   size_t countHighScores = scores.count_if([](int score) {
	//	   return score > 90;
	//	   });
    template<typename Predicate>
    size_t count_if(Predicate pred) const
    {
        size_t count = 0;
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(get_mutex_for_index(i));
            if (pred(data[i]))
            {
                ++count;
            }
        }
        return count;
    }

    //@brief Atomically update an element and return old value
	//@param index Index of the element to update
	//@param new_value New value to set
	//@return Old value before the update
    T exchange(size_t index, const T& new_value)
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        if (index >= data.size())
        {
            throw std::out_of_range("Index out of range");
        }
        T old_value = std::move(data[index]);
        data[index] = new_value;
        return old_value;
    }

    // @brief Atomically update an element using a function
	// @param index Index of the element to update
	// @param update_func Function that takes a reference to the element and modifies it    
	// @return Updated value
	// Usage example:
	// Atomically increment an element and get the new value
	//  int newValue = numbers.update_and_get(2, [](int& value) {
	//      value += 10;
	//      });
    template<typename Func>
    T update_and_get(size_t index, Func update_func)
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        if (index >= data.size())
        {
            throw std::out_of_range("Index out of range");
        }
        update_func(data[index]);
        return data[index];
    }

        // === COPY AND MOVE OPERATIONS ===

    // Copy constructor
    StripedThreadSafeArray(const StripedThreadSafeArray& other)
    {
        std::vector<std::unique_lock<std::mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = other.data;
    }

    // Copy assignment operator
    StripedThreadSafeArray& operator=(const StripedThreadSafeArray& other)
    {
        if (this != &other)
        {
            // Lock both arrays to avoid race conditions
            std::vector<std::unique_lock<std::mutex>> this_locks;
            std::vector<std::unique_lock<std::mutex>> other_locks;

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
    StripedThreadSafeArray(StripedThreadSafeArray&& other) noexcept
    {
        std::vector<std::unique_lock<std::mutex>> other_locks;
        for (auto& mutex : other.mutexes)
        {
            other_locks.emplace_back(mutex);
        }
        data = std::move(other.data);
    }

    // @brief Move assignment operator
	// @param other Other array to move from
	// @return Reference to this array
    StripedThreadSafeArray& operator=(StripedThreadSafeArray&& other) noexcept
    {
        if (this != &other)
        {
            std::vector<std::unique_lock<std::mutex>> this_locks;
            std::vector<std::unique_lock<std::mutex>> other_locks;

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

    // === COMPARISON OPERATORS ===

    // @brief Equality operator
	// @param other Other array to compare with
	// @return True if arrays are equal
    bool operator==(const StripedThreadSafeArray& other) const
    {
        if (data.size() != other.data.size()) return false;

        // Compare element by element with appropriate locking
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock1(get_mutex_for_index(i));
            std::lock_guard<std::mutex> lock2(other.get_mutex_for_index(i));

            if (data[i] != other.data[i])
            {
                return false;
            }
        }
        return true;
    }

    // Inequality operator
    bool operator!=(const StripedThreadSafeArray& other) const
    {
        return !(*this == other);
    }

    // Less than operator (lexicographical comparison)
    bool operator<(const StripedThreadSafeArray& other) const
    {
        size_t min_size = std::min(data.size(), other.data.size());

        for (size_t i = 0; i < min_size; ++i)
        {
            std::lock_guard<std::mutex> lock1(get_mutex_for_index(i));
            std::lock_guard<std::mutex> lock2(other.get_mutex_for_index(i));

            if (data[i] < other.data[i]) return true;
            if (other.data[i] < data[i]) return false;
        }

        return data.size() < other.data.size();
    }

    // Other comparison operators
    bool operator<=(const StripedThreadSafeArray& other) const
    {
        return !(other < *this);
    }

    bool operator>(const StripedThreadSafeArray& other) const
    {
        return other < *this;
    }

    bool operator>=(const StripedThreadSafeArray& other) const
    {
        return !(*this < other);
    }

    // === ELEMENT ACCESS OPERATORS ===

    // Subscript operator (non-const)
    T& operator[](size_t index)
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        return data[index];
    }

    // Subscript operator (const)
    const T& operator[](size_t index) const
    {
        std::lock_guard<std::mutex> lock(get_mutex_for_index(index));
        return data[index];
    }

    // === ASSIGNMENT OPERATORS ===

    // @brief Assign from initializer list
    // @param init_list
    // @return The atributtion element
    StripedThreadSafeArray& operator=(std::initializer_list<T> init_list)
    {
        std::vector<std::unique_lock<std::mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        data = init_list;
        return *this;
    }

    // @brief Assign from vector
    // @param Vector to assign
	// @return The atribuition element
    StripedThreadSafeArray& operator=(const std::vector<T>& vec)
    {
        std::vector<std::unique_lock<std::mutex>> locks;
        for (auto& mutex : mutexes)
        {
            locks.emplace_back(mutex);
        }
        data = vec;
        return *this;
    }

    // === ARITHMETIC OPERATORS ===

    // @brief Addition operator (element-wise)
	// @param other Other array to add
	// return New array with the result
    StripedThreadSafeArray operator+(const StripedThreadSafeArray& other) const
    {
        StripedThreadSafeArray result(std::max(data.size(), other.data.size()));

        for (size_t i = 0; i < result.data.size(); ++i)
        {
            T value = T();
            if (i < data.size())
            {
                std::lock_guard<std::mutex> lock1(get_mutex_for_index(i));
                value = value + data[i];
            }
            if (i < other.data.size())
            {
                std::lock_guard<std::mutex> lock2(other.get_mutex_for_index(i));
                value = value + other.data[i];
            }

            std::lock_guard<std::mutex> lock3(result.get_mutex_for_index(i));
            result.data[i] = value;
        }
        return result;
    }

    // @brief Compound addition assignment
	// @param other Other array to add
	// @return Reference to this array
    StripedThreadSafeArray& operator+=(const StripedThreadSafeArray& other)
    {
        size_t new_size = std::max(data.size(), other.data.size());
        if (new_size > data.size())
        {
            resize(new_size);
        }

        for (size_t i = 0; i < other.data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock1(get_mutex_for_index(i));
            std::lock_guard<std::mutex> lock2(other.get_mutex_for_index(i));
            data[i] += other.data[i];
        }
        return *this;
    }

    // @brief Scalar multiplication (element-wise)
	// @param scalar Scalar value to multiply
	// @return New array with the result
    StripedThreadSafeArray operator*(const T& scalar) const
    {
        StripedThreadSafeArray result(data.size());

        for (size_t i = 0; i < data.size(); ++i)
        {
            std::lock_guard<std::mutex> lock1(get_mutex_for_index(i));
            std::lock_guard<std::mutex> lock2(result.get_mutex_for_index(i));
            result.data[i] = data[i] * scalar;
        }
        return result;
    }

    // @brief Compound scalar multiplication assignment
	// @param scalar Scalar value to multiply
	// @return Reference to this array
    StripedThreadSafeArray& operator*=(const T& scalar)
    {
        for_each([&scalar](T& element) { element *= scalar; });
        return *this;
    }

    // === STREAM OPERATORS ===

    // @brief Output stream operator
	// @param os Output stream
	// @param array Array to output
	// @return Reference to the output stream
	// Usage example:
	// Print array contents
	//   std::cout << "Array: " << numbers << std::endl;
	// Output sample:
	// [1, 2, 3, 4, 5]
	// Note: This requires that T supports operator<<
    friend std::ostream& operator<<(std::ostream& os, const StripedThreadSafeArray& array)
    {
        os << "[";
        for (size_t i = 0; i < array.data.size(); ++i)
        {
            if (i > 0) os << ", ";
            std::lock_guard<std::mutex> lock(array.get_mutex_for_index(i));
            os << array.data[i];
        }
        os << "]";
        return os;
    }

};

#endif // _STRIPED_THRAD_SAFE_ARRAY_H_