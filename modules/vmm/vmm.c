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

static vmm_space_t s_space_registry[MAX_SPACE_TRACKERS];
static vm_area_t s_vma_pool[MAX_VMA_POOL_SIZE];

/* Allocates an independent tracking structure descriptor using explicit flags */
static vm_area_t *vma_alloc(void)
{
	for (u64 i = 0; i < MAX_VMA_POOL_SIZE; i++) {
		if (!s_vma_pool[i].in_use) {
			kmemset(&s_vma_pool[i], 0, sizeof(vm_area_t));
			s_vma_pool[i].in_use = true;
			return &s_vma_pool[i];
		}
	}
	return NULL;
}

/* Releases an active tracking block descriptor context safely back into the pool */
static void vma_free(vm_area_t *vma)
{
	if (vma) {
		kmemset(vma, 0, sizeof(vm_area_t));
	}
}

/* Locates structural trackers tied to selected active page maps */
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
		vma->prev = NULL;
		vma->next = NULL;
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

int vmm_space_destroy(u64 table_root)
{
	if (!table_root)
		return EINVAL;
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

	/* Stacks grow downward; add lower edge guard safety padding zone */
	if (type == VMM_REGION_STACK) {
		allocation_size += VMM_DEFAULT_ALIGNMENT;
		guard_offset = VMM_DEFAULT_ALIGNMENT;
	}

	target_addr = find_unmapped_area(space, *vaddr, allocation_size);
	if (!target_addr)
		return ENOMEM;

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
			pmm.release(phys_page);
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
				pmm.release(phys);
			}
		}
	}

	/* Edge Case: Handle list links directly to avoid internal pointer corruption loops */
	if (vaddr == vma->base && sz == vma->size) {
		/* If this was a stack frame, locate and reclaim the lower guard node automatically */
		if (vma->type == VMM_REGION_STACK && vma->prev &&
		    vma->prev->type == VMM_REGION_GUARD) {
			vm_area_t *guard = vma->prev;
			if (guard->prev)
				guard->prev->next = vma->next;
			else
				space->vma_head = vma->next;
			if (vma->next)
				vma->next->prev = guard->prev;
			vma_free(guard);
		} else {
			if (vma->prev)
				vma->prev->next = vma->next;
			if (vma->next)
				vma->next->prev = vma->prev;
			if (space->vma_head == vma)
				space->vma_head = vma->next;
		}
		if (space->mmap_cache == vma)
			space->mmap_cache = NULL;
		vma_free(vma);
	} else if (vaddr == vma->base) {
		vma->base += sz;
		vma->size -= sz;
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		vma->size -= sz;
	} else {
		/* Splitting intermediate structures requires explicit linkage modifications */
		vm_area_t *split_vma = vma_alloc();
		if (!split_vma)
			return ENOMEM;

		split_vma->base = vaddr + sz;
		split_vma->size = (vma->base + vma->size) - split_vma->base;
		split_vma->flags = vma->flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;

		vma->size = vaddr - vma->base;

		/* Safely wire the split node right behind the current element */
		split_vma->next = vma->next;
		split_vma->prev = vma;
		if (vma->next)
			vma->next->prev = split_vma;
		vma->next = split_vma;
	}

	return 0;
}

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
		for (i = new_sz; i < old_sz; i += VMM_DEFAULT_ALIGNMENT) {
			u64 phys = 0;
			enum mmu_flags f = 0;
			if (mmu.translate(root, vaddr + i, &phys, &f) == 0) {
				mmu.unmap(root, vaddr + i, 1, PS_4KB);
				pmm.release(phys);
			}
		}
		vma->size = new_sz;
		return 0;
	}

	u64 next_vaddr = vaddr + old_sz;
	u64 growth = new_sz - old_sz;

	if (vma->next && vma->next->base < (next_vaddr + growth))
		return ENOMEM;
	if (next_vaddr + growth > VMM_USER_SPACE_MAX)
		return ENOMEM;

	for (i = old_sz; i < new_sz; i += VMM_DEFAULT_ALIGNMENT) {
		u64 phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (status)
			goto rollback_expansion;

		status = mmu.map(root, vaddr + i, phys_page, 1, PS_4KB,
				 vma->flags);
		if (status) {
			pmm.release(phys_page);
			goto rollback_expansion;
		}
	}

	vma->size = new_sz;
	return 0;

rollback_expansion:
	for (u64 undo = old_sz; undo < i; undo += VMM_DEFAULT_ALIGNMENT) {
		u64 phys = 0;
		enum mmu_flags f = 0;
		if (mmu.translate(root, vaddr + undo, &phys, &f) == 0) {
			mmu.unmap(root, vaddr + undo, 1, PS_4KB);
			pmm.release(phys);
		}
	}
	return ENOMEM;
}

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

	/* If changing flags across a subset, fragment the tracking structures to preserve precision */
	if (vaddr == vma->base && sz == vma->size) {
		vma->flags = new_flags;
	} else if (vaddr == vma->base) {
		vm_area_t *split_vma = vma_alloc();
		if (!split_vma)
			return ENOMEM;
		split_vma->base = vaddr + sz;
		split_vma->size = vma->size - sz;
		split_vma->flags = vma->flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;

		vma->size = sz;
		vma->flags = new_flags;

		split_vma->next = vma->next;
		split_vma->prev = vma;
		if (vma->next)
			vma->next->prev = split_vma;
		vma->next = split_vma;
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		vm_area_t *split_vma = vma_alloc();
		if (!split_vma)
			return ENOMEM;
		split_vma->base = vaddr;
		split_vma->size = sz;
		split_vma->flags = new_flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;

		vma->size -= sz;

		split_vma->next = vma->next;
		split_vma->prev = vma;
		if (vma->next)
			vma->next->prev = split_vma;
		vma->next = split_vma;
	} else {
		/* Complex middle modification requires a 3-way tracking split */
		vm_area_t *mid = vma_alloc();
		vm_area_t *tail = vma_alloc();
		if (!mid || !tail) {
			if (mid)
				vma_free(mid);
			if (tail)
				vma_free(tail);
			return ENOMEM;
		}

		tail->base = vaddr + sz;
		tail->size = (vma->base + vma->size) - tail->base;
		tail->flags = vma->flags;
		tail->type = vma->type;
		tail->is_paged = vma->is_paged;

		mid->base = vaddr;
		mid->size = sz;
		mid->flags = new_flags;
		mid->type = vma->type;
		mid->is_paged = vma->is_paged;

		vma->size = vaddr - vma->base;

		tail->next = vma->next;
		if (vma->next)
			vma->next->prev = tail;

		vma->next = mid;
		mid->prev = vma;
		mid->next = tail;
		tail->prev = mid;
	}

	return 0;
}

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

int vmm_activate(u64 root)
{
	return mmu.set_user_ctx(root, 1);
}

int vmm_sync(u64 root, u64 vaddr, u64 sz)
{
	(void)root;
	u64 pages = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) / VMM_DEFAULT_ALIGNMENT;
	return mmu.invalidate(vaddr, pages, PS_4KB);
}
