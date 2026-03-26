import os
import json
import subprocess
import shutil

def build_kernel(config_path):
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    sets = config['compiler_settings']
    module_list = config['modules']
    
    # Setup build directory
    if os.path.exists(sets['output_dir']):
        shutil.rmtree(sets['output_dir'])
    os.makedirs(sets['output_dir'])

    obj_files = []

    for mod in module_list:
        folder = mod['folder']
        name = mod['name']
        
        # Start with the implementation name for the current module
        defines = [f"-DIMPL_NAME={name}"]
        
        # Add explicit dependencies from the JSON list
        for dep in mod.get('dependencies', []):
            dep_type = dep['type'].upper()
            dep_name = dep['name']
            defines.append(f"-D{dep_type}_NAME=\"{dep_name}\"")

        print(f"--- Building Module: {name} (from {folder}) ---")

        module_src_path = os.path.join(sets['base_src_dir'], folder)
        
        # Recursive search for .c files
        for root, _, files in os.walk(module_src_path):
            for file in files:
                if file.endswith('.c'):
                    src_file = os.path.join(root, file)
                    
                    # Create a flat object name to avoid folder nesting issues in build/
                    obj_name = src_file.replace(os.sep, '_').replace('.c', '.o')
                    obj_path = os.path.join(sets['output_dir'], obj_name)

                    compile_cmd = [
                        sets['compiler'],
                        *sets['cflags'],
                        "-fPIE",
                        f"-I{sets['shared_include']}",
                        f"-I{sets['base_src_dir']}",
                        *defines,
                        "-c", src_file,
                        "-o", obj_path
                    ]

                    # Run the compiler
                    res = subprocess.run(compile_cmd)
                    if res.returncode != 0:
                        print(f"Exiting: Compilation failed for {src_file}")
                        return
                    
                    obj_files.append(obj_path)

    # Final Linking Phase
    print("--- Linking Kernel ELF ---")
    kernel_elf = os.path.join(sets['output_dir'], "kernel.elf")
    # Using the compiler as a linker driver is often safer for aarch64
    link_cmd = [sets['linker'], "-o", kernel_elf, "-pie", "-T", sets['linker_script'], *obj_files]
    
    if subprocess.run(link_cmd).returncode == 0:
        print(f"Build Finished: {kernel_elf}")
    else:
        print("Linker error occurred.")

if __name__ == "__main__":
    build_kernel('kernel_config.json')
