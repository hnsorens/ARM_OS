all:
	for dir in */; do \
		name=$$(basename "$$dir"); \
		echo "Building $$name..."; \
		aarch64-linux-gnu-gcc -std=gnu99 -ffreestanding -nostdlib -nostartfiles -fno-stack-protector -mgeneral-regs-only -fno-pic -fno-pie -c "$$dir"*.c -o "$$dir$$name.o"; \
		aarch64-linux-gnu-ld -T module.ld -o "$$dir$$name.elf" "$$dir$$name.o"; \
		aarch64-linux-gnu-objcopy -O binary "$$dir$$name.elf" ../module_executables/"$$name.efi"; \
		rm "$$dir$$name.o" "$$dir$$name.elf"; \
	done
