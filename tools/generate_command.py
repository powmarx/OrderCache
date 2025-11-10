#!/usr/bin/env python3
"""
OrderCache Command Generator

This script generates C++ API command code based on templates and definitions.
It follows the patterns identified in the OrderCache codebase.

Usage:
    python generate_command.py --config command_definition.yaml
    python generate_command.py --interactive
    python generate_command.py --example add_order
"""

import argparse
import os
import sys
from pathlib import Path
from typing import Dict, List, Any
import json

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False
    print("Warning: PyYAML not installed. Using JSON format instead.")
    print("Install with: pip install pyyaml")


class CommandGenerator:
    """Generates C++ API command code from templates."""
    
    def __init__(self, template_dir: str = None):
        """Initialize the generator with template directory."""
        if template_dir is None:
            template_dir = Path(__file__).parent / "templates"
        self.template_dir = Path(template_dir)
        self.ensure_templates_exist()
    
    def ensure_templates_exist(self):
        """Create default templates if they don't exist."""
        self.template_dir.mkdir(parents=True, exist_ok=True)
    
    def generate_interface_method(self, command: Dict[str, Any]) -> str:
        """Generate interface method declaration."""
        name = command['name']
        return_type = command.get('return_type', 'void')
        params = command.get('parameters', [])
        
        param_strs = []
        for param in params:
            param_type = param['type']
            param_name = param['name']
            pass_by = param.get('pass_by', 'value')
            
            if pass_by == 'const_ref':
                param_str = f"const {param_type}& {param_name}"
            elif pass_by == 'ref':
                param_str = f"{param_type}& {param_name}"
            else:
                param_str = f"{param_type} {param_name}"
            
            param_strs.append(param_str)
        
        params_str = ', '.join(param_strs)
        const_suffix = ' const' if command.get('is_const', False) else ''
        
        return f"    virtual {return_type} {name}({params_str}){const_suffix} = 0;"
    
    def generate_header_method(self, command: Dict[str, Any]) -> str:
        """Generate class header method declaration."""
        name = command['name']
        return_type = command.get('return_type', 'void')
        params = command.get('parameters', [])
        
        param_strs = []
        for param in params:
            param_type = param['type']
            param_name = param['name']
            pass_by = param.get('pass_by', 'value')
            
            if pass_by == 'const_ref':
                param_str = f"const {param_type}& {param_name}"
            elif pass_by == 'ref':
                param_str = f"{param_type}& {param_name}"
            else:
                param_str = f"{param_type} {param_name}"
            
            param_strs.append(param_str)
        
        params_str = ', '.join(param_strs)
        const_suffix = ' const' if command.get('is_const', False) else ''
        
        return f"    {return_type} {name}({params_str}){const_suffix} override;"
    
    def generate_implementation(self, command: Dict[str, Any]) -> str:
        """Generate implementation method."""
        name = command['name']
        return_type = command.get('return_type', 'void')
        params = command.get('parameters', [])
        command_type = command.get('type', 'custom')
        
        param_strs = []
        for param in params:
            param_type = param['type']
            param_name = param['name']
            pass_by = param.get('pass_by', 'value')
            
            if pass_by == 'const_ref':
                param_str = f"const {param_type}& {param_name}"
            elif pass_by == 'ref':
                param_str = f"{param_type}& {param_name}"
            else:
                param_str = f"{param_type} {param_name}"
            
            param_strs.append(param_str)
        
        params_str = ', '.join(param_strs)
        const_suffix = ' const' if command.get('is_const', False) else ''
        
        # Generate method signature
        code = f"{return_type} OrderCache::{name}({params_str}){const_suffix} {{\n"
        
        # Generate implementation based on command type
        if command_type == 'add':
            code += self._generate_add_implementation(command)
        elif command_type == 'delete':
            code += self._generate_delete_implementation(command)
        elif command_type == 'bulk_delete':
            code += self._generate_bulk_delete_implementation(command)
        elif command_type == 'query':
            code += self._generate_query_implementation(command)
        else:
            code += "    // TODO: Implement custom logic\n"
        
        code += "}\n"
        return code
    
    def _generate_add_implementation(self, command: Dict[str, Any]) -> str:
        """Generate add command implementation."""
        entity = command.get('entity', 'Entity')
        params = command.get('parameters', [])
        indexes = command.get('indexes', [])
        
        if not params:
            return "    // TODO: Add parameters\n"
        
        param_name = params[0]['name']
        id_field = command.get('id_field', f"{param_name}Id")
        
        code = f"    const string& {id_field} = {param_name}.{id_field}();\n"
        code += f"    \n"
        code += f"    if (!(orders_.try_emplace({id_field}, {param_name})).second) {{\n"
        code += f"        throw invalid_argument(\"Duplicate {entity} ID: \" + {id_field});\n"
        code += f"    }}\n"
        code += f"    \n"
        
        for index in indexes:
            index_field = index.replace('ordersBy', '').replace('_', '').lower()
            code += f"    // Update {index} index\n"
            code += f"    auto& indexSet = {index}[{param_name}.{index_field}()];\n"
            code += f"    if (indexSet.empty()) indexSet.reserve(10000);\n"
            code += f"    indexSet.max_load_factor(0.7F);\n"
            code += f"    indexSet.insert({id_field});\n"
            code += f"    \n"
        
        return code
    
    def _generate_delete_implementation(self, command: Dict[str, Any]) -> str:
        """Generate delete command implementation."""
        params = command.get('parameters', [])
        entity = command.get('entity', 'Entity')
        
        if not params:
            return "    // TODO: Add parameters\n"
        
        id_param = params[0]['name']
        
        code = f"    auto orderIt = orders_.find({id_param});\n"
        code += f"    if (orderIt == orders_.end()) {{\n"
        code += f"        throw out_of_range(\"{entity} ID not found: \" + {id_param});\n"
        code += f"    }}\n"
        code += f"    \n"
        code += f"    const Order& order = orderIt->second;\n"
        code += f"    // TODO: Remove from indexes\n"
        code += f"    \n"
        code += f"    orders_.erase(orderIt);\n"
        
        return code
    
    def _generate_bulk_delete_implementation(self, command: Dict[str, Any]) -> str:
        """Generate bulk delete command implementation."""
        params = command.get('parameters', [])
        filter_by = command.get('filter_by', 'attribute')
        
        if not params:
            return "    // TODO: Add parameters\n"
        
        filter_param = params[0]['name']
        index_name = f"ordersBy{filter_by.capitalize()}_"
        
        code = f"    auto indexIt = {index_name}.find({filter_param});\n"
        code += f"    if (indexIt == {index_name}.end()) {{\n"
        code += f"        throw out_of_range(\"Filter value not found: \" + {filter_param});\n"
        code += f"    }}\n"
        code += f"    \n"
        code += f"    auto orderIds = indexIt->second;\n"
        code += f"    \n"
        code += f"    for (const auto& orderId : orderIds) {{\n"
        
        if len(params) > 1:  # Has additional filter
            code += f"        auto orderIt = orders_.find(orderId);\n"
            code += f"        if (orderIt != orders_.end() /* && additional_filter */) {{\n"
            code += f"            cancelOrder(orderId);\n"
            code += f"        }}\n"
        else:
            code += f"        cancelOrder(orderId);\n"
        
        code += f"    }}\n"
        
        return code
    
    def _generate_query_implementation(self, command: Dict[str, Any]) -> str:
        """Generate query command implementation."""
        return_type = command.get('return_type', 'void')
        
        code = f"    // TODO: Implement query logic\n"
        
        if 'vector' in return_type.lower():
            code += f"    {return_type} result;\n"
            code += f"    result.reserve(orders_.size());\n"
            code += f"    // TODO: Populate result\n"
            code += f"    return result;\n"
        elif return_type in ['int', 'unsigned int', 'size_t']:
            code += f"    return 0; // TODO: Implement\n"
        else:
            code += f"    // TODO: Return appropriate value\n"
        
        return code
    
    def generate_test_method(self, command: Dict[str, Any]) -> str:
        """Generate test method."""
        name = command['name']
        
        code = f"void test_{name}() {{\n"
        code += f"    cout << \"Testing {name}...\\n\";\n"
        code += f"    OrderCache cache;\n"
        code += f"    \n"
        code += f"    // TODO: Add test setup\n"
        code += f"    \n"
        code += f"    // TODO: Call {name}\n"
        code += f"    \n"
        code += f"    // TODO: Add assertions\n"
        code += f"    \n"
        code += f"    cout << \"{name} test passed!\\n\";\n"
        code += f"}}\n"
        
        return code
    
    def generate_from_definition(self, definition: Dict[str, Any]) -> Dict[str, str]:
        """Generate all code files from definition."""
        commands = definition.get('commands', [])
        
        results = {
            'interface_methods': [],
            'header_methods': [],
            'implementations': [],
            'tests': []
        }
        
        for command in commands:
            results['interface_methods'].append(
                self.generate_interface_method(command)
            )
            results['header_methods'].append(
                self.generate_header_method(command)
            )
            results['implementations'].append(
                self.generate_implementation(command)
            )
            results['tests'].append(
                self.generate_test_method(command)
            )
        
        return results
    
    def generate_from_file(self, filepath: str) -> Dict[str, str]:
        """Generate code from definition file."""
        path = Path(filepath)
        
        if not path.exists():
            raise FileNotFoundError(f"Definition file not found: {filepath}")
        
        # Load definition
        with open(path, 'r') as f:
            if path.suffix in ['.yaml', '.yml']:
                if not HAS_YAML:
                    raise ImportError("PyYAML required for YAML files")
                definition = yaml.safe_load(f)
            else:
                definition = json.load(f)
        
        return self.generate_from_definition(definition)


def create_example_definition(output_path: str = "command_definition.yaml"):
    """Create an example command definition file."""
    example = {
        'commands': [
            {
                'name': 'addOrder',
                'type': 'add',
                'entity': 'Order',
                'return_type': 'void',
                'parameters': [
                    {
                        'name': 'order',
                        'type': 'Order',
                        'pass_by': 'value'
                    }
                ],
                'id_field': 'orderId',
                'indexes': ['ordersBySecurity_', 'ordersByUser_']
            },
            {
                'name': 'cancelOrder',
                'type': 'delete',
                'entity': 'Order',
                'return_type': 'void',
                'parameters': [
                    {
                        'name': 'orderId',
                        'type': 'string',
                        'pass_by': 'const_ref'
                    }
                ]
            },
            {
                'name': 'cancelOrdersForUser',
                'type': 'bulk_delete',
                'entity': 'Order',
                'filter_by': 'User',
                'return_type': 'void',
                'parameters': [
                    {
                        'name': 'user',
                        'type': 'string',
                        'pass_by': 'const_ref'
                    }
                ]
            },
            {
                'name': 'getAllOrders',
                'type': 'query',
                'return_type': 'vector<Order>',
                'parameters': [],
                'is_const': True
            }
        ]
    }
    
    path = Path(output_path)
    
    if path.suffix in ['.yaml', '.yml']:
        if not HAS_YAML:
            print("PyYAML not available, creating JSON instead")
            path = path.with_suffix('.json')
            with open(path, 'w') as f:
                json.dump(example, f, indent=2)
        else:
            with open(path, 'w') as f:
                yaml.dump(example, f, default_flow_style=False)
    else:
        with open(path, 'w') as f:
            json.dump(example, f, indent=2)
    
    print(f"Example definition created: {path}")
    return str(path)


def interactive_mode():
    """Interactive command generation."""
    print("\n=== OrderCache Command Generator (Interactive Mode) ===\n")
    
    command = {}
    
    # Get command name
    command['name'] = input("Command name (e.g., 'addOrder'): ").strip()
    
    # Get command type
    print("\nCommand types:")
    print("  1. add - Add a new entity")
    print("  2. delete - Delete by ID")
    print("  3. bulk_delete - Delete multiple by filter")
    print("  4. query - Query/retrieve data")
    print("  5. custom - Custom implementation")
    
    type_choice = input("Choose type (1-5): ").strip()
    type_map = {'1': 'add', '2': 'delete', '3': 'bulk_delete', '4': 'query', '5': 'custom'}
    command['type'] = type_map.get(type_choice, 'custom')
    
    # Get return type
    command['return_type'] = input("Return type (default: void): ").strip() or 'void'
    
    # Get parameters
    command['parameters'] = []
    while True:
        param_name = input("\nParameter name (or Enter to finish): ").strip()
        if not param_name:
            break
        
        param_type = input(f"  Type for '{param_name}': ").strip()
        pass_by = input(f"  Pass by (value/const_ref/ref, default: const_ref): ").strip() or 'const_ref'
        
        command['parameters'].append({
            'name': param_name,
            'type': param_type,
            'pass_by': pass_by
        })
    
    # Get entity name if applicable
    if command['type'] in ['add', 'delete', 'bulk_delete']:
        command['entity'] = input("\nEntity name (default: Order): ").strip() or 'Order'
    
    # Generate code
    generator = CommandGenerator()
    
    print("\n=== Generated Code ===\n")
    print("// Interface method:")
    print(generator.generate_interface_method(command))
    print("\n// Header method:")
    print(generator.generate_header_method(command))
    print("\n// Implementation:")
    print(generator.generate_implementation(command))
    print("\n// Test:")
    print(generator.generate_test_method(command))


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Generate C++ API command code for OrderCache'
    )
    parser.add_argument(
        '--config',
        help='Path to command definition file (YAML or JSON)'
    )
    parser.add_argument(
        '--output-dir',
        default='generated',
        help='Output directory for generated code (default: generated)'
    )
    parser.add_argument(
        '--example',
        action='store_true',
        help='Create example definition file'
    )
    parser.add_argument(
        '--interactive',
        action='store_true',
        help='Run in interactive mode'
    )
    
    args = parser.parse_args()
    
    if args.example:
        create_example_definition()
        return
    
    if args.interactive:
        interactive_mode()
        return
    
    if not args.config:
        parser.print_help()
        print("\nTip: Use --example to create an example definition file")
        print("     Use --interactive for interactive mode")
        return
    
    # Generate from config file
    generator = CommandGenerator()
    results = generator.generate_from_file(args.config)
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Write output files
    with open(output_dir / 'interface_methods.cpp', 'w') as f:
        f.write("// Generated interface methods\n\n")
        f.write('\n'.join(results['interface_methods']))
    
    with open(output_dir / 'header_methods.cpp', 'w') as f:
        f.write("// Generated header methods\n\n")
        f.write('\n'.join(results['header_methods']))
    
    with open(output_dir / 'implementations.cpp', 'w') as f:
        f.write("// Generated implementations\n\n")
        f.write('\n'.join(results['implementations']))
    
    with open(output_dir / 'tests.cpp', 'w') as f:
        f.write("// Generated tests\n\n")
        f.write('\n'.join(results['tests']))
    
    print(f"Code generated successfully in: {output_dir}")
    print("\nGenerated files:")
    print(f"  - interface_methods.cpp")
    print(f"  - header_methods.cpp")
    print(f"  - implementations.cpp")
    print(f"  - tests.cpp")


if __name__ == '__main__':
    main()
