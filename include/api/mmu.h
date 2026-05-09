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

typedef struct mmu_interface
{
    k_status_t (*table_alloc)(phys_addr_t *out_root);
    k_status_t (*table_free)(phys_addr_t root);
    k_status_t (*table_copy)(phys_addr_t src_root, phys_addr_t *dest_root);

    k_status_t (*activate)(phys_addr_t root, uint16_t acid);

    k_status_t (*map)(phys_addr_t root, virt_addr_t v, phys_addr_t p, size_t sz, mmu_flags_t f);
    k_status_t (*unmap)(phys_addr_t root, virt_addr_t v, size_t sz);
    k_status_t (*protect)(phys_addr_t root, virt_addr_t v, size_t sz, mmu_flags_t f);

    k_status_t (*translate)(phys_addr_t root, virt_addr_t v, phys_addr_t *out_p, mmu_flags_t *out_f);

    k_status_t (*flush_tlb)(void);
    k_status_t (*tlb_invalidate)(virt_addr_t v, size_t sz);
    k_status_t (*set_mair)(uint8_t index, uint8_t attr);
} mmu_interface_t;

#endif
