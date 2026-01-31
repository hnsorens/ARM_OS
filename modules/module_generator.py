import sys

from pycparser import parse_file, c_ast
from pathlib import Path

class StructVisitor(c_ast.NodeVisitor):
    module_types = list()
    def get_prefix(self, text):
        return text.split('_vtable_t')[0]
    def visit_Struct(self, node):
        with open('includes/module_vtables.h', 'r') as f:
            source_lines = f.readlines()
        if node.coord and str(node.coord).startswith('includes/module_vtables.h'):
            if node.decls:
                current_decl_line = node.coord.line + 3
                current_decl_line2 = node.coord.line + 3
                module_name = self.get_prefix(node.name)
                strcture_file_path = Path("includes/modules/structures") / f"{module_name}.h"
                open(strcture_file_path, 'a').close()
                file_path = Path("includes/modules") / f"{module_name}.h"
                self.module_types.append(f"{module_name.upper()}")
                open(file_path, 'w').close()
                with open(file_path, 'a', encoding='utf-8') as f:
                    f.write(f"#ifndef {module_name.upper()}_H\n#define {module_name.upper()}_H\n")
                    f.write(f'\n#include "modules/structures/{module_name}.h"\n#include "module_vtables.h"\n\n#ifndef {module_name.upper()}\n#define {module_name.upper()} {module_name}\n#endif\n\n#define EXPAND(var) var\n#define CONCAT(a, b) a##b\n#define CONCAT_EXPAND(a, b) CONCAT(a, b)\n\n#ifdef __MAIN__\n#define GLOBAL __attribute__((visibility("hidden")))\n#define END = 0;\n#else\n#define GLOBAL __attribute__((visibility("hidden"))) extern \n#define END ;\n#endif\n\n')
                    for decl in node.decls:
                        # Skip members named 'init'
                        if decl.name == 'init' or decl.name == "fetch":
                            continue
                        current_line = decl.coord.line
                        for line_num in range(current_decl_line, current_line - 1):
                            f.write(f'{source_lines[line_num].rstrip()} \n')
                        member_name = decl.name
                        member_type = self._get_function_type(decl.type, f"CONCAT_EXPAND({module_name.upper()}, _{member_name})")
                        f.write(f'GLOBAL {member_type} END \n')
                        current_decl_line = current_line
                    f.write(f'#ifdef __MAIN__\n\n')
                    f.write(f"static void {module_name}_fetch(kernel_vtable_t *kvtable){'{'}\n")
                    f.write(f"\t{node.name}* module = ({node.name}*)kvtable->find_module_vtable_by_type(MODULE_{module_name.upper()});\n")
                    for decl in node.decls:
                        if decl.name == 'init' or decl.name == "fetch":
                            continue
                        
                        member_name = decl.name
                        f.write(f"\tCONCAT_EXPAND({module_name.upper()}, _{member_name}) = module->{member_name};\n");
                    f.write(f'{'}'}\n#endif\n')
                    f.write(f'\n#undef GLOBAL\n#undef {module_name.upper()}\n\n#endif')
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
    def create_modules_enum(self):
        file_path = Path("includes") / "module_enum.h"
        open(file_path, 'w').close()
        with open(file_path, 'a', encoding='utf-8') as f:
            f.write('#ifndef MODULE_ENUM_H\n#define MODULE_ENUM_H\n\ntypedef enum module_type_t {\n\tMODULE_KERNEL_CORE,\n')
            for module in self.module_types:
                f.write(f"\tMODULE_{module},\n")
            f.write('} module_type_t;\n\n')
            f.write('\n\n#endif')
        file_path = Path("includes") / "module_names.h"
        open(file_path, 'w').close()
        with open(file_path, 'a', encoding='utf-8') as f:
            f.write('#ifndef MODULE_NAME_H\n#define MODULE_NAME_H\n\n[[gnu::unused]]')
            f.write('\nstatic char* module_names[] = {\n\t"KERNEL_CORE",\n')
            for module in self.module_types:
                f.write(f'\t"{module}",\n')
            f.write('};\n\n#endif')


def parse_header(filename):
    dir_path = Path("includes/modules")
    dir_path.mkdir(exist_ok=True)

    ast = parse_file(filename, use_cpp=True)
    visitor = StructVisitor()
    visitor.visit(ast)

    visitor.create_modules_enum()

if __name__ == "__main__":
    parse_header('includes/module_vtables.h')
