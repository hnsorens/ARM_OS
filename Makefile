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

# --- Recursive File Discovery (Bootloader) ---
BOOT_CSRCS   := $(shell find boot -name '*.c' 2>/dev/null)
BOOT_SSRCS   := $(shell find boot -name '*.S' 2>/dev/null)
BOOT_OBJS    := $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_CSRCS)) \
                $(patsubst boot/%.S, $(BUILD_DIR)/boot/%.o, $(BOOT_SSRCS))

# --- Recursive File Discovery (Modules) ---
# Find all directories inside modules/ containing either .c OR .S files
MODULE_SOURCE_DIRS := $(shell find modules -type f \( -name '*.c' -o -name '*.S' \) -exec dirname {} \; | sort -u)

MODULE_NAMES := $(subst /,.,$(patsubst modules/%,%,$(MODULE_SOURCE_DIRS)))
MODULE_ELFS  := $(patsubst %, $(BUILD_DIR)/modules/%.elf, $(MODULE_NAMES))

.PHONY: all clean run dirs

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

$(BUILD_DIR)/boot/%.o: boot/%.S
	@mkdir -p $(dir $@)
	@echo "  AS      [boot]    $<"
	@$(CLANG) $(EFI_CFLAGS) -Iboot $< -o $@

$(BOOTLOADER): $(BOOT_OBJS)
	@echo "  LD      [boot]    $(BOOTLOADER)"
	@$(CLANG) $(EFI_LDFLAGS) $(BOOT_OBJS) -o $@

# --- 2. Hierarchical Modules Build Rules Template ---

# $(1) = Dot-separated module name (e.g., memory.allocators.heap)
# $(2) = Original path containing the source files (e.g., modules/memory/allocators/heap)
define MODULE_RULE
$(1)_C_FILES   := $$(shell find $(2) -maxdepth 1 -name '*.c' 2>/dev/null)
$(1)_S_FILES   := $$(shell find $(2) -maxdepth 1 -name '*.S' 2>/dev/null)

$(1)_OBJ_FILES := $$(patsubst $(2)/%.c, $$(BUILD_DIR)/modules/$(1)/%.o, $$($(1)_C_FILES)) \
                  $$(patsubst $(2)/%.S, $$(BUILD_DIR)/modules/$(1)/%.o, $$($(1)_S_FILES))

# Rule to link the final flat ELF file into the build/modules directory
$$(BUILD_DIR)/modules/$(1).elf: $$($(1)_OBJ_FILES)
	@echo "  MOD_LD  [$(1)]    $$(BUILD_DIR)/modules/$(1).elf"
	@$$(LD) $$(K_LDFLAGS) $$^ -o $$@

# Rule to compile C files
$$(BUILD_DIR)/modules/$(1)/%.o: $(2)/%.c
	@mkdir -p $$(dir $$@)
	@clang-format -i $$<
	@echo "  CC      [$(1)]    $$<"
	@$$(CC) $$(KFLAGS) -I$(2) $$< -o $$@

# Rule to assemble S files
$$(BUILD_DIR)/modules/$(1)/%.o: $(2)/%.S
	@mkdir -p $$(dir $$@)
	@echo "  AS      [$(1)]    $$<"
	@$$(CC) $$(KFLAGS) -I$(2) $$< -o $$@
endef

# Dynamic evaluation loop passing BOTH the dotted name and original path to the template
$(foreach dir,$(MODULE_SOURCE_DIRS),\
    $(eval $(call MODULE_RULE,$(subst /,.,$(patsubst modules/%,%,$(dir))),$(dir)))\
)

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
	@qemu-system-aarch64 -m 16G -cpu cortex-a72 -smp 4 \
		-M virt,gic-version=3 \
		-accel tcg,thread=multi -bios $(QEMU_FW) \
		-drive file=$(IMG),format=raw,if=none,id=d0 \
		-device virtio-blk-device,drive=d0 -mem-prealloc \
		-gdb tcp::1234 -nographic

clean:
	@echo "  Removing target build trees..."
	@rm -rf $(BUILD_DIR) $(IMG)

test: KFLAGS += -DTESTING
test: run
