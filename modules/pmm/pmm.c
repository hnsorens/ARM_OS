#include "pmm.h"
#include "../utils.h"
#include "../../include/errno.h"
#include "../modules.h"
#include "../../include/api/serial_debug.h"

EXTERN_IMPORT_INTERFACE(serial, serial);

/* Global tracking instance variables */
static pmm_allocator_t g_pmm_allocator;
static pmm_page_meta_t *g_pmm_meta_array = NULL;
static u64 g_pmm_total_pages = 0;
static u64 g_pmm_hhdm_offset = 0;

/* Helper address arithmetic translation inline routines */
static inline void *u64o_kv(u64 phys)
{
	return (void *)(phys + g_pmm_hhdm_offset);
}

static inline u64 kv_to_paddr(void *virt)
{
	return (u64)((u64)virt - g_pmm_hhdm_offset);
}

static inline u64 u64o_index(u64 phys)
{
	return phys / PMM_PAGE_SIZE;
}

static inline u64 index_to_paddr(u64 index)
{
	return (u64)(index * PMM_PAGE_SIZE);
}

/* --- Internal Core Free List Manipulation Helpers --- */
static void pmm_list_add(u8 order, pmm_block_node_t *node)
{
	pmm_order_list_t *list = &g_pmm_allocator.orders[order];
	node->next = list->head;
	node->prev = NULL;
	if (list->head) {
		list->head->prev = node;
	}
	list->head = node;
	list->block_count++;
}

static void pmm_list_remove(u8 order, pmm_block_node_t *node)
{
	pmm_order_list_t *list = &g_pmm_allocator.orders[order];
	if (node->prev) {
		node->prev->next = node->next;
	} else {
		list->head = node->next;
	}
	if (node->next) {
		node->next->prev = node->prev;
	}
	list->block_count--;
}

/* --- Initialization Subsystem Routines --- */
void pmm_init(memory_region_t *memory_map, u64 region_count, u64 hhdm_offset)
{
	g_pmm_hhdm_offset = hhdm_offset;
	u64 highest_address = 0;

	for (u64 i = 0; i < region_count; i++) {
		u64 end_addr = memory_map[i].start +
			       (memory_map[i].size * PMM_PAGE_SIZE);
		if (end_addr > highest_address) {
			highest_address = end_addr;
		}
	}

	g_pmm_total_pages = highest_address / PMM_PAGE_SIZE;
	u64 meta_array_size = g_pmm_total_pages * sizeof(pmm_page_meta_t);
	u64 meta_pages_needed =
		(meta_array_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

	/* Secure space for the global allocation array inside a free block */
	u64 meta_phys_alloc_start = 0;
	for (u64 i = 0; i < region_count; i++) {
		if (memory_map[i].memory_type == MEMORY_FREE &&
		    memory_map[i].size >= meta_pages_needed) {
			meta_phys_alloc_start = memory_map[i].start;

			memory_map[i].start +=
				(meta_pages_needed * PMM_PAGE_SIZE);
			memory_map[i].size -= meta_pages_needed;
			break;
		}
	}

	g_pmm_meta_array = (pmm_page_meta_t *)u64o_kv(meta_phys_alloc_start);

	/* Pre-initialize physical page frame descriptor limits */
	kmemset(g_pmm_meta_array, 0,
		sizeof(pmm_page_meta_t) * g_pmm_total_pages);

	for (u8 o = 0; o < PMM_MAX_ORDER; o++) {
		g_pmm_allocator.orders[o].head = NULL;
		g_pmm_allocator.orders[o].block_count = 0;
	}

	g_pmm_allocator.total_memory_bytes = 0;
	g_pmm_allocator.free_memory_bytes = 0;

	/* Populate buddy sections sequentially using valid memory maps */
	for (u64 i = 0; i < region_count; i++) {
		if (memory_map[i].memory_type != MEMORY_FREE)
			continue;

		u64 chunk_cursor = memory_map[i].start;
		u64 chunk_end =
			chunk_cursor + (memory_map[i].size * PMM_PAGE_SIZE);

		while (chunk_cursor < chunk_end) {
			u64 remaining_bytes = chunk_end - chunk_cursor;
			u8 target_order = PMM_MAX_ORDER - 1;

			/* FIX: Look for the LARGEST valid buddy order block that fits alignment and bounds constraints */
			while (target_order > 0) {
				u64 block_bytes =
					(1UL << target_order) * PMM_PAGE_SIZE;
				if (block_bytes <= remaining_bytes &&
				    (chunk_cursor % block_bytes) == 0) {
					break;
				}
				target_order--;
			}

			u64 allocated_bytes =
				(1UL << target_order) * PMM_PAGE_SIZE;
			u64 base_page_idx = u64o_index(chunk_cursor);
			u64 block_pages = 1UL << target_order;

			/* Set metadata configurations across the entire range of pages inside this block */
			for (u64 p = 0; p < block_pages; p++) {
				g_pmm_meta_array[base_page_idx + p].is_free = 1;
				g_pmm_meta_array[base_page_idx + p].order =
					target_order;
				g_pmm_meta_array[base_page_idx + p].ref_count =
					0;
			}

			/* Overlay the free-list node directly into the newly registered frame head */
			pmm_block_node_t *node =
				(pmm_block_node_t *)u64o_kv(chunk_cursor);
			pmm_list_add(target_order, node);

			g_pmm_allocator.total_memory_bytes += allocated_bytes;
			g_pmm_allocator.free_memory_bytes += allocated_bytes;

			chunk_cursor += allocated_bytes;
		}
	}
}

/* --- Allocation & Release Operations Implementation --- */
int pmm_alloc_page(u8 page_order, u64 *out_frame)
{
	if (page_order >= PMM_MAX_ORDER || !out_frame)
		return EINVAL;

	for (u8 current_order = page_order; current_order < PMM_MAX_ORDER;
	     current_order++) {
		pmm_order_list_t *list = &g_pmm_allocator.orders[current_order];
		if (list->head == NULL)
			continue;

		pmm_block_node_t *chosen_node = list->head;
		u64 found_block_phys = kv_to_paddr(chosen_node);
		pmm_list_remove(current_order, chosen_node);

		u64 tracking_index = u64o_index(found_block_phys);

		u64 allocated_pages = 1UL << current_order;
		for (u64 p = 0; p < allocated_pages; p++) {
			g_pmm_meta_array[tracking_index + p].is_free = 0;
		}

		/* Split larger blocks into required buddy sizes sequentially */
		while (current_order > page_order) {
			current_order--;
			u64 split_block_size =
				(1UL << current_order) * PMM_PAGE_SIZE;
			u64 buddy_phys_part =
				found_block_phys + split_block_size;
			u64 buddy_index_part = u64o_index(buddy_phys_part);
			u64 buddy_pages = 1UL << current_order;

			for (u64 p = 0; p < buddy_pages; p++) {
				g_pmm_meta_array[buddy_index_part + p].is_free =
					1;
				g_pmm_meta_array[buddy_index_part + p].order =
					current_order;
				g_pmm_meta_array[buddy_index_part + p]
					.ref_count = 0;
			}

			pmm_block_node_t *buddy_node =
				(pmm_block_node_t *)u64o_kv(buddy_phys_part);
			pmm_list_add(current_order, buddy_node);
		}

		g_pmm_meta_array[tracking_index].order = page_order;
		g_pmm_meta_array[tracking_index].ref_count = 1;
		g_pmm_allocator.free_memory_bytes -=
			(1UL << page_order) * PMM_PAGE_SIZE;

		*out_frame = found_block_phys;
		return 0;
	}

	return ENOMEM;
}

static int pmm_free_page(u8 page_order, u64 frame)
{
	if (page_order >= PMM_MAX_ORDER || (frame % PMM_PAGE_SIZE) != 0)
		return EINVAL;

	u64 current_order = page_order;
	u64 current_frame = frame;
	u64 initial_block_bytes = (1UL << page_order) * PMM_PAGE_SIZE;

	while (current_order < PMM_MAX_ORDER - 1) {
		u64 block_bytes = (1UL << current_order) * PMM_PAGE_SIZE;
		u64 buddy_frame = current_frame ^ block_bytes;
		u64 buddy_index = u64o_index(buddy_frame);

		if (buddy_index >= g_pmm_total_pages)
			break;

		if (!g_pmm_meta_array[buddy_index].is_free ||
		    g_pmm_meta_array[buddy_index].order != current_order) {
			break;
		}

		pmm_block_node_t *buddy_node =
			(pmm_block_node_t *)u64o_kv(buddy_frame);
		pmm_list_remove(current_order, buddy_node);

		u64 buddy_pages = 1UL << current_order;
		for (u64 p = 0; p < buddy_pages; p++) {
			g_pmm_meta_array[buddy_index + p].is_free = 0;
		}

		current_frame = (current_frame < buddy_frame) ? current_frame :
								buddy_frame;
		current_order++;
	}

	u64 final_index = u64o_index(current_frame);
	u64 final_pages = 1UL << current_order;
	for (u64 p = 0; p < final_pages; p++) {
		g_pmm_meta_array[final_index + p].is_free = 1;
		g_pmm_meta_array[final_index + p].order = current_order;
		g_pmm_meta_array[final_index + p].ref_count = 0;
	}

	pmm_block_node_t *final_node =
		(pmm_block_node_t *)u64o_kv(current_frame);
	pmm_list_add(current_order, final_node);

	g_pmm_allocator.free_memory_bytes += initial_block_bytes;
	return 0;
}

/* --- Specialized Multi-Page Allocation Controllers --- */
int pmm_alloc_aligned(u64 count, u64 alignment, u64 *out)
{
	if (count == 0 || alignment < PMM_PAGE_SIZE || !out)
		return EINVAL;

	u8 target_order = 0;
	while ((1UL << target_order) < count) {
		target_order++;
		if (target_order >= PMM_MAX_ORDER)
			return EINVAL;
	}

	for (u8 o = target_order; o < PMM_MAX_ORDER; o++) {
		pmm_block_node_t *curr = g_pmm_allocator.orders[o].head;
		while (curr) {
			u64 phys = kv_to_paddr(curr);
			if ((phys % alignment) == 0) {
				pmm_list_remove(o, curr);
				u64 base_idx = u64o_index(phys);

				u64 total_allocated_pages = 1UL << o;
				for (u64 p = 0; p < total_allocated_pages;
				     p++) {
					g_pmm_meta_array[base_idx + p].is_free =
						0;
				}

				u8 active_order = o;
				while (active_order > target_order) {
					active_order--;
					u64 split_sz = (1UL << active_order) *
						       PMM_PAGE_SIZE;
					u64 split_buddy = phys + split_sz;
					u64 buddy_idx = u64o_index(split_buddy);
					u64 buddy_pages = 1UL << active_order;

					for (u64 p = 0; p < buddy_pages; p++) {
						g_pmm_meta_array[buddy_idx + p]
							.is_free = 1;
						g_pmm_meta_array[buddy_idx + p]
							.order = active_order;
						g_pmm_meta_array[buddy_idx + p]
							.ref_count = 0;
					}

					pmm_list_add(
						active_order,
						(pmm_block_node_t *)u64o_kv(
							split_buddy));
				}

				g_pmm_meta_array[base_idx].order = target_order;
				g_pmm_meta_array[base_idx].ref_count = 1;
				g_pmm_allocator.free_memory_bytes -=
					(1UL << target_order) * PMM_PAGE_SIZE;

				*out = phys;
				return 0;
			}
			curr = curr->next;
		}
	}
	return ENOMEM;
}

int pmm_alloc_in_range(u64 count, u64 max_addr, u64 *out)
{
	if (count == 0 || !out)
		return EINVAL;

	u8 target_order = 0;
	while ((1UL << target_order) < count) {
		target_order++;
		if (target_order >= PMM_MAX_ORDER)
			return EINVAL;
	}

	for (u8 o = target_order; o < PMM_MAX_ORDER; o++) {
		pmm_block_node_t *curr = g_pmm_allocator.orders[o].head;
		while (curr) {
			u64 phys = kv_to_paddr(curr);
			u64 allocation_bytes =
				(1UL << target_order) * PMM_PAGE_SIZE;

			if ((phys + allocation_bytes) <= max_addr) {
				pmm_list_remove(o, curr);
				u64 base_idx = u64o_index(phys);

				u64 total_allocated_pages = 1UL << o;
				for (u64 p = 0; p < total_allocated_pages;
				     p++) {
					g_pmm_meta_array[base_idx + p].is_free =
						0;
				}

				u8 active_order = o;
				while (active_order > target_order) {
					active_order--;
					u64 split_sz = (1UL << active_order) *
						       PMM_PAGE_SIZE;
					u64 split_buddy = phys + split_sz;
					u64 buddy_idx = u64o_index(split_buddy);
					u64 buddy_pages = 1UL << active_order;

					for (u64 p = 0; p < buddy_pages; p++) {
						g_pmm_meta_array[buddy_idx + p]
							.is_free = 1;
						g_pmm_meta_array[buddy_idx + p]
							.order = active_order;
						g_pmm_meta_array[buddy_idx + p]
							.ref_count = 0;
					}

					pmm_list_add(
						active_order,
						(pmm_block_node_t *)u64o_kv(
							split_buddy));
				}

				g_pmm_meta_array[base_idx].order = target_order;
				g_pmm_meta_array[base_idx].ref_count = 1;
				g_pmm_allocator.free_memory_bytes -=
					allocation_bytes;

				*out = phys;
				return 0;
			}
			curr = curr->next;
		}
	}
	return ENOMEM;
}

/* --- Reference Counting Engine Routines --- */
int pmm_retain(u64 frame)
{
	u64 page_idx = u64o_index(frame);
	if (page_idx >= g_pmm_total_pages || g_pmm_meta_array[page_idx].is_free)
		return EFAULT;

	g_pmm_meta_array[page_idx].ref_count++;
	return 0;
}

int pmm_release(u64 frame)
{
	u64 page_idx = u64o_index(frame);
	if (page_idx >= g_pmm_total_pages || g_pmm_meta_array[page_idx].is_free)
		return EFAULT;

	if (g_pmm_meta_array[page_idx].ref_count > 0) {
		g_pmm_meta_array[page_idx].ref_count--;
		if (g_pmm_meta_array[page_idx].ref_count == 0) {
			return pmm_free_page(g_pmm_meta_array[page_idx].order,
					     frame);
		}
		return 0;
	}
	return EFAULT;
}

/* --- System Resource Metrics & Statistics Helpers --- */
u64 pmm_get_total_memory(void)
{
	return g_pmm_allocator.total_memory_bytes;
}

u64 pmm_get_free_memory(void)
{
	return g_pmm_allocator.free_memory_bytes;
}

int pmm_reserve_range(u64 start, u64 sz)
{
	if ((start % PMM_PAGE_SIZE) != 0 || sz == 0)
		return 0;

	u64 start_page_idx = u64o_index(start);
	u64 pages_to_reserve = (sz + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
	u64 end_page_idx = start_page_idx + pages_to_reserve;

	if (end_page_idx > g_pmm_total_pages)
		return EINVAL;

	for (u64 idx = start_page_idx; idx < end_page_idx; idx++) {
		if (g_pmm_meta_array[idx].is_free) {
			u8 current_order = g_pmm_meta_array[idx].order;
			u64 block_base_phys = index_to_paddr(idx);
			pmm_block_node_t *node =
				(pmm_block_node_t *)u64o_kv(block_base_phys);

			pmm_list_remove(current_order, node);

			u64 block_pages = 1UL << current_order;
			for (u64 p = 0; p < block_pages; p++) {
				g_pmm_meta_array[idx + p].is_free = 0;
				g_pmm_meta_array[idx + p].ref_count = 1;
				g_pmm_meta_array[idx + p].order = 0;
			}

			g_pmm_allocator.free_memory_bytes -=
				(1UL << current_order) * PMM_PAGE_SIZE;
		}
	}

	return 0;
}
