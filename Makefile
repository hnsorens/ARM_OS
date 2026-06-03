# --- Toolchain ---
CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
CLANG   = clang

# --- Paths ---
SYSROOT    = /usr/aarch64-linux-gnu
EFI_INC    = $(SYSROOT)/include/efi
BUILD_DIR  = build
QEMU_FW    = /usr/share/edk2/aarch64/QEMU_EFI.fd

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
              -fno-pic -fno-plt -c

K_LDFLAGS   = -static -T kernel.ld -nostdlib --emit-relocs

# --- Recursive File Discovery ---
BOOT_SRCS   := $(shell find boot -name '*.c' 2>/dev/null)
BOOT_OBJS   := $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_SRCS))

MODULE_DIRS := $(wildcard modules/*/)
MODULE_NAMES := $(patsubst modules/%/,%,$(MODULE_DIRS))
MODULE_ELFS := $(patsubst %, $(BUILD_DIR)/modules/%.elf, $(MODULE_NAMES))

.PHONY: all clean run dirs

all: dirs $(BOOTLOADER) $(MODULE_ELFS) $(IMG)

dirs:
	@mkdir -p $(BUILD_DIR)
	@if [ -d "boot" ]; then mkdir -p $(sort $(dir $(BOOT_OBJS))); fi
	@mkdir -p $(BUILD_DIR)/modules

# --- 1. Bootloader Build Rules ---

$(BUILD_DIR)/boot/%.o: boot/%.c
	@mkdir -p $(dir $@)
	clang-format -i $<
	$(CLANG) $(EFI_CFLAGS) -Iboot $< -o $@

$(BOOTLOADER): $(BOOT_OBJS)
	@echo "Linking Bootloader: $@"
	$(CLANG) $(EFI_LDFLAGS) $(BOOT_OBJS) -o $@

# --- 2. Modules Build Rules (Corrected Template) ---

define MODULE_RULE
# Define local variables for this specific module
$(1)_SRC_FILES := $(shell find modules/$(1) -name '*.c' 2>/dev/null)
$(1)_OBJ_FILES := $$(patsubst modules/$(1)/%.c, $(BUILD_DIR)/modules/$(1)/%.o, $$($(1)_SRC_FILES))

# Rule to link the ELF from the object files
$(BUILD_DIR)/modules/$(1).elf: $$($(1)_OBJ_FILES)
	@echo "Linking Module ELF: $(1)"
	$(LD) $(K_LDFLAGS) $$^ -o $$@

# Rule to compile the source files into object files
$(BUILD_DIR)/modules/$(1)/%.o: modules/$(1)/%.c
	@mkdir -p $$(dir $$@)
	clang-format -i $$<
	$(CC) $(KFLAGS) -Imodules/$(1) $$< -o $$@
endef

# Apply the template for every module folder
$(foreach mod,$(MODULE_NAMES),$(eval $(call MODULE_RULE,$(mod))))

# --- 3. Disk Image Creation ---

$(IMG): $(BOOTLOADER) $(MODULE_ELFS)
	@echo "Building Disk Image: $(IMG)"
	@rm -f $(IMG)
	truncate -s 128M $(IMG)
	sgdisk -o $(IMG)
	sgdisk -n 1:2048:262110 -t 1:ef00 $(IMG)
	mformat -i $(IMG)@@1M -F -H 2048 -c 1 -v "ESP" ::
	mmd -i $(IMG)@@1M ::/EFI
	mmd -i $(IMG)@@1M ::/EFI/BOOT
	mmd -i $(IMG)@@1M ::/modules
	mcopy -i $(IMG)@@1M $(BOOTLOADER) ::/EFI/BOOT/BOOTAA64.EFI
	if [ -f $(KERNEL_INI) ]; then mcopy -i $(IMG)@@1M $(KERNEL_INI) ::/kernel.ini; fi
	@for mod in $(MODULE_ELFS); do \
		echo "Adding module: $$mod"; \
		mcopy -i $(IMG)@@1M $$mod ::/modules/; \
	done

run: $(IMG)
	qemu-system-aarch64 -m 16G -cpu cortex-a72 -smp 4 -M virt -accel tcg,thread=multi -bios $(QEMU_FW) \
		-serial stdio -drive file=$(IMG),format=raw,if=none,id=d0 \
		-device virtio-blk-device,drive=d0 -mem-prealloc\
		-gdb tcp::1234

clean:
	rm -rf $(BUILD_DIR) $(IMG)
