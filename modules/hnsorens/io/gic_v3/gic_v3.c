#include "gic_v3.h"
#include "errno.h"
#include "modules.h"
#include <api/serial_debug.h>
#include <constants.h>

EXTERN_IMPORT_INTERFACE(serial, serial);

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

/* --------------------------------------------------------------------------
 * 2. REGISTERS AND BIT DEFINITIONS
 * -------------------------------------------------------------------------- */
#define GICD_CTLR 0x0000
#define GICD_ISENABLER(n) (0x0100 + ((n) * 4))
#define GICD_ICENABLER(n) (0x0180 + ((n) * 4))

#define GICR_CTLR 0x0000
#define GICR_WAKER 0x0014
#define GICR_SGI_OFFSET 0x10000
#define GICR_IGROUPR0 (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI_OFFSET + 0x0180)
#define GICR_IPRIORITYR(n) (GICR_SGI_OFFSET + 0x0400 + ((n) * 4))
#define GICR_ICFGR1 (GICR_SGI_OFFSET + 0x0C04)
#define GICR_IGRPMODR0 (GICR_SGI_OFFSET + 0x0D00) // Added: Group Modifier Reg

// Non-Secure View Bit fields
#define GICD_CTLR_NS_ARE_NS (1U << 4) // Affinity Routing Enable
#define GICD_CTLR_NS_ENA_GRP1NS \
	(1U << 1) // Enable Non-Secure Group 1 distribution

#define GICR_WAKER_ProcessorSleep (1U << 1)
#define GICR_WAKER_ChildrenAsleep (1U << 2)

#define ICC_SRE_SRE (1U << 0)
#define ICC_SRE_DFB (1U << 1)
#define ICC_SRE_DIB (1U << 2)

#define ICC_IGRPEN1_ENABLE (1U << 0) // Enable Non-Secure Group 1 signaling

#define TIMER_INTID 30 // Non-Secure Physical Timer PPI is ID 30

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

int gicv3_init_global(void)
{
	uint64_t cbar;
	__asm__ volatile("mrs %0, S3_1_C15_C3_0" : "=r"(cbar));
	cbar += HHDM_OFFSET;

	g_gicd = cbar;
	g_gicr = cbar + 0xA0000;

	// Set ONLY Non-Secure bits allowed in EL1 context
	uint32_t ctlr = gicd_read_ctlr();
	ctlr |= GICD_CTLR_NS_ARE_NS | GICD_CTLR_NS_ENA_GRP1NS;
	gicd_write_ctlr(ctlr);
	__asm__ volatile("dsb sy");
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

void gicv3_enable_timer_interrupt(void)
{
	uintptr_t core_gicr = get_current_gicr_base();
	uint32_t prio_idx = TIMER_INTID / 4;
	uint32_t prio_shift = (TIMER_INTID % 4) * 8;

	// 1. Set priority
	uint32_t uint32_val = io_read32(core_gicr + GICR_IPRIORITYR(prio_idx));
	uint32_val &= ~(0xFFU << prio_shift);
	uint32_val |= (0x00U << prio_shift);
	io_write32(core_gicr + GICR_IPRIORITYR(prio_idx), uint32_val);

	// 2. Set configuration as level-triggered
	uint32_t icfgr = io_read32(core_gicr + GICR_ICFGR1);
	icfgr &= ~(0x2U << 28);
	io_write32(core_gicr + GICR_ICFGR1, icfgr);

	// 3. Set Group 1 allocation
	uint32_t igroupr = io_read32(core_gicr + GICR_IGROUPR0);
	igroupr |= (1U << TIMER_INTID);
	io_write32(core_gicr + GICR_IGROUPR0, igroupr);

	// 4. Clear Group Modifier bit to lock into Non-Secure Group 1
	uint32_t igrpmodr = io_read32(core_gicr + GICR_IGRPMODR0);
	igrpmodr &= ~(1U << TIMER_INTID);
	io_write32(core_gicr + GICR_IGRPMODR0, igrpmodr);

	// 5. Enable the interrupt
	io_write32(core_gicr + GICR_ISENABLER0, (1U << TIMER_INTID));
	__asm__ volatile("dsb sy");
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
	serial.printf("interrupt_id %lx\n", interrupt_id);

	if (interrupt_id == TIMER_INTID) {
		static int tick_count = 0;
		serial.printf("Timer Tick: %d\n", ++tick_count);

		// Reset countdown to 24M ticks
		__asm__ volatile("msr CNTP_TVAL_EL0, %0" : : "r"(100000000ULL));
	}

	icc_write_eoir1_el1(iar);
}

/* --------------------------------------------------------------------------
 * 6. EXTERNAL MODULE INTERFACE LIFECYCLE HOOKS
 * -------------------------------------------------------------------------- */
int enable(u64 irq_vector)
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

int disable(u64 irq_vector)
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

int ack(u64 irq_vector)
{
	(void)irq_vector;
	return (int)(icc_read_iar1_el1() & 0xFFFFFFFF);
}

int eoi(u64 irq_vector)
{
	icc_write_eoir1_el1(irq_vector);
	return 0;
}

void driver_timer_start_sequence(void)
{
	serial.printf("starting init global\n");
	gicv3_init_global();
	serial.printf("starting init redistributor\n");
	gicv3_init_redistributor();
	serial.printf("starting init cpu interface\n");
	gicv3_init_cpu_interface_ns();
	serial.printf("starting init vectors\n");
	arm64_init_vectors();
	serial.printf("starting enable timer interrupt\n");
	gicv3_enable_timer_interrupt();

	serial.printf("starting core timer\n");
	arm64_core_timer_start(
		1000000U); // Changed from 240 back to 24M for a clean 1-sec countdown
	arm64_unmask_cpu_exceptions();
}
