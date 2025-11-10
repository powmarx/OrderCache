# API Command Patterns Reference

## Overview

This document describes the command patterns used in the OrderCache API and how they can be applied to generate similar commands.

## Pattern Categories

### 1. CRUD Operations Pattern

The OrderCache follows standard CRUD (Create, Read, Update, Delete) patterns:

```cpp
// CREATE
void add<Entity>(<Entity> entity)

// READ
<ReturnType> get<Data>()
<ReturnType> get<Data>For<Filter>(const string& filterValue)

// UPDATE (not currently implemented, but pattern would be)
void update<Entity>(const string& id, <Entity> entity)

// DELETE
void cancel<Entity>(const string& id)
void cancel<Entities>For<Filter>(const string& filterValue)
```

### 2. Index-Based Operations Pattern

Operations use indexes for O(1) lookup:

```cpp
// Main storage
unordered_map<string, Entity> entities_;

// Indexes
unordered_map<string, unordered_set<string>> entitiesByAttribute1_;
unordered_map<string, unordered_set<string>> entitiesByAttribute2_;

// Pattern: Update all indexes when adding
void addEntity(Entity entity) {
    // 1. Add to main storage
    entities_.try_emplace(entity.id(), entity);
    
    // 2. Update each index
    entitiesByAttribute1_[entity.attribute1()].insert(entity.id());
    entitiesByAttribute2_[entity.attribute2()].insert(entity.id());
}

// Pattern: Remove from all indexes when deleting
void removeEntity(const string& id) {
    // 1. Find entity
    auto it = entities_.find(id);
    
    // 2. Remove from indexes
    entitiesByAttribute1_[it->second.attribute1()].erase(id);
    entitiesByAttribute2_[it->second.attribute2()].erase(id);
    
    // 3. Remove from main storage
    entities_.erase(it);
}
```

### 3. Bulk Operations Pattern

Bulk operations use indexes and delegate to single-entity operations:

```cpp
void removeEntitiesForAttribute(const string& attrValue) {
    // 1. Get IDs from index
    auto& ids = entitiesByAttribute_[attrValue];
    
    // 2. Copy IDs (to avoid iterator invalidation)
    auto idsCopy = ids;
    
    // 3. Iterate and remove each
    for (const auto& id : idsCopy) {
        removeEntity(id);  // Delegate to single remove
    }
}
```

### 4. Query/Aggregation Pattern

Query operations read data without modification:

```cpp
// Pattern: Simple retrieval
vector<Entity> getAllEntities() const {
    vector<Entity> result;
    result.reserve(entities_.size());
    
    for (const auto& [id, entity] : entities_) {
        result.push_back(entity);
    }
    
    return result;
}

// Pattern: Filtered retrieval
vector<Entity> getEntitiesForAttribute(const string& attrValue) const {
    vector<Entity> result;
    
    auto it = entitiesByAttribute_.find(attrValue);
    if (it != entitiesByAttribute_.end()) {
        result.reserve(it->second.size());
        
        for (const auto& id : it->second) {
            result.push_back(entities_.at(id));
        }
    }
    
    return result;
}

// Pattern: Aggregation
unsigned int computeMetric() const {
    unsigned int total = 0;
    
    for (const auto& [id, entity] : entities_) {
        total += entity.getValue();
    }
    
    return total;
}
```

### 5. Error Handling Pattern

Consistent error handling across all operations:

```cpp
// Pattern: Not found error
void removeEntity(const string& id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        throw out_of_range("Entity ID not found: " + id);
    }
    // ... rest of logic
}

// Pattern: Duplicate error
void addEntity(Entity entity) {
    if (!(entities_.try_emplace(entity.id(), entity)).second) {
        throw invalid_argument("Duplicate Entity ID: " + entity.id());
    }
    // ... rest of logic
}

// Pattern: Invalid operation error
void performOperation() {
    if (!isValid()) {
        throw logic_error("Invalid operation: reason");
    }
    // ... rest of logic
}
```

## Command Template Structure

### Basic Command Template

```cpp
// Interface declaration
class EntityCacheInterface {
public:
    virtual <ReturnType> <commandName>(<Parameters>) <const?> = 0;
};

// Class declaration
class EntityCache : public EntityCacheInterface {
public:
    <ReturnType> <commandName>(<Parameters>) <const?> override;
private:
    // Data structures
    unordered_map<string, Entity> entities_;
    unordered_map<string, unordered_set<string>> entitiesByIndex_;
};

// Implementation
<ReturnType> EntityCache::<commandName>(<Parameters>) <const?> {
    // 1. Validate inputs
    // 2. Perform operation
    // 3. Update indexes (if needed)
    // 4. Return result (if needed)
}
```

## Naming Conventions

### Method Names

Follow these conventions for consistency:

```cpp
// Addition: add<Entity>
void addOrder(Order order);
void addProduct(Product product);

// Deletion: cancel<Entity> or remove<Entity>
void cancelOrder(const string& orderId);
void removeProduct(const string& productId);

// Bulk deletion: cancel<Entities>For<Filter>
void cancelOrdersForUser(const string& user);
void removeProductsForCategory(const string& category);

// Query: get<Data>
vector<Order> getAllOrders() const;
Order getOrder(const string& orderId) const;

// Filtered query: get<Data>For<Filter>
vector<Order> getOrdersForUser(const string& user) const;
unsigned int getCountForSecurity(const string& secId) const;

// Aggregation: compute<Metric> or calculate<Value>
unsigned int computeTotalQuantity() const;
double calculateTotalValue() const;
```

### Variable Names

```cpp
// Collections: plural, suffix with underscore
unordered_map<string, Order> orders_;
vector<Product> products_;

// Indexes: <entity>By<Attribute>_
unordered_map<string, unordered_set<string>> ordersBySecurity_;
unordered_map<string, unordered_set<string>> productsByCategory_;

// Iterators: <name>It
auto orderIt = orders_.find(id);
auto indexIt = ordersBySecurity_.find(secId);

// IDs: <entity>Id
string orderId = order.orderId();
string productId = product.productId();
```

## Performance Patterns

### Pre-allocation Pattern

```cpp
void addEntity(Entity entity) {
    // Pre-allocate space in indexes
    auto& indexSet = entitiesByAttribute_[entity.attribute()];
    if (indexSet.empty()) {
        indexSet.reserve(10000);  // Expected size
    }
    indexSet.insert(entity.id());
}
```

### Load Factor Optimization

```cpp
void addEntity(Entity entity) {
    auto& indexSet = entitiesByAttribute_[entity.attribute()];
    if (indexSet.empty()) {
        indexSet.reserve(10000);
        indexSet.max_load_factor(0.7F);  // Reduce collisions
    }
    indexSet.insert(entity.id());
}
```

### Copy Avoidance Pattern

```cpp
// Use const reference for read-only access
const Order& order = orderIt->second;
const string& securityId = order.securityId();

// Reserve space before population
vector<Order> result;
result.reserve(expectedSize);
```

### Index Cleanup Pattern

```cpp
void removeEntity(const string& id) {
    // Remove from index
    auto& indexSet = entitiesByAttribute_[attrValue];
    indexSet.erase(id);
    
    // Clean up empty index entries
    if (indexSet.empty()) {
        entitiesByAttribute_.erase(attrValue);
    }
}
```

## Testing Patterns

### Test Structure Pattern

```cpp
void testCommandName() {
    cout << "Testing commandName...\n";
    
    // Setup
    EntityCache cache;
    Entity testEntity("id1", "value1");
    
    // Execute
    cache.commandName(params);
    
    // Verify
    auto result = cache.getResult();
    assert(result == expectedValue && "Description of expected behavior");
    
    cout << "commandName test passed!\n";
}
```

### Test Case Categories

```cpp
// 1. Happy path
void testAddOrder_ValidOrder_Success();

// 2. Error cases
void testAddOrder_DuplicateId_ThrowsException();
void testCancelOrder_NotFound_ThrowsException();

// 3. Edge cases
void testCancelOrdersForUser_EmptySet_NoError();
void testGetOrders_EmptyCache_ReturnsEmptyVector();

// 4. Performance cases
void testAddOrder_ThousandsOfOrders_AcceptablePerformance();
```

## Common Command Examples

### Example 1: Add with Multiple Indexes

```cpp
void addOrder(Order order) {
    const string& orderId = order.orderId();
    const string& securityId = order.securityId();
    const string& user = order.user();
    
    // Validate and add to main storage
    if (!(orders_.try_emplace(orderId, order)).second) {
        throw invalid_argument("Duplicate Order ID: " + orderId);
    }
    
    // Update security index
    auto& secSet = ordersBySecurity_[securityId];
    if (secSet.empty()) {
        secSet.reserve(10000);
        secSet.max_load_factor(0.7F);
    }
    secSet.insert(orderId);
    
    // Update user index
    auto& userSet = ordersByUser_[user];
    if (userSet.empty()) {
        userSet.reserve(5000);
        userSet.max_load_factor(0.7F);
    }
    userSet.insert(orderId);
}
```

### Example 2: Delete with Index Cleanup

```cpp
void cancelOrder(const string& orderId) {
    // Find order
    auto orderIt = orders_.find(orderId);
    if (orderIt == orders_.end()) {
        throw out_of_range("Order ID not found: " + orderId);
    }
    
    const Order& order = orderIt->second;
    const string& securityId = order.securityId();
    const string& user = order.user();
    
    // Remove from security index
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt != ordersBySecurity_.end()) {
        secIt->second.erase(orderId);
        if (secIt->second.empty()) {
            ordersBySecurity_.erase(secIt);
        }
    }
    
    // Remove from user index
    auto userIt = ordersByUser_.find(user);
    if (userIt != ordersByUser_.end()) {
        userIt->second.erase(orderId);
        if (userIt->second.empty()) {
            ordersByUser_.erase(userIt);
        }
    }
    
    // Remove from main storage
    orders_.erase(orderIt);
}
```

### Example 3: Bulk Delete with Filter

```cpp
void cancelOrdersForSecIdWithMinimumQty(
    const string& securityId, 
    unsigned int minQty
) {
    auto secIt = ordersBySecurity_.find(securityId);
    if (secIt == ordersBySecurity_.end()) {
        throw out_of_range("Security ID not found: " + securityId);
    }
    
    // Copy IDs to avoid iterator invalidation
    auto orderIds = secIt->second;
    
    for (const auto& orderId : orderIds) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end() && orderIt->second.qty() >= minQty) {
            cancelOrder(orderId);
        }
    }
}
```

### Example 4: Query with Filtering

```cpp
vector<Order> getOrdersForUserAndSecurity(
    const string& user,
    const string& securityId
) const {
    vector<Order> result;
    
    auto userIt = ordersByUser_.find(user);
    if (userIt == ordersByUser_.end()) {
        return result;  // Empty result
    }
    
    for (const auto& orderId : userIt->second) {
        auto orderIt = orders_.find(orderId);
        if (orderIt != orders_.end() && 
            orderIt->second.securityId() == securityId) {
            result.push_back(orderIt->second);
        }
    }
    
    return result;
}
```

## Summary

These patterns provide a solid foundation for generating new API commands:

1. **CRUD operations** for basic entity management
2. **Index-based lookups** for efficient queries
3. **Bulk operations** that delegate to single operations
4. **Query/aggregation** for data retrieval
5. **Consistent error handling** for robustness

Use the code generator tool with these patterns to create new commands quickly and consistently.
