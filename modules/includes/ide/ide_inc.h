#ifndef __IDE_INC_H__
#define __IDE_INC_H__


#include "ide_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL void* (*ide_read_fn)( unsigned int, unsigned int ) END 
GLOBAL void (*ide_write_fn)( unsigned int, unsigned int, void* ) END 

#ifdef __MAIN__

static void ide_fetch(core_ops *ops) {
	ide_driver *driver = (ide_driver*)ops->find_module_by_type(MODULE_IDE);
ide_read_fn = driver->ide->read_fn;
ide_write_fn = driver->ide->write_fn;
}

#endif
#endif