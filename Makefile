# Arch Linux specific toolchain
CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

CFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only -Iinclude -O2

build: # MAKE SURE TO USE -E SUDO
	cd edk2 &&	\
	. ./edksetup.sh &&	\
	cd ..	&& \
	ln -sf $(PWD)/boot edk2/boot && \
	build -a AARCH64 -p boot/boot.dsc -t GCC5

	mkdir -p module_executables

	make -C modules
	
	mkdir -p build

	# Create disk image
	dd if=/dev/zero of=build/disk.img bs=1M count=128
	sudo parted build/disk.img --script mklabel gpt
	sudo parted build/disk.img --script mkpart primary fat32 1MiB 64MiB
	sudo parted build/disk.img --script mkpart primary 64MiB 100%
	LOOPDEV=$$(sudo losetup --find --partscan --show build/disk.img); \
	sudo mkfs.fat -F32 "$${LOOPDEV}p1"; \
	sudo mkfs.ext2 "$${LOOPDEV}p2"; \
	mkdir -p mnt1 mnt2; \
	sudo mount "$${LOOPDEV}p1" mnt1; \
	sudo mount "$${LOOPDEV}p2" mnt2; \
	sudo mkdir -p mnt1/efi/boot; \
	sudo cp -f -n edk2/Build/Bootloader/DEBUG_GCC5/AARCH64/BootLoader.efi mnt1/efi/boot/BOOTAA64.EFI; \
	sudo cp -r -n module_executables/* mnt1; \
	sudo cp -r filesystem/* mnt2; \
	sudo umount mnt1 mnt2; \
	sudo rm -rf mnt1 mnt2; \
	sudo losetup -d "$${LOOPDEV}"
	sudo chmod 666 build/disk.img
	# sudo rm -rf module_executables

run: 
	qemu-system-aarch64 \
  -bios QEMU_EFI.fd \
  -M virt,gic-version=3 \
  -cpu cortex-a72 \
  -drive file=build/disk.img,format=raw,if=none,id=disk0 \
  -device virtio-blk-device,drive=disk0 \
  -serial mon:stdio \
  -smp 4 \
  -m 16G \
  -device virtio-gpu-device \
  -display sdl \
  -gdb tcp::1234

.PHONY: all clean run
