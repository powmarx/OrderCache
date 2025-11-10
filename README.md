# OrderCache - High-Performance Order Management System

## Overview

OrderCache is a high-performance C++ order management system designed for efficient storage, retrieval, and manipulation of trading orders. It uses hash-based indexing for O(1) operations and follows a clean interface-based design pattern.

## Features

- **Fast Operations**: O(1) average time complexity for add, cancel, and lookup operations
- **Multiple Indexes**: Efficient querying by security, user, and other attributes
- **Thread-Safe Option**: Striped thread-safe array implementation available
- **Comprehensive Testing**: Includes unit tests, stress tests, and benchmarks
- **Code Generation**: Automated tools for creating new API commands

## Project Structure

```
OrderCache/
├── OrderCache/                    # Main source code
│   ├── OrderCacheInterface.h     # Interface definition
│   ├── OrderCache.h              # Implementation header
│   ├── OrderCache.cpp            # Implementation
│   ├── OrderCacheTest.cpp        # Unit tests
│   ├── OrderCacheStressTest.cpp  # Stress tests
│   ├── OrderCacheBenchmark.cpp   # Performance benchmarks
│   └── StripedThreadsafeArray.h  # Thread-safe utilities
├── tools/                         # Code generation tools
│   ├── generate_command.py       # Command generator
│   ├── README.md                 # Tool usage guide
│   └── templates/                # Code templates
├── CODE_GENERATION_GUIDE.md      # Comprehensive generation guide
├── API_PATTERNS.md               # API design patterns reference
└── README.md                     # This file
```

## API Commands

### Core Operations

```cpp
// Add a new order
void addOrder(Order order);

// Cancel a specific order
void cancelOrder(const string& orderId);

// Cancel all orders for a user
void cancelOrdersForUser(const string& user);

// Cancel orders for a security with minimum quantity
void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty);

// Get matching size for cross-company trades
unsigned int getMatchingSizeForSecurity(const string& securityId);

// Retrieve all orders
vector<Order> getAllOrders() const;
```

## Building

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Python 3.6+ (for code generation tools)

### Compile and Run Tests

```bash
# Navigate to source directory
cd OrderCache

# Compile
g++ -std=c++17 -o test_runner \
    OrderCache.cpp \
    OrderCacheTest.cpp \
    OrderCacheTestRunner.cpp \
    OrderCacheStressTest.cpp \
    OrderCacheBenchmark.cpp \
    -I.

# Run tests (interactive menu)
./test_runner
```

### Using Visual Studio

Open `OrderCache.sln` in Visual Studio 2017 or later and build the solution.

## Usage Example

```cpp
#include "OrderCache.h"

int main() {
    OrderCache cache;
    
    // Add orders
    cache.addOrder(Order("OrdId1", "SecId1", "Buy", 1000, "User1", "CompanyA"));
    cache.addOrder(Order("OrdId2", "SecId1", "Sell", 500, "User2", "CompanyB"));
    
    // Query
    unsigned int matchSize = cache.getMatchingSizeForSecurity("SecId1");
    cout << "Matching size: " << matchSize << endl;  // Output: 500
    
    // Cancel
    cache.cancelOrder("OrdId1");
    
    // Get all orders
    auto orders = cache.getAllOrders();
    cout << "Total orders: " << orders.size() << endl;  // Output: 1
    
    return 0;
}
```

## Code Generation

### Quick Start

Generate new API commands using the provided tools:

```bash
# Create example definition
cd tools
python generate_command.py --example

# Generate code from definition
python generate_command.py --config command_definition.yaml --output-dir generated

# Interactive mode
python generate_command.py --interactive
```

### Documentation

- **[Code Generation Guide](CODE_GENERATION_GUIDE.md)**: Comprehensive guide to automated code generation
- **[Tool Usage](tools/README.md)**: Detailed usage instructions for the code generator
- **[API Patterns](API_PATTERNS.md)**: Reference for API command patterns

## Performance Characteristics

### Time Complexity

| Operation | Average | Worst Case |
|-----------|---------|------------|
| addOrder | O(1) | O(n)* |
| cancelOrder | O(1) | O(n)* |
| cancelOrdersForUser | O(k) | O(k·n)* |
| getMatchingSizeForSecurity | O(m) | O(m·n)* |
| getAllOrders | O(n) | O(n) |

*Hash table worst case, very rare with proper load factor

Where:
- n = total number of orders
- k = orders for specific user
- m = orders for specific security

### Space Complexity

- Main storage: O(n)
- Indexes: O(n·i) where i is number of indexes
- Total: O(n·(1 + i))

### Optimizations

- Pre-allocated hash tables with reserve()
- Load factor optimization (0.7) to reduce collisions
- Reference usage to avoid unnecessary copies
- Efficient index cleanup on deletion

## Testing

### Unit Tests

Run comprehensive unit tests:

```bash
./test_runner
# Select option 1-5 for different test suites
```

Test coverage includes:
- Basic CRUD operations
- Edge cases (duplicates, not found, empty sets)
- Cross-company matching algorithm
- Bulk operations
- Error handling

### Stress Testing

Test with large datasets:

```bash
./test_runner
# Select option 40 for stress test
```

### Benchmarks

Measure performance:

```bash
./test_runner
# Select option 50 for benchmarks
```

## Design Patterns

### Interface Pattern

Clean separation of interface and implementation:

```cpp
class OrderCacheInterface {
public:
    virtual void addOrder(Order order) = 0;
    // ... other methods
};

class OrderCache : public OrderCacheInterface {
    // Implementation
};
```

### Index Pattern

Multiple indexes for efficient querying:

```cpp
// Main storage
unordered_map<string, Order> orders_;

// Indexes
unordered_map<string, unordered_set<string>> ordersBySecurity_;
unordered_map<string, unordered_set<string>> ordersByUser_;
```

### Command Pattern

Each operation follows a consistent command pattern with:
1. Input validation
2. Main operation
3. Index updates
4. Error handling

## Extending the System

### Adding New Commands

1. Define command in YAML:
```yaml
commands:
  - name: myNewCommand
    type: add
    entity: Order
    # ... rest of definition
```

2. Generate code:
```bash
python tools/generate_command.py --config my_commands.yaml
```

3. Review and integrate generated code

4. Add tests

### Adding New Indexes

1. Add index data structure to `OrderCache.h`:
```cpp
unordered_map<string, unordered_set<string>> ordersByNewAttribute_;
```

2. Update `addOrder` to populate index
3. Update `cancelOrder` to clean up index
4. Add query methods using the index

## Best Practices

### Performance

- Pre-allocate collections when size is known
- Use const references for read-only parameters
- Minimize copies with move semantics
- Choose appropriate load factors for hash tables

### Error Handling

- Throw descriptive exceptions
- Use standard exception types (`invalid_argument`, `out_of_range`, `logic_error`)
- Validate inputs before operations
- Provide clear error messages

### Code Style

- Use meaningful variable names
- Follow const-correctness
- Document complex algorithms
- Keep methods focused and concise

## Troubleshooting

### Compilation Issues

**Error**: `undefined reference to std::find`
- **Solution**: Add `#include <algorithm>` to test files

**Error**: Duplicate symbol
- **Solution**: Ensure header guards are correct and no duplicate definitions

### Runtime Issues

**Exception**: "Duplicate Order ID"
- **Cause**: Attempting to add order with existing ID
- **Solution**: Check for existing orders before adding

**Exception**: "Order ID not found"
- **Cause**: Attempting to cancel non-existent order
- **Solution**: Verify order exists before canceling

### Performance Issues

**Slow lookups**
- Check hash table load factors
- Ensure indexes are being used
- Profile to identify bottlenecks

## Contributing

### Guidelines

1. Follow existing code style
2. Add tests for new features
3. Update documentation
4. Use code generation tools for consistency
5. Profile performance-critical changes

### Testing Checklist

- [ ] All unit tests pass
- [ ] New tests added for new functionality
- [ ] Stress tests pass
- [ ] Performance benchmarks acceptable
- [ ] Code reviewed

## License

[Add your license information here]

## Authors

[Add author information here]

## Acknowledgments

- Design inspired by high-performance trading systems
- Uses standard C++ containers for portability
- Code generation approach based on modern template systems

## Support

For questions, issues, or suggestions:
1. Check the documentation files
2. Review existing tests for examples
3. Try the code generation tools
4. Open an issue with detailed description

## Future Enhancements

Potential improvements:
- [ ] Thread-safe operations
- [ ] Persistent storage
- [ ] Additional query methods
- [ ] Performance monitoring
- [ ] REST API wrapper
- [ ] Python bindings
- [ ] Distributed cache support

---

**Version**: 1.0  
**Last Updated**: 2025-11-10
