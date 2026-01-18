import sys

from pycparser import parse_file, c_ast
from pathlib import Path

class StructVisitor(c_ast.NodeVisitor):
    module_types = list()
    def get_prefix(self, text):
        return text.split('_vtable_t')[0]
    def visit_Struct(self, node):
        if node.coord and str(node.coord).startswith('includes/module_vtables.h'):
            if node.decls:
                module_name = self.get_prefix(node.name)
                strcture_file_path = Path("includes/modules/structures") / f"{module_name}.h"
                open(strcture_file_path, 'a').close()
                file_path = Path("includes/modules") / f"{module_name}.h"
                self.module_types.append(f"{module_name.upper()}")
                open(file_path, 'w').close()
                with open(file_path, 'a', encoding='utf-8') as f:
                    f.write(f"#ifndef {module_name.upper()}_H\n#define {module_name.upper()}_H\n")
                    f.write(f'#include "modules/structures/{module_name}.h"\n#include "module_vtables.h"\n\n#ifndef {module_name.upper()}\n#define {module_name.upper()} {module_name}\n#endif\n\n#define EXPAND(var) var\n#define CONCAT(a, b) a##b\n#define CONCAT_EXPAND(a, b) CONCAT(a, b)\n\n#ifdef __MAIN__\n\n#define __{module_name.upper()}__DEF(prefix) \\\n')
                    for decl in node.decls:
                        # Skip members named 'init'
                        if decl.name == 'init':
                            continue
                        
                        member_name = decl.name
                        member_type = self._get_function_type(decl.type, f"CONCAT_EXPAND(prefix, _{member_name})")
                        f.write(f'__attribute__((visibility("hidden"))) {member_type} = 0; \\\n')
                    f.write(f"\\\nstatic void {module_name}_fetch(kernel_vtable_t *kvtable){'{'}\\\n")
                    f.write(f"\t{node.name}* module = ({node.name}*)kvtable->find_module_vtable_by_type(MODULE_{module_name.upper()});\\\n")
                    for decl in node.decls:
                        if decl.name == 'init':
                            continue
                        
                        member_name = decl.name
                        f.write(f"\tCONCAT_EXPAND(prefix, _{member_name}) = module->{member_name};\\\n");
                    f.write(f'{'}'}\n\n__{module_name.upper()}__DEF({module_name.upper()}) \n#undef __{module_name.upper()}__DEF\n\n#else\n\n#define __{module_name.upper()}__DEF(prefix) \\\n')
                    for decl in node.decls:
                        # Skip members named 'init'
                        if decl.name == 'init':
                            continue
                        
                        member_name = decl.name
                        member_type = self._get_function_type(decl.type, f"CONCAT_EXPAND(prefix, _{member_name})")
                        f.write(f'__attribute__((visibility("hidden"))) extern {member_type}; \\\n')
                    f.write(f'\n\n__{module_name.upper()}__DEF({module_name.upper()}) \n#undef __{module_name.upper()}__DEF\n\n#endif\n#endif')
    def _get_function_type(self, n, name):
        if isinstance(n, c_ast.PtrDecl):
            # If the pointer points to a FuncDecl, it's a function pointer
            if isinstance(n.type, c_ast.FuncDecl):
                return self._get_func_signature(n.type, f"{name}")
            return f"{self._get_type(n.type)}*"

        return "...";

    def _get_type(self, n):
        """Recursively resolve the type name, specifically handling function pointers."""
        if isinstance(n, c_ast.TypeDecl):
            return self._get_type(n.type)
        
        elif isinstance(n, c_ast.IdentifierType):
            return " ".join(n.names)
        
        elif isinstance(n, c_ast.PtrDecl):
            # If the pointer points to a FuncDecl, it's a function pointer
            if isinstance(n.type, c_ast.FuncDecl):
                return self._get_func_signature(n.type, '')
            return f"{self._get_type(n.type)}*"
        
        elif isinstance(n, c_ast.ArrayDecl):
            return f"{self._get_type(n.type)}[]"
            
        return "..."

    def _get_func_signature(self, func_decl, name):
        """Helper to format function pointer signatures: ret_type (*)(arg_types)"""
        ret_type = self._get_type(func_decl.type)
        
        args = []
        if func_decl.args:
            for arg in func_decl.args.params:
                # Handle cases like 'void' in func(void) which are Typename nodes
                arg_node = arg.type if hasattr(arg, 'type') else arg
                args.append(self._get_type(arg_node))
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
