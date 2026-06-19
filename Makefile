# --- Toolchain ---
CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
CLANG   = clang

# --- Paths ---
PROJECT_ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SYSROOT        = /usr/aarch64-linux-gnu
EFI_INC        = $(SYSROOT)/include/efi
MODULE_API_INC = $(PROJECT_ROOT)/include
MODULE_INC     = $(PROJECT_ROOT)/modules
BUILD_DIR      = build
QEMU_FW        = /usr/share/edk2/aarch64/QEMU_EFI.fd

# --- Targets ---
BOOTLOADER = $(BUILD_DIR)/bootloader.efi
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
              -fno-pic -fno-plt -c -I$(MODULE_INC) -I$(MODULE_API_INC)

K_LDFLAGS   = -static -T kernel.ld -nostdlib --emit-relocs

# --- Recursive File Discovery ---
BOOT_SRCS   := $(shell find boot -name '*.c' 2>/dev/null)
BOOT_OBJS   := $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_SRCS))

MODULE_DIRS  := $(wildcard modules/*/)
MODULE_NAMES := $(patsubst modules/%/,%,$(MODULE_DIRS))
MODULE_ELFS  := $(patsubst %, $(BUILD_DIR)/modules/%.elf, $(MODULE_NAMES))

.PHONY: all clean run dirs

# The @ at the start of recipes keeps them silent unless we print explicit status lines
all: dirs $(BOOTLOADER) $(MODULE_ELFS) $(IMG)

dirs:
	@mkdir -p $(BUILD_DIR)
	@if [ -d "boot" ]; then mkdir -p $(sort $(dir $(BOOT_OBJS))); fi
	@mkdir -p $(BUILD_DIR)/modules

# --- 1. Bootloader Build Rules ---

$(BUILD_DIR)/boot/%.o: boot/%.c
	@mkdir -p $(dir $@)
	@clang-format -i $<
	@echo "  CC      [boot]    $<"
	@$(CLANG) $(EFI_CFLAGS) -Iboot $< -o $@

$(BOOTLOADER): $(BOOT_OBJS)
	@echo "  LD      [boot]    $(BOOTLOADER)"
	@$(CLANG) $(EFI_LDFLAGS) $(BOOT_OBJS) -o $@

# --- 2. Modules Build Rules Template ---

define MODULE_RULE
$(1)_SRC_FILES := $$(shell find modules/$(1) -name '*.c' 2>/dev/null)
$(1)_OBJ_FILES := $$(patsubst modules/$(1)/%.c, $$(BUILD_DIR)/modules/$(1)/%.o, $$($(1)_SRC_FILES))

# Rule to link the ELF from the object files
$$(BUILD_DIR)/modules/$(1).elf: $$($(1)_OBJ_FILES)
	@echo "  MOD_LD  [$(1)]    $$(BUILD_DIR)/modules/$(1).elf"
	@$$(LD) $$(K_LDFLAGS) $$^ -o $$@

# Rule to compile the source files into object files
$$(BUILD_DIR)/modules/$(1)/%.o: modules/$(1)/%.c
	@mkdir -p $$(dir $$@)
	@clang-format -i $$<
	@echo "  CC      [$(1)]    $$<"
	@$$(CC) $$(KFLAGS) -Imodules/$(1) $$< -o $$@
endef

# Apply the template for every module folder
$(foreach mod,$(MODULE_NAMES),$(eval $(call MODULE_RULE,$(mod))))

# --- 3. Disk Image Creation ---

$(IMG): $(BOOTLOADER) $(MODULE_ELFS)
	@echo "  IMAGE   Generating $(IMG)..."
	@rm -f $(IMG)
	@truncate -s 128M $(IMG) > /dev/null 2>&1
	@sgdisk -o $(IMG) > /dev/null 2>&1
	@sgdisk -n 1:2048:262110 -t 1:ef00 $(IMG) > /dev/null 2>&1
	@mformat -i $(IMG)@@1M -F -H 2048 -c 1 -v "ESP" :: > /dev/null 2>&1
	@mmd -i $(IMG)@@1M ::/EFI > /dev/null 2>&1
	@mmd -i $(IMG)@@1M ::/EFI/BOOT > /dev/null 2>&1
	@mmd -i $(IMG)@@1M ::/modules > /dev/null 2>&1
	@mcopy -i $(IMG)@@1M $(BOOTLOADER) ::/EFI/BOOT/BOOTAA64.EFI > /dev/null 2>&1
	@if [ -f $(KERNEL_INI) ]; then mcopy -i $(IMG)@@1M $(KERNEL_INI) ::/kernel.ini > /dev/null 2>&1; fi
	@for mod in $(MODULE_ELFS); do \
		mcopy -i $(IMG)@@1M $$mod ::/modules/ > /dev/null 2>&1; \
	done
	@echo "  READY   $(IMG) is built successfully."

run: $(IMG)
	@echo "  QEMU    Launching virtual machine..."
	@qemu-system-aarch64 -m 16G -cpu cortex-a72 -smp 4 -M virt -accel tcg,thread=multi -bios $(QEMU_FW) \
		-drive file=$(IMG),format=raw,if=none,id=d0 \
		-device virtio-blk-device,drive=d0 -mem-prealloc \
		-gdb tcp::1234 -nographic

clean:
	@echo "  CLEAN   Removing target build trees..."
	@rm -rf $(BUILD_DIR) $(IMG)

test: KFLAGS += -DTESTING

test: run
