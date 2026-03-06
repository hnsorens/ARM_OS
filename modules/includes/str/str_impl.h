#ifndef __STR_IMPL_T__
#define __STR_IMPL_T__

#include "str_driver.h"
#include "str.h"

#define __MODULE_NAME__ STR
#define __MODULE_NAME_STR__ "STR"
#define __MAIN__

str_ops __str__;
str_driver __str_ops__;

unsigned long __load_offset__;

void str_init(str_ops* str);
void str_fetch(core_ops* ops);
void str_start(core_ops* ops);

__attribute__((section(".text._entry")))
str_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	str_init(&__str__);
	__str_ops__.str = &__str__;


	__str_ops__.start = str_start;
	__str_ops__.fetch = str_fetch;

	return &__str_ops__;
}

#endif