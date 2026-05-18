#include "../modules.h"

#include "../../include/api/pmm.h"
#include "../boot_info.h"

#include "pmm.h"

#define HHDM_OFFSET 0xFFFF800000000000

int main(boot_info_t *boot_info) {
    pmm_init(boot_info->memory_regions, boot_info->memory_map_size, HHDM_OFFSET);
}

EXPORT_INTERFACE(pmm, idk, {
        .alloc_page = pmm_alloc_page,
        .free_page = pmm_free_page,
        .alloc_aligned = pmm_alloc_aligned,
        .alloc_in_range = pmm_alloc_in_range,
        .retain = pmm_retain,
        .release = pmm_release,
        .get_total_memory = pmm_get_total_memory,
        .get_free_memory = pmm_get_free_memory,
        .reserve_range = pmm_reserve_range,
        });
