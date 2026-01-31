
#include "module_types.h"
#include <stdint.h>

#ifndef MODULE_VTABLES_H
#define MODULE_VTABLES_H

#define vtable_def void (*fetch)(kernel_vtable_t*, virt_addr_t load); void (*init)(kernel_vtable_t*);

/**
 * @brief Physical Memory Manager (PMM) VTable
 * 
 * Manages physical memory allocation and tracking for the kernel.
 * Responsible for allocating/freeing physical pages and tracking
 * available physical memory.
 */
typedef struct pmm_vtable_t
{
  vtable_def

  /**
    * @brief Allocate contiguous virtual memory in kernel space
    * 
    * Allocates a region of physical memory and places it in the kernel memory map.
    * This does not keep track of virtual memory allocations, this is only for placing
    * physical allocations into a spot in virtual memory.
    * 
    * @param vaddr Virual address in page table to map memory region to
    * @param size Size of the region to allocate in bytes
    * @return void* Starting virtual address of allocated region, or NULL on failure
    * 
    */
  void* (*alloc_virt_kernel)(void* vaddr, unsigned long size);

  /**
    * @brief Free previously allocated kernel virtual memory
    * 
    * Releases a region of physical address space that was placed in the
    * kernel's page table.
    * 
    * @param vaddr Starting virtual address of region to free
    * @param size Size of the region in bytes (must match allocation size)
    * 
    */
  void (*free_virt_kernel)(void* vaddr, unsigned long size);

  /**
    * @brief Allocate an order of physical memory
    * 
    * Allocates a single order of physical memory given the order
    * (power of 2 starting at 1 page (4kb))
    * 
    * @param order Memory Chunk Order (power of 2, 0 being (4kb))
    * @return void* Physical address of first allocated chunk, or NULL on failure
    * 
    */
  void* (*alloc_phys)(unsigned long order);

  /**
    * @brief Free previously allocated physical memory order
    * 
    * Frees a physical memory order that was allocated with alloc_phys
    * 
    * @param paddr Starting physical address of order (must be aligned)
    * @param order Order of memory being freed (must be the same as allocation)
    * 
    */
  void (*free_phys)(void* paddr, unsigned long order);

  /**
     * @brief Get total available physical memory
     * 
     * Returns the total amount of physical memory (in bytes) that is
     * currently available for allocation (not currently in use).
     * 
     * @return unsigned long Available physical memory in bytes
     * 
     */
  unsigned long (*memory_available)();
} pmm_vtable_t;

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

/**
 * @brief Kernel Memory Manager (KMM) VTable
 * 
 * Provides dynamic memory allocation for kernel objects and data
 * structures. Manages kernel heap memory with various allocation
 * strategies.
 */
typedef struct kmm_vtable_t
{
  vtable_def
  
  /**
    * @brief Allocate memory from kernel heap
    * 
    * Allocates a contiguous block of memory from the kernel's heap.
    * Memory is not initialized (contains garbage data).
    * 
    * @param size Number of bytes to allocate
    * @return void* Pointer to allocated memory, or NULL on failure
    * 
    */
  void* (*kmalloc)(unsigned long size);

  /**
    * @brief Allocate aligned memory from kernel heap
    * 
    * Allocates memory with specific alignment requirement.
    * Useful for DMA buffers or hardware that requires specific alignment.
    * 
    * @param size Number of bytes to allocate
    * @param align Alignment requirement (must be power of 2)
    * @return void* Pointer to allocated aligned memory, or NULL on failure
    * 
    */
  void* (*kmalloc_aligned)(unsigned long size, unsigned long align);

  /**
    * @brief Allocate and zero-initialize memory
    * 
    * Allocates memory and sets all bytes to zero.
    * Equivalent to kmalloc + memset(0).
    * 
    * @param count Number of elements to allocate
    * @param size Size of each element in bytes
    * @return void* Pointer to zero-initialized memory, or NULL on failure
    * 
    */
  void* (*kcalloc)(unsigned long count, unsigned long size);

  /**
    * @brief Reallocate memory block
    * 
    * Changes the size of a previously allocated memory block.
    * Contents are preserved up to the minimum of old and new size.
    * 
    * @param ptr Pointer to previously allocated memory (or NULL)
    * @param size New size in bytes
    * @return void* Pointer to reallocated memory, or NULL on failure
    * 
    * Implementation Notes:
    * - If ptr is NULL, equivalent to kmalloc(size)
    * - If size is 0 and ptr not NULL, equivalent to kfree(ptr)
    * - May move memory to new location if can't expand in place
    * - Preserve data from old location up to min(old_size, new_size)
    */
  void* (*krealloc)(void* ptr, unsigned long size);

  /**
     * @brief Free previously allocated kernel memory
     * 
     * Returns memory to the kernel heap for reuse.
     * Pointer must have been returned by kmalloc, kcalloc, or krealloc.
     * 
     * @param ptr Pointer to memory to free (can be NULL)
     * 
     */
  void (*kfree)(void* ptr);
} kmm_vtable_t;

/**
 * @brief Generic Interrupt Controller (GIC) VTable
 * 
 * Manages hardware interrupts, including enabling/disabling,
 * priority configuration, and interrupt routing to CPUs.
 */
typedef struct gic_vtable_t
{
  vtable_def
  
} gic_vtable_t;

typedef struct ext2_vtable_t 
{
  vtable_def
  void* (*create_fs)(unsigned int, unsigned int);
} ext2_vtable_t;

typedef struct ide_vtable_t 
{
  vtable_def
  void* (*read_fn)(unsigned int, unsigned int);
  void (*write_fn)(unsigned int, unsigned int, void*);
} ide_vtable_t;

typedef struct bus_controller_vtable_t
{
  vtable_def
  uintptr_t (*find_device)(unsigned char id);
  void (*init_device)(void* device_base);
  int (*setup_queue)(void* base, uint32_t queue_idx, void* queue);
  int (*submit_request)(void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status);
} bus_controller_vtable_t;

typedef struct serial_debug_vtable_t
{
  vtable_def
  int (*serial_printf)(char*, ...);
} serial_debug_vtable_t;

typedef struct libk_vtable_t
{
  vtable_def

} libk_vtable_t;

typedef struct blk_dev_vtable_t
{
  vtable_def
  void* (*create)(void* dev);
  int (*read_sectors)(void *dev, uint64_t sector, void *buffer);
  int (*write_sector)(void *dev, uint64_t sector, const void *buffer);
  int (*flush)(void *dev);
} blk_dev_vtable_t;

typedef struct gpt_vtable_t
{
  vtable_def
  

} gpt_vtable_t;

/**
 * @brief String/Memory Operations VTable
 * 
 * Standard C library-style string and memory manipulation functions
 * optimized for kernel use (no dynamic allocation, safe for all contexts).
 */
typedef struct str_vtable_t
{
  vtable_def

  /**
    * @brief Fill memory with constant byte
    * 
    * Sets the first 'n' bytes of the memory area pointed to by 's'
    * to the specified value 'c'.
    * 
    * @param s Pointer to memory area to fill
    * @param c Value to set (converted to unsigned char)
    * @param n Number of bytes to set
    * @return void* Original pointer 's'
    * 
    */
  void* (*memset)(void* s, int c, unsigned long n);

  /**
    * @brief Copy memory area
    * 
    * Copies 'n' bytes from memory area 'src' to memory area 'dest'.
    * Memory areas must not overlap (use memmove for overlapping).
    * 
    * @param dest Destination memory area
    * @param src Source memory area
    * @param n Number of bytes to copy
    * @return void* Original destination pointer 'dest'
    * 
    */
  void* (*memcpy)(void* dest, const void* src, unsigned long n);

  /**
    * @brief Copy memory area (safe for overlap)
    * 
    * Copies 'n' bytes from memory area 'src' to memory area 'dest'.
    * Handles overlapping memory areas correctly.
    * 
    * @param dest Destination memory area
    * @param src Source memory area
    * @param n Number of bytes to copy
    * @return void* Original destination pointer 'dest'
    * 
    */
  void* (*memmove)(void* dest, const void* src, unsigned long n);

  /**
    * @brief Compare memory areas
    * 
    * Compares the first 'n' bytes of memory areas 's1' and 's2'.
    * 
    * @param s1 First memory area
    * @param s2 Second memory area
    * @param n Number of bytes to compare
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*memcmp)(const void* s1, const void* s2, unsigned long n);

  /**
    * @brief Locate byte in memory
    * 
    * Scans the first 'n' bytes of memory area 's' for the first
    * occurrence of byte 'c' (converted to unsigned char).
    * 
    * @param s Memory area to scan
    * @param c Byte to search for
    * @param n Number of bytes to scan
    * @return void* Pointer to matching byte, or NULL if not found
    * 
    */
  void* (*memchr)(const void* s, int c, unsigned long n);

  /**
    * @brief Calculate string length
    * 
    * Computes the length of the string 's' (number of characters
    * before the terminating null byte).
    * 
    * @param s Null-terminated string
    * @return unsigned long Length of string in bytes
    * 
    */
  unsigned long (*strlen)(const char* s);

  /**
    * @brief Copy string
    * 
    * Copies the string pointed to by 'src' (including null terminator)
    * to the buffer pointed to by 'dest'.
    * 
    * @param dest Destination buffer (must be large enough)
    * @param src Source string
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strcpy)(char* dest, const char* src);

  /**
    * @brief Copy string with length limit
    * 
    * Copies at most 'n' characters from 'src' to 'dest'.
    * If 'src' is shorter than 'n', pads with null bytes.
    * 
    * @param dest Destination buffer
    * @param src Source string
    * @param n Maximum number of characters to copy
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strncpy)(char* dest, const char* src, unsigned long n);

  /**
    * @brief Concatenate strings
    * 
    * Appends a copy of 'src' to the end of 'dest'.
    * The null terminator from 'dest' is overwritten.
    * 
    * @param dest Destination buffer (must have enough space)
    * @param src Source string to append
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strcat)(char* dest, const char* src);

  /**
    * @brief Concatenate strings with length limit
    * 
    * Appends at most 'n' characters from 'src' to 'dest',
    * then adds a null terminator.
    * 
    * @param dest Destination buffer
    * @param src Source string to append
    * @param n Maximum number of characters to append
    * @return char* Original destination pointer 'dest'
    * 
    */
  char* (*strncat)(char* dest, const char* src, unsigned long n);

  /**
    * @brief Compare two strings
    * 
    * Compares strings 's1' and 's2' lexicographically.
    * 
    * @param s1 First string
    * @param s2 Second string
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*strcmp)(const char* s1, const char* s2);

  /**
    * @brief Compare strings with length limit
    * 
    * Compares at most 'n' characters of strings 's1' and 's2'.
    * 
    * @param s1 First string
    * @param s2 Second string
    * @param n Maximum number of characters to compare
    * @return int <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
    * 
    */
  int (*strncmp)(const char* s1, const char* s2, unsigned long n);

  /**
    * @brief Locate first occurrence of character in string
    * 
    * Returns a pointer to the first occurrence of character 'c'
    * in the string 's'. The null terminator is considered part
    * of the string.
    * 
    * @param s String to search
    * @param c Character to locate (converted to char)
    * @return char* Pointer to character, or NULL if not found
    * 
    */
  char* (*strchr)(const char* s, int c);

  /**
    * @brief Locate last occurrence of character in string
    * 
    * Returns a pointer to the last occurrence of character 'c'
    * in the string 's'.
    * 
    * @param s String to search
    * @param c Character to locate
    * @return char* Pointer to last occurrence, or NULL if not found
    * 
    */
  char* (*strrchr)(const char* s, int c);

  /**
    * @brief Locate substring
    * 
    * Finds the first occurrence of the substring 'needle'
    * in the string 'haystack'. The null terminator is not compared.
    * 
    * @param haystack String to search in
    * @param needle Substring to search for
    * @return char* Pointer to beginning of substring, or NULL if not found
    * 
    */
  char* (*strstr)(const char* haystack, const char* needle);

  /**
    * @brief Duplicate string
    * 
    * Returns a pointer to a new string which is a duplicate of 's'.
    * Memory is obtained with kmalloc and must be freed with kfree.
    * 
    * @param s String to duplicate
    * @return char* Pointer to duplicated string, or NULL on failure
    * 
    */
  char* (*strdup)(const char* s);

} str_vtable_t;

#endif
