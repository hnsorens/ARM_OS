#ifndef VMM_VTABLE_H
#define VMM_VTABLE_H

#include "../../module_vtable.h"

/**
 * @brief Virtual Memory Manager (VMM) VTable
 * 
 * Manages virtual to physical memory mappings, page tables, and
 * memory protection. Handles kernel space memory mapping operations.
 */
typedef struct vmm_vtable_t
{
  vtable_def

  /**
    * @brief Map physical pages to kernel virtual addresses
    * 
    * Creates virtual-to-physical mappings in the kernel's page tables.
    * Maps a range of physical pages to a contiguous range of kernel
    * virtual addresses with specified protection flags.
    * 
    * @param vaddr Starting virtual address in kernel space
    * @param paddr Starting physical address to map
    * @param page_order Order of pages being mapped (0=4kb, 1=2mb, 2=1gb)
    * @param page_count Number of pages being written to virtual memory
    * 
    */
  void (*pages_map_kernel)(void* vaddr, void* paddr, unsigned long page_order, unsigned long page_count);

  /**
    * @brief Translate kernel virtual address to physical address
    * 
    * Performs a page table walk to find the physical address that
    * corresponds to a given kernel virtual address.
    * 
    * @param vaddr Kernel virtual address to translate
    * @return unsigned long Corresponding physical address, or 0 if unmapped
    */
  unsigned long (*virt_to_phys_kernel)(void* vaddr);
} vmm_vtable_t;

#endif