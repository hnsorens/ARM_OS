#include "pt.h"
#include "../modules.h"

#include "../../include/api/mmu.h"
#include "../../include/api/pmm.h"

IMPORT_INTERFACE_ANY(pmm, pmm);

EXPORT_INTERFACE(mmu, MemoryManagementUnit, {
        .table_alloc = pt_alloc,
        .table_free = pt_free,
        .table_copy = pt_copy,
        .set_user_context = pt_set_user_ctx,
        .set_kernel_context = pt_set_kernel_ctx,
        .map = pt_map,
        .unmap = pt_unmap,
        .protect = pt_protect,
        .translate = pt_translate,
        .flush = pt_flush,
        .invalidate = pt_invalidate,
        .set_mair = pt_set_mair,
        });
