# --- Toolchain ---
CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
CLANG   = clang

# --- Paths ---
SYSROOT    = /usr/aarch64-linux-gnu
EFI_INC    = $(SYSROOT)/include/efi
EFI_LIB    = $(SYSROOT)/lib
QEMU_FW    = /usr/share/edk2/aarch64/QEMU_EFI.fd

# Build Directory
BUILD_DIR  = build

# --- Targets ---
BOOTLOADER = $(BUILD_DIR)/bootloader.efi
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
IMG        = disk.img
KERNEL_INI = kernel.ini

# --- Flags ---
EFI_CFLAGS  = -target aarch64-unknown-windows -I$(EFI_INC) \
              -ffreestanding -fshort-wchar -mgeneral-regs-only \
              -fno-stack-protector -fno-builtin -c -mstrict-align

EFI_LDFLAGS = -target aarch64-unknown-windows -fuse-ld=lld-link -nostdlib \
              -Wl,-entry:efi_main -Wl,-subsystem:efi_application

KFLAGS      = -ffreestanding -fno-stack-protector -fno-stack-check \
              -mgeneral-regs-only -fno-builtin -nostdlib -mcmodel=large \
              -fno-pic -fno-plt -c

# Use the kernel linker script for both kernel and modules
K_LDFLAGS   = -static -T kernel.ld -nostdlib --emit-relocs

# --- File Discovery ---
BOOT_SRCS   = $(wildcard boot/*.c)
BOOT_OBJS   = $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_SRCS))

KERNEL_SRCS = $(wildcard kernel/*.c)
KERNEL_OBJS = $(patsubst kernel/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_SRCS))

MODULE_DIRS = $(wildcard modules/*/)
MODULE_ELFS = $(patsubst modules/%/, $(BUILD_DIR)/modules/%.elf, $(MODULE_DIRS))

.PHONY: all clean run dirs

all: dirs $(BOOTLOADER) $(MODULE_ELFS) $(IMG)

# Create the build directory structure
dirs:
	@mkdir -p $(BUILD_DIR)/boot
	@mkdir -p $(BUILD_DIR)/modules

# --- 1. BOOTLOADER BUILD ---
$(BUILD_DIR)/boot/%.o: boot/%.c
	clang-format -i $<
	$(CLANG) $(EFI_CFLAGS) $< -o $@

$(BOOTLOADER): $(BOOT_OBJS)
	@echo "Linking Bootloader"
	$(CLANG) $(EFI_LDFLAGS) $(BOOT_OBJS) -o $@

# --- 2. MODULES BUILD (One ELF per module folder) ---
$(MODULE_ELFS): $(BUILD_DIR)/modules/%.elf:
	$(eval SUB_DIR_NAME := $(patsubst $(BUILD_DIR)/modules/%.elf, %, $@))
	$(eval SRC_DIR := modules/$(SUB_DIR_NAME))
	$(eval OBJ_DIR := $(BUILD_DIR)/modules/$(SUB_DIR_NAME))
	@mkdir -p $(OBJ_DIR)
	@echo "Linking Module Executable: $(SUB_DIR_NAME) -> $@"
	@for src in $(wildcard $(SRC_DIR)/*.c); do \
		clang-format -i $$src; \
		obj=$(OBJ_DIR)/$$(basename $${src%.c}.o); \
		$(CC) $(KFLAGS) $$src -o $$obj; \
	done
	$(LD) $(K_LDFLAGS) $(OBJ_DIR)/*.o -o $@
	@rm -rf $(OBJ_DIR)

# --- 3. DISK IMAGE ---
$(IMG): $(BOOTLOADER) $(MODULE_ELFS)
	@echo "Building Disk Image"
	@rm -f $(IMG)
	truncate -s 128M $(IMG)
	sgdisk -o $(IMG)
	sgdisk -n 1:2048:262110 -t 1:ef00 $(IMG)
	mformat -i $(IMG)@@1M -F -H 2048 -c 1 -v "ESP" ::
	mmd -i $(IMG)@@1M ::/EFI
	mmd -i $(IMG)@@1M ::/EFI/BOOT
	mmd -i $(IMG)@@1M ::/modules
	# Copy Core Files
	mcopy -i $(IMG)@@1M $(BOOTLOADER) ::/EFI/BOOT/BOOTAA64.EFI
	mcopy -i $(IMG)@@1M $(KERNEL_INI) ::/kernel.ini
	# Copy all separate Module ELFs
	@for mod in $(MODULE_ELFS); do \
		echo "Adding module: $$mod"; \
		mcopy -i $(IMG)@@1M $$mod ::/modules/; \
	done

run: $(IMG)
	qemu-system-aarch64 -m 16G -cpu cortex-a72 -M virt -bios $(QEMU_FW) \
		-serial stdio -drive file=$(IMG),format=raw,if=none,id=d0 \
		-device virtio-blk-device,drive=d0 \
		-gdb tcp::1234

clean:
	rm -rf $(BUILD_DIR) $(IMG)
