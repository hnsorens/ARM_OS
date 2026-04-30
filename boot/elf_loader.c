#include "elf_loader.h"

EFI_STATUS
Load_Kernel(
        IN EFI_SYSTEM_TABLE *SystemTable,
        IN CHAR8 *KernelElfBuffer,
        OUT PAGE_TABLE_T *UpperPageTable
)
{
    return EFI_SUCCESS;
}
