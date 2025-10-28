# Arch Linux specific toolchain
CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

CFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only -Iinclude -O2

build: # MAKE SURE TO USE -E SUDO
	cd edk2 &&	\
	. ./edksetup.sh &&	\
	cd ..	&& \
	ln -sf $(PWD)/OS edk2/OS && \
	bear -- build -a AARCH64 -p OS/OS.dsc -t GCC5
	
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
	sudo cp -f -n edk2/Build/OS/DEBUG_GCC5/AARCH64/Kernel.efi mnt1/efi/boot/BOOTAA64.EFI; \
	sudo cp -r -n boot/* mnt1; \
	sudo cp -r filesystem/* mnt2; \
	sudo umount mnt1 mnt2; \
	sudo rm -rf mnt1 mnt2; \
	sudo losetup -d "$${LOOPDEV}"
	sudo chmod 666 build/disk.img

run: 
	qemu-system-aarch64 -bios QEMU_EFI.fd -drive file=build/disk.img,format=raw,if=virtio -M virt -cpu cortex-a72 -serial stdio -smp 1 -m 16G -device virtio-gpu-pci -display sdl

.PHONY: all clean run
