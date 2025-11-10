# Code Generator Usage Guide

## Quick Start

### 1. Generate Example Definition

```bash
cd tools
python generate_command.py --example
```

This creates a `command_definition.yaml` (or `.json` if PyYAML is not installed) with example commands.

### 2. Generate Code from Definition

```bash
python generate_command.py --config command_definition.yaml --output-dir generated
```

This generates C++ code files in the `generated` directory.

### 3. Interactive Mode

```bash
python generate_command.py --interactive
```

Follow the prompts to define and generate a command interactively.

## Installation

### Requirements

- Python 3.6+
- PyYAML (optional, for YAML support)

```bash
pip install pyyaml
```

Without PyYAML, the tool will use JSON format.

## Command Definition Format

### YAML Format

```yaml
commands:
  - name: addOrder              # Method name
    type: add                   # Command type: add, delete, bulk_delete, query, custom
    entity: Order               # Entity name
    return_type: void           # Return type
    parameters:
      - name: order             # Parameter name
        type: Order             # Parameter type
        pass_by: value          # How to pass: value, const_ref, ref
    id_field: orderId          # ID field name
    indexes:                    # Indexes to update
      - ordersBySecurity_
      - ordersByUser_
```

### JSON Format

```json
{
  "commands": [
    {
      "name": "addOrder",
      "type": "add",
      "entity": "Order",
      "return_type": "void",
      "parameters": [
        {
          "name": "order",
          "type": "Order",
          "pass_by": "value"
        }
      ],
      "id_field": "orderId",
      "indexes": ["ordersBySecurity_", "ordersByUser_"]
    }
  ]
}
```

## Command Types

### 1. Add Commands (`type: add`)

Generates code to add an entity to the cache.

**Required fields:**
- `name`: Method name
- `entity`: Entity class name
- `parameters`: List with at least one parameter (the entity)
- `indexes`: List of indexes to update

**Optional fields:**
- `id_field`: ID field name (default: `{param_name}Id`)

**Example:**
```yaml
- name: addProduct
  type: add
  entity: Product
  parameters:
    - name: product
      type: Product
      pass_by: value
  id_field: productId
  indexes:
    - productsByCategory_
    - productsBySupplier_
```

### 2. Delete Commands (`type: delete`)

Generates code to delete a single entity by ID.

**Required fields:**
- `name`: Method name
- `entity`: Entity class name
- `parameters`: One parameter (the ID)

**Example:**
```yaml
- name: removeProduct
  type: delete
  entity: Product
  parameters:
    - name: productId
      type: string
      pass_by: const_ref
```

### 3. Bulk Delete Commands (`type: bulk_delete`)

Generates code to delete multiple entities based on a filter.

**Required fields:**
- `name`: Method name
- `entity`: Entity class name
- `filter_by`: Attribute name to filter by
- `parameters`: At least one parameter (the filter value)

**Optional fields:**
- Additional parameters for extra filtering

**Example:**
```yaml
- name: removeProductsBySupplier
  type: bulk_delete
  entity: Product
  filter_by: Supplier
  parameters:
    - name: supplierId
      type: string
      pass_by: const_ref
```

### 4. Query Commands (`type: query`)

Generates code to retrieve data.

**Required fields:**
- `name`: Method name
- `return_type`: Type to return
- `parameters`: Query parameters

**Optional fields:**
- `is_const`: Set to true for const methods (default: false)

**Example:**
```yaml
- name: getProductsByCategory
  type: query
  return_type: vector<Product>
  is_const: true
  parameters:
    - name: category
      type: string
      pass_by: const_ref
```

### 5. Custom Commands (`type: custom`)

Generates a skeleton for custom implementation.

**Example:**
```yaml
- name: calculateInventoryValue
  type: custom
  return_type: double
  is_const: true
  parameters: []
```

## Parameter Pass-By Options

- `value`: Pass by value (for small types)
- `const_ref`: Pass by const reference (recommended for strings and objects)
- `ref`: Pass by reference (for output parameters)

## Generated Files

The generator creates four files:

1. **interface_methods.cpp**: Interface method declarations
   - Add these to your interface class

2. **header_methods.cpp**: Class header method declarations
   - Add these to your implementation class header

3. **implementations.cpp**: Method implementations
   - Add these to your .cpp file
   - Review and customize the generated logic

4. **tests.cpp**: Test method skeletons
   - Add these to your test file
   - Complete the test logic

## Integration Workflow

### Step 1: Define Commands

Create or edit `command_definition.yaml`:

```yaml
commands:
  - name: myNewCommand
    type: add
    # ... rest of definition
```

### Step 2: Generate Code

```bash
python tools/generate_command.py --config my_commands.yaml --output-dir generated
```

### Step 3: Review Generated Code

Check the files in `generated/`:
- Review implementations
- Customize as needed
- Ensure error handling is appropriate

### Step 4: Integrate into Codebase

Copy relevant sections to your source files:

```bash
# Example integration
cat generated/interface_methods.cpp >> OrderCache/OrderCacheInterface.h
cat generated/header_methods.cpp >> OrderCache/OrderCache.h
cat generated/implementations.cpp >> OrderCache/OrderCache.cpp
cat generated/tests.cpp >> OrderCache/OrderCacheTest.cpp
```

**Important:** Always review and test generated code before committing!

### Step 5: Build and Test

```bash
cd OrderCache
g++ -std=c++17 -o test_runner OrderCache.cpp OrderCacheTest.cpp OrderCacheTestRunner.cpp -I.
./test_runner
```

## Advanced Usage

### Multiple Command Definitions

Organize commands into separate files:

```
definitions/
  ├── order_commands.yaml
  ├── product_commands.yaml
  └── user_commands.yaml
```

Generate from each:

```bash
for file in definitions/*.yaml; do
  python tools/generate_command.py --config "$file" --output-dir "generated/$(basename $file .yaml)"
done
```

### Custom Templates

Create custom templates in `tools/templates/`:

```
tools/templates/
  ├── interface.template
  ├── implementation.template
  └── test.template
```

### Batch Generation

Create a shell script to automate generation:

```bash
#!/bin/bash
# generate_all.sh

python tools/generate_command.py --config definitions/orders.yaml --output-dir generated/orders
python tools/generate_command.py --config definitions/products.yaml --output-dir generated/products

echo "Code generation complete!"
```

## Examples

### Example 1: Simple Add Command

**Definition:**
```yaml
commands:
  - name: addCustomer
    type: add
    entity: Customer
    parameters:
      - name: customer
        type: Customer
        pass_by: value
    id_field: customerId
    indexes:
      - customersByRegion_
```

**Generated Interface:**
```cpp
virtual void addCustomer(Customer customer) = 0;
```

**Generated Implementation:**
```cpp
void OrderCache::addCustomer(Customer customer) {
    const string& customerId = customer.customerId();
    
    if (!(orders_.try_emplace(customerId, customer)).second) {
        throw invalid_argument("Duplicate Customer ID: " + customerId);
    }
    
    // Update customersByRegion_ index
    auto& indexSet = customersByRegion_[customer.region()];
    if (indexSet.empty()) indexSet.reserve(10000);
    indexSet.max_load_factor(0.7F);
    indexSet.insert(customerId);
}
```

### Example 2: Query Command

**Definition:**
```yaml
commands:
  - name: getActiveOrders
    type: query
    return_type: vector<Order>
    is_const: true
    parameters: []
```

**Generated:**
```cpp
vector<Order> OrderCache::getActiveOrders() const {
    // TODO: Implement query logic
    vector<Order> result;
    result.reserve(orders_.size());
    // TODO: Populate result
    return result;
}
```

### Example 3: Bulk Delete with Filter

**Definition:**
```yaml
commands:
  - name: cancelOrdersBySecurityAndPrice
    type: bulk_delete
    entity: Order
    filter_by: Security
    parameters:
      - name: securityId
        type: string
        pass_by: const_ref
      - name: minPrice
        type: double
        pass_by: value
```

## Tips and Best Practices

### 1. Start Simple
Begin with basic commands and gradually add complexity.

### 2. Review Generated Code
Always review and test generated code. The tool provides a good starting point but may need customization.

### 3. Keep Definitions Updated
Maintain your command definitions in version control alongside your code.

### 4. Use Consistent Naming
Follow consistent naming conventions:
- `add*` for additions
- `cancel*` or `remove*` for deletions
- `get*` for queries

### 5. Document Custom Changes
If you modify generated code, document the changes in comments.

### 6. Test Thoroughly
Add comprehensive tests for each generated command.

### 7. Validate Indexes
Ensure index names in definitions match your actual data structures.

## Troubleshooting

### Import Error: No module named 'yaml'

Install PyYAML:
```bash
pip install pyyaml
```

Or use JSON format instead of YAML.

### Generated Code Doesn't Compile

Common issues:
1. Check index names match your data structures
2. Verify parameter types are correct
3. Ensure all referenced classes are included
4. Review custom logic in generated implementations

### Performance Issues

If generated code has performance issues:
1. Review index usage
2. Check reserve sizes for hash maps
3. Consider load factor optimizations
4. Profile with actual workload

## Next Steps

1. Try the interactive mode to understand the tool
2. Create definitions for your new commands
3. Review the generated code
4. Integrate and test in your project
5. Share feedback to improve the generator

## Support

For issues or questions:
1. Check the main documentation: `CODE_GENERATION_GUIDE.md`
2. Review example definitions
3. Try interactive mode for guided generation
4. Examine existing OrderCache code for patterns

## Contributing

To improve the code generator:
1. Add new command types to `CommandGenerator` class
2. Enhance templates with better patterns
3. Add validation for command definitions
4. Improve error messages and diagnostics
5. Add more examples and use cases
