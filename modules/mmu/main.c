#include "page_table.h"
#include "../modules.h"

#include "../../include/api/mmu.h"
#include "../../include/api/pmm.h"

IMPORT_INTERFACE_ANY(pmm, pmm);

EXPORT_INTERFACE(mmu, MemoryManagementUnit, {
        .table_alloc = table_alloc,
        .table_free = table_free,
        .table_copy = table_copy,
        .activate = activate,
        .map = map,
        .unmap = unmap,
        .protect = protect,
        .translate = translate,
        .flush_tlb = flush_tlb,
        .tlb_invalidate = tlb_invalidate,
        .set_mair = set_mair,
        });
