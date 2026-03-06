#include "vmm/vmm_impl.h"

#include "mmu/mmu_inc.h"
#include "pmm/pmm_inc.h"
#include "serial_debug/serial_debug_inc.h"



override void vmm_fetch(core_ops* ops)
{
  serial_debug_fetch(ops);
  pmm_fetch(ops);
  mmu_fetch(ops);
}

override void vmm_start(core_ops* kvtable)
{

}

override void vmm_init(vmm_ops *ops)
{
  ops->alloc_kernel = ;
  ops->free_kernel = ;
  ops->v2p_kernel = ;
}
