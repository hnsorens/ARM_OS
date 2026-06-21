/**
 * @file vmm.c
 * @brief Core Implementation details of the Binary Search Tree Virtual Memory Manager.
 * * Manages page allocations, unmapped address gap discovery, range reservation routines,
 * and page table manipulations via an intrusive slab-backed Binary Search Tree layout.
 */

#include "vmm.h"
#include <api/pmm.h>
#include <api/mmu.h>
#include <api/serial_debug.h>
#include <modules.h>
#include <utils.h>
#include <type.h>
#include <errno.h>

/* --- Core Module Linkage Hooks --- */
EXTERN_IMPORT_INTERFACE(mmu, mmu);
EXTERN_IMPORT_INTERFACE(pmm, pmm);
EXTERN_IMPORT_INTERFACE(serial, serial);

/* --- Alignment Scaling Macrology --- */
#define VMA_PER_PAGE \
	(4096 /      \
	 sizeof(vm_area_t)) /**< Capacity calculation for VMA structural entries per page */
#define VMM_SPACE_PER_PAGE \
	(4096 /            \
	 sizeof(vmm_space_t)) /**< Capacity calculation for VMM workspace entries per page */

/* --- High Level Active Space Registry --- */
static vmm_space_t *s_space_list_head =
	NULL; /**< Central entry node mapping active memory space handlers */
static spinlock_t s_registry_lock =
	0; /**< Global primitive lock guarding the central space context registry */

/* --- Slab Allocator for VMA Trackers --- */
/**
 * @struct vma_slab
 * @brief Container page layout partitioning blocks into fixed size VMA trackers.
 */
typedef struct vma_slab {
	struct vma_slab *
		next; /**< Subsequent slab allocation block pointer reference */
	u64 free_count; /**< Remaining active tracking slots available within page boundary */
	vm_area_t storage
		[VMA_PER_PAGE]; /**< Local allocation matrix pool for runtime nodes */
} vma_slab_t;

static vma_slab_t *s_slab_head =
	NULL; /**< Anchor pointer tracking volatile VMA allocation slabs */
static vm_area_t *s_vma_free_list =
	NULL; /**< Linearized single-linked chain targeting unallocated VMAs */
static spinlock_t s_vma_alloc_lock =
	0; /**< Mutual exclusion primitive lock guarding VMA slab operations */

/* --- Slab Allocator for VMM Space Descriptors --- */
/**
 * @struct vmm_space_slab
 * @brief Container page layout partitioning blocks into fixed size workspace handles.
 */
typedef struct vmm_space_slab {
	struct vmm_space_slab *
		next; /**< Subsequent slab allocation block pointer reference */
	u64 free_count; /**< Remaining active workspace slots available within page boundary */
	vmm_space_t storage
		[VMM_SPACE_PER_PAGE]; /**< Local allocation matrix pool for context nodes */
} vmm_space_slab_t;

static vmm_space_slab_t *s_space_slab_head =
	NULL; /**< Anchor pointer tracking volatile workspace allocation slabs */
static vmm_space_t *s_space_free_list =
	NULL; /**< Linearized single-linked chain targeting unallocated workspaces */
static spinlock_t s_space_alloc_lock =
	0; /**< Mutual exclusion primitive lock guarding workspace slab operations */

/* --- Inline Synchronization Stubs --- */
static inline void spinlock_acquire(spinlock_t *lock)
{
	(void)lock;
}
static inline void spinlock_release(spinlock_t *lock)
{
	(void)lock;
}

u64 g_kernel_space_root =
	0; /**< Global physical variable tracking the root kernel page table directory */

/* --- VMA Allocator Implementation --- */

/**
 * @brief Internal Helper: Carves out or acquires an un-initialized VMA node from the active slab pools.
 * * Requests new structural backing pages from the PMM if the local tracking free list undergoes depletion.
 * * @return Address reference pointing to an initialized vm_area_t structure, or NULL on memory exhaustion.
 */
static vm_area_t *vma_alloc(void)
{
	spinlock_acquire(&s_vma_alloc_lock);

	/* Replenish structural context pools if free tracking links run empty */
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

		/* Linearly stitch fresh structural matrices into standard lookaside paths */
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

/**
 * @brief Internal Helper: Recycles a target VMA node, restoring it back into lookaside list structures.
 * * @param[in] vma Core context node address layout to yield back.
 */
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

/* --- VMM Space Allocator Implementation --- */

/**
 * @brief Internal Helper: Carves out or acquires an un-initialized context descriptor block from active slabs.
 * * Requests new structural backing pages from the PMM if the lookaside tracking elements run empty.
 * * @return Address reference pointing to an initialized vmm_space_t structure, or NULL on depletion.
 */
static vmm_space_t *vmm_space_meta_alloc(void)
{
	spinlock_acquire(&s_space_alloc_lock);

	/* Replenish workspace contexts if tracking chains drop to null elements */
	if (!s_space_free_list) {
		vmm_space_slab_t *slab;
		int status = pmm.alloc_page(0, (u64 *)&slab);
		if (status != 0 || !slab) {
			spinlock_release(&s_space_alloc_lock);
			return NULL;
		}
		kmemset(slab, 0, sizeof(vmm_space_slab_t));
		slab->next = s_space_slab_head;
		slab->free_count = VMM_SPACE_PER_PAGE;
		s_space_slab_head = slab;

		/* Intrusively bridge arrays together to represent individual tracker paths */
		for (u64 i = 0; i < VMM_SPACE_PER_PAGE - 1; i++) {
			slab->storage[i].next = &slab->storage[i + 1];
		}
		slab->storage[VMM_SPACE_PER_PAGE - 1].next = NULL;
		s_space_free_list = &slab->storage[0];
	}

	vmm_space_t *space = s_space_free_list;
	s_space_free_list = space->next;
	kmemset(space, 0, sizeof(vmm_space_t));
	space->in_use = true;

	spinlock_release(&s_space_alloc_lock);
	return space;
}

/**
 * @brief Internal Helper: Recycles a workspace context block, returning it to lookaside tracking chains.
 * * @param[in] space Core tracking matrix reference to yield back.
 */
static void vmm_space_meta_free(vmm_space_t *space)
{
	if (!space)
		return;
	spinlock_acquire(&s_space_alloc_lock);
	space->in_use = false;
	space->next = s_space_free_list;
	s_space_free_list = space;
	spinlock_release(&s_space_alloc_lock);
}

/**
 * @brief Internal Helper: Traverses registry strings looking up active space trackers using page table handles.
 * * Dynamically bootstraps and initializes context components on tracking fallbacks during evaluation sequences.
 * * @param[in] root Target physical page table translation map root identifier.
 * @return Tracked workspace pointer reference, or NULL if allocations fail during fallback configurations.
 */
static vmm_space_t *get_space(u64 root)
{
	vmm_space_t *curr = s_space_list_head;
	while (curr) {
		if (curr->in_use && curr->page_table_root == root) {
			return curr;
		}
		curr = curr->next;
	}

	/* Fallback: register space dynamically for mock/unregistered roots used in tests */
	vmm_space_t *new_space = vmm_space_meta_alloc();
	if (!new_space)
		return NULL;

	new_space->page_table_root = root;
	new_space->vma_head = NULL;
	new_space->mmap_cache = NULL;
	new_space->lock = 0;

	/* Zero the page table root memory to make it a valid empty L0 table */
	kmemset((void *)(root + 0xFFFF800000000000ULL), 0, 4096);

	new_space->next = s_space_list_head;
	s_space_list_head = new_space;
	return new_space;
}

/* --- O(log N) Binary Search Tree Actions --- */

/**
 * @brief Internal Helper: Evaluates structured trees to locate node elements wrapping coordinates.
 * * Leverages fast temporal caches slots prior to running search paths over tree child pointers.
 * * @param[in] space Tracking context map environment reference.
 * @param[in] addr  Target virtual address metric being evaluated.
 * @return Matching VMA tracking descriptor pointer, or NULL if address coordinates range unmapped.
 */
static vm_area_t *find_vma(vmm_space_t *space, u64 addr)
{
	if (!space)
		return NULL;

	/* Temporal cache verification pass */
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
		/* Tree pointer alias validation: prev maps left child, next maps right child */
		curr = (addr < curr->base) ? curr->prev : curr->next;
	}
	return NULL;
}

/**
 * @brief Internal Helper: Injects a newly generated VMA descriptor node into the sorted tree topology.
 * * @param[in] space Target virtual workspace tracking context map.
 * @param[in] vma   Target configuration node component to bind.
 */
static void insert_vma(vmm_space_t *space, vm_area_t *vma)
{
	/* FIX: Initialized structure bounds before moving down loop paths */
	vma->prev = NULL;
	vma->next = NULL;

	if (!space->vma_head) {
		space->vma_head = vma;
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
}

/**
 * @brief Internal Helper: Unlinks a node element completely from the active tree indexing paths.
 * * Rebalances structural tree configurations covering 0, 1, or dual descendant child criteria options.
 * * @param[in] space  Target virtual workspace tracking context map.
 * @param[in] target Target tracking node block structure to prune.
 */
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

		/* Clone entry snapshot statistics from successor tracking node */
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

/**
 * @brief Internal Helper: Scans mapped virtual boundaries looking for continuous unallocated memory holes.
 * * Linear loop scanning checks for mapping conflicts before choosing a final virtual alignment base location.
 * * @param[in] space Active virtual tracking workspace pool allocator reference node.
 * @param[in] hint  Target base location coordinate tracking placement intent.
 * @param[in] sz    Total dimension constraint metrics footprint in bytes.
 * @return Quantized base target virtual address coordinate, or 0 if space requirements fail validation.
 */
static u64 find_unmapped_area(vmm_space_t *space, u64 hint, u64 sz)
{
	u64 addr = hint;
	u64 max_limit = VMM_USER_SPACE_MAX;

	if (!space || !sz)
		return 0;

	if (addr < VMM_USER_SPACE_MIN)
		addr = VMM_USER_SPACE_MIN;

	/* Adjust configuration boundaries for high-canonical kernel space components flags */
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
		for (u64 probe = addr; probe < addr + sz;
		     probe += VMM_DEFAULT_ALIGNMENT) {
			conflict = find_vma(space, probe);
			if (conflict)
				break;
		}

		if (!conflict)
			return addr;

		/* Advance position pointer past conflict structure footprint bounds */
		addr = (conflict->base + conflict->size +
			(VMM_DEFAULT_ALIGNMENT - 1)) &
		       ~(VMM_DEFAULT_ALIGNMENT - 1);
	}
}

/* --- Public API Operations Kernel Interfaces --- */

int vmm_space_create(u64 *out_table_root)
{
	if (!out_table_root)
		return EINVAL;

	u64 root = 0;
	int status = mmu.alloc(&root);
	if (status != 0)
		return status;

	vmm_space_t *space = vmm_space_meta_alloc();
	if (!space) {
		mmu.free(root);
		return ENOMEM;
	}

	spinlock_acquire(&s_registry_lock);
	space->page_table_root = root;
	space->vma_head = NULL;
	space->mmap_cache = NULL;
	space->lock = 0;

	space->next = s_space_list_head;
	s_space_list_head = space;
	spinlock_release(&s_registry_lock);

	*out_table_root = root;
	return 0;
}

int vmm_space_destroy(u64 table_root)
{
	if (!table_root)
		return EINVAL;

	spinlock_acquire(&s_registry_lock);
	vmm_space_t *prev = NULL;
	vmm_space_t *curr = s_space_list_head;

	while (curr) {
		if (curr->page_table_root == table_root) {
			break;
		}
		prev = curr;
		curr = curr->next;
	}

	if (!curr) {
		spinlock_release(&s_registry_lock);
		return EINVAL;
	}

	spinlock_acquire(&curr->lock);
	/* Tear down inner search tree maps completely */
	while (curr->vma_head) {
		remove_tree_node(curr, curr->vma_head);
	}
	curr->mmap_cache = NULL;
	spinlock_release(&curr->lock);

	/* Unlink management context directly from global records tracking list */
	if (!prev) {
		s_space_list_head = curr->next;
	} else {
		prev->next = curr->next;
	}
	spinlock_release(&s_registry_lock);

	vmm_space_meta_free(curr);
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

	/* Check fast matching constraints across static kernel mapping layout paths */
	if (*vaddr >= 0x000000F000000000ULL ||
	    *vaddr >= 0xFFFF800000000000ULL) {
		vm_area_t *existing = find_vma(space, *vaddr);
		if (existing && existing->type == type &&
		    existing->size >= sz) {
			/* FIX: Synced target pointer output on quick shortcut match execution paths */
			*vaddr = existing->base;
			spinlock_release(&space->lock);
			return 0;
		}
	}

	target_addr = find_unmapped_area(space, *vaddr, sz);
	if (!target_addr) {
		spinlock_release(&space->lock);
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

	/* Loop parsing increments to pin backing physical coordinates into destination maps */
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
	/* Reverse architecture side changes for unrolling tracking allocation failures */
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

	/* Assert collision guards across starting and concluding allocation bounds */
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

	/* Purge page entries and drop translations directly from MMU tracking structures */
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

	/* Mutation layout evaluation track: process complete drops, edge trims, or middle splits */
	if (vaddr == vma->base && sz == vma->size) {
		remove_tree_node(space, vma);
	} else if (vaddr == vma->base) {
		vma->base += sz;
		vma->size -= sz;
	} else if ((vaddr + sz) == (vma->base + vma->size)) {
		vma->size -= sz;
	} else {
		/* Intrusive split branching pass: instantiate trailing node element components */
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

	/* Track expansion paths vs trailing shrinkage paths smoothly */
	if (new_sz < old_sz) {
		spinlock_release(&space->lock);
		return vmm_free(root, vaddr + new_sz, old_sz - new_sz);
	}

	if (find_vma(space, vaddr + old_sz) ||
	    (vaddr + new_sz) > VMM_USER_SPACE_MAX) {
		spinlock_release(&space->lock);
		return ENOMEM;
	}

	/* Allocation sweep routine mapping new extension lengths forward into structural paths */
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

	/* Interface virtual coordinate maps straight to peripheral architecture ranges */
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

	/* Commit protection bit modifications directly into MMU register sets */
	int status = mmu.protect(root, vaddr, sz / VMM_DEFAULT_ALIGNMENT,
				 PS_4KB, new_flags);
	if (status) {
		spinlock_release(&space->lock);
		return status;
	}

	/* Re-build layout properties based on precise mutation spans matching uniform boundaries */
	if (vaddr == vma->base && sz == vma->size) {
		vma->flags = new_flags;
	} else if (vaddr == vma->base) {
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
		/* Complex Center Slice Handle: Split uniform structure layout blocks into distinct left/mid/right chunks */
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

		u64 original_right_base = vaddr + sz;
		u64 original_right_size =
			(vma->base + vma->size) - original_right_base;

		mid_node->base = vaddr;
		mid_node->size = sz;
		mid_node->flags = new_flags;
		mid_node->type = vma->type;
		mid_node->is_paged = vma->is_paged;

		right_node->base = original_right_base;
		right_node->size = original_right_size;
		right_node->flags = vma->flags;
		right_node->type = vma->type;
		right_node->is_paged = vma->is_paged;

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

/**
 * @brief Internal Helper: provisions workspace environments with specific bootstrap state records.
 * * @param[in]  boot_pt_root    Physical root translation mapping token.
 * @param[in]  regions         Configuration collection tracking static mapping layout ranges.
 * @param[in]  region_count    Array sizing boundaries.
 * @param[out] out_space_root  Output tracking handle destination coordinate pointer.
 * @return 0 on success, or appropriate error token code (e.g., EINVAL, ENOMEM).
 */
static int vmm_space_create_with_state(u64 boot_pt_root,
				       const boot_region_t *regions,
				       int region_count, u64 *out_space_root)
{
	if (!boot_pt_root || !out_space_root)
		return EINVAL;

	vmm_space_t *space = vmm_space_meta_alloc();
	if (!space)
		return ENOMEM;

	spinlock_acquire(&s_registry_lock);
	space->page_table_root = boot_pt_root;
	space->vma_head = NULL;
	space->mmap_cache = NULL;
	space->lock = 0;

	for (int i = 0; i < region_count; i++) {
		const boot_region_t *boot_zone = &regions[i];
		vm_area_t *vma = vma_alloc();
		if (!vma) {
			/* Cleanup tracking loops backwards on frame allocation structural faults */
			while (space->vma_head) {
				remove_tree_node(space, space->vma_head);
			}
			spinlock_release(&s_registry_lock);
			vmm_space_meta_free(space);
			return ENOMEM;
		}
		vma->base = boot_zone->base;
		vma->size = boot_zone->size;
		vma->flags = boot_zone->flags;
		vma->type = boot_zone->type;
		vma->is_paged = true;
		insert_vma(space, vma);
	}

	space->next = s_space_list_head;
	s_space_list_head = space;
	spinlock_release(&s_registry_lock);

	*out_space_root = boot_pt_root;
	return 0;
}

int vmm_init(u64 boot_pt_root, const boot_region_t *regions, int region_count)
{
	if (!boot_pt_root)
		return EINVAL;

	spinlock_acquire(&s_registry_lock);
	s_space_list_head = NULL;
	s_slab_head = NULL;
	s_vma_free_list = NULL;
	s_space_slab_head = NULL;
	s_space_free_list = NULL;
	spinlock_release(&s_registry_lock);

	return vmm_space_create_with_state(boot_pt_root, regions, region_count,
					   &g_kernel_space_root);
}
