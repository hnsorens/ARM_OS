#ifndef GICV3_H
#define GICV3_H

#include "gic/gic_types.h"

typedef void (*irq_handler_t)(int irq, void *data);

int gicv3_init(base_t dist_base, base_t redist_base);

#endif