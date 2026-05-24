#ifndef MMU_API_H
#define MMU_API_H

#include "../type.h"

enum mmu_flags
{
    MMU_READ          = (1 << 0),
    MMU_WRITE         = (1 << 1),
    MMU_EXEC          = (1 << 2),
    MMU_USER          = (1 << 3),
    MMU_NOCACHE       = (1 << 4),
    MMU_WRITE_THROUGH = (1 << 5),
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

    int (*map)(u64 root, u64 virt, u64 phys, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);
    int (*unmap)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size);
    int (*protect)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);

    int (*translate)(u64 root, u64 virt, u64 *phys_out, enum mmu_flags *flags_out);

    int (*flush)(void);
    int (*invalidate)(u64 virt, u64 pg_count, enum page_size pg_size);
    int (*set_mair)(u64 mair);
} mmu_interface_t;

#endif
