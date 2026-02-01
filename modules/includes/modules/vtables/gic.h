#ifndef GIC_VTABLE_H
#define GIC_VTABLE_H

#include "../../module_vtable.h"

/**
 * @brief Generic Interrupt Controller (GIC) VTable
 * 
 * Manages hardware interrupts, including enabling/disabling,
 * priority configuration, and interrupt routing to CPUs.
 */
typedef struct gic_vtable_t
{
  vtable_def
  
} gic_vtable_t;

#endif