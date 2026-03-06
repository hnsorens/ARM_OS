#ifndef __GIC_IMPL_T__
#define __GIC_IMPL_T__

#include "gic_driver.h"
#include "gic.h"

#define __MODULE_NAME__ GIC
#define __MODULE_NAME_STR__ "GIC"
#define __MAIN__

gic_ops __gic__;
gic_driver __gic_ops__;

unsigned long __load_offset__;

void gic_init(gic_ops* gic);
void gic_fetch(core_ops* ops);
void gic_start(core_ops* ops);

__attribute__((section(".text._entry")))
gic_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	gic_init(&__gic__);
	__gic_ops__.gic = &__gic__;


	__gic_ops__.start = gic_start;
	__gic_ops__.fetch = gic_fetch;

	return &__gic_ops__;
}

#endif