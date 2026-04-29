#ifndef __GPT_INC_H__
#define __GPT_INC_H__


#include "gpt_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define gpt_create CONCAT(GPT_NAME, _create_func)

 
extern gpt_partition_t* gpt_create( blk_device_t dev );
#endif