#ifndef MEMORY_H
#define MEMORY_H

#include "../boot/bootinfo.h"

void* kernel_alloc(BootInfoStruct *bootInfo, unsigned long pageCount);

#endif
