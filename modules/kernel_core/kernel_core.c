#include "../module.h"
#include "../module_vtables.h"
#include "../module_debug.h"
#include <stdint.h>

vtable(kernel_vtable_t);
kernel_start(init, kernel_entry);

kernel_entry_t kentry;

void kernel_entry(kernel_entry_t* entry)
{

  kentry = *entry;

  kernel_vtable_t* vtable = kentry.module_table.modules[0].vtable;

  // SKIP KERNEL CORE
  for (int i = 1; i < kentry.module_table.size; i++)
  {
    ((vtable_init_t*)(kentry.module_table.modules[i].vtable))->init(vtable, (virt_addr_t)kentry.module_table.modules[i].base);
  }

  while(1)
  {
	uint64_t daif, pmr, icc_igrpen1, cntv_ctl, cntv_tval, cntpct;

    asm volatile(
        "mrs %0, daif\n"        // DAIF interrupt mask
        "mrs %1, ICC_PMR_EL1\n" // Interrupt priority mask
        "mrs %2, ICC_IGRPEN1_EL1\n" // Group 1 enable
        "mrs %3, CNTV_CTL_EL0\n" // Timer control
        "mrs %4, CNTV_TVAL_EL0\n" // Timer value
        "mrs %5, CNTPCT_EL0\n"    // Timer counter
        : "=r"(daif), "=r"(pmr), "=r"(icc_igrpen1),
          "=r"(cntv_ctl), "=r"(cntv_tval), "=r"(cntpct)
        :
        : "memory"
    );

    LOG(x5, daif);
    LOG(x6, pmr);
    LOG(x7, icc_igrpen1);
    LOG(x8, cntv_ctl);
    LOG(x9, cntv_tval);
    LOG(x10, cntpct);

	LOG(x11, *((uint64_t*)0x080A0100))


//asm volatile("wfi");

  }
}

uintptr_t find_module_vtable_by_type(module_type_t type)
{
  for (int i = 0; i < kentry.module_table.size; i++)
  {
    if (kentry.module_table.modules[i].type == type)
    {
      return (unsigned long)kentry.module_table.modules[i].vtable;
    }
  }
  return 0;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

uintptr_t find_module_vtable_by_name(char* name)
{
  for (int i = 0; i < kentry.module_table.size; i++)
  {
    if (strcmp(kentry.module_table.modules[i].moduleName, name) == 0)
    {
      return (unsigned long)kentry.module_table.modules[i].vtable;
    }
  }
  return 0;
}

unsigned long total_system_memory()
{
  return kentry.total_memory;
}

memory_region_t* memory_regions()
{
  return kentry.memory_map;
}

unsigned long memory_regions_count()
{
  return kentry.memory_map_entry_count;
}

void init(kernel_vtable_t *table)
{
  table->find_module_vtable_by_type = find_module_vtable_by_type;
  table->find_module_vtable_by_name = find_module_vtable_by_name;
  table->total_system_memory = total_system_memory;
  table->memory_regions = memory_regions;
  table->memory_regions_count = memory_regions_count;
}
