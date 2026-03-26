#ifndef __GPT_INC_H__
#define __GPT_INC_H__


#include "gpt_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define gpt_create CONCAT(IMPL_NAME, _create_func)

 
__attribute__((used)) gpt_partition_t* gpt_create( blk_device_t dev );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif