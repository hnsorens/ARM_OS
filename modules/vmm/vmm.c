#include "vmm.h"
#include "../../include/api/pmm.h"
#include "../../include/api/mmu.h"
#include "../modules.h"
#include "../utils.h"
#include "../../include/type.h"

EXTERN_IMPORT_INTERFACE(mmu, mmu);
EXTERN_IMPORT_INTERFACE(pmm, pmm);

#define MAX_SPACE_TRACKERS 64
#define MAX_VMA_POOL_SIZE 512

/* --- Internal Linux-Style Translation Layer Storage --- */
static vmm_space_t s_space_registry[MAX_SPACE_TRACKERS];
static vm_area_t s_vma_pool[MAX_VMA_POOL_SIZE];

/* Allocates an independent tracking structure descriptor out of the static pool */
static vm_area_t *vma_alloc(void)
{
	for (size_t i = 0; i < MAX_VMA_POOL_SIZE; i++) {
		if (s_vma_pool[i].base == 0 && s_vma_pool[i].size == 0) {
			return &s_vma_pool[i];
		}
	}
	return NULL;
}

/* Releases an active tracking block descriptor context back into the pool */
static void vma_free(vm_area_t *vma)
{
	if (vma) {
		kmemset(vma, 0, sizeof(vm_area_t));
	}
}

/* Locates or reserves structural trackers tied to selected active page maps */
static vmm_space_t *get_space(paddr_t root)
{
	for (size_t i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (s_space_registry[i].page_table_root == root) {
			return &s_space_registry[i];
		}
	}
	return NULL;
}

/* Finds the specific virtual memory area containing the target address */
static vm_area_t *find_vma(vmm_space_t *space, vaddr_t addr)
{
	if (!space)
		return NULL;

	/* Linux-style mmap cache acceleration lookup check */
	if (space->mmap_cache && addr >= space->mmap_cache->base &&
	    addr < (space->mmap_cache->base + space->mmap_cache->size)) {
		return space->mmap_cache;
	}

	vm_area_t *curr = space->vma_head;
	while (curr) {
		if (addr >= curr->base && addr < (curr->base + curr->size)) {
			space->mmap_cache = curr;
			return curr;
		}
		curr = curr->next;
	}
	return NULL;
}

/* Inserts a configured VMA descriptor block cleanly into a sorted tracker sequence */
static void insert_vma(vmm_space_t *space, vm_area_t *vma)
{
	if (!space->vma_head) {
		space->vma_head = vma;
		return;
	}

	vm_area_t *curr = space->vma_head;
	while (curr) {
		if (vma->base < curr->base) {
			vma->next = curr;
			vma->prev = curr->prev;
			if (curr->prev)
				curr->prev->next = vma;
			else
				space->vma_head = vma;
			curr->prev = vma;
			return;
		}
		if (!curr->next) {
			curr->next = vma;
			vma->prev = curr;
			vma->next = NULL;
			return;
		}
		curr = curr->next;
	}
}

/* Scans structural lists searching for a free gap conforming to alignment limits */
static vaddr_t find_unmapped_area(vmm_space_t *space, vaddr_t hint, size_t sz)
{
	vaddr_t addr = (hint >= VMM_USER_SPACE_MIN) ? hint : VMM_USER_SPACE_MIN;
	addr = (addr + (VMM_DEFAULT_ALIGNMENT - 1)) &
	       ~(VMM_DEFAULT_ALIGNMENT - 1);

	while (addr + sz <= VMM_USER_SPACE_MAX) {
		vm_area_t *conflict = NULL;
		vm_area_t *curr = space->vma_head;

		while (curr) {
			if (!(addr + sz <= curr->base ||
			      addr >= curr->base + curr->size)) {
				conflict = curr;
				break;
			}
			curr = curr->next;
		}

		if (!conflict)
			return addr;
		addr = (conflict->base + conflict->size +
			(VMM_DEFAULT_ALIGNMENT - 1)) &
		       ~(VMM_DEFAULT_ALIGNMENT - 1);
	}
	return 0;
}

/* Allocates an empty 4KB physical page table to serve as a user address space root */
k_status_t vmm_space_create(paddr_t *out_table_root)
{
	k_status_t status;
	paddr_t root;

	if (!out_table_root)
		return K_STATUS_INVALID_ARG;

	status = mmu.alloc(&root);
	if (k_error(status))
		return status;

	for (size_t i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (s_space_registry[i].page_table_root == 0) {
			s_space_registry[i].page_table_root = root;
			s_space_registry[i].vma_head = NULL;
			s_space_registry[i].mmap_cache = NULL;
			*out_table_root = root;
			return K_STATUS_OK;
		}
	}

	mmu.free(root);
	return K_STATUS_OUT_OF_MEMORY;
}

/* Destroys and cleans up nested page tables and tracked memory ranges recursively */
k_status_t vmm_space_destroy(paddr_t table_root)
{
	vmm_space_t *space = get_space(table_root);
	if (!space)
		return K_STATUS_NOT_FOUND;

	vm_area_t *curr = space->vma_head;
	while (curr) {
		vm_area_t *next = curr->next;
		if (curr->is_paged) {
			mmu.unmap(table_root, curr->base,
				  curr->size / VMM_DEFAULT_ALIGNMENT, PS_4KB);
		}
		vma_free(curr);
		curr = next;
	}

	mmu.free(table_root);
	kmemset(space, 0, sizeof(vmm_space_t));
	return K_STATUS_OK;
}

/* Allocates an independent virtual memory area tracking segment chunk context */
k_status_t vmm_allocate(paddr_t root, vaddr_t *vaddr, size_t sz,
			mmu_flags_t flags, vmm_region_type_t type)
{
	vmm_space_t *space;
	vm_area_t *vma;
	vaddr_t target_addr;
	k_status_t status;
	size_t i;
	size_t allocation_size;
	size_t guard_offset = 0;

	if (!root || !vaddr || sz == 0)
		return K_STATUS_INVALID_ARG;

	space = get_space(root);
	if (!space)
		return K_STATUS_NOT_FOUND;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);
	allocation_size = sz;

	/* Security Defense: Automatically compute and provision a lower stack guard page */
	if (type == VMM_REGION_STACK) {
		allocation_size += VMM_DEFAULT_ALIGNMENT;
		guard_offset = VMM_DEFAULT_ALIGNMENT;
	}

	target_addr = find_unmapped_area(space, *vaddr, allocation_size);
	if (!target_addr)
		return K_STATUS_OUT_OF_MEMORY;

	/* Build and inject the unmappable lower guard node if handling a stack area */
	if (type == VMM_REGION_STACK) {
		vm_area_t *guard_vma = vma_alloc();
		if (!guard_vma)
			return K_STATUS_OUT_OF_MEMORY;

		guard_vma->base = target_addr;
		guard_vma->size = VMM_DEFAULT_ALIGNMENT;
		guard_vma->flags = 0;
		guard_vma->type = VMM_REGION_GUARD;
		guard_vma->is_paged = false;
		insert_vma(space, guard_vma);
	}

	vma = vma_alloc();
	if (!vma) {
		if (guard_offset > 0)
			vmm_free(root, target_addr, VMM_DEFAULT_ALIGNMENT);
		return K_STATUS_OUT_OF_MEMORY;
	}

	vma->base = target_addr + guard_offset;
	vma->size = sz;
	vma->flags = flags;
	vma->type = type;
	vma->is_paged = true;

	for (i = 0; i < sz; i += VMM_DEFAULT_ALIGNMENT) {
		paddr_t phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (k_error(status))
			goto cleanup;

		status = mmu.map(root, vma->base + i, phys_page, 1, PS_4KB,
				 flags);
		if (k_error(status)) {
			pmm.free_page(0, phys_page);
			goto cleanup;
		}
	}

	insert_vma(space, vma);
	*vaddr = vma->base;
	return K_STATUS_OK;

cleanup:
	vmm_free(root, vma->base, i);
	if (guard_offset > 0)
		vmm_free(root, target_addr, VMM_DEFAULT_ALIGNMENT);
	vma_free(vma);
	return K_STATUS_OUT_OF_MEMORY;
}

/* Anchors explicit arbitrary reservation zones into specified virtual addresses */
k_status_t vmm_reserve(paddr_t root, vaddr_t vaddr, size_t sz)
{
	vmm_space_t *space;
	vm_area_t *vma;

	if (!root || !vaddr || sz == 0)
		return K_STATUS_INVALID_ARG;

	space = get_space(root);
	if (!space)
		return K_STATUS_NOT_FOUND;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	/* Ensure no overlapping segments exist before making the reservation */
	vmm_region_info_t info;
	if (vmm_query(root, vaddr, &info) == K_STATUS_OK)
		return K_STATUS_ALREADY_EXISTS;

	vma = vma_alloc();
	if (!vma)
		return K_STATUS_OUT_OF_MEMORY;

	vma->base = vaddr;
	vma->size = sz;
	vma->flags = 0;
	vma->type = VMM_REGION_GUARD;
	vma->is_paged = false;

	insert_vma(space, vma);
	return K_STATUS_OK;
}

/* Evicts memory backing sectors and supports partial/sub-range trimming of VMAs */
k_status_t vmm_free(paddr_t root, vaddr_t vaddr, size_t sz)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);

	if (!vma || vaddr < vma->base || (vaddr + sz) > (vma->base + vma->size))
		return K_STATUS_NOT_FOUND;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	if (vma->is_paged) {
		for (size_t i = 0; i < sz; i += VMM_DEFAULT_ALIGNMENT) {
			paddr_t phys = 0;
			mmu_flags_t f = 0;

			if (mmu.translate(root, vaddr + i, &phys, &f) ==
			    K_STATUS_OK) {
				mmu.unmap(root, vaddr + i, 1, PS_4KB);
				pmm.free_page(0, phys);
			}
		}
	}

	/* Edge Case: If freeing a partial inner range, shrink or split the VMA layout structural node */
	if (vaddr == vma->base && sz == vma->size) {
		/* Total deletion of the VMA block */
		if (vma->prev)
			vma->prev->next = vma->next;
		if (vma->next)
			vma->next->prev = vma->prev;
		if (space->vma_head == vma)
			space->vma_head = vma->next;
		if (space->mmap_cache == vma)
			space->mmap_cache = NULL;
		vma_free(vma);
	} else if (vaddr == vma->base) {
		/* Shrink from the base upward */
		vma->base += sz;
		vma->size -= sz;
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		/* Shrink from the tail downward */
		vma->size -= sz;
	} else {
		/* Advanced Split: Fragment intermediate nodes cleanly into sibling allocations */
		vm_area_t *split_vma = vma_alloc();
		if (!split_vma)
			return K_STATUS_OUT_OF_MEMORY;

		split_vma->base = vaddr + sz;
		split_vma->size = (vma->base + vma->size) - split_vma->base;
		split_vma->flags = vma->flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;

		vma->size = vaddr - vma->base;
		insert_vma(space, split_vma);
	}

	return K_STATUS_OK;
}

/* Resizes an active mapped allocation range with complete atomic error rollbacks */
k_status_t vmm_resize(paddr_t root, vaddr_t vaddr, size_t old_sz, size_t new_sz)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);
	k_status_t status;
	size_t i = 0;

	if (!vma || vma->base != vaddr || vma->size != old_sz)
		return K_STATUS_NOT_FOUND;

	new_sz = (new_sz + (VMM_DEFAULT_ALIGNMENT - 1)) &
		 ~(VMM_DEFAULT_ALIGNMENT - 1);
	if (new_sz == old_sz)
		return K_STATUS_OK;

	if (new_sz < old_sz) {
		/* Prune the structural mapping layout size downward */
		for (i = new_sz; i < old_sz; i += VMM_DEFAULT_ALIGNMENT) {
			paddr_t phys = 0;
			mmu_flags_t f = 0;
			if (mmu.translate(root, vaddr + i, &phys, &f) ==
			    K_STATUS_OK) {
				mmu.unmap(root, vaddr + i, 1, PS_4KB);
				pmm.free_page(0, phys);
			}
		}
		vma->size = new_sz;
		return K_STATUS_OK;
	}

	/* Verify if the expanding space triggers conflicts with neighbors */
	vaddr_t next_vaddr = vaddr + old_sz;
	size_t growth = new_sz - old_sz;

	if (vma->next && vma->next->base < (next_vaddr + growth))
		return K_STATUS_OUT_OF_MEMORY;
	if (next_vaddr + growth > VMM_USER_SPACE_MAX)
		return K_STATUS_OUT_OF_MEMORY;

	/* Allocation Expansion Loop with explicit rollback safety */
	for (i = old_sz; i < new_sz; i += VMM_DEFAULT_ALIGNMENT) {
		paddr_t phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (k_error(status))
			goto rollback_expansion;

		status = mmu.map(root, vaddr + i, phys_page, 1, PS_4KB,
				 vma->flags);
		if (k_error(status)) {
			pmm.free_page(0, phys_page);
			goto rollback_expansion;
		}
	}

	vma->size = new_sz;
	return K_STATUS_OK;

rollback_expansion:
	/* Undo step allocations and preserve original untouched VMA state bounds */
	for (size_t undo = old_sz; undo < i; undo += VMM_DEFAULT_ALIGNMENT) {
		paddr_t phys = 0;
		mmu_flags_t f = 0;
		if (mmu.translate(root, vaddr + undo, &phys, &f) ==
		    K_STATUS_OK) {
			mmu.unmap(root, vaddr + undo, 1, PS_4KB);
			pmm.free_page(0, phys);
		}
	}
	return K_STATUS_OUT_OF_MEMORY;
}

/* Configures direct physical-to-virtual hardware mappings bypasses */
k_status_t vmm_map_external(paddr_t root, vaddr_t v, paddr_t p, size_t sz,
			    mmu_flags_t f)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma;
	k_status_t status;

	if (!space)
		return K_STATUS_NOT_FOUND;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	vma = vma_alloc();
	if (!vma)
		return K_STATUS_OUT_OF_MEMORY;

	vma->base = v;
	vma->size = sz;
	vma->flags = f;
	vma->type = VMM_REGION_MMIO;
	vma->is_paged = false;

	status = mmu.map(root, v, p, sz / VMM_DEFAULT_ALIGNMENT, PS_4KB, f);
	if (k_error(status)) {
		vma_free(vma);
		return status;
	}

	insert_vma(space, vma);
	return K_STATUS_OK;
}

/* Modifies operational mapping validation features across selected virtual ranges */
k_status_t vmm_protect(paddr_t root, vaddr_t vaddr, size_t sz,
		       mmu_flags_t new_flags)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);
	k_status_t status;

	if (!vma || vaddr < vma->base || (vaddr + sz) > (vma->base + vma->size))
		return K_STATUS_NOT_FOUND;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	status = mmu.protect(root, vaddr, sz / VMM_DEFAULT_ALIGNMENT, PS_4KB,
			     new_flags);
	if (k_error(status))
		return status;

	vma->flags = new_flags;
	return K_STATUS_OK;
}

/* Queries information metrics regarding specific active virtual mapping boundaries */
k_status_t vmm_query(paddr_t root, vaddr_t vaddr, vmm_region_info_t *out_info)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);

	if (!vma)
		return K_STATUS_NOT_MAPPED;

	out_info->base = vma->base;
	out_info->size = vma->size;
	out_info->flags = vma->flags;
	out_info->type = vma->type;
	out_info->is_paged = vma->is_paged;

	return K_STATUS_OK;
}

/* Sets active address translation profiles directly into structural context registers */
k_status_t vmm_activate(paddr_t root)
{
	return mmu.set_user_ctx(root, 1);
}

/* Synchronizes address ranges across processors flushing specialized entry tracks */
k_status_t vmm_sync(paddr_t root, vaddr_t vaddr, size_t sz)
{
	(void)root;
	size_t pages =
		(sz + (VMM_DEFAULT_ALIGNMENT - 1)) / VMM_DEFAULT_ALIGNMENT;
	return mmu.invalidate(vaddr, pages, PS_4KB);
}
