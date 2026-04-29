[Defines]
  PLATFORM_NAME = OS
  PLATFORM_GUID = 12345678-1234-1234-1234-123456789abc
  FILE_GUID = 12345678-1234-5678-9ABC-DEF123456789
  PLATFORM_VERSION = 1.0
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/Bootloader
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = DEBUG

[Packages]
  MdePkg/MdePkg.dec
  MdeModulePkg/MdeModulePkg.dec
  boot/boot.dec

[Components]
  boot/bootloader/bootloader.inf

[BuildOptions]
  *_*_*_CC_FLAGS = -std=c23 $(FLAGS)

[LibraryClasses.common]
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf