#ifndef MMU_API_H
#define MMU_API_H

#include "../type.h"

typedef enum mmu_flags
{
    MMU_READ          = (1 << 0),
    MMU_WRITE         = (1 << 1),
    MMU_EXEC          = (1 << 2),
    MMU_USER          = (1 << 3),
    MMU_NOCACHE       = (1 << 4),
    MMU_WRITE_THROUGH = (1 << 5),
} mmu_flags_t;

typedef enum page_size
{
    PS_4KB = 0x1000,
    PS_2MB = 0x200000,
    PS_1GB = 0x40000000,
} page_size_t;

typedef uint16_t asid_t;

typedef struct mmu_interface
{
    k_status_t (*alloc)(paddr_t *out_root);
    k_status_t (*free)(paddr_t root);
    k_status_t (*copy)(paddr_t src_root, paddr_t *dest_root);

    k_status_t (*set_user_ctx)(paddr_t root, asid_t acid);
    k_status_t (*set_kernel_ctx)(paddr_t root, asid_t acid);

    k_status_t (*map)(paddr_t root, vaddr_t virt, paddr_t phys, uint64_t pg_count, page_size_t pg_size, mmu_flags_t flags);
    k_status_t (*unmap)(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size);
    k_status_t (*protect)(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size, mmu_flags_t flags);

    k_status_t (*translate)(paddr_t root, vaddr_t virt, paddr_t *phys_out, mmu_flags_t *flags_out);

    k_status_t (*flush)(void);
    k_status_t (*invalidate)(vaddr_t virt, uint64_t pg_count, page_size_t pg_size);
    k_status_t (*set_mair)(uint64_t mair);
} mmu_interface_t;

#endif
