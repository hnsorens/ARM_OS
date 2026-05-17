#include "../modules.h"

#include "../../include/api/pmm.h"
#include "../boot_info.h"

#include "buddy.h"

int main(boot_info_t *boot_info) {
    buddy_init(boot_info->memory_regions, boot_info->memory_map_size);
}

EXPORT_INTERFACE(pmm, idk, {
           .alloc_page = buddy_alloc_page,
           .free_page = buddy_free_page,
           .alloc_pages = buddy_alloc_pages,
           .free_pages = buddy_free_pages
        })
