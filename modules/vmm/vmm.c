#include "vmm.h"
#include "../../include/api/pmm.h"
#include "../../include/api/mmu.h"
#include "../modules.h"
#include "../utils.h"
#include "../../include/type.h"
#include "../../include/errno.h"

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
	for (u64 i = 0; i < MAX_VMA_POOL_SIZE; i++) {
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
static vmm_space_t *get_space(u64 root)
{
	for (u64 i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (s_space_registry[i].page_table_root == root) {
			return &s_space_registry[i];
		}
	}
	return NULL;
}

/* Finds the specific virtual memory area containing the target address */
static vm_area_t *find_vma(vmm_space_t *space, u64 addr)
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
static u64 find_unmapped_area(vmm_space_t *space, u64 hint, u64 sz)
{
	u64 addr = (hint >= VMM_USER_SPACE_MIN) ? hint : VMM_USER_SPACE_MIN;
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
int vmm_space_create(u64 *out_table_root)
{
	int status;
	u64 root;

	if (!out_table_root)
		return EINVAL;

	status = mmu.alloc(&root);
	if (status)
		return status;

	for (u64 i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (s_space_registry[i].page_table_root == 0) {
			s_space_registry[i].page_table_root = root;
			s_space_registry[i].vma_head = NULL;
			s_space_registry[i].mmap_cache = NULL;
			*out_table_root = root;
			return 0;
		}
	}

	mmu.free(root);
	return ENOMEM;
}

/* Destroys and cleans up nested page tables and tracked memory ranges recursively */
int vmm_space_destroy(u64 table_root)
{
	vmm_space_t *space = get_space(table_root);
	if (!space)
		return EINVAL;

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
	return 0;
}

/* Allocates an independent virtual memory area tracking segment chunk context */
int vmm_allocate(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags,
		 enum vmm_region_type type)
{
	vmm_space_t *space;
	vm_area_t *vma;
	u64 target_addr;
	int status;
	u64 i;
	u64 allocation_size;
	u64 guard_offset = 0;

	if (!root || !vaddr || sz == 0)
		return EINVAL;

	space = get_space(root);
	if (!space)
		return EINVAL;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);
	allocation_size = sz;

	/* Security Defense: Automatically compute and provision a lower stack guard page */
	if (type == VMM_REGION_STACK) {
		allocation_size += VMM_DEFAULT_ALIGNMENT;
		guard_offset = VMM_DEFAULT_ALIGNMENT;
	}

	target_addr = find_unmapped_area(space, *vaddr, allocation_size);
	if (!target_addr)
		return ENOMEM;

	/* Build and inject the unmappable lower guard node if handling a stack area */
	if (type == VMM_REGION_STACK) {
		vm_area_t *guard_vma = vma_alloc();
		if (!guard_vma)
			return ENOMEM;

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
		return ENOMEM;
	}

	vma->base = target_addr + guard_offset;
	vma->size = sz;
	vma->flags = flags;
	vma->type = type;
	vma->is_paged = true;

	for (i = 0; i < sz; i += VMM_DEFAULT_ALIGNMENT) {
		u64 phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (status)
			goto cleanup;

		status = mmu.map(root, vma->base + i, phys_page, 1, PS_4KB,
				 flags);
		if (status) {
			pmm.free_page(0, phys_page);
			goto cleanup;
		}
	}

	insert_vma(space, vma);
	*vaddr = vma->base;
	return 0;

cleanup:
	vmm_free(root, vma->base, i);
	if (guard_offset > 0)
		vmm_free(root, target_addr, VMM_DEFAULT_ALIGNMENT);
	vma_free(vma);
	return ENOMEM;
}

/* Anchors explicit arbitrary reservation zones into specified virtual addresses */
int vmm_reserve(u64 root, u64 vaddr, u64 sz)
{
	vmm_space_t *space;
	vm_area_t *vma;

	if (!root || !vaddr || sz == 0)
		return EINVAL;

	space = get_space(root);
	if (!space)
		return EINVAL;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	/* Ensure no overlapping segments exist before making the reservation */
	struct vmm_region_info info;
	if (vmm_query(root, vaddr, &info) == 0)
		return EEXIST;

	vma = vma_alloc();
	if (!vma)
		return ENOMEM;

	vma->base = vaddr;
	vma->size = sz;
	vma->flags = 0;
	vma->type = VMM_REGION_GUARD;
	vma->is_paged = false;

	insert_vma(space, vma);
	return 0;
}

/* Evicts memory backing sectors and supports partial/sub-range trimming of VMAs */
int vmm_free(u64 root, u64 vaddr, u64 sz)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);

	if (!vma || vaddr < vma->base || (vaddr + sz) > (vma->base + vma->size))
		return EINVAL;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	if (vma->is_paged) {
		for (u64 i = 0; i < sz; i += VMM_DEFAULT_ALIGNMENT) {
			u64 phys = 0;
			enum mmu_flags f = 0;

			if (mmu.translate(root, vaddr + i, &phys, &f) == 0) {
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
			return ENOMEM;

		split_vma->base = vaddr + sz;
		split_vma->size = (vma->base + vma->size) - split_vma->base;
		split_vma->flags = vma->flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;

		vma->size = vaddr - vma->base;
		insert_vma(space, split_vma);
	}

	return 0;
}

/* Resizes an active mapped allocation range with complete atomic error rollbacks */
int vmm_resize(u64 root, u64 vaddr, u64 old_sz, u64 new_sz)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);
	int status;
	u64 i = 0;

	if (!vma || vma->base != vaddr || vma->size != old_sz)
		return EINVAL;

	new_sz = (new_sz + (VMM_DEFAULT_ALIGNMENT - 1)) &
		 ~(VMM_DEFAULT_ALIGNMENT - 1);
	if (new_sz == old_sz)
		return 0;

	if (new_sz < old_sz) {
		/* Prune the structural mapping layout size downward */
		for (i = new_sz; i < old_sz; i += VMM_DEFAULT_ALIGNMENT) {
			u64 phys = 0;
			enum mmu_flags f = 0;
			if (mmu.translate(root, vaddr + i, &phys, &f) == 0) {
				mmu.unmap(root, vaddr + i, 1, PS_4KB);
				pmm.free_page(0, phys);
			}
		}
		vma->size = new_sz;
		return 0;
	}

	/* Verify if the expanding space triggers conflicts with neighbors */
	u64 next_vaddr = vaddr + old_sz;
	u64 growth = new_sz - old_sz;

	if (vma->next && vma->next->base < (next_vaddr + growth))
		return ENOMEM;
	if (next_vaddr + growth > VMM_USER_SPACE_MAX)
		return ENOMEM;

	/* Allocation Expansion Loop with explicit rollback safety */
	for (i = old_sz; i < new_sz; i += VMM_DEFAULT_ALIGNMENT) {
		u64 phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (status)
			goto rollback_expansion;

		status = mmu.map(root, vaddr + i, phys_page, 1, PS_4KB,
				 vma->flags);
		if (status) {
			pmm.free_page(0, phys_page);
			goto rollback_expansion;
		}
	}

	vma->size = new_sz;
	return 0;

rollback_expansion:
	/* Undo step allocations and preserve original untouched VMA state bounds */
	for (u64 undo = old_sz; undo < i; undo += VMM_DEFAULT_ALIGNMENT) {
		u64 phys = 0;
		enum mmu_flags f = 0;
		if (mmu.translate(root, vaddr + undo, &phys, &f) == 0) {
			mmu.unmap(root, vaddr + undo, 1, PS_4KB);
			pmm.free_page(0, phys);
		}
	}
	return ENOMEM;
}

/* Configures direct physical-to-virtual hardware mappings bypasses */
int vmm_map_external(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma;
	int status;

	if (!space)
		return EINVAL;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	vma = vma_alloc();
	if (!vma)
		return ENOMEM;

	vma->base = v;
	vma->size = sz;
	vma->flags = f;
	vma->type = VMM_REGION_MMIO;
	vma->is_paged = false;

	status = mmu.map(root, v, p, sz / VMM_DEFAULT_ALIGNMENT, PS_4KB, f);
	if (status) {
		vma_free(vma);
		return status;
	}

	insert_vma(space, vma);
	return 0;
}

/* Modifies operational mapping validation features across selected virtual ranges */
int vmm_protect(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);
	int status;

	if (!vma || vaddr < vma->base || (vaddr + sz) > (vma->base + vma->size))
		return EINVAL;

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	status = mmu.protect(root, vaddr, sz / VMM_DEFAULT_ALIGNMENT, PS_4KB,
			     new_flags);
	if (status)
		return status;

	vma->flags = new_flags;
	return 0;
}

/* Queries information metrics regarding specific active virtual mapping boundaries */
int vmm_query(u64 root, u64 vaddr, struct vmm_region_info *out_info)
{
	vmm_space_t *space = get_space(root);
	vm_area_t *vma = find_vma(space, vaddr);

	if (!vma)
		return EINVAL;

	out_info->base = vma->base;
	out_info->size = vma->size;
	out_info->flags = vma->flags;
	out_info->type = vma->type;
	out_info->is_paged = vma->is_paged;

	return 0;
}

/* Sets active address translation profiles directly into structural context registers */
int vmm_activate(u64 root)
{
	return mmu.set_user_ctx(root, 1);
}

/* Synchronizes address ranges across processors flushing specialized entry tracks */
int vmm_sync(u64 root, u64 vaddr, u64 sz)
{
	(void)root;
	u64 pages = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) / VMM_DEFAULT_ALIGNMENT;
	return mmu.invalidate(vaddr, pages, PS_4KB);
}
