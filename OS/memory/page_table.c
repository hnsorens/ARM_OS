#include "page_table.h"
#include <stdint.h>

page_table_indices_t extract_indices(uint64_t virtual_address) {
  page_table_indices_t indices;
  indices.offset = virtual_address & 0xFFF;     /* Page offset (bits 0-11) */
  indices.p3_index = P3_INDEX(virtual_address); /* Page Table index */
  indices.p2_index = P2_INDEX(virtual_address); /* Page Directory index */
  indices.p1_index = P1_INDEX(virtual_address); /* PDPT index */
  indices.p0_index = P0_INDEX(virtual_address); /* PML4 index */
  return indices;
}

size_t get_kernel_page_table_page_count(size_t total_memory) { return 2; }

void *memset(void *dest, int value, size_t count) {
  unsigned char *ptr = (unsigned char *)dest;
  unsigned char byte_value = (unsigned char)value;

  for (size_t i = 0; i < count; i++) {
    ptr[i] = byte_value;
  }

  return dest;
}

// ARM64 MAIR encodings
#define MAIR_DEVICE_nGnRnE 0x00
#define MAIR_NORMAL_NC 0x44
#define MAIR_NORMAL_WB 0xFF

void create_kernel_page_table(phys_addr_t phys_base) {
  // Get pointers to table memory
  uint64_t *l0_table = (uint64_t *)phys_base;
  uint64_t *l1_table = (uint64_t *)(phys_base + 0x1000);

  // Clear tables
  memset(l0_table, 0, 0x2000);

  // Set up L0 -> L1 table
  l0_table[0] = (uint64_t)l1_table | 0x3; // Valid table descriptor

  // Create 1GB block mappings for identity mapping
  for (int i = 0; i < 512; i++) {
    uint64_t phys_addr = i * 0x40000000ULL; // 1GB blocks
    l1_table[i] = phys_addr | (1UL << 10) | // Access Flag
                  (0x3UL << 8) |            // SH=Inner Shareable
                  (0x0UL << 6) |            // AP=RW at all levels
                  (0x1UL << 2) | // AttrIndex=Normal WB (MAIR index 1)
                  (0x1UL << 0);  // Block entry
  }

  // Configure MAIR
  uint64_t mair = (MAIR_NORMAL_WB << 8) | MAIR_DEVICE_nGnRnE;
  __asm__ volatile("msr mair_el1, %0" : : "r"(mair));

  // Configure TCR for 48-bit addressing
  uint64_t tcr = (16UL << 0) |  // T0SZ=16 (48-bit)
                 (16UL << 16) | // T1SZ=16 (48-bit)
                 (1UL << 8) |   // IRGN0=WB
                 (1UL << 10) |  // ORGN0=WB
                 (1UL << 12) |  // SH0=Inner
                 (2UL << 14) |  // TG0=4KB
                 (1UL << 23) |  // EPD1=Disable TTBR1
                 (0UL << 24);   // EPD0=Enable TTBR0
  __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr));

  // Set TTBR0 for lower half (identity mapping)
  __asm__ volatile("msr ttbr0_el1, %0" : : "r"(phys_base));

  // Invalidate TLB
  __asm__ volatile("tlbi vmalle1");
  __asm__ volatile("dsb sy");
  __asm__ volatile("isb");

  while (1);
}
