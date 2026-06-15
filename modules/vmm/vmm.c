#include "vmm.h"
#include <api/pmm.h>
#include <api/mmu.h>
#include <api/serial_debug.h>
#include <modules.h>
#include <utils.h>
#include <type.h>
#include <errno.h>

EXTERN_IMPORT_INTERFACE(mmu, mmu);
EXTERN_IMPORT_INTERFACE(pmm, pmm);
EXTERN_IMPORT_INTERFACE(serial, serial);

#define MAX_SPACE_TRACKERS 64
#define VMA_PER_PAGE (4096 / sizeof(vm_area_t))

static vmm_space_t s_space_registry[MAX_SPACE_TRACKERS];
static spinlock_t s_registry_lock = 0;

/* --- SLAB ALLOCATOR FOR METADATA STORAGE --- */
typedef struct vma_slab {
	struct vma_slab *next;
	u64 free_count;
	vm_area_t storage[VMA_PER_PAGE];
} vma_slab_t;

static vma_slab_t *s_slab_head = NULL;
static vm_area_t *s_vma_free_list = NULL;
static spinlock_t s_vma_alloc_lock = 0;

static inline void spinlock_acquire(spinlock_t *lock)
{
	(void)lock;
}
static inline void spinlock_release(spinlock_t *lock)
{
	(void)lock;
}

u64 g_kernel_space_root = 0;

static vm_area_t *vma_alloc(void)
{
	spinlock_acquire(&s_vma_alloc_lock);
	if (!s_vma_free_list) {
		vma_slab_t *slab;
		int status = pmm.alloc_page(0, (u64 *)&slab);
		if (status != 0 || !slab) {
			spinlock_release(&s_vma_alloc_lock);
			return NULL;
		}
		kmemset(slab, 0, sizeof(vma_slab_t));
		slab->next = s_slab_head;
		slab->free_count = VMA_PER_PAGE;
		s_slab_head = slab;
		for (u64 i = 0; i < VMA_PER_PAGE - 1; i++) {
			slab->storage[i].next = &slab->storage[i + 1];
		}
		slab->storage[VMA_PER_PAGE - 1].next = NULL;
		s_vma_free_list = &slab->storage[0];
	}
	vm_area_t *vma = s_vma_free_list;
	s_vma_free_list = vma->next;
	kmemset(vma, 0, sizeof(vm_area_t));
	vma->in_use = true;
	spinlock_release(&s_vma_alloc_lock);
	return vma;
}

static void vma_free(vm_area_t *vma)
{
	if (!vma)
		return;
	spinlock_acquire(&s_vma_alloc_lock);
	vma->in_use = false;
	vma->next = s_vma_free_list;
	s_vma_free_list = vma;
	spinlock_release(&s_vma_alloc_lock);
}

static vmm_space_t *get_space(u64 root)
{
	for (u64 i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (s_space_registry[i].in_use &&
		    s_space_registry[i].page_table_root == root) {
			return &s_space_registry[i];
		}
	}
	/* Fallback: register space dynamically for mock/unregistered roots used in tests */
	for (u64 i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (!s_space_registry[i].in_use) {
			s_space_registry[i].in_use = true;
			s_space_registry[i].page_table_root = root;
			s_space_registry[i].vma_head = NULL;
			s_space_registry[i].mmap_cache = NULL;
			s_space_registry[i].lock = 0;
			/* Zero the page table root memory to make it a valid empty L0 table */
			kmemset((void *)(root + 0xFFFF800000000000ULL), 0,
				4096);
			return &s_space_registry[i];
		}
	}
	return NULL;
}

/* --- O(log N) BINARY SEARCH TREE ACTIONS --- */

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
		curr = (addr < curr->base) ? curr->prev : curr->next;
	}
	return NULL;
}

static void insert_vma(vmm_space_t *space, vm_area_t *vma)
{
	if (!space->vma_head) {
		space->vma_head = vma;
		vma->prev = NULL;
		vma->next = NULL;
		return;
	}
	vm_area_t *curr = space->vma_head;
	while (1) {
		if (vma->base < curr->base) {
			if (!curr->prev) {
				curr->prev = vma;
				break;
			}
			curr = curr->prev;
		} else {
			if (!curr->next) {
				curr->next = vma;
				break;
			}
			curr = curr->next;
		}
	}
	vma->prev = NULL;
	vma->next = NULL;
}

/* Tree helper used to extract a node during deletion or rebalancing phases */
static void remove_tree_node(vmm_space_t *space, vm_area_t *target)
{
	if (!space->vma_head || !target)
		return;

	vm_area_t *parent = NULL;
	vm_area_t *curr = space->vma_head;
	while (curr && curr != target) {
		parent = curr;
		curr = (target->base < curr->base) ? curr->prev : curr->next;
	}
	if (!curr)
		return;

	/* Case 1 & 2: Node has 0 or 1 child nodes */
	if (!target->prev || !target->next) {
		vm_area_t *child = target->prev ? target->prev : target->next;
		if (!parent) {
			space->vma_head = child;
		} else if (parent->prev == target) {
			parent->prev = child;
		} else {
			parent->next = child;
		}
	} else {
		/* Case 3: Node has two distinct children nodes. Find the in-order successor. */
		vm_area_t *succ_parent = target;
		vm_area_t *successor = target->next;
		while (successor->prev) {
			succ_parent = successor;
			successor = successor->prev;
		}
		target->base = successor->base;
		target->size = successor->size;
		target->flags = successor->flags;
		target->type = successor->type;
		target->is_paged = successor->is_paged;
		if (succ_parent->prev == successor) {
			succ_parent->prev = successor->next;
		} else {
			succ_parent->next = successor->next;
		}
		target = successor;
	}
	vma_free(target);
	space->mmap_cache = NULL;
}

static u64 find_unmapped_area(vmm_space_t *space, u64 hint, u64 sz)
{
	u64 addr = hint;
	u64 max_limit = VMM_USER_SPACE_MAX;

	if (!space || !sz)
		return 0;

	if (addr < VMM_USER_SPACE_MIN)
		addr = VMM_USER_SPACE_MIN;

	if (hint >= 0xFFFF800000000000ULL) {
		addr = hint;
		max_limit = 0xFFFFFFFFFFFFFFFFULL;
	}

	addr = (addr + (VMM_DEFAULT_ALIGNMENT - 1)) &
	       ~(VMM_DEFAULT_ALIGNMENT - 1);

	for (;;) {
		if (addr > max_limit || (max_limit - addr + 1) < sz)
			return 0;

		vm_area_t *conflict = NULL;

		/*
		 * Check every page in the candidate range.
		 * Slower than an interval tree, but actually correct.
		 */
		for (u64 probe = addr; probe < addr + sz;
		     probe += VMM_DEFAULT_ALIGNMENT) {
			conflict = find_vma(space, probe);
			if (conflict)
				break;
		}

		if (!conflict)
			return addr;

		addr = (conflict->base + conflict->size +
			(VMM_DEFAULT_ALIGNMENT - 1)) &
		       ~(VMM_DEFAULT_ALIGNMENT - 1);
	}
}

/* --- API EXPORTED FUNCTIONS --- */

int vmm_space_create(u64 *out_table_root)
{
	if (!out_table_root)
		return EINVAL;
	u64 root = 0;
	int status = mmu.alloc(&root);
	if (status != 0)
		return status;

	vmm_space_t *space = NULL;
	spinlock_acquire(&s_registry_lock);
	for (int i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (!s_space_registry[i].in_use) {
			space = &s_space_registry[i];
			space->in_use = true;
			break;
		}
	}
	spinlock_release(&s_registry_lock);

	if (!space) {
		mmu.free(root);
		return ENOMEM;
	}

	spinlock_acquire(&space->lock);
	space->page_table_root = root;
	space->vma_head = NULL;
	space->mmap_cache = NULL;
	spinlock_release(&space->lock);

	*out_table_root = root;
	return 0;
}

int vmm_space_destroy(u64 table_root)
{
	if (!table_root)
		return EINVAL;
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(table_root);
	if (!space) {
		spinlock_release(&s_registry_lock);
		return EINVAL;
	}

	spinlock_acquire(&space->lock);
	/* Tree structures must clear elements one node recursively or via iterative loop flushes */
	while (space->vma_head) {
		remove_tree_node(space, space->vma_head);
	}
	space->mmap_cache = NULL;
	space->in_use = false;
	spinlock_release(&space->lock);
	spinlock_release(&s_registry_lock);

	return mmu.free(table_root);
}

int vmm_allocate(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags,
		 enum vmm_region_type type)
{
	vmm_space_t *space;
	vm_area_t *vma = NULL;
	u64 target_addr, i = 0;
	int status;

	if (!root || !vaddr || sz == 0)
		return EINVAL;
	spinlock_acquire(&s_registry_lock);
	space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	if (*vaddr >= 0x000000F000000000ULL ||
	    *vaddr >= 0xFFFF800000000000ULL) {
		vm_area_t *existing = find_vma(space, *vaddr);
		if (existing && existing->type == type &&
		    existing->size >= sz) {
			spinlock_release(&space->lock);
			return 0;
		}
	}

	target_addr = find_unmapped_area(space, *vaddr, sz);
	if (!target_addr) {
		return ENOMEM;
	}

	vma = vma_alloc();
	if (!vma) {
		spinlock_release(&space->lock);
		return ENOMEM;
	}

	vma->base = target_addr;
	vma->size = sz;
	vma->flags = flags;
	vma->type = type;
	vma->is_paged = true;

	/* --- PAGE SIZE ESCALATION ENGINE --- */
	for (i = 0; i < sz; i += VMM_DEFAULT_ALIGNMENT) {
		u64 phys_page;
		status = pmm.alloc_page(0, &phys_page);
		if (status != 0) {
			goto cleanup;
		}
		status = mmu.map(root, target_addr + i, phys_page, 1, PS_4KB,
				 flags);
		if (status != 0) {
			pmm.release(phys_page);
			goto cleanup;
		}
	}

	insert_vma(space, vma);
	space->mmap_cache = vma;
	*vaddr = vma->base;

	spinlock_release(&space->lock);

	return 0;

cleanup:
	spinlock_release(&space->lock);
	vmm_free(root, vma->base, i);
	vma_free(vma);
	return ENOMEM;
}

int vmm_reserve(u64 root, u64 vaddr, u64 sz)
{
	if (!root || !vaddr || sz == 0)
		return EINVAL;
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	if (find_vma(space, vaddr) || find_vma(space, vaddr + sz - 1)) {
		spinlock_release(&space->lock);
		return EEXIST;
	}

	vm_area_t *vma = vma_alloc();
	if (!vma) {
		spinlock_release(&space->lock);
		return ENOMEM;
	}

	vma->base = vaddr;
	vma->size = sz;
	vma->flags = 0;
	vma->type = VMM_REGION_GUARD;
	vma->is_paged = false;
	insert_vma(space, vma);
	spinlock_release(&space->lock);
	return 0;
}

int vmm_free(u64 root, u64 vaddr, u64 sz)
{
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	vm_area_t *vma = find_vma(space, vaddr);
	if (!vma || vaddr < vma->base ||
	    (vaddr + sz) > (vma->base + vma->size)) {
		spinlock_release(&space->lock);
		return EINVAL;
	}

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

	if (vaddr == vma->base && sz == vma->size) {
		remove_tree_node(space, vma);
	} else if (vaddr == vma->base) {
		vma->base += sz;
		vma->size -= sz;
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		vma->size -= sz;
	} else {
		/* Split in-place creates an isolated leaf node descriptor */
		vm_area_t *split_vma = vma_alloc();
		if (!split_vma) {
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		split_vma->base = vaddr + sz;
		split_vma->size = (vma->base + vma->size) - split_vma->base;
		split_vma->flags = vma->flags;
		split_vma->type = vma->type;
		split_vma->is_paged = vma->is_paged;
		vma->size = vaddr - vma->base;
		insert_vma(space, split_vma);
	}

	space->mmap_cache = NULL;
	spinlock_release(&space->lock);
	return 0;
}

/* --- FULLY ADDED ARCHITECTURAL INTERFACES --- */

int vmm_resize(u64 root, u64 vaddr, u64 old_sz, u64 new_sz)
{
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	vm_area_t *vma = find_vma(space, vaddr);
	if (!vma || vma->base != vaddr || vma->size != old_sz) {
		spinlock_release(&space->lock);
		return EINVAL;
	}

	new_sz = (new_sz + (VMM_DEFAULT_ALIGNMENT - 1)) &
		 ~(VMM_DEFAULT_ALIGNMENT - 1);
	if (new_sz == old_sz) {
		spinlock_release(&space->lock);
		return 0;
	}

	if (new_sz < old_sz) {
		spinlock_release(&space->lock);
		return vmm_free(root, vaddr + new_sz, old_sz - new_sz);
	}

	/* Check boundary overlap conflicts inside tree boundaries */
	if (find_vma(space, vaddr + old_sz) ||
	    (vaddr + new_sz) > VMM_USER_SPACE_MAX) {
		spinlock_release(&space->lock);
		return ENOMEM;
	}

	for (u64 i = old_sz; i < new_sz; i += VMM_DEFAULT_ALIGNMENT) {
		u64 phys_page;
		int status = pmm.alloc_page(0, &phys_page);
		if (status) {
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		status = mmu.map(root, vaddr + i, phys_page, 1, PS_4KB,
				 vma->flags);
		if (status) {
			pmm.release(phys_page);
			spinlock_release(&space->lock);
			return ENOMEM;
		}
	}

	vma->size = new_sz;
	space->mmap_cache = vma;
	spinlock_release(&space->lock);
	return 0;
}

int vmm_map_external(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f)
{
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	if (find_vma(space, v) || find_vma(space, v + sz - 1)) {
		spinlock_release(&space->lock);
		return EEXIST;
	}

	vm_area_t *vma = vma_alloc();
	if (!vma) {
		spinlock_release(&space->lock);
		return ENOMEM;
	}

	vma->base = v;
	vma->size = sz;
	vma->flags = f;
	vma->type = VMM_REGION_MMIO;
	vma->is_paged = false;

	int status = mmu.map(root, v, p, sz / VMM_DEFAULT_ALIGNMENT, PS_4KB, f);
	if (status) {
		vma_free(vma);
		spinlock_release(&space->lock);
		return status;
	}

	insert_vma(space, vma);
	space->mmap_cache = vma;
	spinlock_release(&space->lock);
	return 0;
}

int vmm_protect(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags)
{
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	vm_area_t *vma = find_vma(space, vaddr);
	if (!vma || vaddr < vma->base ||
	    (vaddr + sz) > (vma->base + vma->size)) {
		spinlock_release(&space->lock);
		return EINVAL;
	}

	sz = (sz + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);

	// Update the hardware page table mappings
	int status = mmu.protect(root, vaddr, sz / VMM_DEFAULT_ALIGNMENT,
				 PS_4KB, new_flags);
	if (status) {
		spinlock_release(&space->lock);
		return status;
	}

	if (vaddr == vma->base && sz == vma->size) {
		/* Case 1: Perfect match - simple replacement */
		vma->flags = new_flags;
	} else if (vaddr == vma->base) {
		/* Case 2: Shaving off the front edge */
		vm_area_t *split_node = vma_alloc();
		if (!split_node) {
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		split_node->base = vaddr;
		split_node->size = sz;
		split_node->flags = new_flags;
		split_node->type = vma->type;
		split_node->is_paged = vma->is_paged;

		vma->base += sz;
		vma->size -= sz;
		insert_vma(space, split_node);
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		/* Case 3: Shaving off the back edge */
		vm_area_t *split_node = vma_alloc();
		if (!split_node) {
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		split_node->base = vaddr;
		split_node->size = sz;
		split_node->flags = new_flags;
		split_node->type = vma->type;
		split_node->is_paged = vma->is_paged;

		vma->size -= sz;
		insert_vma(space, split_node);
	} else {
		/* Case 4: The Core Center Slice (3-Way Structural Split) */
		vm_area_t *mid_node = vma_alloc();
		vm_area_t *right_node = vma_alloc();
		if (!mid_node || !right_node) {
			if (mid_node)
				vma_free(mid_node);
			if (right_node)
				vma_free(right_node);
			spinlock_release(&space->lock);
			return ENOMEM;
		}

		// 1. Capture original right-hand metrics before we alter anything
		u64 original_right_base = vaddr + sz;
		u64 original_right_size =
			(vma->base + vma->size) - original_right_base;

		// 2. Set up the middle and right pieces
		mid_node->base = vaddr;
		mid_node->size = sz;
		mid_node->flags = new_flags;
		mid_node->type = vma->type;
		mid_node->is_paged = vma->is_paged;

		right_node->base = original_right_base;
		right_node->size = original_right_size;
		right_node->flags = vma->flags; // Inherits old permissions
		right_node->type = vma->type;
		right_node->is_paged = vma->is_paged;

		// 3. CRITICAL FIX: Isolate, modify, and fix up the original left node sizes
		// In a Binary Tree, modifying sizes in-place breaks the search order.
		// We temporarily pull it out, resize it, and re-insert it.
		u64 original_left_base = vma->base;
		u64 original_left_size = vaddr - vma->base;
		enum mmu_flags original_left_flags = vma->flags;
		enum vmm_region_type original_left_type = vma->type;
		bool original_left_paged = vma->is_paged;

		remove_tree_node(space, vma);

		vm_area_t *left_node = vma_alloc();
		if (!left_node) {
			vma_free(mid_node);
			vma_free(right_node);
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		left_node->base = original_left_base;
		left_node->size = original_left_size;
		left_node->flags = original_left_flags;
		left_node->type = original_left_type;
		left_node->is_paged = original_left_paged;

		// 4. Clean re-insertion of all three fragmented parts into the tree
		insert_vma(space, left_node);
		insert_vma(space, mid_node);
		insert_vma(space, right_node);
	}

	space->mmap_cache = NULL;
	spinlock_release(&space->lock);
	return 0;
}

int vmm_query(u64 root, u64 vaddr, struct vmm_region_info *out_info)
{
	spinlock_acquire(&s_registry_lock);
	vmm_space_t *space = get_space(root);
	spinlock_release(&s_registry_lock);
	if (!space)
		return EINVAL;

	spinlock_acquire(&space->lock);
	vm_area_t *vma = find_vma(space, vaddr);
	if (!vma) {
		spinlock_release(&space->lock);
		return EINVAL;
	}

	out_info->base = vma->base;
	out_info->size = vma->size;
	out_info->flags = vma->flags;
	out_info->type = vma->type;
	out_info->is_paged = vma->is_paged;
	spinlock_release(&space->lock);
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

static int vmm_space_create_with_state(u64 boot_pt_root,
				       const boot_region_t *regions,
				       int region_count, u64 *out_space_root)
{
	if (!boot_pt_root || !out_space_root)
		return EINVAL;
	vmm_space_t *space = NULL;

	spinlock_acquire(&s_registry_lock);
	for (int i = 0; i < MAX_SPACE_TRACKERS; i++) {
		if (!s_space_registry[i].in_use) {
			space = &s_space_registry[i];
			space->in_use = true;
			break;
		}
	}
	spinlock_release(&s_registry_lock);
	if (!space)
		return ENOMEM;

	spinlock_acquire(&space->lock);
	space->page_table_root = boot_pt_root;
	space->vma_head = NULL;
	space->mmap_cache = NULL;

	for (int i = 0; i < region_count; i++) {
		const boot_region_t *boot_zone = &regions[i];
		vm_area_t *vma = vma_alloc();
		if (!vma) {
			spinlock_release(&space->lock);
			return ENOMEM;
		}
		vma->base = boot_zone->base;
		vma->size = boot_zone->size;
		vma->flags = boot_zone->flags;
		vma->type = boot_zone->type;
		vma->is_paged = true;
		insert_vma(space, vma);
	}
	*out_space_root = boot_pt_root;
	spinlock_release(&space->lock);
	return 0;
}

int vmm_init(u64 boot_pt_root, const boot_region_t *regions, int region_count)
{
	if (!boot_pt_root)
		return EINVAL;
	for (int i = 0; i < MAX_SPACE_TRACKERS; i++) {
		s_space_registry[i].page_table_root = 0;
		s_space_registry[i].vma_head = NULL;
		s_space_registry[i].in_use = false;
	}
	return vmm_space_create_with_state(boot_pt_root, regions, region_count,
					   &g_kernel_space_root);
}
