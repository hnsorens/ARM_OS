#include "memory.h"

#define PAGE_SIZE 4096
#define KERNEL_SLAB_SIZE 1024

static unsigned long* kernel_slab = 0;
static unsigned long size;
static unsigned long* top;

void setup_kernel_allocator(BootInfoStruct *bootInfo)
{
    for (int i = 0; i < bootInfo->memoryMapSize; ++i)
    {
        if (bootInfo->memoryRegions[i].page_count >= KERNEL_SLAB_SIZE && bootInfo->memoryRegions[i].type == MEMORY_FREE)
        {
            kernel_slab = (void*)bootInfo->memoryRegions[i].start;
            bootInfo->memoryRegions[i].page_count -= KERNEL_SLAB_SIZE;
            bootInfo->memoryRegions[i].start += (KERNEL_SLAB_SIZE * PAGE_SIZE);
        }
    }

    if (!kernel_slab)
    {
        // panic
    }

    *kernel_slab = 0;

    for (int i = 1; i < KERNEL_SLAB_SIZE; ++i)
    {
        unsigned long* page = (void*)((unsigned long)kernel_slab + i * PAGE_SIZE);
        *page = (unsigned long)page - PAGE_SIZE;
        top = page;
    }

    size = KERNEL_SLAB_SIZE;
}

void* kernel_alloc_page()
{
    size --;
    void* return_page = top;
    top = (unsigned long*)(*top);

    return return_page;
}

void kernel_free_page(void *page)
{
    size++;
    *(unsigned long*)page = (unsigned long)top;
    top = page;
}
