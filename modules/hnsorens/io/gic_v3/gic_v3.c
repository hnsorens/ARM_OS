#include "gic_v3.h"
#include "api/gic_v3.h"
#include "errno.h"
#include "modules.h"
#include <api/serial_debug.h>
#include <constants.h>

EXTERN_IMPORT_INTERFACE(serial, serial);

static registered_isr_t s_isr_table[MAX_INTERRUPT_VECTORS];

/* --------------------------------------------------------------------------
 * Simple spinlock for protecting the shared ISR table.
 * -------------------------------------------------------------------------- */
typedef struct {
	volatile u32 lock;
} spinlock_t;

static inline void spin_lock(spinlock_t *s)
{
	u32 expected;
	u32 val = 1;
	__asm__ volatile("1: ldaxr %w0, [%1]\n"
			 "   cbnz %w0, 1b\n"
			 "   stlxr %w0, %w2, [%1]\n"
			 "   cbnz %w0, 1b\n"
			 : "=&r"(expected)
			 : "r"(&s->lock), "r"(val)
			 : "memory", "cc");
}

static inline void spin_unlock(spinlock_t *s)
{
	__asm__ volatile("stlr %w0, [%1]\n" ::"r"(0), "r"(&s->lock) : "memory");
}

static spinlock_t s_isr_lock;
static gic_core_map_t s_core_topology[MAX_CORES_SUPPORTED];

/* --------------------------------------------------------------------------
 * Global State Addresses
 * -------------------------------------------------------------------------- */
static uintptr_t g_gicd = 0;
static uintptr_t g_gicr = 0;

/* --------------------------------------------------------------------------
 * 1. MMIO LOW-LEVEL ACCESSORS
 * -------------------------------------------------------------------------- */
static inline void io_write32(uintptr_t addr, uint32_t val)
{
	*(volatile uint32_t *)addr = val;
}

static inline uint32_t io_read32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void io_write64(uintptr_t addr, uint64_t val)
{
	*(volatile uint64_t *)addr = val;
}

static inline uint64_t io_read64(uintptr_t addr)
{
	return *(volatile uint64_t *)addr;
}

/* ========================================================================== */
/*                          GICD (Distributor) Macros                        */
/* ========================================================================== */
#define GICD_CTLR 0x0000
#define GICD_ISENABLER(n) (0x0100 + ((n) * 4))
#define GICD_ICENABLER(n) (0x0180 + ((n) * 4))

/* Dynamic Distributor Attribute Arrays (Added for SPIs >= 32) */
#define GICD_IGROUPR(n) (0x0080 + ((n) * 4))
#define GICD_IPRIORITYR(n) (0x0400 + ((n) * 4))
#define GICD_ICFGR(n) (0x0C00 + ((n) * 4))
#define GICD_IGRPMODR(n) (0x0D00 + ((n) * 4))

// Non-Secure View Bit fields
#define GICD_CTLR_NS_ARE_NS (1U << 4) // Affinity Routing Enable
#define GICD_CTLR_NS_ENA_GRP1NS \
	(1U << 1) // Enable Non-Secure Group 1 distribution

/* ========================================================================== */
/*                        GICR (Redistributor) Macros                        */
/* ========================================================================== */
#define GICR_CTLR 0x0000
#define GICR_WAKER 0x0014

#define GICR_WAKER_ProcessorSleep (1U << 1)
#define GICR_WAKER_ChildrenAsleep (1U << 2)

/* --- Redistributor SGI/PPI Frame Offset --- */
#define GICR_SGI_OFFSET 0x10000

/* Static Base Macros (Retained from your original codebase) */
#define GICR_IGROUPR0 (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI_OFFSET + 0x0180)
#define GICR_IPRIORITYR(n) (GICR_SGI_OFFSET + 0x0400 + ((n) * 4))
#define GICR_ICFGR1 (GICR_SGI_OFFSET + 0x0C04)
#define GICR_IGRPMODR0 (GICR_SGI_OFFSET + 0x0D00)

/* Dynamic Redistributor SGI Base Arrays (Added for generic SGI/PPI math) */
#define GICR_SGI_IGROUPR(n) (GICR_SGI_OFFSET + 0x0080 + ((n) * 4))
#define GICR_SGI_IPRIORITYR(n) (GICR_SGI_OFFSET + 0x0400 + ((n) * 4))
#define GICR_SGI_ICFGR(n) (GICR_SGI_OFFSET + 0x0C00 + ((n) * 4))
#define GICR_SGI_IGRPMODR(n) (GICR_SGI_OFFSET + 0x0D00 + ((n) * 4))

/* ========================================================================== */
/*                     CPU Interface System Registers                        */
/* ========================================================================== */
#define ICC_SRE_SRE (1U << 0)
#define ICC_SRE_DFB (1U << 1)
#define ICC_SRE_DIB (1U << 2)

#define ICC_IGRPEN1_ENABLE (1U << 0) // Enable Non-Secure Group 1 signaling

/* --------------------------------------------------------------------------
 * 3. CORE-AWARE REDISTRIBUTOR LOOKUP & WRAPPERS
 * -------------------------------------------------------------------------- */
static inline uintptr_t get_current_gicr_base(void)
{
	uint64_t mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

	// Extract Affinity levels to determine linear core ID
	uint32_t aff0 = (mpidr >> 0) & 0xFF;
	uint32_t aff1 = (mpidr >> 8) & 0xFF;
	uint32_t core_id = aff0 + (aff1 * 1);

	// Each core's Redistributor frame is exactly 128 KiB (0x20000 bytes) wide
	return g_gicr + (core_id * 0x20000ULL);
}

static void gicd_write_ctlr(uint32_t val)
{
	io_write32(g_gicd + GICD_CTLR, val);
}
static uint32_t gicd_read_ctlr(void)
{
	return io_read32(g_gicd + GICD_CTLR);
}
static uint32_t gicr_read_waker(void)
{
	return io_read32(get_current_gicr_base() + GICR_WAKER);
}
static void gicr_write_waker(uint32_t val)
{
	io_write32(get_current_gicr_base() + GICR_WAKER, val);
}

static inline void icc_write_pmr_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}
static inline void icc_write_ctlr_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_CTLR_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}
static inline void icc_write_igrpen1_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}
static inline uint64_t icc_read_iar1_el1(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, ICC_IAR1_EL1\n dsb sy" : "=r"(v));
	return v;
}
static inline void icc_write_eoir1_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_EOIR1_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}
static inline void icc_write_sre_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_SRE_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}
static inline uint64_t icc_read_sre_el1(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(v));
	return v;
}
static inline void icc_write_bpr1_el1(uint64_t val)
{
	__asm__ volatile("msr ICC_BPR1_EL1, %0" : : "r"(val));
	__asm__ volatile("isb");
}

uint64_t get_current_mpidr(void)
{
	uint64_t mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
	return mpidr;
}

/* --------------------------------------------------------------------------
 * 4. SYSTEM INITIALIZATION API
 * -------------------------------------------------------------------------- */
int gicv3_init_base(const gicv3_mmo_t *mmo)
{
	if (!mmo || !mmo->gicd_base || !mmo->gicr_base) {
		return EIO;
	}
	g_gicd = mmo->gicd_base;
	g_gicr = mmo->gicr_base;
	return 0;
}

int gicv3_init_redistributor(void)
{
	uint32_t waker = gicr_read_waker();
	waker &= ~GICR_WAKER_ProcessorSleep;
	gicr_write_waker(waker);
	__asm__ volatile("dsb sy");

	while (1) {
		waker = gicr_read_waker();
		if ((waker & GICR_WAKER_ChildrenAsleep) == 0)
			break;
	}
	return 0;
}

int gicv3_init_cpu_interface_ns(void)
{
	uint64_t sre = icc_read_sre_el1();
	sre |= ICC_SRE_SRE | ICC_SRE_DFB | ICC_SRE_DIB;
	icc_write_sre_el1(sre);

	icc_write_pmr_el1(0xFFULL);
	icc_write_bpr1_el1(0ULL);
	icc_write_ctlr_el1(0ULL);
	icc_write_igrpen1_el1(ICC_IGRPEN1_ENABLE);

	__asm__ volatile("dsb sy\n isb");
	return 0;
}

void arm64_core_timer_start(uint32_t ticks)
{
	__asm__ volatile("msr CNTP_TVAL_EL0, %0" : : "r"((uint64_t)ticks));
	__asm__ volatile("msr CNTP_CTL_EL0, %0" : : "r"(1ULL));
	__asm__ volatile("isb");
}

void arm64_unmask_cpu_exceptions(void)
{
	__asm__ volatile(
		"msr daifclr, #15"); // Clear all status mask flags (D, A, I, F)
	__asm__ volatile("isb");
}

void arm64_init_vectors(void)
{
	extern void exception_vector_table(void);
	__asm__ volatile("msr vbar_el1, %0" : : "r"(exception_vector_table));
	__asm__ volatile("isb");
}

/* --------------------------------------------------------------------------
 * 5. CORE C EXCEPTION DISPATCHER
 * -------------------------------------------------------------------------- */
void c_interrupt_handler(void)
{
	uint64_t iar = icc_read_iar1_el1();
	uint32_t interrupt_id = (uint32_t)(iar & 0xFFFFFFFF);

	if (interrupt_id == 1023)
		return;

	registered_isr_t local = { .handler = NULL, .arg = NULL };

	if (interrupt_id < MAX_INTERRUPT_VECTORS) {
		spin_lock(&s_isr_lock);
		local.handler = s_isr_table[interrupt_id].handler;
		local.arg = s_isr_table[interrupt_id].arg;
		spin_unlock(&s_isr_lock);

		if (local.handler != NULL) {
			local.handler(local.arg);
		} else {
			serial.printf("Unhandled Interrupt ID: %u\n",
				      interrupt_id);
		}
	} else {
		serial.printf("Interrupt ID out of bounds: %u\n", interrupt_id);
	}

	icc_write_eoir1_el1(iar);
}

/* --------------------------------------------------------------------------
 * 6. EXTERNAL MODULE INTERFACE LIFECYCLE HOOKS
 * -------------------------------------------------------------------------- */
int enable(irq_vector_t irq_vector)
{
	if (irq_vector >= 32) {
		uint32_t reg_idx = irq_vector / 32;
		uint32_t bit_shift = irq_vector % 32;
		io_write32(g_gicd + GICD_ISENABLER(reg_idx), (1U << bit_shift));
	} else {
		io_write32(get_current_gicr_base() + GICR_ISENABLER0,
			   (1U << irq_vector));
	}
	__asm__ volatile("dsb sy");
	return 0;
}

int disable(irq_vector_t irq_vector)
{
	if (irq_vector >= 32) {
		uint32_t reg_idx = irq_vector / 32;
		uint32_t bit_shift = irq_vector % 32;
		io_write32(g_gicd + GICD_ICENABLER(reg_idx), (1U << bit_shift));
	} else {
		io_write32(get_current_gicr_base() + GICR_ICENABLER0,
			   (1U << irq_vector));
	}
	__asm__ volatile("dsb sy");
	return 0;
}

irq_vector_t acknowledge(void)
{
	return (irq_vector_t)(icc_read_iar1_el1() & 0xFFFFFFFF);
}

int eoi(irq_vector_t irq_vector)
{
	icc_write_eoir1_el1(irq_vector);
	return 0;
}

int register_handler(irq_vector_t vector, isr_handler_t handler, void *arg)
{
	if (vector >= MAX_INTERRUPT_VECTORS || handler == NULL) {
		return EINVAL;
	}

	spin_lock(&s_isr_lock);

	if (s_isr_table[vector].handler != NULL) {
		spin_unlock(&s_isr_lock);
		return EBUSY;
	}

	s_isr_table[vector].handler = handler;
	s_isr_table[vector].arg = arg;
	spin_unlock(&s_isr_lock);

	int status = enable(vector);
	if (status != 0) {
		spin_lock(&s_isr_lock);
		s_isr_table[vector].handler = NULL;
		s_isr_table[vector].arg = NULL;
		spin_unlock(&s_isr_lock);
		return status;
	}

	return 0;
}

int unregister_handler(irq_vector_t vector)
{
	if (vector >= MAX_INTERRUPT_VECTORS) {
		return EINVAL;
	}

	int status = disable(vector);
	if (status != 0) {
		return status;
	}

	spin_lock(&s_isr_lock);
	s_isr_table[vector].handler = NULL;
	s_isr_table[vector].arg = NULL;
	spin_unlock(&s_isr_lock);

	return 0;
}

int init_global(uintptr_t d_base, uintptr_t r_base)
{
	if (!d_base || !r_base) {
		return EIO;
	}
	g_gicd = d_base;
	g_gicr = r_base;

	// Enable Group 1 Non-Secure distribution
	uint32_t ctlr = gicd_read_ctlr();
	ctlr |= GICD_CTLR_NS_ARE_NS | GICD_CTLR_NS_ENA_GRP1NS;
	gicd_write_ctlr(ctlr);
	__asm__ volatile("dsb sy");
	return 0;
}

int init_core(void)
{
	uint64_t mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

	// Extract logical core index (Aff0 + Aff1*256 -> linear)
	uint32_t aff0 = (mpidr >> 0) & 0xFF;
	uint32_t aff1 = (mpidr >> 8) & 0xFF;
	uint32_t core_id = aff0 + (aff1 * 1); // assuming single cluster

	if (core_id >= MAX_CORES_SUPPORTED) {
		return EINVAL;
	}

	// Save routing affinity (bits[39:32] Aff3, bits[23:16] Aff2, bits[15:8] Aff1, bits[7:0] Aff0)
	s_core_topology[core_id].mpidr = mpidr & 0xFF00000000ULL |
					 mpidr & 0xFFFFFFULL;
	s_core_topology[core_id].allocated = true;

	int status = gicv3_init_redistributor();
	if (status != 0)
		return status;

	status = gicv3_init_cpu_interface_ns();
	if (status != 0)
		return status;

	arm64_init_vectors();
	return 0;
}

int set_core_priority_mask(irq_prio_t mask)
{
	// Write to the Priority Mask Register (PMR) to filter lower priority interrupts
	icc_write_pmr_el1((uint64_t)mask);
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

int configure(irq_vector_t irq, irq_trigger_t trigger, irq_prio_t priority)
{
	uintptr_t base;
	uint32_t prio_idx = irq / 4;
	uint32_t prio_shift = (irq % 4) * 8;

	uint32_t cfg_idx = irq / 16;
	uint32_t cfg_shift = (irq % 16) * 2;
	uint32_t trigger_val = (trigger == IRQ_TRIGGER_EDGE) ? 0x2U : 0x0U;

	uint32_t grp_idx = irq / 32;
	uint32_t bit_shift = irq % 32;

	if (irq < 32) {
		base = get_current_gicr_base();

		// 1. Set priority
		uint32_t val = io_read32(base + GICR_SGI_IPRIORITYR(prio_idx));
		val &= ~(0xFFU << prio_shift);
		val |= ((uint32_t)priority << prio_shift);
		io_write32(base + GICR_SGI_IPRIORITYR(prio_idx), val);

		// 2. Set configuration (level vs edge)
		uint32_t icfgr = io_read32(base + GICR_SGI_ICFGR(cfg_idx));
		icfgr &= ~(0x3U << cfg_shift);
		icfgr |= (trigger_val << cfg_shift);
		io_write32(base + GICR_SGI_ICFGR(cfg_idx), icfgr);

		// 3. Set Group 1 allocation
		uint32_t igroupr = io_read32(base + GICR_SGI_IGROUPR(grp_idx));
		igroupr |= (1U << bit_shift);
		io_write32(base + GICR_SGI_IGROUPR(grp_idx), igroupr);

		// 4. Clear Group Modifier bit
		uint32_t igrpmodr =
			io_read32(base + GICR_SGI_IGRPMODR(grp_idx));
		igrpmodr &= ~(1U << bit_shift);
		io_write32(base + GICR_SGI_IGRPMODR(grp_idx), igrpmodr);
	} else {
		base = g_gicd;

		// 1. Set priority
		uint32_t val = io_read32(base + GICD_IPRIORITYR(prio_idx));
		val &= ~(0xFFU << prio_shift);
		val |= ((uint32_t)priority << prio_shift);
		io_write32(base + GICD_IPRIORITYR(prio_idx), val);

		// 2. Set configuration
		uint32_t icfgr = io_read32(base + GICD_ICFGR(cfg_idx));
		icfgr &= ~(0x3U << cfg_shift);
		icfgr |= (trigger_val << cfg_shift);
		io_write32(base + GICD_ICFGR(cfg_idx), icfgr);

		// 3. Set Group 1 allocation
		uint32_t igroupr = io_read32(base + GICD_IGROUPR(grp_idx));
		igroupr |= (1U << bit_shift);
		io_write32(base + GICD_IGROUPR(grp_idx), igroupr);

		// 4. Clear Group Modifier bit
		uint32_t igrpmodr = io_read32(base + GICD_IGRPMODR(grp_idx));
		igrpmodr &= ~(1U << bit_shift);
		io_write32(base + GICD_IGRPMODR(grp_idx), igrpmodr);
	}

	__asm__ volatile("dsb sy" : : : "memory");
	return 0;
}

int set_group(irq_vector_t irq, irq_group_t group)
{
	if (irq >= MAX_INTERRUPT_VECTORS) {
		return -1;
	}

	// GICD_IGROUPR registers hold 32 bits, with each bit representing one interrupt.
	// Register index = irq / 32, Bit position = irq % 32
	uint32_t reg_offset = (irq / 32) * 4;
	uint32_t bit_shift = irq % 32;

	// Determine the base depending on if it's per-core (SGI/PPI) or global (SPI)
	uintptr_t base_addr;
	if (irq < 32) {
		// SGIs and PPIs are managed inside the core's local Redistributor (SGI_base offset 0x10000)
		base_addr = get_current_gicr_base() + 0x10000;
	} else {
		// SPIs are managed inside the global Distributor
		base_addr = g_gicd;
	}

	// Read-modify-write the Group register (Base offset 0x0080 for IGROUPR)
	uint32_t val = io_read32(base_addr + 0x0080 + reg_offset);

	if (group == IRQ_GROUP_NON_SECURE) {
		val |= (1U << bit_shift); // 1 = Non-Secure
	} else {
		val &= ~(1U << bit_shift); // 0 = Secure
	}

	io_write32(base_addr + 0x0080 + reg_offset, val);

	// Ensure the hardware registers sync up across the system pipeline
	__asm__ volatile("dsb sy" ::: "memory");

	return 0;
}

int route_to_core(irq_vector_t irq, uint64_t mpidr)
{
	// Per-core interrupts (SGIs/PPIs < 32) cannot be cross-routed
	if (irq < 32 || irq >= MAX_INTERRUPT_VECTORS) {
		return EINVAL;
	}

	// Ensure the target core has been initialized
	int found = 0;
	for (int i = 0; i < MAX_CORES_SUPPORTED; i++) {
		if (s_core_topology[i].allocated && s_core_topology[i].mpidr == mpidr) {
			found = 1;
			break;
		}
	}
	if (!found) {
		return EINVAL;   // Core not registered
	}

	// Clear IRM bit (bit 31) to force unicast delivery
	uint64_t routing_val = mpidr & ~(1ULL << 31);

	// Each SPI has a 64-bit IROUTER register at offset 0x6000 + (irq * 8)
	io_write64(g_gicd + 0x6000ULL + ((uint64_t)irq * 8), routing_val);
	__asm__ volatile("dsb sy" ::: "memory");
	return 0;
}
