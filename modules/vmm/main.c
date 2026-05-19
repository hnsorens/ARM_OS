#include "vmm.h"
#include "../modules.h"

#include "../../include/api/vmm.h"
#include "../../include/api/pmm.h"
#include "../../include/api/mmu.h"

EXPORT_INTERFACE(vmm, VirtualMemoryManager, {
    .space_create = vmm_space_create,
    .space_destroy = vmm_space_destroy,
    .allocate      = vmm_allocate,
    .reserve       = vmm_reserve,
    .free          = vmm_free,
    .resize        = vmm_resize,
    .map_external  = vmm_map_external,
    .protect       = vmm_protect,
    .query         = vmm_query,
    .activate      = vmm_activate,
    .sync          = vmm_sync
});

IMPORT_INTERFACE_ANY(pmm, pmm);
IMPORT_INTERFACE_ANY(mmu, mmu);
