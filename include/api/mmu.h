#ifndef MMU_API_H
#define MMU_API_H

#include <type.h>

enum mmu_flags
{
    // If this bit is 0, the page is Read/Write. If the user passes MMU_RO, it sets Bit 7 to 1 (Read-Only).
    MMU_RO            = (1ULL << 7),  
    
    // Maps directly to AP[1] (Bit 6). 1 = User space accessible, 0 = Kernel only.
    MMU_USER          = (1ULL << 6),  

    // Maps directly to UXN (Unprivileged Execute Never - Bit 54)
    MMU_NO_EXEC       = (1ULL << 54), 

    // Bits 2-4 select the MAIR (Memory Attribute Indirection Register) cache profiles
    MMU_NOCACHE       = (1ULL << 2),  // Points to MAIR slot 1 (Device memory)
    MMU_WRITE_THROUGH = (2ULL << 2),  // Points to MAIR slot 2 (Write-Through)
};

enum page_size
{
    PS_4KB = 0x1000,
    PS_2MB = 0x200000,
    PS_1GB = 0x40000000,
};

typedef struct mmu_interface
{
    int (*alloc)(u64 *out_root);
    int (*free)(u64 root);
    int (*copy)(u64 src_root, u64 *dest_root);

    int (*set_user_ctx)(u64 root, u16 acid);
    int (*set_kernel_ctx)(u64 root, u16 acid);
    int (*get_user_ctx)(u64 *root);
    int (*get_kernel_ctx)(u64 *root);

    int (*map)(u64 root, u64 virt, u64 phys, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);
    int (*unmap)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size);
    int (*protect)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);

    int (*translate)(u64 root, u64 virt, u64 *phys_out, enum mmu_flags *flags_out);

    int (*flush)(void);
    int (*invalidate)(u64 virt, u64 pg_count, enum page_size pg_size);
    int (*set_mair)(u64 mair);
} mmu_interface_t;

#endif
