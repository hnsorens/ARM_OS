#ifndef __MMU_IMPL_T__
#define __MMU_IMPL_T__

#include "mmu_driver.h"
#include "mmu.h"

#define __MODULE_NAME__ MMU
#define __MODULE_NAME_STR__ "MMU"
#define __MAIN__

mmu_ops __mmu__;
mmu_driver __mmu_ops__;

unsigned long __load_offset__;

void mmu_init(mmu_ops* mmu);
void mmu_fetch(core_ops* ops);
void mmu_start(core_ops* ops);

__attribute__((section(".text._entry")))
mmu_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	mmu_init(&__mmu__);
	__mmu_ops__.mmu = &__mmu__;


	__mmu_ops__.start = mmu_start;
	__mmu_ops__.fetch = mmu_fetch;

	return &__mmu_ops__;
}

#endif