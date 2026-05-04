#ifndef MEMORY_H
#define MEMORY_H

#include "../boot/bootinfo.h"

void setup_kernel_allocator(BootInfoStruct *bootInfo);
void* kernel_alloc_page();
void kernel_free_page(void *page);

#endif
