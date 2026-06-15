git submodule update --init --recursive
cd edk2
source edksetup.sh
make -C BaseTools
cd ..
wget -O QEMU_EFI.fd https://releases.linaro.org/components/kernel/uefi-linaro/16.02/release/qemu64/QEMU_EFI.fd
