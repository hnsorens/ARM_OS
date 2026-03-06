#ifndef __IDE_IMPL_T__
#define __IDE_IMPL_T__

#include "ide_driver.h"
#include "ide.h"

#define __MODULE_NAME__ IDE
#define __MODULE_NAME_STR__ "IDE"
#define __MAIN__

ide_ops __ide__;
ide_driver __ide_ops__;

unsigned long __load_offset__;

void ide_init(ide_ops* ide);
void ide_fetch(core_ops* ops);
void ide_start(core_ops* ops);

__attribute__((section(".text._entry")))
ide_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	ide_init(&__ide__);
	__ide_ops__.ide = &__ide__;


	__ide_ops__.start = ide_start;
	__ide_ops__.fetch = ide_fetch;

	return &__ide_ops__;
}

#endif