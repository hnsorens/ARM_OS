#ifndef VMM_H
#define VMM_H

#include "modules/structures/vmm.h"
#include "modules/vtables/vmm.h"

#ifndef VMM
#define VMM vmm
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

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
GLOBAL void (*CONCAT_EXPAND(VMM, _pages_map_kernel))( void* vaddr, void* paddr, unsigned long page_order, unsigned long page_count ) END 
 
  /** 
    * @brief Translate kernel virtual address to physical address 
    * 
    * Performs a page table walk to find the physical address that 
    * corresponds to a given kernel virtual address. 
    * 
    * @param vaddr Kernel virtual address to translate 
    * @return unsigned long Corresponding physical address, or 0 if unmapped 
    */ 
GLOBAL unsigned long (*CONCAT_EXPAND(VMM, _virt_to_phys_kernel))( void* vaddr ) END 
#ifdef __MAIN__

static void vmm_fetch(kernel_vtable_t *kvtable){
	vmm_vtable_t* module = (vmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_VMM);
	CONCAT_EXPAND(VMM, _pages_map_kernel) = module->pages_map_kernel;
	CONCAT_EXPAND(VMM, _virt_to_phys_kernel) = module->virt_to_phys_kernel;
}
#endif

#undef GLOBAL
#undef VMM

#endif