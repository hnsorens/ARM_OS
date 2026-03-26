#ifndef GPT_VTABLE_H
#define GPT_VTABLE_H


#include "gpt_types.h"

typedef struct gpt_ops
{

  gpt_partition_t* (*create)(blk_device_t dev);

} gpt_ops;

#endif
