[Defines]
  PLATFORM_NAME = OS
  PLATFORM_GUID = 12345678-1234-1234-1234-123456789abc
  PLATFORM_VERSION = 1.0
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/OS
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = DEBUG

[Packages]
  MdePkg/MdePkg.dec
  OS/OS.dec

[Components]
  OS/kernel/kernel.inf

[LibraryClasses]
  KernelMemory|OS/memory/memory.inf
  KernelLoader|OS/kernelLoader/kernelLoader.inf
  KernelDataStructures|OS/data_structures/data_structures.inf
