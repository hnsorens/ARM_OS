/*
 * GICv3 Interrupt Controller Driver
 * ===================================
 * Implements the generic interrupt_manager_interface_t.
 * Supports multi‑core (GICv3 redistributors), SPI routing, SGI/PPI configuration.
 */

#include "gic_v3.h"
#include "api/gic_v3.h"
#include <errno.h>
#include <modules.h>
#include <api/serial_debug.h>
#include <constants.h>

EXTERN_IMPORT_INTERFACE(serial, serial);

/* ========================================================================== */
/*                          Global State                                      */
/* ========================================================================== */
static uintptr_t       g_gicd = 0;                    /* Distributor physical base */
static uintptr_t       g_gicr = 0;                    /* Redistributor physical base (first frame) */
static bool            g_initialized = false;         /* True after init_global() */

static registered_isr_t s_isr_table[MAX_INTERRUPT_VECTORS];
static gic_core_map_t   s_core_topology[MAX_CORES_SUPPORTED];

/* Simple spinlock for ISR table */
typedef volatile u32 spinlock_t;
static spinlock_t s_isr_lock = 0;

static inline void spin_lock(spinlock_t *s) {
    u32 expected;
    __asm__ volatile("1: ldaxr %w0, [%1]\n"
                     "   cbnz %w0, 1b\n"
                     "   stlxr %w0, %w2, [%1]\n"
                     "   cbnz %w0, 1b\n"
                     : "=&r"(expected)
                     : "r"(s), "r"(1U)
                     : "memory", "cc");
}
static inline void spin_unlock(spinlock_t *s) {
    __asm__ volatile("stlr %w0, [%1]\n" :: "r"(0), "r"(s) : "memory");
}

/* ========================================================================== */
/*                    MMIO accessors                                          */
/* ========================================================================== */
static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void mmio_write64(uintptr_t addr, uint64_t val) {
    *(volatile uint64_t *)addr = val;
}
static inline uint64_t mmio_read64(uintptr_t addr) {
    return *(volatile uint64_t *)addr;
}

/* ========================================================================== */
/*              GICD (Distributor) register offsets                           */
/* ========================================================================== */
#define GICD_CTLR          0x0000
#define GICD_ISENABLER(n)  (0x0100 + ((n) * 4))
#define GICD_ICENABLER(n)  (0x0180 + ((n) * 4))
#define GICD_IGROUPR(n)    (0x0080 + ((n) * 4))
#define GICD_IPRIORITYR(n) (0x0400 + ((n) * 4))
#define GICD_ICFGR(n)      (0x0C00 + ((n) * 4))
#define GICD_IGRPMODR(n)   (0x0D00 + ((n) * 4))
#define GICD_IROUTER(n)    (0x6000ULL + ((uint64_t)(n) * 8))

#define GICD_CTLR_NS_ARE_NS   (1U << 4)
#define GICD_CTLR_NS_ENA_GRP1NS (1U << 1)

/* ========================================================================== */
/*              GICR (Redistributor) – per‑core frame offsets                 */
/* ========================================================================== */
#define GICR_CTLR           0x0000
#define GICR_WAKER          0x0014
#define GICR_WAKER_ProcessorSleep  (1U << 1)
#define GICR_WAKER_ChildrenAsleep  (1U << 2)

/* SGI/PPI frame (offset 0x10000 inside each redistributor) */
#define GICR_SGI_OFFSET     0x10000
#define GICR_ISENABLER0     (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0     (GICR_SGI_OFFSET + 0x0180)
#define GICR_IGROUPR0       (GICR_SGI_OFFSET + 0x0080)
#define GICR_IPRIORITYR(n)  (GICR_SGI_OFFSET + 0x0400 + ((n) * 4))
#define GICR_ICFGR(n)       (GICR_SGI_OFFSET + 0x0C00 + ((n) * 4))
#define GICR_IGRPMODR0      (GICR_SGI_OFFSET + 0x0D00)

/* ========================================================================== */
/*              CPU Interface system registers                                */
/* ========================================================================== */
#define ICC_SRE_SRE  (1U << 0)
#define ICC_SRE_DFB  (1U << 1)
#define ICC_SRE_DIB  (1U << 2)
#define ICC_IGRPEN1_ENABLE (1U << 0)

/* ========================================================================== */
/*         Helper: translate MPIDR to linear core index                       */
/* ========================================================================== */
static uint32_t mpidr_to_core_id(uint64_t mpidr) {
    uint32_t aff0 = (mpidr >> 0) & 0xFF;
    uint32_t aff1 = (mpidr >> 8) & 0xFF;
    /* single cluster => linear index */
    return aff0 + (aff1 * 1);
}

/* ========================================================================== */
/*           Get the current core's redistributor base                        */
/* ========================================================================== */
static inline uintptr_t get_current_gicr_base(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    uint32_t cid = mpidr_to_core_id(mpidr);
    return g_gicr + (cid * 0x20000ULL);
}

/* ========================================================================== */
/*           Low‑level GICR wake‑up                                           */
/* ========================================================================== */
static int gicv3_init_redistributor(void) {
    uintptr_t base = get_current_gicr_base();
    uint32_t waker = mmio_read32(base + GICR_WAKER);
    waker &= ~GICR_WAKER_ProcessorSleep;
    mmio_write32(base + GICR_WAKER, waker);
    __asm__ volatile("dsb sy");

    for (int i = 0; i < 1000; i++) {
        waker = mmio_read32(base + GICR_WAKER);
        if ((waker & GICR_WAKER_ChildrenAsleep) == 0)
            return 0;
    }
    return EIO; // timeout
}

/* ========================================================================== */
/*           CPU interface initialisation (NS)                                */
/* ========================================================================== */
static int gicv3_init_cpu_interface_ns(void) {
    uint64_t sre;
    __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(sre));
    sre |= ICC_SRE_SRE | ICC_SRE_DFB | ICC_SRE_DIB;
    __asm__ volatile("msr ICC_SRE_EL1, %0" : : "r"(sre));
    __asm__ volatile("isb");

    __asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(0xFFULL));
    __asm__ volatile("msr ICC_BPR1_EL1, %0" : : "r"(0ULL));
    __asm__ volatile("msr ICC_CTLR_EL1, %0" : : "r"(0ULL));
    __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(ICC_IGRPEN1_ENABLE));
    __asm__ volatile("dsb sy\n isb");
    return 0;
}

/* ========================================================================== */
/*           System register wrappers                                          */
/* ========================================================================== */
static inline void icc_write_pmr_el1(uint64_t val) {
    __asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(val));
    __asm__ volatile("isb");
}
static inline uint64_t icc_read_iar1_el1(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1\n dsb sy" : "=r"(v));
    return v;
}
static inline void icc_write_eoir1_el1(uint64_t val) {
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" : : "r"(val));
    __asm__ volatile("isb");
}

/* ========================================================================== */
/*                    1. System‑wide initialisation                           */
/* ========================================================================== */
int init_global(uintptr_t d_base, uintptr_t r_base) {
    if (!d_base || !r_base)
        return EINVAL;
    if (g_initialized)
        return EBUSY;  // cannot re‑init

    g_gicd = d_base;
    g_gicr = r_base;

    /* Enable Group 1 Non‑Secure distribution */
    uint32_t ctlr = mmio_read32(g_gicd + GICD_CTLR);
    ctlr |= GICD_CTLR_NS_ARE_NS | GICD_CTLR_NS_ENA_GRP1NS;
    mmio_write32(g_gicd + GICD_CTLR, ctlr);
    __asm__ volatile("dsb sy");

    g_initialized = true;
    return 0;
}

/* ========================================================================== */
/*                    2. Per‑core initialisation                              */
/* ========================================================================== */
int init_core(void) {
    if (!g_initialized)
        return EIO;

    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    uint32_t core_id = mpidr_to_core_id(mpidr);
    if (core_id >= MAX_CORES_SUPPORTED)
        return EINVAL;

    /* Mark core in topology map (idempotent) */
    s_core_topology[core_id].mpidr     = mpidr;
    s_core_topology[core_id].allocated = true;

    int ret = gicv3_init_redistributor();
    if (ret) return ret;
    ret = gicv3_init_cpu_interface_ns();
    if (ret) return ret;

    /* Optionally install the vector table (should be done once globally) */
    /* The caller should call arm64_init_vectors() once before enabling IRQs */
    return 0;
}

/* ========================================================================== */
/*             3. Priority mask (per‑core)                                    */
/* ========================================================================== */
int set_core_priority_mask(irq_prio_t mask) {
    icc_write_pmr_el1((uint64_t)mask);
    return 0;
}

/* ========================================================================== */
/*           4. Interrupt enable / disable                                    */
/* ========================================================================== */
int enable(irq_vector_t vector) {
    if (vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    if (vector < 32) {
        /* PPI / SGI – use current redistributor */
        uintptr_t base = get_current_gicr_base() + GICR_ISENABLER0;
        mmio_write32(base, (1U << vector));
    } else {
        /* SPI – use distributor */
        uint32_t reg_idx = vector / 32;
        uint32_t bit     = 1U << (vector % 32);
        mmio_write32(g_gicd + GICD_ISENABLER(reg_idx), bit);
    }
    __asm__ volatile("dsb sy");
    return 0;
}

int disable(irq_vector_t vector) {
    if (vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    if (vector < 32) {
        uintptr_t base = get_current_gicr_base() + GICR_ICENABLER0;
        mmio_write32(base, (1U << vector));
    } else {
        uint32_t reg_idx = vector / 32;
        uint32_t bit     = 1U << (vector % 32);
        mmio_write32(g_gicd + GICD_ICENABLER(reg_idx), bit);
    }
    __asm__ volatile("dsb sy");
    return 0;
}

/* ========================================================================== */
/*           5. Interrupt configuration (trigger, priority)                   */
/* ========================================================================== */
int configure(irq_vector_t vector, irq_trigger_t trigger, irq_prio_t priority) {
    if (vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    uintptr_t base;
    uint32_t prio_idx    = vector / 4;
    uint32_t prio_shift  = (vector % 4) * 8;
    uint32_t cfg_idx     = vector / 16;
    uint32_t cfg_shift   = (vector % 16) * 2;
    uint32_t trigger_val = (trigger == IRQ_TRIGGER_EDGE) ? 0x2U : 0x0U;
    uint32_t grp_idx     = vector / 32;
    uint32_t bit_shift   = vector % 32;

    if (vector < 32) {
        base = get_current_gicr_base();
        /* IPRIORITYR */
        uint32_t val = mmio_read32(base + GICR_IPRIORITYR(prio_idx));
        val &= ~(0xFFU << prio_shift);
        val |= ((uint32_t)priority << prio_shift);
        mmio_write32(base + GICR_IPRIORITYR(prio_idx), val);
        /* ICFGR */
        uint32_t icfgr = mmio_read32(base + GICR_ICFGR(cfg_idx));
        icfgr &= ~(0x3U << cfg_shift);
        icfgr |= (trigger_val << cfg_shift);
        mmio_write32(base + GICR_ICFGR(cfg_idx), icfgr);
        /* IGROUPR */
        uint32_t igroupr = mmio_read32(base + GICR_SGI_OFFSET + 0x0080 + (grp_idx * 4));
        igroupr |= (1U << bit_shift);
        mmio_write32(base + GICR_SGI_OFFSET + 0x0080 + (grp_idx * 4), igroupr);
        /* IGRPMODR – clear for Non‑Secure */
        uint32_t igrpmodr = mmio_read32(base + GICR_SGI_OFFSET + 0x0D00 + (grp_idx * 4));
        igrpmodr &= ~(1U << bit_shift);
        mmio_write32(base + GICR_SGI_OFFSET + 0x0D00 + (grp_idx * 4), igrpmodr);
    } else {
        base = g_gicd;
        /* IPRIORITYR */
        uint32_t val = mmio_read32(base + GICD_IPRIORITYR(prio_idx));
        val &= ~(0xFFU << prio_shift);
        val |= ((uint32_t)priority << prio_shift);
        mmio_write32(base + GICD_IPRIORITYR(prio_idx), val);
        /* ICFGR */
        uint32_t icfgr = mmio_read32(base + GICD_ICFGR(cfg_idx));
        icfgr &= ~(0x3U << cfg_shift);
        icfgr |= (trigger_val << cfg_shift);
        mmio_write32(base + GICD_ICFGR(cfg_idx), icfgr);
        /* IGROUPR */
        uint32_t igroupr = mmio_read32(base + GICD_IGROUPR(grp_idx));
        igroupr |= (1U << bit_shift);
        mmio_write32(base + GICD_IGROUPR(grp_idx), igroupr);
        /* IGRPMODR – clear for Non‑Secure */
        uint32_t igrpmodr = mmio_read32(base + GICD_IGRPMODR(grp_idx));
        igrpmodr &= ~(1U << bit_shift);
        mmio_write32(base + GICD_IGRPMODR(grp_idx), igrpmodr);
    }
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

/* ========================================================================== */
/*           6. Group assignment                                              */
/* ========================================================================== */
int set_group(irq_vector_t vector, irq_group_t group) {
    if (vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    uint32_t reg_offset = (vector / 32) * 4;
    uint32_t bit_shift  = vector % 32;
    uintptr_t base;
    if (vector < 32)
        base = get_current_gicr_base() + GICR_SGI_OFFSET;
    else
        base = g_gicd;

    uint32_t val = mmio_read32(base + 0x0080 + reg_offset);
    if (group == IRQ_GROUP_NON_SECURE)
        val |= (1U << bit_shift);
    else
        val &= ~(1U << bit_shift);
    mmio_write32(base + 0x0080 + reg_offset, val);
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

/* ========================================================================== */
/*           7. SPI routing to a core                                         */
/* ========================================================================== */
int route_to_core(irq_vector_t vector, uint64_t mpidr) {
    if (vector < 32 || vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    /* Check that the target core has been initialized */
    int found = 0;
    for (int i = 0; i < MAX_CORES_SUPPORTED; i++) {
        if (s_core_topology[i].allocated && s_core_topology[i].mpidr == mpidr) {
            found = 1;
            break;
        }
    }
    if (!found)
        return EINVAL;

    /* Clear IRM bit (bit 31) to force unicast delivery */
    uint64_t routing_val = mpidr & ~(1ULL << 31);
    mmio_write64(g_gicd + GICD_IROUTER(vector), routing_val);
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

/* ========================================================================== */
/*           8. Acknowledge & End Of Interrupt                               */
/* ========================================================================== */
irq_vector_t acknowledge(void) {
    return (irq_vector_t)(icc_read_iar1_el1() & 0xFFFFFFFF);
}

int eoi(irq_vector_t vector) {
    icc_write_eoir1_el1((uint64_t)vector);
    return 0;
}

/* ========================================================================== */
/*           9. Handler registration                                          */
/* ========================================================================== */
int register_handler(irq_vector_t vector, isr_handler_t handler, void* arg) {
    if (vector >= MAX_INTERRUPT_VECTORS || handler == NULL)
        return EINVAL;

    spin_lock(&s_isr_lock);
    if (s_isr_table[vector].handler != NULL) {
        spin_unlock(&s_isr_lock);
        return EBUSY;
    }
    s_isr_table[vector].handler = handler;
    s_isr_table[vector].arg     = arg;
    spin_unlock(&s_isr_lock);

    /* Enable the interrupt line */
    int ret = enable(vector);
    if (ret) {
        /* Rollback */
        spin_lock(&s_isr_lock);
        s_isr_table[vector].handler = NULL;
        s_isr_table[vector].arg     = NULL;
        spin_unlock(&s_isr_lock);
    }
    return ret;
}

int unregister_handler(irq_vector_t vector) {
    if (vector >= MAX_INTERRUPT_VECTORS)
        return EINVAL;

    int ret = disable(vector);
    if (ret)
        return ret;

    spin_lock(&s_isr_lock);
    s_isr_table[vector].handler = NULL;
    s_isr_table[vector].arg     = NULL;
    spin_unlock(&s_isr_lock);
    return 0;
}

/* ========================================================================== */
/*          10. C‑level interrupt dispatcher                                  */
/* ========================================================================== */
void c_interrupt_handler(void) {
    uint64_t iar = icc_read_iar1_el1();
    irq_vector_t id = (irq_vector_t)(iar & 0xFFFFFFFF);

    if (id == 1023)
        return;

    registered_isr_t local = { .handler = NULL, .arg = NULL };

    if (id < MAX_INTERRUPT_VECTORS) {
        spin_lock(&s_isr_lock);
        local.handler = s_isr_table[id].handler;
        local.arg     = s_isr_table[id].arg;
        spin_unlock(&s_isr_lock);

        if (local.handler)
            local.handler(local.arg);
        else
            serial.printf("Unhandled Interrupt ID: %u\n", id);
    } else {
        serial.printf("Interrupt ID out of bounds: %u\n", id);
    }

    icc_write_eoir1_el1(iar);
}

/* ========================================================================== */
/*          11. Helper to read current MPIDR (useful for OS)                  */
/* ========================================================================== */
uint64_t get_current_mpidr(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr;
}
