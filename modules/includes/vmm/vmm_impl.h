#ifndef __VMM_IMPL_T__
#define __VMM_IMPL_T__

#include "vmm_driver.h"
#include "vmm.h"

#define __MODULE_NAME__ VMM
#define __MODULE_NAME_STR__ "VMM"
#define __MAIN__

vmm_ops __vmm__;
vmm_driver __vmm_ops__;

unsigned long __load_offset__;

void vmm_init(vmm_ops* vmm);
void vmm_fetch(core_ops* ops);
void vmm_start(core_ops* ops);

__attribute__((section(".text._entry")))
vmm_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	vmm_init(&__vmm__);
	__vmm_ops__.vmm = &__vmm__;


	__vmm_ops__.start = vmm_start;
	__vmm_ops__.fetch = vmm_fetch;

	return &__vmm_ops__;
}

#endif