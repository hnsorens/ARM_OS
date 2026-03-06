#ifndef __KMM_IMPL_T__
#define __KMM_IMPL_T__

#include "kmm_driver.h"
#include "kmm.h"

#define __MODULE_NAME__ KMM
#define __MODULE_NAME_STR__ "KMM"
#define __MAIN__

kmm_ops __kmm__;
kmm_driver __kmm_ops__;

unsigned long __load_offset__;

void kmm_init(kmm_ops* kmm);
void kmm_fetch(core_ops* ops);
void kmm_start(core_ops* ops);

__attribute__((section(".text._entry")))
kmm_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	kmm_init(&__kmm__);
	__kmm_ops__.kmm = &__kmm__;


	__kmm_ops__.start = kmm_start;
	__kmm_ops__.fetch = kmm_fetch;

	return &__kmm_ops__;
}

#endif