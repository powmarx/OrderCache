# API Command Code Generation Guide

## Overview

This guide provides suggestions, frameworks, and insights for automating the creation of API commands in the OrderCache system. The OrderCache follows a clear command pattern that can be automated using code generation techniques.

## Current API Command Pattern

### Interface Design Pattern

The OrderCache API follows these patterns:

1. **Interface Definition** (`OrderCacheInterface.h`): Declares pure virtual methods
2. **Implementation** (`OrderCache.h` and `OrderCache.cpp`): Implements the interface methods
3. **Data Structures**: Uses hash maps and sets for efficient indexing
4. **Error Handling**: Throws exceptions for invalid operations

### Identified Command Patterns

#### Pattern 1: Add Commands
```cpp
// Signature: void add<Entity>(<Entity> entity)
void addOrder(Order order) override;
```
**Characteristics:**
- Takes entity object as parameter
- Adds to main storage and updates indexes
- Validates for duplicates
- O(1) average complexity

#### Pattern 2: Cancel/Delete Commands
```cpp
// Signature: void cancel<Entity>(const string& id)
void cancelOrder(const string& orderId) override;
```
**Characteristics:**
- Takes ID string as parameter
- Removes from main storage and all indexes
- Throws exception if not found
- O(1) average complexity

#### Pattern 3: Bulk Cancel Commands with Filter
```cpp
// Signature: void cancel<Entities>For<Attribute>(const string& attributeValue)
void cancelOrdersForUser(const string& user) override;

// With additional filter:
void cancelOrdersForSecIdWithMinimumQty(const string& securityId, unsigned int minQty) override;
```
**Characteristics:**
- Uses indexes for efficient lookup
- Iterates and conditionally cancels
- Reuses single cancel logic
- O(n) where n is matching entities

#### Pattern 4: Query/Getter Commands
```cpp
// Signature: <ReturnType> get<Data>For<Attribute>(const string& attributeValue)
unsigned int getMatchingSizeForSecurity(const string& securityId) override;
vector<Order> getAllOrders() const override;
```
**Characteristics:**
- Read-only operations (const)
- Returns computed or stored data
- Uses indexes for efficient lookup
- O(1) to O(n) depending on operation

## Recommended Code Generation Approaches

### 1. Template-Based Generation (Simple)

**Best For:** Small projects, quick prototyping

**Tools:**
- Jinja2 (Python)
- Mustache templates
- Custom shell scripts

**Example Template Structure:**
```
templates/
  ├── interface_method.template
  ├── implementation_header.template
  ├── implementation_source.template
  └── test_method.template
```

### 2. Abstract Syntax Tree (AST) Manipulation (Advanced)

**Best For:** Large codebases, complex transformations

**Tools:**
- libclang (C++ AST)
- Python clang bindings
- LLVM/Clang tooling

**Capabilities:**
- Parse existing code
- Generate new methods
- Maintain code style
- Refactor existing code

### 3. Domain-Specific Language (DSL) (Enterprise)

**Best For:** Teams with many similar commands, complex business logic

**Tools:**
- Xtext (Eclipse)
- ANTLR (Parser generator)
- Custom DSL with code generator

**Example DSL:**
```
entity Order {
  key orderId: string
  field securityId: string
  field side: string
  field qty: unsigned int
  field user: string
  field company: string
  
  index by securityId
  index by user
}

command addOrder(order: Order) {
  validate unique orderId
  store in orders_
  update index ordersBySecurity_
  update index ordersByUser_
}
```

### 4. IDE Code Generation Plugins

**Best For:** Developer productivity, real-time generation

**Tools:**
- Visual Studio extensions
- CLion custom actions
- VS Code snippets

## Recommended Framework: Python-Based Template Generator

For the OrderCache project, a **Python-based template generator** is recommended because:

1. **Easy to maintain**: Python scripts are simple to modify
2. **Template flexibility**: Jinja2 templates are readable and powerful
3. **No compilation needed**: Quick iteration and testing
4. **Cross-platform**: Works on Windows, Linux, macOS
5. **Integration**: Can integrate with build systems (CMake, Make)

## Code Generation Architecture

### Workflow

```
┌─────────────────┐
│  Definition     │
│  (JSON/YAML)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Generator      │
│  (Python)       │
└────────┬────────┘
         │
         ├──────────────┬──────────────┬──────────────┐
         ▼              ▼              ▼              ▼
    Interface.h    Class.h        Class.cpp      Tests.cpp
```

### Definition File Format (YAML)

```yaml
commands:
  - name: addOrder
    type: add
    entity: Order
    parameters:
      - name: order
        type: Order
        pass_by: value
    indexes:
      - ordersBySecurity_
      - ordersByUser_
    
  - name: cancelOrder
    type: delete
    entity: Order
    parameters:
      - name: orderId
        type: string
        pass_by: const_ref
    
  - name: cancelOrdersForUser
    type: bulk_delete
    entity: Order
    filter_by: user
    parameters:
      - name: user
        type: string
        pass_by: const_ref
```

## Implementation Strategy

### Phase 1: Template Creation
1. Extract common patterns from existing code
2. Create Jinja2 templates for each pattern
3. Define template variables and control flow

### Phase 2: Generator Script
1. Implement Python script to read definitions
2. Render templates with proper values
3. Handle code formatting and style

### Phase 3: Integration
1. Add to build process (optional)
2. Create developer documentation
3. Set up validation and testing

### Phase 4: Extensions
1. Add support for custom validators
2. Implement automatic test generation
3. Add code documentation generation

## Alternative Approaches

### Code Synthesis Tools

1. **CppCMS Code Generator**: For web service APIs
2. **Protocol Buffers**: For message-based APIs
3. **Thrift**: For cross-language service definitions
4. **gRPC**: For RPC-based services

### Metaprogramming

1. **C++ Templates**: Compile-time code generation
2. **Macros**: Preprocessor-based generation (not recommended)
3. **X Macros**: Pattern for repetitive code

### AI-Assisted Generation

1. **GitHub Copilot**: Real-time code suggestions
2. **ChatGPT/Claude**: Template generation and refinement
3. **Custom trained models**: For domain-specific patterns

## Best Practices

### 1. Keep Templates Simple
- One template per code pattern
- Minimize logic in templates
- Use clear variable names

### 2. Validate Generated Code
- Always compile generated code
- Run existing tests
- Add tests for new commands

### 3. Version Control
- Commit templates and definitions
- Track generator script changes
- Document breaking changes

### 4. Documentation
- Auto-generate API documentation
- Keep generation guide updated
- Provide usage examples

### 5. Incremental Adoption
- Start with simplest commands
- Gradually expand to complex patterns
- Keep manual override option

## Tools Comparison

| Tool | Complexity | Flexibility | Learning Curve | Maintenance |
|------|-----------|-------------|----------------|-------------|
| Template (Jinja2) | Low | Medium | Low | Easy |
| AST (libclang) | High | High | High | Medium |
| DSL (Custom) | Very High | Very High | Very High | Complex |
| IDE Plugins | Medium | Low | Medium | Easy |
| Metaprogramming | High | Medium | High | Hard |

## Conclusion

For the OrderCache project, the **Template-Based Generation with Python and Jinja2** approach offers the best balance of:
- Simplicity and maintainability
- Flexibility for future changes
- Low learning curve for team members
- Easy integration with existing workflow

The provided code generator tool (`tools/generate_command.py`) implements this approach and can be extended as needed.

## Next Steps

1. Review the provided code generator tool
2. Try generating a new command
3. Customize templates for your specific needs
4. Integrate into development workflow
5. Gather feedback and iterate

## References

- [Jinja2 Documentation](https://jinja.palletsprojects.com/)
- [libclang Documentation](https://clang.llvm.org/doxygen/group__CINDEX.html)
- [Code Generation Best Practices](https://martinfowler.com/articles/patterns-of-code-generation.html)
- [Template Method Pattern](https://refactoring.guru/design-patterns/template-method)
