#include "slab.h"
#include "../../include/api/slab.h"
#include "../modules.h"

EXPORT_INTERFACE(slab, SlabAllocator, {
        .create_cache = k_slab_create_cache,
        .destroy_cache = k_slab_destroy_cache,
        .alloc = k_slab_alloc,
        .free = k_slab_free,
        .shrink = k_slab_shrink,
        });
