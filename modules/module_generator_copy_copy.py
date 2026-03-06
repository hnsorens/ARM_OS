import sys
import os

from pycparser import parse_file, c_ast
from pathlib import Path
import json
import re

def json_to_js_object(data, indent=4):
    """Returns a JS-style object string with unquoted keys and proper indentation."""
    
    def format_item(item, level):
        # Current prefix for this level
        spacing = " " * (indent * level)
        next_spacing = " " * (indent * (level + 1))
        
        if isinstance(item, dict):
            if not item:
                return "{}"
            # Format keys without quotes
            lines = []
            for key, value in item.items():
                # Special handling for vtable key
                if key == "vtable" and isinstance(value, str):
                    # Remove quotes from vtable value
                    formatted_value = value  # Keep as-is without extra quotes
                else:
                    formatted_value = format_item(value, level + 1)
                
                lines.append(f"{next_spacing}{key}: {formatted_value}")
            return "{\n" + ",\n".join(lines) + f"\n{spacing}}}"
            
        elif isinstance(item, list):
            if not item:
                return "[]"
            # Format list items
            lines = [f"{next_spacing}{format_item(i, level + 1)}" for i in item]
            return "[\n" + ",\n".join(lines) + f"\n{spacing}]"
            
        elif isinstance(item, str):
            return f'"{item}"'
        elif isinstance(item, bool):
            return 'true' if item else 'false'
        elif item is None:
            return 'null'
        else:
            return str(item)

    return format_item(data, 0)

class StructVisitor(c_ast.NodeVisitor):
    module_types = list()
    file_name = ''
    module_folder = ''
    module_vtable = None
    extension_vtables = list()
    module_json = dict()
    def setFileName(self, folder, file):
        self.module_folder = Path(folder)
        self.file_name = self.module_folder / f'{file}'
        self.extension_vtables.clear()
    def get_prefix(self, text):
        return text.split('_vtable_t')[0]
    def write_module(self, moduleName):
        module_json_path = Path('src') / Path(moduleName) / f'{moduleName}.json'
        with open(module_json_path, 'r') as f:
            json_data = f.read()
            json_file = json.loads(json_data)
            # module_json[f"'{}'"]
            ts_data = json_to_js_object(json_data)
            if f"'{json_file['type']}'" not in self.module_json:
                self.module_json[f"'{json_file['type']}'"] = []
            self.module_json[f"'{json_file['type']}'"].append(json_file)
    def write(self):
        with open('something.ts', 'a') as f:
            ts_data = json_to_js_object(self.module_json)
            f.write('export const moduleCategories: Record<string, ModuleType[]> = ')
            f.write(ts_data)
    def generate(self):
        module_name = self.module_vtable.name.split('_module_t')[0]
        with open('something.ts', 'a') as f:
            f.write(f'const {module_name}: VTableFunction[] = [\n')
            for decl in self.module_vtable.decls:
                if decl.name == 'start' or decl.name == "fetch":
                    continue
                member_name = decl.name
                member_type = self._get_function_type(decl.type, f'{module_name}_{member_name}')
                f.write(f"\t{'{'} name: '{member_name}', signature: '{member_type}', description: '{'something'}', required: true {'}'},\n")
            f.write('];\n\n')
    def visit_Struct(self, node):
        print(node.name)
        if not node.name:
            return
        if (node.name.endswith('_extension_t')):
            self.extension_vtables.append(node)
        if (node.name.endswith('_module_t')):
            print('set')
            self.module_vtable = node
    def _get_function_type(self, n, name):
        if isinstance(n, c_ast.PtrDecl):
            # If the pointer points to a FuncDecl, it's a function pointer
            if isinstance(n.type, c_ast.FuncDecl):
                return self._get_func_signature(n.type, f"{name}")
            return f"{self._get_type(n.type)}*"

        return "...";

    def _get_type(self, n):
        """Recursively resolve the type name with modifiers, specifically handling function pointers."""
        if isinstance(n, c_ast.TypeDecl):
            # Collect modifiers from TypeDecl's declname if present
            modifiers = []
            if hasattr(n, 'quals') and n.quals:
                modifiers.extend(n.quals)
            type_name = self._get_type(n.type)
            if modifiers:
                return f"{' '.join(modifiers)} {type_name}"
            return type_name
        
        elif isinstance(n, c_ast.IdentifierType):
            # IdentifierType might have its own modifiers
            type_name = " ".join(n.names)
            if hasattr(n, 'quals') and n.quals:
                return f"{' '.join(n.quals)} {type_name}"
            return type_name
        
        elif isinstance(n, c_ast.PtrDecl):
            # Collect pointer-specific modifiers
            ptr_modifiers = []
            if hasattr(n, 'quals') and n.quals:
                ptr_modifiers.extend(n.quals)
            
            # Handle function pointers
            if isinstance(n.type, c_ast.FuncDecl):
                func_sig = self._get_func_signature(n.type, '')
                if ptr_modifiers:
                    return f"{' '.join(ptr_modifiers)} {func_sig}"
                return func_sig
            
            # Regular pointer
            pointed_type = self._get_type(n.type)
            
            # Handle pointer-to-const/volatile etc.
            # Format depends on whether the modifier applies to the pointer or the pointee
            if ptr_modifiers:
                # For "const int *" vs "int * const"
                # Check if we're dealing with a TypeDecl with its own modifiers
                if pointed_type.startswith('const ') or pointed_type.startswith('volatile '):
                    # Modifier applies to pointee: "const int *"
                    return f"{pointed_type}*"
                else:
                    # Modifier applies to pointer: "int * const"
                    return f"{pointed_type}* {' '.join(ptr_modifiers)}"
            
            return f"{pointed_type}*"
        
        elif isinstance(n, c_ast.ArrayDecl):
            array_type = self._get_type(n.type)
            # Handle array size if present
            if n.dim:
                array_size = self._get_constant_value(n.dim) if hasattr(self, '_get_constant_value') else str(n.dim)
                return f"{array_type}[{array_size}]"
            return f"{array_type}[]"
        
        elif isinstance(n, c_ast.Struct):
            modifiers = []
            if hasattr(n, 'quals') and n.quals:
                modifiers.extend(n.quals)
            struct_name = n.name if n.name else "anonymous"
            base_type = f"struct {struct_name}"
            if modifiers:
                return f"{' '.join(modifiers)} {base_type}"
            return base_type
        
        elif isinstance(n, c_ast.Union):
            modifiers = []
            if hasattr(n, 'quals') and n.quals:
                modifiers.extend(n.quals)
            union_name = n.name if n.name else "anonymous"
            base_type = f"union {union_name}"
            if modifiers:
                return f"{' '.join(modifiers)} {base_type}"
            return base_type
        
        elif isinstance(n, c_ast.Enum):
            modifiers = []
            if hasattr(n, 'quals') and n.quals:
                modifiers.extend(n.quals)
            enum_name = n.name if n.name else "anonymous"
            base_type = f"enum {enum_name}"
            if modifiers:
                return f"{' '.join(modifiers)} {base_type}"
            return base_type
        
        return "..."


    # Helper method for constant evaluation (add to your class)
    def _get_constant_value(self, node):
        """Extract constant value from expression nodes."""
        if isinstance(node, c_ast.Constant):
            return node.value
        elif isinstance(node, c_ast.UnaryOp):
            if node.op == '-':
                return f"-{self._get_constant_value(node.expr)}"
            elif node.op == '+':
                return f"+{self._get_constant_value(node.expr)}"
        elif isinstance(node, c_ast.BinaryOp):
            left = self._get_constant_value(node.left)
            right = self._get_constant_value(node.right)
            return f"{left}{node.op}{right}"
        return str(node)

    def _get_func_signature(self, func_decl, name):
        """Helper to format function pointer signatures: ret_type (*)(arg_types)"""
        ret_type = self._get_type(func_decl.type)
        
        args = []
        if func_decl.args:
            for param in func_decl.args.params:
                # Handle different types of parameter nodes
                if isinstance(param, c_ast.Typename):
                    # Typename for anonymous parameters like "void" or types without names
                    arg_type = self._get_type(param.type)
                    args.append(arg_type)
                elif isinstance(param, c_ast.Decl):
                    # Regular parameter declaration with name
                    arg_type = self._get_type(param.type)
                    if param.name:
                        args.append(f"{arg_type} {param.name}")
                    else:
                        args.append(arg_type)
                elif isinstance(param, c_ast.ID):
                    # Just an identifier (rare case)
                    args.append(param.name)
                else:
                    # Fallback: get whatever type we can
                    args.append(self._get_type(param))
        else:
            args.append("void")
            
        return f"{ret_type} (*{name})( {', '.join(args)} )"


def parse_header(folder, filename):
    print(f'FILE: {folder}/{filename}')
    dir_path = Path(folder)
    ast = parse_file(f'{folder}/{filename}', use_cpp=True)
    visitor = StructVisitor()
    visitor.setFileName(f'{folder}', f'{filename}')
    visitor.visit(ast)
    visitor.generate()

if __name__ == "__main__":
    with open('something.ts', 'w') as f:
        f.write("import { ModuleType, VTableFunction } from '../types'\n\n")
        f.close()
    folder_path = 'includes'
    for item_name in os.listdir(folder_path):
        item_path = os.path.join(folder_path, item_name)
        if os.path.isdir(item_path):
            print(item_name)
            parse_header(f'{folder_path}/{item_name}', f'{item_name}.h')
    folder_path = 'src'
    visitor = StructVisitor()
    for item_name in os.listdir(folder_path):
        item_path = os.path.join(folder_path, item_name)
        if os.path.isdir(item_path):
            print(item_name)
            visitor.write_module(f'{item_name}')
    visitor.write()
