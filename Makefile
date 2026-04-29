# --- Toolchain ---
CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

# --- Paths (Arch Linux) ---
SYSROOT    = /usr/aarch64-linux-gnu
EFI_INC    = $(SYSROOT)/include/efi
EFI_LIB    = $(SYSROOT)/lib
QEMU_FW    = /usr/share/edk2-armvirt/aarch64/QEMU_EFI.fd

# --- Files ---
TARGET     = main.efi
IMG        = disk.img

# NEW: Automatically find all .c files in boot/ and define their .o counterparts
SRCS       = $(wildcard boot/*.c)
OBJS       = $(SRCS:.c=.o)

# --- Flags ---
CFLAGS     = -I$(EFI_INC) -I$(EFI_INC)/aarch64 -I$(EFI_INC)/protocol \
             -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
             -fshort-wchar -mgeneral-regs-only -fno-builtin -c

LDFLAGS    = -nostdlib -znocombreloc -T $(EFI_LIB)/elf_aarch64_efi.lds \
             -shared -Bsymbolic -L$(EFI_LIB) -lgnuefi -lefi \
             -e efi_main --no-warn-rwx-segments

.PHONY: all clean run

all: $(TARGET) $(IMG)

# NEW: Pattern rule to compile any .c file in boot/ to an .o file
boot/%.o: boot/%.c
	$(CC) $(CFLAGS) $< -o $@

# Link and Convert
$(TARGET): $(OBJS)
	# 1. Link all objects together with crt0
	$(LD) $(EFI_LIB)/crt0-efi-aarch64.o $(OBJS) $(LDFLAGS) -o temp.so
	# 2. Convert to EFI (PE/COFF)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .dynamic \
	           -j .dynstr -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
	           --input-target=elf64-littleaarch64 \
	           --output-target=efi-app-aarch64 temp.so $(TARGET)
	@rm temp.so

# Disk Creation
$(IMG): $(TARGET)
	@rm -f $(IMG)
	truncate -s 64M $(IMG)
	sgdisk -n 1:2048:0 -t 1:ef00 $(IMG)
	mformat -i $(IMG)@@1M -F -v "ESP" ::
	mmd -i $(IMG)@@1M ::/EFI
	mmd -i $(IMG)@@1M ::/EFI/BOOT
	mcopy -i $(IMG)@@1M $(TARGET) ::/EFI/BOOT/BOOTAA64.EFI

# Emulation
run: $(IMG)
	qemu-system-aarch64 -m 16G -cpu cortex-a72 -M virt \
	    -bios $(QEMU_FW) \
	    -display sdl \
	    -device virtio-gpu-pci \
	    -device virtio-keyboard-pci \
	    -drive file=$(IMG),format=raw,if=none,id=d0 \
	    -device virtio-blk-device,drive=d0 \
	    -serial stdio

clean:
	rm -f boot/*.o *.so *.efi $(IMG)
