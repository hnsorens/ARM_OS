#ifndef __STR_DRIVER_H__
#define __STR_DRIVER_H__

#include "str.h"
#include "../module_types.h"
#include "str_types.h"

typedef struct str_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	str_ops* str;
} str_driver;

#endif