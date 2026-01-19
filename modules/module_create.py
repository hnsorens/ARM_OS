#!/usr/bin/env python3
"""
Creates kernel module files in src/{name}/ directory.
Usage: python create_module.py <module_name>
"""

import os
import sys
from datetime import datetime

def create_module_files(module_name, module_type):
    """
    Create module files in src/{module_name}/ directory
    """
    
    # Create directory path
    module_dir = f"src/{module_name}"
    
    # Create directory if it doesn't exist
    try:
        os.makedirs(module_dir, exist_ok=True)
        print(f"✓ Created directory: {module_dir}/")
    except Exception as e:
        print(f"✗ Error creating directory: {e}")
        return False
    
    # Create {module_name}.c file content
    c_file_content = f"""/*
 * {module_name}.c - Kernel module implementation
 * 
 * Created: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
 * Author: Auto-generated script
 */

#include "module.h"

vtable({module_type}_vtable_t);
start(init, {module_name}_fetch, {module_name}_init);

#define __MAIN__
// ------ Includes for other modules ------

#undef __MAIN__

void init({module_type}_vtable_t *vtable)
{'{'}
  // Set Vtable Functions
{'}'}

void {module_name}_fetch(kernel_vtable_t *kvtable)
{'{'}
  // Fetch Modules
{'}'}

void {module_name}_init(kernel_vtable_t *kvtable)
{'{'}
  // Initialize Module
{'}'}

"""
    
    # Create module.ld file content
    ld_file_content = f"""/*
 * module.ld - Linker script for {module_name} module
 * 
 * Created: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
 */

SECTIONS
{'{'}
    . = 0x0;              
    .text : {'{'} 
      *(.text._entry) 
      *(.text*) 
    {'}'}    /* First thing is .text section */
    .data : {'{'} *(.data*) {'}'}
    .bss : {'{'} *(.bss*) *(COMMON) {'}'}
{'}'}

"""
    
    # File paths
    c_file_path = os.path.join(module_dir, f"{module_name}.c")
    ld_file_path = os.path.join(module_dir, "module.ld")
    
    files_created = 0
    
    try:
        # Create {module_name}.c file
        with open(c_file_path, 'w') as f:
            f.write(c_file_content)
        print(f"✓ Created: {c_file_path}")
        files_created += 1
        
        # Create module.ld file
        with open(ld_file_path, 'w') as f:
            f.write(ld_file_content)
        print(f"✓ Created: {ld_file_path}")
        files_created += 1
        
        # Print summary
        print(f"\n✓ Successfully created {files_created} files in {module_dir}/")
        print(f"  - {module_name}.c ({len(c_file_content)} bytes)")
        print(f"  - module.ld ({len(ld_file_content)} bytes)")
        
        return True
        
    except PermissionError:
        print(f"✗ Error: Permission denied for writing to {module_dir}/")
    except Exception as e:
        print(f"✗ Error creating files: {e}")
    
    return False

def main():
    # Check if module name was provided as argument
    if len(sys.argv) < 3:
        print("Error: Please provide a module name as an argument.")
        print(f"Usage: {sys.argv[0]} <module_name>")
        print(f"Example: {sys.argv[0]} mymodule")
        print(f"\nThis will create:")
        print(f"  src/mymodule/mymodule.c")
        print(f"  src/mymodule/module.ld")
        sys.exit(1)
    
    module_name = sys.argv[1]
    module_type = sys.argv[2]
    
    # Validate module name (basic check)
    if not module_name.isidentifier():
        print(f"Error: '{module_name}' is not a valid identifier for a module name.")
        print("Please use alphanumeric characters and underscores, starting with a letter.")
        sys.exit(1)
    
    print(f"Creating module: {module_name}")
    print("-" * 40)
    
    if create_module_files(module_name, module_type):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()