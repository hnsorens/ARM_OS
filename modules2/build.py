import os
import json
import subprocess
import shutil

def build_kernel(config_path):
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    sets = config['compiler_settings']
    module_list = config['modules']
    
    if os.path.exists(sets['output_dir']):
        shutil.rmtree(sets['output_dir'])
    os.makedirs(sets['output_dir'])

    obj_files = []

    for mod in module_list:
        folder = mod['folder']
        name = mod['name']
        defines = [f"-DIMPL_NAME={name}"]
        
        for dep in mod.get('dependencies', []):
            dep_type = dep['type'].upper()
            dep_name = dep['name']
            defines.append(f"-D{dep_type}_NAME=\"{dep_name}\"")

        print(f"--- Building Module: {name} ---")
        module_src_path = os.path.join(sets['base_src_dir'], folder)
        
        for root, _, files in os.walk(module_src_path):
            for file in files:
                if file.endswith('.c'):
                    src_file = os.path.join(root, file)
                    # Flatten object name
                    obj_name = src_file.replace(os.sep, '_').replace('.c', '.o')
                    obj_path = os.path.join(sets['output_dir'], obj_name)

                    compile_cmd = [
                        sets['compiler'],
                        *sets['cflags'],
                        f"-I{sets['shared_include']}",
                        f"-I{sets['base_src_dir']}",
                        *defines,
                        "-c", src_file,
                        "-o", obj_path
                    ]

                    if subprocess.run(compile_cmd).returncode != 0:
                        return
                    obj_files.append(obj_path)

    # Final Linking Phase
    print("--- Linking PIE Kernel ELF ---")
    kernel_elf = os.path.join(sets['output_dir'], "kernel.elf")
    
    # We use GCC as the linker driver to pass flags to LD correctly
    link_cmd = [
        sets['compiler'],
        *sets['cflags'], # Reuse freestanding/nostdlib flags
        "-T", sets['linker_script'],
        "-o", kernel_elf,
        # Pass the specific linker flags through -Wl
        *[f"-Wl,{flag}" for flag in sets['ldflags']],
        *obj_files
    ]
    
    if subprocess.run(link_cmd).returncode == 0:
        print(f"Successfully built: {kernel_elf}")
        # Verify it's an ELF and check for the interpreter
        subprocess.run(["file", kernel_elf])
    else:
        print("Linker error occurred.")

if __name__ == "__main__":
    build_kernel('kernel_config.json')
