#ifndef __IDE_INC_H__
#define __IDE_INC_H__


#include "ide_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define ide_read_fn CONCAT(IDE_NAME, _read_fn_func)
#define ide_write_fn CONCAT(IDE_NAME, _write_fn_func)

extern void* ide_read_fn( unsigned int, unsigned int );
extern void ide_write_fn( unsigned int, unsigned int, void* );
#endif