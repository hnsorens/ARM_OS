#include "pt.h"
#include "../modules.h"
#include "../../include/api/mmu.h"
#include "../../include/api/pmm.h"

IMPORT_INTERFACE_ANY(pmm, pmm);

EXPORT_INTERFACE(mmu, MemoryManagementUnit,
		 {
			 .alloc = pt_alloc,
			 .free = pt_free,
			 .copy = pt_copy,
			 .set_user_ctx = pt_set_user_ctx,
			 .set_kernel_ctx = pt_set_kernel_ctx,
			 .map = pt_map,
			 .unmap = pt_unmap,
			 .protect = pt_protect,
			 .translate = pt_translate,
			 .flush = pt_flush,
			 .invalidate = pt_invalidate,
			 .set_mair = pt_set_mair,
		 });
