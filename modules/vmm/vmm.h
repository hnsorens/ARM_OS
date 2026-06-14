#ifndef VMM_H
#define VMM_H

#include "../../include/api/mmu.h"
#include "../../include/api/vmm.h"

typedef struct boot_region {
    u64 base;       /**< The starting virtual address of the region */
    u64 size;       /**< The total size of the region in bytes */
    u32 flags;      /**< Hardware MMU page execution/write permissions (e.g., MMU_READ) */
    u32 type;       /**< Semantic purpose of the zone (e.g., VMM_REGION_CODE, VMM_REGION_STACK) */
} boot_region_t;

/* --- Linux-Style Architectural Virtual Boundary Configurations --- */
#define VMM_USER_SPACE_MIN           0x0000000000001000ULL 
#define VMM_USER_SPACE_MAX           0x00007FFFFFFFF000ULL 
#define VMM_DEFAULT_ALIGNMENT        4096

/* Mock type define for spinlock tracking. Map this to your kernel's lock mechanism. */
typedef u32 spinlock_t;

/* Modern high-performance Binary Search Tree VMA tracker structure */
typedef struct vm_area {
    u64          base;             /* Segment starting virtual address */
    u64          size;             /* Segment total size in bytes */
    enum mmu_flags      flags;     /* Node architectural MMU permissions */
    enum vmm_region_type type;     /* Memory backing categorization type */
    bool         is_paged;         /* Allocation physical backing state */
    bool         in_use;           /* Slab allocator availability tracking state */
    
    /* * TREE POINTERS: We use next and prev as aliases for right and left children
     * to keep seamless compatibility with layout functions without breaking types.
     */
    struct vm_area  *next;         /* Pointer alias for RIGHT child node in the BST */
    struct vm_area  *prev;         /* Pointer alias for LEFT child node in the BST */
} vm_area_t;

/* Address space descriptor map containing tracking anchors */
typedef struct vmm_space {
    u64          page_table_root;  /* Level 0 table root physical address */
    vm_area_t       *mmap_cache;       /* Fast lookup translation reference descriptor cache */
    vm_area_t       *vma_head;         /* Root node pointer of the balanced sorted VMA tree */
    spinlock_t       lock;              /* Mutual exclusion synchronization primitive */
    bool             in_use;            /* Descriptor registration state flag */
} vmm_space_t;

/* --- Exported Virtual Memory Manager Interface API --- */

int vmm_init(u64 boot_pt_root, const boot_region_t *regions, int region_count);
int vmm_space_create(u64 *out_table_root);
int vmm_space_destroy(u64 table_root);
int vmm_allocate(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags, enum vmm_region_type type);
int vmm_reserve(u64 root, u64 vaddr, u64 sz);
int vmm_free(u64 root, u64 vaddr, u64 sz);
int vmm_resize(u64 root, u64 vaddr, u64 old_sz, u64 new_sz);
int vmm_map_external(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f);
int vmm_protect(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags);
int vmm_query(u64 root, u64 vaddr, struct vmm_region_info *out_info);
int vmm_activate(u64 root);
int vmm_sync(u64 root, u64 vaddr, u64 sz);

#endif /* VMM_H */
