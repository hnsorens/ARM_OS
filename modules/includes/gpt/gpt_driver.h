#ifndef __GPT_DRIVER_H__
#define __GPT_DRIVER_H__

#include "gpt.h"
#include "../module_types.h"
#include "gpt_types.h"

typedef struct gpt_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	gpt_ops* gpt;
} gpt_driver;

#endif