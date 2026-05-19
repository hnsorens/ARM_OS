#include "heap.h"
#include "../modules.h"
#include "../../include/api/heap.h"
#include "../../include/api/vmm.h"

IMPORT_INTERFACE_ANY(vmm, vmm);

EXPORT_INTERFACE(heap, Heap,
		 { .malloc = heap_malloc,
		   .free = heap_free,
		   .realloc = heap_realloc,
		   .memalign = heap_memalign,
		   .get_stats = heap_get_stats });
