// #include "gicv3_old.h"
// #include "module_debug.h"

// // System register access macros
// #define SYS_READ(reg, val)  __asm__ volatile("mrs %0, " #reg : "=r"(val))
// #define SYS_WRITE(reg, val) __asm__ volatile("msr " #reg ", %0" : : "r"(val) : "memory")

// // ARM Generic Timer registers
// #define CNTFRQ_EL0      CNTPCT_FRQ_EL0   // Alias for frequency register
// #define CNTV_TVAL_EL0   CNTV_TVAL_EL0
// #define CNTV_CTL_EL0    CNTV_CTL_EL0

// // GICv3 register offsets
// #define GICD_CTLR           0x0000
// #define GICD_ISENABLERn     0x0100
// #define GICD_ICENABLERn     0x0180
// #define GICD_IPRIORITYRn    0x0400
// #define GICD_ICFGRn         0x0C00
// #define GICD_IROUTERn       0x6100  // + (irq-32)*8 for SPIs

// #define GICR_WAKER          0x0014
// #define GICR_SGI_BASE       0x10000 
// #define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080)
// #define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100)
// #define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180)
// #define GICR_IPRIORITYR0    (GICR_SGI_BASE + 0x0400)
// #define GICR_ICFGR0         (GICR_SGI_BASE + 0x0C00)
// #define GICR_ICFGR1         (GICR_SGI_BASE + 0x0C04)

// // Globals
// static uintptr_t g_dist_base;
// static uintptr_t g_redist_base;     // Per-core redistributor base
// static spinlock_t g_lock;
// static irq_handler_t g_handlers[1024] = {0};
// static void *g_handler_data[1024] = {0};

// // Simple spinlock implementation
// void spinlock_init(spinlock_t *lock) {
//     lock->lock = 0;
// }

// void spinlock_acquire(spinlock_t *lock) {
//     uint32_t tmp;
//     __asm__ volatile(
//         "   sevl\n"
//         "1: wfe\n"
//         "   ldaxr   %w[tmp], [%x[lock]]\n"
//         "   cbnz    %w[tmp], 1b\n"
//         "   stxr    %w[tmp], %w[one], [%x[lock]]\n"
//         "   cbnz    %w[tmp], 1b\n"
//         : [tmp] "=&r" (tmp)
//         : [lock] "r" (&lock->lock), [one] "r" (1)
//         : "memory");
// }

// void spinlock_release(spinlock_t *lock) {
//     __asm__ volatile("stlrb wzr, [%0]" : : "r" (&lock->lock) : "memory");
// }

// // MMIO helpers
// static inline uint32_t readl(uintptr_t addr) { return *(volatile uint32_t *)addr; }
// static inline void writel(uint32_t val, uintptr_t addr) { *(volatile uint32_t *)addr = val; }
// static inline uint64_t readq(uintptr_t addr) { return *(volatile uint64_t *)addr; }
// static inline void writeq(uint64_t val, uintptr_t addr) { *(volatile uint64_t *)addr = val; }

// int gicv3_init(uintptr_t dist_base, uintptr_t redist_base) {
//     g_dist_base = dist_base;
//     g_redist_base = redist_base;

//     // Wake up redistributor (Frame 0)
//     writel(readl(g_redist_base + GICR_WAKER) & ~(1U << 1), g_redist_base + GICR_WAKER);
//     while (readl(g_redist_base + GICR_WAKER) & (1U << 2));
//     __asm__ volatile("dsb ish; isb" ::: "memory");

//     // --- FIX 2: Correctly target SGI_BASE for Grouping ---
//     writel(0xFFFFFFFF, g_redist_base + GICR_IGROUPR0);

//     // --- FIX 3: Correctly target SGI_BASE for Priorities ---
//     for (int i = 0; i < 8; i++) {
//         writel(0xA0A0A0A0, g_redist_base + GICR_IPRIORITYR0 + i * 4);
//     }

//     uint32_t ctlr = readl(g_dist_base + GICD_CTLR);
//     ctlr |= (1 << 4);  // ARE_NS
//     ctlr |= (1 << 1);  // EnableGrp1NS
//     writel(ctlr, g_dist_base + GICD_CTLR);

//     SYS_WRITE(ICC_SRE_EL1, 1);   
//     __asm__ volatile("isb");

//     SYS_WRITE(ICC_PMR_EL1, 0xFF);           
//     SYS_WRITE(ICC_BPR1_EL1, 0x3);           
//     SYS_WRITE(ICC_IGRPEN1_EL1, 1);          

//     spinlock_init(&g_lock);
//     return 0;
// }

// int gicv3_request_irq(int irq, irq_type_t type, irq_handler_t handler, void *data) {
//     if (irq < 0 || irq >= 1024 || g_handlers[irq]) return -1;

//     spinlock_acquire(&g_lock);

//     // --- FIX 4: Correct ICFGR selection for PPIs (16-31) ---
//     uintptr_t cfg_base;
//     if (irq < 16) {
//         cfg_base = g_redist_base + GICR_ICFGR0;
//     } else if (irq < 32) {
//         cfg_base = g_redist_base + GICR_ICFGR1;
//     } else {
//         cfg_base = g_dist_base + GICD_ICFGRn + (irq / 16) * 4;
//     }

//     uint32_t cfg = readl(cfg_base);
//     int field = (irq % 16) * 2;
//     cfg &= ~(3U << field);
//     cfg |= (type == IRQ_TYPE_EDGE ? 2U : 1U) << field;
//     writel(cfg, cfg_base);

//     // Priority
//     uintptr_t prio_base = (irq < 32) ? g_redist_base + GICR_IPRIORITYR0 + (irq / 4) * 4
//                                     : g_dist_base + GICD_IPRIORITYRn + (irq / 4) * 4;
//     uint32_t prio = readl(prio_base);
//     prio &= ~(0xFFU << ((irq % 4) * 8));
//     prio |= 0xA0U << ((irq % 4) * 8);
//     writel(prio, prio_base);

//     g_handlers[irq] = handler;
//     g_handler_data[irq] = data;
//     gicv3_enable_irq(irq);

//     spinlock_release(&g_lock);
//     return 0;
// }

// void gicv3_enable_irq(int irq) {
//     if (irq < 32) {
//         // Clear then Set to ensure state
//         writel(1U << irq, g_redist_base + GICR_ICENABLER0);
//         __asm__ volatile("dsb sy");
//         writel(1U << irq, g_redist_base + GICR_ISENABLER0);
//     } else {
//         uintptr_t reg = g_dist_base + GICD_ISENABLERn + (irq / 32) * 4;
//         writel(1U << (irq % 32), reg);
//     }
// }
// void gicv3_free_irq(int irq) {
//     if (irq < 0 || irq >= 1024) return;
//     spinlock_acquire(&g_lock);
//     gicv3_disable_irq(irq);
//     g_handlers[irq] = NULL;
//     g_handler_data[irq] = NULL;
//     spinlock_release(&g_lock);
// }

// void gicv3_disable_irq(int irq) {
//     uintptr_t reg = (irq < 32) ? g_redist_base + GICR_ICENABLER0
//                               : g_dist_base + GICD_ICENABLERn + (irq / 32) * 4;
//     writel(1U << (irq % 32), reg);
// }

// int gicv3_set_affinity(int irq, uint64_t mpidr) {
//     if (irq < 32) return -1;  // Only SPIs
//     writeq(mpidr, g_dist_base + GICD_IROUTERn + (uint64_t)(irq - 32) * 8);
//     return 0;
// }

// void gicv3_global_enable(void) {
// __asm__ volatile("msr daifclr, #0xf" ::: "memory");
// __asm__ volatile("isb");
// }

// void gicv3_global_disable(void) {
//     __asm__ volatile("msr daifset, #2" ::: "memory");  // Set I bit
// __asm__ volatile("isb");
// }

// void gicv3_handle_irq(void) {

//     uint64_t iar;
//     SYS_READ(ICC_IAR1_EL1, iar);
//     uint32_t irq = iar & 0xFFFFFF;

//     if (irq == 1023) return;  // Spurious

//     if (g_handlers[irq]) {
//         g_handlers[irq]((int)irq, g_handler_data[irq]);
//     }

//     SYS_WRITE(ICC_EOIR1_EL1, iar);
//     if (irq >= 32) {
//         SYS_WRITE(ICC_DIR_EL1, iar);   // Deactivate SPI
//     }
// }