all:
# 	@echo "Running module generator..."
# 	@python3 module_generator.py || (echo "Python script failed!"; exit 1)
	
	@bash -c '\
	mkdir -p build; \
	for dir in src/*/; do \
		name=$$(basename "$$dir"); \
		echo "Building $$name..."; \
		src_files=""; \
		for f in "$$dir"*.c "$$dir"*.s; do \
			if [ -f "$$f" ]; then src_files="$$src_files $$f"; fi; \
		done; \
		if [ -n "$$src_files" ]; then \
			echo "  Compiling Position Independent Binary..."; \
			aarch64-linux-gnu-gcc \
        -std=c23 -fPIC \
        -Iincludes -Isrc \
        -shared \
        -Wl,-Bsymbolic \
        -o "build/$$name.so" \
        $$src_files; \
# 			aarch64-linux-gnu-gcc \
# 				-std=c23 \
# 				-ffreestanding -nostdlib -nostartfiles -fno-builtin \
# 				-fpie -fno-stack-protector -mgeneral-regs-only -fvisibility=hidden -fno-zero-initialized-in-bss \
# 				-mcmodel=small \
# 				-Iincludes -Isrc \
# 				-Wl,-pie \
# 				-Wl,--no-dynamic-linker \
# 				-Wl,-T,"$$dir/module.ld" \
# 				-Wl,-Bsymbolic \
# 				-o "$$dir$$name.elf" \
# 				-o "build/$$name.elf" \
# 				$$src_files; \
# 			aarch64-linux-gnu-objcopy -O binary "$$dir$$name.elf" "build/$$name.efi"; \
			rm -f "$$dir$$name.elf"; \
		else \
			echo "No source files found in $$dir"; \
		fi; \
	done'

