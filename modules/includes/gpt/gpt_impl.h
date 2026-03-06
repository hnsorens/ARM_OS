#ifndef __GPT_IMPL_T__
#define __GPT_IMPL_T__

#include "gpt_driver.h"
#include "gpt.h"

#define __MODULE_NAME__ GPT
#define __MODULE_NAME_STR__ "GPT"
#define __MAIN__

gpt_ops __gpt__;
gpt_driver __gpt_ops__;

unsigned long __load_offset__;

void gpt_init(gpt_ops* gpt);
void gpt_fetch(core_ops* ops);
void gpt_start(core_ops* ops);

__attribute__((section(".text._entry")))
gpt_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	gpt_init(&__gpt__);
	__gpt_ops__.gpt = &__gpt__;


	__gpt_ops__.start = gpt_start;
	__gpt_ops__.fetch = gpt_fetch;

	return &__gpt_ops__;
}

#endif