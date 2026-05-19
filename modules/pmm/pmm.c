#include "pmm.h"
#include "../utils.h"

/* Global tracking instance variables */
static pmm_allocator_t g_pmm_allocator;
static pmm_page_meta_t *g_pmm_meta_array = NULL;
static size_t g_pmm_total_pages = 0;
static paddr_t g_pmm_hhdm_offset = 0;

/* Helper address arithmetic translation inline routines */
static inline void *paddr_to_kv(paddr_t phys)
{
	return (void *)(phys + g_pmm_hhdm_offset);
}

static inline paddr_t kv_to_paddr(void *virt)
{
	return (paddr_t)((uint64_t)virt - g_pmm_hhdm_offset);
}

static inline size_t paddr_to_index(paddr_t phys)
{
	return phys / PMM_PAGE_SIZE;
}

static inline paddr_t index_to_paddr(size_t index)
{
	return (paddr_t)(index * PMM_PAGE_SIZE);
}

/* --- Internal Core Free List Manipulation Helpers --- */
static void pmm_list_add(uint8_t order, pmm_block_node_t *node)
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

static void pmm_list_remove(uint8_t order, pmm_block_node_t *node)
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
void pmm_init(memory_region_t *memory_map, size_t region_count,
	      paddr_t hhdm_offset)
{
	g_pmm_hhdm_offset = hhdm_offset;
	size_t highest_address = 0;

	for (size_t i = 0; i < region_count; i++) {
		size_t end_addr = memory_map[i].start + memory_map[i].size;
		if (end_addr > highest_address) {
			highest_address = end_addr;
		}
	}

	g_pmm_total_pages = highest_address / PMM_PAGE_SIZE;
	size_t meta_array_size = g_pmm_total_pages * sizeof(pmm_page_meta_t);
	size_t meta_pages_needed =
		(meta_array_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

	/* Secure space for the global allocation array inside a free block */
	paddr_t meta_phys_alloc_start = 0;
	for (size_t i = 0; i < region_count; i++) {
		if (memory_map[i].memory_type == MEMORY_FREE &&
		    memory_map[i].size >= (meta_pages_needed * PMM_PAGE_SIZE)) {
			meta_phys_alloc_start = memory_map[i].start;
			memory_map[i].start +=
				(meta_pages_needed * PMM_PAGE_SIZE);
			memory_map[i].size -=
				(meta_pages_needed * PMM_PAGE_SIZE);
			break;
		}
	}

	g_pmm_meta_array =
		(pmm_page_meta_t *)paddr_to_kv(meta_phys_alloc_start);

	/* Pre-initialize physical page frame descriptor limits */
	for (size_t i = 0; i < g_pmm_total_pages; i++) {
		g_pmm_meta_array[i].ref_count = 0;
		g_pmm_meta_array[i].order = 0;
		g_pmm_meta_array[i].is_free =
			0; /* Default block behavior: Marked Allocated */
	}

	for (uint8_t o = 0; o < PMM_MAX_ORDER; o++) {
		g_pmm_allocator.orders[o].head = NULL;
		g_pmm_allocator.orders[o].block_count = 0;
	}

	/* Populate buddy sections sequentially using valid memory maps */
	for (size_t i = 0; i < region_count; i++) {
		if (memory_map[i].memory_type != MEMORY_FREE)
			continue;

		paddr_t chunk_cursor = memory_map[i].start;
		paddr_t chunk_end = chunk_cursor + memory_map[i].size;

		while (chunk_cursor < chunk_end) {
			size_t remaining_bytes = chunk_end - chunk_cursor;
			uint8_t target_order = PMM_MAX_ORDER - 1;

			/* Extract target allocation limits aligned to order boundaries */
			while (target_order > 0) {
				size_t block_bytes =
					(1UL << target_order) * PMM_PAGE_SIZE;
				if (block_bytes <= remaining_bytes &&
				    (chunk_cursor % block_bytes) == 0) {
					break;
				}
				target_order--;
			}

			size_t page_idx = paddr_to_index(chunk_cursor);
			g_pmm_meta_array[page_idx].is_free = 1;
			g_pmm_meta_array[page_idx].order = target_order;
			g_pmm_meta_array[page_idx].ref_count = 0;

			pmm_block_node_t *node =
				(pmm_block_node_t *)paddr_to_kv(chunk_cursor);
			pmm_list_add(target_order, node);

			size_t allocated_bytes =
				(1UL << target_order) * PMM_PAGE_SIZE;
			g_pmm_allocator.total_memory_bytes += allocated_bytes;
			g_pmm_allocator.free_memory_bytes += allocated_bytes;

			chunk_cursor += allocated_bytes;
		}
	}
}

/* --- Allocation & Release Operations Implementation --- */
k_status_t pmm_alloc_page(uint8_t page_order, paddr_t *out_frame)
{
	if (page_order >= PMM_MAX_ORDER || !out_frame)
		return K_STATUS_INVALID_ARG;

	for (uint8_t current_order = page_order; current_order < PMM_MAX_ORDER;
	     current_order++) {
		pmm_order_list_t *list = &g_pmm_allocator.orders[current_order];
		if (list->head == NULL)
			continue;

		/* Pop a tracking block node from the target block list */
		pmm_block_node_t *chosen_node = list->head;
		paddr_t found_block_phys = kv_to_paddr(chosen_node);
		pmm_list_remove(current_order, chosen_node);

		size_t tracking_index = paddr_to_index(found_block_phys);
		g_pmm_meta_array[tracking_index].is_free = 0;

		/* Split larger blocks into required buddy sizes sequentially */
		while (current_order > page_order) {
			current_order--;
			size_t split_block_size =
				(1UL << current_order) * PMM_PAGE_SIZE;
			paddr_t buddy_phys_part =
				found_block_phys + split_block_size;
			size_t buddy_index_part =
				paddr_to_index(buddy_phys_part);

			g_pmm_meta_array[buddy_index_part].is_free = 1;
			g_pmm_meta_array[buddy_index_part].order =
				current_order;
			g_pmm_meta_array[buddy_index_part].ref_count = 0;

			pmm_block_node_t *buddy_node =
				(pmm_block_node_t *)paddr_to_kv(
					buddy_phys_part);
			pmm_list_add(current_order, buddy_node);
		}

		g_pmm_meta_array[tracking_index].order = page_order;
		g_pmm_meta_array[tracking_index].ref_count = 1;
		g_pmm_allocator.free_memory_bytes -=
			(1UL << page_order) * PMM_PAGE_SIZE;

		*out_frame = found_block_phys;
		return K_STATUS_OK;
	}

	return K_STATUS_OUT_OF_MEMORY;
}

k_status_t pmm_free_page(uint8_t page_order, paddr_t frame)
{
	if (page_order >= PMM_MAX_ORDER || (frame % PMM_PAGE_SIZE) != 0)
		return K_STATUS_INVALID_ARG;

	size_t current_order = page_order;
	paddr_t current_frame = frame;
	size_t initial_block_bytes = (1UL << page_order) * PMM_PAGE_SIZE;

	while (current_order < PMM_MAX_ORDER - 1) {
		size_t block_bytes = (1UL << current_order) * PMM_PAGE_SIZE;
		paddr_t buddy_frame = current_frame ^ block_bytes;
		size_t buddy_index = paddr_to_index(buddy_frame);

		if (buddy_index >= g_pmm_total_pages)
			break;

		/* Verify block integration safety properties */
		if (!g_pmm_meta_array[buddy_index].is_free ||
		    g_pmm_meta_array[buddy_index].order != current_order) {
			break;
		}

		/* Coalesce: Remove buddy node from current order freelist tracking */
		pmm_block_node_t *buddy_node =
			(pmm_block_node_t *)paddr_to_kv(buddy_frame);
		pmm_list_remove(current_order, buddy_node);
		g_pmm_meta_array[buddy_index].is_free = 0;

		current_frame = (current_frame < buddy_frame) ? current_frame :
								buddy_frame;
		current_order++;
	}

	size_t final_index = paddr_to_index(current_frame);
	g_pmm_meta_array[final_index].is_free = 1;
	g_pmm_meta_array[final_index].order = current_order;
	g_pmm_meta_array[final_index].ref_count = 0;

	pmm_block_node_t *final_node =
		(pmm_block_node_t *)paddr_to_kv(current_frame);
	pmm_list_add(current_order, final_node);

	g_pmm_allocator.free_memory_bytes += initial_block_bytes;
	return K_STATUS_OK;
}

/* --- Specialized Multi-Page Allocation Controllers --- */
k_status_t pmm_alloc_aligned(size_t count, size_t alignment, paddr_t *out)
{
	if (count == 0 || alignment < PMM_PAGE_SIZE || !out)
		return K_STATUS_INVALID_ARG;

	/* Calculate the order required to cleanly cover the requested page count */
	uint8_t target_order = 0;
	while ((1UL << target_order) < count) {
		target_order++;
		if (target_order >= PMM_MAX_ORDER)
			return K_STATUS_INVALID_ARG;
	}

	/* Continuously look for a block that satisfies the alignment requirements */
	for (uint8_t o = target_order; o < PMM_MAX_ORDER; o++) {
		pmm_block_node_t *curr = g_pmm_allocator.orders[o].head;
		while (curr) {
			paddr_t phys = kv_to_paddr(curr);
			if ((phys % alignment) == 0) {
				/* Found an aligned block! Remove it and process any necessary splits */
				pmm_list_remove(o, curr);
				size_t base_idx = paddr_to_index(phys);
				g_pmm_meta_array[base_idx].is_free = 0;

				uint8_t active_order = o;
				while (active_order > target_order) {
					active_order--;
					size_t split_sz =
						(1UL << active_order) *
						PMM_PAGE_SIZE;
					paddr_t split_buddy = phys + split_sz;
					size_t buddy_idx =
						paddr_to_index(split_buddy);

					g_pmm_meta_array[buddy_idx].is_free = 1;
					g_pmm_meta_array[buddy_idx].order =
						active_order;
					g_pmm_meta_array[buddy_idx].ref_count =
						0;

					pmm_list_add(
						active_order,
						(pmm_block_node_t *)paddr_to_kv(
							split_buddy));
				}

				g_pmm_meta_array[base_idx].order = target_order;
				g_pmm_meta_array[base_idx].ref_count = 1;
				g_pmm_allocator.free_memory_bytes -=
					(1UL << target_order) * PMM_PAGE_SIZE;

				*out = phys;
				return K_STATUS_OK;
			}
			curr = curr->next;
		}
	}
	return K_STATUS_OUT_OF_MEMORY;
}

k_status_t pmm_alloc_in_range(size_t count, paddr_t max_addr, paddr_t *out)
{
	if (count == 0 || !out)
		return K_STATUS_INVALID_ARG;

	uint8_t target_order = 0;
	while ((1UL << target_order) < count) {
		target_order++;
		if (target_order >= PMM_MAX_ORDER)
			return K_STATUS_INVALID_ARG;
	}

	for (uint8_t o = target_order; o < PMM_MAX_ORDER; o++) {
		pmm_block_node_t *curr = g_pmm_allocator.orders[o].head;
		while (curr) {
			paddr_t phys = kv_to_paddr(curr);
			size_t allocation_bytes =
				(1UL << target_order) * PMM_PAGE_SIZE;

			if ((phys + allocation_bytes) <= max_addr) {
				pmm_list_remove(o, curr);
				size_t base_idx = paddr_to_index(phys);
				g_pmm_meta_array[base_idx].is_free = 0;

				uint8_t active_order = o;
				while (active_order > target_order) {
					active_order--;
					size_t split_sz =
						(1UL << active_order) *
						PMM_PAGE_SIZE;
					paddr_t split_buddy = phys + split_sz;
					size_t buddy_idx =
						paddr_to_index(split_buddy);

					g_pmm_meta_array[buddy_idx].is_free = 1;
					g_pmm_meta_array[buddy_idx].order =
						active_order;
					g_pmm_meta_array[buddy_idx].ref_count =
						0;

					pmm_list_add(
						active_order,
						(pmm_block_node_t *)paddr_to_kv(
							split_buddy));
				}

				g_pmm_meta_array[base_idx].order = target_order;
				g_pmm_meta_array[base_idx].ref_count = 1;
				g_pmm_allocator.free_memory_bytes -=
					allocation_bytes;

				*out = phys;
				return K_STATUS_OK;
			}
			curr = curr->next;
		}
	}
	return K_STATUS_OUT_OF_MEMORY;
}

/* --- Reference Counting Engine Routines --- */
void pmm_retain(paddr_t frame)
{
	size_t page_idx = paddr_to_index(frame);
	if (page_idx >= g_pmm_total_pages || g_pmm_meta_array[page_idx].is_free)
		return;

	g_pmm_meta_array[page_idx].ref_count++;
}

void pmm_release(paddr_t frame)
{
	size_t page_idx = paddr_to_index(frame);
	if (page_idx >= g_pmm_total_pages || g_pmm_meta_array[page_idx].is_free)
		return;

	if (g_pmm_meta_array[page_idx].ref_count > 0) {
		g_pmm_meta_array[page_idx].ref_count--;
		if (g_pmm_meta_array[page_idx].ref_count == 0) {
			pmm_free_page(g_pmm_meta_array[page_idx].order, frame);
		}
	}
}

/* --- System Resource Metrics & Statistics Helpers --- */
size_t pmm_get_total_memory(void)
{
	return g_pmm_allocator.total_memory_bytes;
}

size_t pmm_get_free_memory(void)
{
	return g_pmm_allocator.free_memory_bytes;
}

k_status_t pmm_reserve_range(paddr_t start, size_t sz)
{
	if ((start % PMM_PAGE_SIZE) != 0 || sz == 0)
		return K_STATUS_INVALID_ARG;

	size_t start_page_idx = paddr_to_index(start);
	size_t pages_to_reserve = (sz + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
	size_t end_page_idx = start_page_idx + pages_to_reserve;

	if (end_page_idx > g_pmm_total_pages)
		return K_STATUS_INVALID_ARG;

	/* Marks target frame ranges allocated to protect them from allocations */
	for (size_t idx = start_page_idx; idx < end_page_idx; idx++) {
		if (g_pmm_meta_array[idx].is_free) {
			/* Find and extract the host tracking block from the current order list */
			uint8_t current_order = g_pmm_meta_array[idx].order;
			paddr_t block_base_phys = index_to_paddr(idx);
			pmm_block_node_t *node =
				(pmm_block_node_t *)paddr_to_kv(
					block_base_phys);

			pmm_list_remove(current_order, node);
			g_pmm_meta_array[idx].is_free = 0;
			g_pmm_meta_array[idx].ref_count = 1;
			g_pmm_meta_array[idx].order = 0;

			g_pmm_allocator.free_memory_bytes -=
				(1UL << current_order) * PMM_PAGE_SIZE;
		}
	}

	return K_STATUS_OK;
}
