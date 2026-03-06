#ifndef __PMM_IMPL_T__
#define __PMM_IMPL_T__

#include "pmm_driver.h"
#include "pmm.h"

#define __MODULE_NAME__ PMM
#define __MODULE_NAME_STR__ "PMM"
#define __MAIN__

pmm_ops __pmm__;
pmm_driver __pmm_ops__;

unsigned long __load_offset__;

void pmm_init(pmm_ops* pmm);
void pmm_fetch(core_ops* ops);
void pmm_start(core_ops* ops);

__attribute__((section(".text._entry")))
pmm_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	pmm_init(&__pmm__);
	__pmm_ops__.pmm = &__pmm__;


	__pmm_ops__.start = pmm_start;
	__pmm_ops__.fetch = pmm_fetch;

	return &__pmm_ops__;
}

#endif