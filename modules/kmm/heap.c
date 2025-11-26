#include "heap.h"

#include "../module_debug.h"

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))


void* memset(void* ptr, int value, unsigned long num) {
    unsigned char* p = (unsigned char*)ptr;
    unsigned char byte_value = (unsigned char)value;
    
    for (unsigned long i = 0; i < num; i++) {
        p[i] = byte_value;
    }
    
    return ptr;
}

unsigned long memcpy(unsigned long dest, const unsigned long src, unsigned long num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    for (unsigned long i = 0; i < num; i++) {
        d[i] = s[i];
    }
    
    return (unsigned long)dest;
}

static block_header_t *find_free_block(heap_t* heap, unsigned long size)
{
  block_header_t* prev = 0;
    block_header_t* curr = heap->free_list;
    block_header_t* best_fit = 0;
    block_header_t* best_fit_prev = 0;
    
    // Best fit algorithm
    while (curr) {
        if (curr->size >= size) {
            if (!best_fit || curr->size < best_fit->size) {
                best_fit = curr;
                best_fit_prev = prev;
            }
        }
        prev = curr;
        curr = curr->next;
    }
    
    if (best_fit) {
        // Remove from free list
        if (best_fit_prev) {
            best_fit_prev->next = best_fit->next;
        } else {
            heap->free_list = best_fit->next;
        }
        return best_fit;
    }
    
    return 0;
}

static void split_block(heap_t* heap, block_header_t *block, unsigned long size)
{
  unsigned long remaining_size = block->size - size - sizeof(block_header_t);
    
    if (remaining_size >= ALIGNMENT) { // Only split if worthwhile
        block_header_t* new_block = (block_header_t*)((char*)block + sizeof(block_header_t) + size);
        new_block->size = remaining_size;
        new_block->is_free = 1;
        new_block->next = heap->free_list;
        heap->free_list = new_block;
        
        block->size = size;
    }
}

static void coalesce_blocks(heap_t* heap)
{
  block_header_t* curr = heap->free_list;
    block_header_t* new_list = 0;
    
    // Sort blocks by address to make coalescing easier
    // Simple bubble sort (inefficient for large lists, but simple)
    int swapped;
    do {
        swapped = 0;
        curr = heap->free_list;
        block_header_t* prev = 0;
        
        while (curr && curr->next) {
            if ((void*)curr > (void*)curr->next) {
                // Swap nodes
                block_header_t* temp = curr->next;
                curr->next = temp->next;
                temp->next = curr;
                
                if (prev) {
                    prev->next = temp;
                } else {
                    heap->free_list = temp;
                }
                
                prev = temp;
                swapped = 0;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    } while (swapped);
    
    // Now coalesce adjacent blocks
    curr = heap->free_list;
    while (curr && curr->next) {
        block_header_t* next = (block_header_t*)((char*)curr + sizeof(block_header_t) + curr->size);
        
        if (next == curr->next) {
            // Merge current block with next
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

static int is_valid_pointer(heap_t* heap, unsigned long ptr)
{
  if (!ptr || !heap->start) {
        return 0;
    }
    
    // Check if pointer is within heap bounds
    if (ptr < heap->start || ptr >= heap->end) {
        return 0;
    }
    
    // Check if pointer is properly aligned and points to a valid block header
    block_header_t* block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    
    // Simple validation: check if block size seems reasonable
    if ((char*)block + sizeof(block_header_t) + block->size > (char*)heap->end) {
        return 0;
    }
    
    return 1;
}

void heap_init(heap_t* heap, ppm_vtable_t* pmm, unsigned long heap_base, unsigned long heap_size)
{
  if (heap_size < sizeof(block_header_t))
  {
    return;
  }

  heap->start = heap_base;
  heap->end = heap_base + heap_size;
  pmm->alloc_virt_kernel(heap->start, heap_size);

  block_header_t* first_block = (block_header_t*)heap_base;
  first_block->size = heap_size - sizeof(block_header_t);
  first_block->next = 0;
  first_block->is_free = 1;

  heap->free_list = first_block;
}

unsigned long heap_get_used_memory(heap_t* heap)
{
    unsigned long used = 0;
    char* current = (char*)heap->start;
    
    while (current < (char*)heap->end) {
        block_header_t* block = (block_header_t*)current;
        if (!block->is_free) {
            used += block->size;
        }
        current += sizeof(block_header_t) + block->size;
    }
    
    return used;
}

unsigned long heap_get_free_memory(heap_t* heap)
{
    unsigned long free_mem = 0;
    block_header_t* curr = heap->free_list;
    
    while (curr) {
        free_mem += curr->size;
        curr = curr->next;
    }
    
    return free_mem;
}

unsigned long heap_get_total_memory(heap_t* heap)
{
  return heap->total_size - sizeof(block_header_t);
}

unsigned long heap_malloc(heap_t* heap, unsigned long size)
{
  if (size == 0 || !heap->start)
  {
    return 0;
  }

  unsigned long aligned_size = ALIGN(size);
  block_header_t *block = find_free_block(heap, aligned_size);

  if (!block)
  {
    coalesce_blocks(heap);
    block = find_free_block(heap, aligned_size);

    // TODO INSTEAD JUST INCREASE SIZE OF HEAP
    if (!block) {
      return 0;
    }
  }

  if (block->size >= aligned_size + sizeof(block_header_t) + ALIGNMENT) {
    split_block(heap, block, aligned_size);
  }

  block->is_free = 0;

  return (unsigned long)((char*)block + sizeof(block_header_t));
}

unsigned long heap_calloc(heap_t* heap, unsigned long num, unsigned long size)
{
  unsigned long total_size = num * size;

  if (num != 0 && total_size / num != size) {
    return 0; // Multiplication overflow
  }
  
  unsigned long ptr = heap_malloc(heap, total_size);

  if (ptr)
  {
    memset((void*)ptr, 0, total_size);
  }

  return ptr;
}

unsigned long heap_realloc(heap_t* heap, unsigned long ptr, unsigned long new_size)
{
  if (!ptr) {
    return heap_malloc(heap, new_size);
  }

  if (new_size == 0)
  {
    heap_free(heap, ptr);
    return 0;
  }

  if (!is_valid_pointer(heap, ptr))
  {
    return 0;
  }

  block_header_t *block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
  unsigned long aligned_new_size = ALIGN(new_size);

  if (block->size >= aligned_new_size)
  {
    if (block->size >= aligned_new_size + sizeof(block_header_t) + ALIGNMENT)
    {
      split_block(heap, block, aligned_new_size);
    }
    return ptr;
  }

  block_header_t* next_block = (block_header_t*)((char*)block + sizeof(block_header_t) + block->size);
  if ((char*)next_block < (char*)heap->end && 
      next_block->is_free && 
      block->size + sizeof(block_header_t) + next_block->size >= aligned_new_size) {
      
      // Remove next block from free list
      block_header_t* prev = 0;
      block_header_t* curr = heap->free_list;
      while (curr && curr != next_block) {
          prev = curr;
          curr = curr->next;
      }
      
      if (curr == next_block) {
          if (prev) {
              prev->next = curr->next;
          } else {
              heap->free_list = curr->next;
          }
      }
      
      // Merge blocks
      block->size += sizeof(block_header_t) + next_block->size;
      
      // Split if necessary
      if (block->size >= aligned_new_size + sizeof(block_header_t) + ALIGNMENT) {
          split_block(heap, block, aligned_new_size);
      }
      
      return ptr;
  }
  
  // Need to allocate new block and copy data
  unsigned long new_ptr = heap_malloc(heap, new_size);
  if (new_ptr) {
      unsigned long copy_size = (block->size < new_size) ? block->size : new_size;
      memcpy(new_ptr, ptr, copy_size);
      heap_free(heap, ptr);
  }
  
  return new_ptr;
}

void heap_free(heap_t* heap, unsigned long ptr)
{
    if (!ptr || !is_valid_pointer(heap, ptr)) {
        return;
    }
    
    block_header_t* block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    block->is_free = 1;
    
    // Add to free list (simple insertion at beginning)
    block->next = heap->free_list;
    heap->free_list = block;
    
    // Coalesce adjacent free blocks
    coalesce_blocks(heap);
}