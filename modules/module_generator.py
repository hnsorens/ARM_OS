import os

from pycparser import parse_file, c_ast
from pathlib import Path

class StructVisitor(c_ast.NodeVisitor):
    module_types = list()
    file_name = ''
    module_folder = ''
    module_ops = None
    extension_ops = list()
    def setFileName(self, folder, file):
        self.module_folder = Path(folder)
        self.file_name = self.module_folder / f'{file}'
        self.extension_ops.clear()
    def get_prefix(self, text):
        return text.split('_ops')[0]
    def generate(self):
        with open(self.file_name, 'r') as f:
            source_lines = f.readlines()
        module_name = self.module_ops.name.split('_ops')[0]
        print(f"adding {module_name.upper()}")
        self.module_types.append(f"{module_name.upper()}")
        module_types_path = self.module_folder / f"{module_name}_types.h"
        open(module_types_path, 'a').close()
        module_inc_path = self.module_folder / f"{module_name}_inc.h"
        open(module_inc_path, 'w').close()
        with open(module_inc_path, 'a', encoding='utf-8') as f:
            f.write(f'#ifndef __{module_name.upper()}_INC_H__\n')
            f.write(f'#define __{module_name.upper()}_INC_H__\n\n')
            f.write('\n')
            f.write(f'#include "{module_name}_types.h"\n\n')
            f.write('#ifndef CONCAT_HIDDEN\n')
            f.write('#define CONCAT_HIDDEN(a, b) a ## b\n')
            f.write('#define CONCAT(a, b) CONCAT_HIDDEN(a, b)\n')
            f.write('#endif\n\n')
            for decl in self.module_ops.decls:
                if decl.name == 'start':
                    continue
                f.write(f'#define {module_name}_{decl.name} CONCAT({module_name.upper()}_NAME, _{decl.name}_func)\n')
            f.write('\n')
            current_line = 0
            current_decl_line = self.module_ops.coord.line+1

            for decl in self.module_ops.decls:
                if decl.name == 'start' or decl.name == "fetch":
                    continue
                current_line = decl.coord.line
                for line_num in range(current_decl_line, current_line - 1):
                    f.write(f'{source_lines[line_num].rstrip()} \n')
                member_name = decl.name
                member_type = self._get_function_type(decl.type, f'{module_name}_{member_name}')
                f.write(f'extern {member_type};\n')
                current_decl_line = current_line
            for ext in self.extension_ops:
                ext_name = ext.name.split('_ext')[0]
                f.write(f'#ifdef {ext_name.upper()}_EXTENSION\n')
                for decl in ext:
                    if decl.name == 'start' or decl.name == "fetch":
                        continue
                    current_line = decl.coord.line
                    for line_num in range(current_decl_line, current_line - 1):
                        f.write(f'{source_lines[line_num].rstrip()} \n')
                    member_name = decl.name
                    member_type = self._get_function_type(decl.type, f'{module_name}_{member_name}')
                    f.write(f'extern {member_type};\n')
                    current_decl_line = current_line
                f.write('#endif\n')
            f.write('#endif')
        module_impl_path = self.module_folder / f"{module_name}_impl.h"
        open(module_impl_path, 'w').close()
        with open(module_impl_path, 'a', encoding='utf-8') as f:
            f.write(f'#ifndef __{module_name.upper()}_INC_H__\n')
            f.write(f'#define __{module_name.upper()}_INC_H__\n\n')
            f.write('\n')
            f.write(f'#include "{module_name}_types.h"\n\n')
            f.write('#ifndef CONCAT_HIDDEN\n')
            f.write('#define CONCAT_HIDDEN(a, b) a ## b\n')
            f.write('#define CONCAT(a, b) CONCAT_HIDDEN(a, b)\n')
            f.write('#endif\n\n')
            for decl in self.module_ops.decls:
                if decl.name == 'start':
                    continue
                f.write(f'#define {module_name}_{decl.name} CONCAT(IMPL_NAME, _{decl.name}_func)\n')
            f.write('\n')
            current_line = 0
            current_decl_line = self.module_ops.coord.line+1

            for decl in self.module_ops.decls:
                if decl.name == 'start' or decl.name == "fetch":
                    continue
                current_line = decl.coord.line
                for line_num in range(current_decl_line, current_line - 1):
                    f.write(f'{source_lines[line_num].rstrip()} \n')
                member_name = decl.name
                member_type = self._get_function_type(decl.type, f'{module_name}_{member_name}')
                f.write(f'__attribute__((used)) {member_type};\n')
                current_decl_line = current_line
            for ext in self.extension_ops:
                ext_name = ext.name.split('_ext')[0]
                f.write(f'#ifdef {ext_name.upper()}_EXTENSION\n')
                for decl in ext:
                    if decl.name == 'start' or decl.name == "fetch":
                        continue
                    current_line = decl.coord.line
                    for line_num in range(current_decl_line, current_line - 1):
                        f.write(f'{source_lines[line_num].rstrip()} \n')
                    member_name = decl.name
                    member_type = self._get_function_type(decl.type, f'{module_name}_{member_name}')
                    f.write(f'__attribute__((used)) {member_type};\n')
                    current_decl_line = current_line
                f.write('#endif\n')
            f.write('typedef void (*init_fn_t)(void);\n')
            f.write('#define __init_func __attribute__((section(".init_array"), used))\n')
            f.write('#define MODULE_INIT(func) \\\n')
            f.write('static init_fn_t __init_ptr##func __init_func = func;\n')
            f.write('#endif')
    def visit_Struct(self, node):
        print(node.name)
        if not node.name:
            return
        if (node.name.endswith('_ext')):
            self.extension_ops.append(node)
        if (node.name.endswith('_ops')):
            print('set')
            self.module_ops = node
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
            
        return f"{ret_type} {name}( {', '.join(args)} )"
    def create_modules_enum(self):
        file_path = Path("include") / "module_enum.h"
        open(file_path, 'w').close()
        with open(file_path, 'a', encoding='utf-8') as f:
            f.write('#ifndef MODULE_ENUM_H\n#define MODULE_ENUM_H\n\ntypedef enum module_type_t {\n\tMODULE_KERNEL_CORE,\n')
            for module in self.module_types:
                f.write(f"\tMODULE_{module},\n")
            f.write('} module_type_t;\n\n')
            f.write('\n\n#endif')
        file_path = Path("include") / "module_names.h"
        open(file_path, 'w').close()
        with open(file_path, 'a', encoding='utf-8') as f:
            f.write('#ifndef MODULE_NAME_H\n#define MODULE_NAME_H\n\n[[gnu::unused]]')
            f.write('\nstatic char* module_names[] = {\n\t"KERNEL_CORE",\n')
            for module in self.module_types:
                f.write(f'\t"{module}",\n')
            f.write('};\n\n#endif')

if __name__ == "__main__":
    folder_path = 'include'
    visitor = StructVisitor()
    for item_name in os.listdir(folder_path):
        item_path = os.path.join(folder_path, item_name)
        if os.path.isdir(item_path):
            print(item_name)
            folder = f'{folder_path}/{item_name}'
            print(f'FILE: {folder}/{item_name}.h')
            dir_path = Path(folder)
            ast = parse_file(f'{folder}/{item_name}.h', use_cpp=True)
            visitor.setFileName(f'{folder}', f'{item_name}.h')
            visitor.visit(ast)
            visitor.generate()


    visitor.create_modules_enum()
