all:
	for dir in */; do \
		name=$$(basename "$$dir"); \
		echo "Building $$name..."; \
		obj_files=""; \
		for c_file in "$$dir"*.c; do \
			if [ -f "$$c_file" ]; then \
				base_name=$$(basename "$$c_file" .c); \
				aarch64-linux-gnu-gcc -std=gnu99 -ffreestanding -nostdlib -nostartfiles -fno-stack-protector -mgeneral-regs-only -fpic -fpie -c "$$c_file" -o "$$dir$$base_name.o"; \
				obj_files="$$obj_files $$dir$$base_name.o"; \
			fi; \
		done; \
		if [ -n "$$obj_files" ]; then \
			aarch64-linux-gnu-ld -T module.ld -o "$$dir$$name.elf" $$obj_files; \
			aarch64-linux-gnu-objcopy -O binary "$$dir$$name.elf" ../module_executables/"$$name.efi"; \
			rm $$obj_files "$$dir$$name.elf"; \
		fi; \
	done