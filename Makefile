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

# NEW: Build Directory
BUILD_DIR  = build

# --- Targets ---
BOOTLOADER = $(BUILD_DIR)/bootloader.efi
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
IMG        = disk.img

# --- Flags ---
EFI_CFLAGS  = -target aarch64-unknown-windows -I$(EFI_INC) \
              -ffreestanding -fshort-wchar -mgeneral-regs-only \
              -fno-stack-protector -fno-builtin -c -mstrict-align

EFI_LDFLAGS = -target aarch64-unknown-windows -fuse-ld=lld-link -nostdlib \
              -Wl,-entry:efi_main -Wl,-subsystem:efi_application

KERNEL_ADDR = 0xFFFFFFFF80000000
KFLAGS      = -ffreestanding -fno-stack-protector -fno-stack-check \
              -mgeneral-regs-only -fno-builtin -nostdlib -mcmodel=large \
              -fno-pic -fno-plt -c
K_LDFLAGS   = -static -Ttext $(KERNEL_ADDR) -e _start -nostdlib

# --- File Discovery ---
BOOT_SRCS   = $(wildcard boot/*.c)
BOOT_OBJS   = $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_SRCS))

MODULE_DIRS = $(wildcard modules/*/)
# This creates build/modules/serial_debug.o for example
MODULE_COMBINED_OBJS = $(patsubst modules/%/, $(BUILD_DIR)/modules/%.o, $(MODULE_DIRS))

.PHONY: all clean run dirs

all: dirs $(BOOTLOADER) $(KERNEL_BIN) $(IMG)

# Create the build directory structure
dirs:
	@mkdir -p $(BUILD_DIR)/boot
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/modules
	@for dir in $(MODULE_DIRS); do mkdir -p $(BUILD_DIR)/$$dir; done

# --- 1. BOOTLOADER BUILD ---
$(BUILD_DIR)/boot/%.o: boot/%.c
	$(CLANG) $(EFI_CFLAGS) $< -o $@

$(BOOTLOADER): $(BOOT_OBJS)
	@echo "Linking Bootloader with Clang/LLD"
	$(CLANG) $(EFI_LDFLAGS) $(BOOT_OBJS) -o $@

# --- 2. KERNEL BUILD (Aggregate into build/kernel/kernel.o) ---
$(BUILD_DIR)/kernel/kernel.o: $(wildcard kernel/*.c)
	@echo "Combining Kernel objects into $@"
	@for src in $^; do \
		obj=$(BUILD_DIR)/kernel/$$(basename $${src%.c}.tmp.o); \
		$(CC) $(KFLAGS) $$src -o $$obj; \
	done
	$(LD) -r $(BUILD_DIR)/kernel/*.tmp.o -o $@
	@rm $(BUILD_DIR)/kernel/*.tmp.o

# --- 3. MODULES BUILD (One .o per module folder) ---
$(MODULE_COMBINED_OBJS): $(BUILD_DIR)/modules/%.o:
	$(eval SUB_DIR_NAME := $(patsubst $(BUILD_DIR)/modules/%.o, %, $@))
	$(eval SRC_DIR := modules/$(SUB_DIR_NAME))
	$(eval OBJ_DIR := $(BUILD_DIR)/modules/$(SUB_DIR_NAME))
	@mkdir -p $(OBJ_DIR)
	@echo "Combining Module $(SRC_DIR) into $@"
	@for src in $(wildcard $(SRC_DIR)/*.c); do \
		obj=$(OBJ_DIR)/$$(basename $${src%.c}.tmp.o); \
		$(CC) $(KFLAGS) $$src -o $$obj; \
	done
	$(LD) -r $(OBJ_DIR)/*.tmp.o -o $@
	@rm -rf $(OBJ_DIR)

# --- 4. FINAL KERNEL LINK ---
$(KERNEL_BIN): $(BUILD_DIR)/kernel/kernel.o $(MODULE_COMBINED_OBJS)
	@echo "Linking final Kernel binary"
	$(LD) $(K_LDFLAGS) $^ -o $(BUILD_DIR)/kernel.bin

# --- 5. DISK IMAGE ---
$(IMG): $(BOOTLOADER) $(KERNEL_BIN)
	@rm -f $(IMG)
	truncate -s 128M $(IMG)
	sgdisk -o $(IMG)
	sgdisk -n 1:2048:262110 -t 1:ef00 $(IMG)
	mformat -i $(IMG)@@1M -F -H 2048 -c 1 -v "ESP" ::
	mmd -i $(IMG)@@1M ::/EFI
	mmd -i $(IMG)@@1M ::/EFI/BOOT
	mcopy -i $(IMG)@@1M $(BOOTLOADER) ::/EFI/BOOT/BOOTAA64.EFI
	mcopy -i $(IMG)@@1M $(KERNEL_BIN) ::/kernel.bin

run: $(IMG)
	qemu-system-aarch64 -m 16G -cpu cortex-a72 -M virt -bios $(QEMU_FW) \
		-serial stdio -drive file=$(IMG),format=raw,if=none,id=d0 \
		-device virtio-blk-device,drive=d0

clean:
	rm -rf $(BUILD_DIR) $(IMG)
