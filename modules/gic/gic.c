
#include "../module_debug.h"
#include "../module_vtables.h"

vtable(gic_vtable_t);
start(init, gic_init);

#include <stdint.h>

#define GICD_BASE 0x08000000
#define GICC_BASE 0x08010000

#define TIMER_IRQ 30

typedef void (*interrupt_t)();
typedef uint8_t interrupt_vector_t;

interrupt_t idt_sp0[256];
interrupt_t idt_spx[256];

static inline void write32(volatile void *addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t read32(volatile void *addr) {
    return *(volatile uint32_t *)addr;
}

void timer_init(uint64_t interval_ms) {
    uint64_t freq;
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
    
    uint64_t ticks = (freq * interval_ms) / 1000;
    asm volatile("msr cntp_ctl_el0, %0" : : "r"((long)0));
    asm volatile("msr cntp_tval_el0, %0" : : "r"(ticks));
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1));
}

void gic_handle_irq(void)
{
  uint32_t iar = read32((void *)(GICC_BASE + 0x0C));
  uint32_t irq = iar & 0x3FF;

  LOG(x10, 0x50);
  BREAK

  if (irq >= 1023) {
      return;
  }
  
  if (irq == TIMER_IRQ) {
      static int count = 0;
      count++;
      

      uint64_t freq;
      __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
      asm volatile("msr cntp_tval_el0, %0" : : "r"(freq / 1000));
  }
  
  // End of Interrupt
  write32((void *)(GICC_BASE + 0x10), iar);
}

void enable_interrupts(void) {
    __asm__ volatile("msr daifclr, #2");
}

uint64_t get_timer_count(void) {
    uint64_t cnt;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(cnt));
    return cnt;
}



typedef enum interrupt_type_t
{
  INTERRUPT_IRQ,
  INTERRUPT_FIQ,
} interrupt_type_t;

static int configure_interrupt(interrupt_vector_t vector, interrupt_type_t type, int is_secure)
{
    uint32_t *gicd_ctlr = (uint32_t*)(GICD_BASE + 0x0000);
    uint32_t original_gicd_ctlr = *gicd_ctlr;
    uint32_t *gicc_ctlr = (uint32_t*)(GICC_BASE + 0x0000);
    uint32_t original_gicc_ctlr = *gicc_ctlr;
    
    *gicd_ctlr = 0;
    *gicc_ctlr = 0;
    
    uint32_t group_reg = GICD_BASE + 0x80 + ((vector / 32) * 4);
    uint32_t group_val = read32((void*)group_reg);

    if (is_secure)
      group_val &= ~(1 << (vector % 32));
    else
      group_val |= (1 << (vector % 32)); 

    write32((void*)group_reg, group_val);

    uint32_t enable_reg = GICD_BASE + 0x100 + ((vector / 32) * 4);
    write32((void*)enable_reg, 1 << (vector % 32));

    uint32_t prio_reg = GICD_BASE + 0x400 + (vector / 4) * 4;
    uint32_t prio_val = read32((void*)prio_reg);
    uint32_t shift = (vector % 4) * 8;
    prio_val = (prio_val & ~(0xFF << shift)) | (0x80 << shift);
    write32((void*)prio_reg, prio_val);
    
    *gicd_ctlr = original_gicd_ctlr;
    *gicc_ctlr = original_gicc_ctlr;
    
    return 0;
}

int enable_sp0(interrupt_t interrupt, interrupt_type_t type, interrupt_vector_t vector)
{
  idt_sp0[vector] = interrupt;

  configure_interrupt(vector, type, 1);
}

int enable_spx(interrupt_t interrupt, interrupt_type_t type, interrupt_vector_t vector)
{
  idt_spx[vector] = interrupt;

  configure_interrupt(vector, type, 0);
}

void gic_default_handle(void)
{
  uint32_t iar = read32((void *)(GICC_BASE + 0x0C));
  uint32_t irq = iar & 0x3FF;
  write32((void *)(GICC_BASE + 0x10), iar);
}

void gic_init(kernel_vtable_t* kvtable, virt_addr_t load)
{
    // 1. Set vector table
    extern void vectors(void);
    uintptr_t vaddr = (uintptr_t)vectors;
    asm volatile("msr vbar_el1, %0" : : "r"(vaddr + load));
    asm volatile("isb");

    // 2. Disable distributor and CPU interface
    write32((void*)GICD_BASE, 0);
    write32((void*)GICC_BASE, 0);

    write32((void*)GICD_BASE, 1);

    // 3. CRITICAL: Set timer interrupt to Group 0 (IRQ) not Group 1 (FIQ)
    
    // 4. Enable distributor
    
    // 7. Enable CPU interface with ALL priorities allowed
    write32((void*)(GICC_BASE + 0x4), 0xFF);  // Allow all priorities 0x00-0xFF
    write32((void*)GICC_BASE, 1);             // Enable CPU interface
    
    timer_init(10);
    
    enable_spx(gic_handle_irq, INTERRUPT_IRQ, TIMER_IRQ);

    // 8. Start timer

    // 9. Enable interrupts
    enable_interrupts();
}

void init(gic_vtable_t* vtable)
{
  // Set all tables to default
  for (int i = 0; i < 256; i++)
  {
    idt_sp0[i] = gic_default_handle;
    idt_spx[i] = gic_default_handle;
  }
}
