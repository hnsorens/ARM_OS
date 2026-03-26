#ifndef __IDE_INC_H__
#define __IDE_INC_H__


#include "ide_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define ide_read_fn CONCAT(IMPL_NAME, _read_fn_func)
#define ide_write_fn CONCAT(IMPL_NAME, _write_fn_func)

__attribute__((used)) void* ide_read_fn( unsigned int, unsigned int );
__attribute__((used)) void ide_write_fn( unsigned int, unsigned int, void* );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif