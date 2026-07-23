#include "boot_info.h"
#include <modules.h>
#include <api/gic_v3.h>
#include "gic_v3.h"
#include <api/serial_debug.h>
#include <errno.h>
#include <test.h>
#include <type.h>

IMPORT_INTERFACE_ANY(serial, serial);

/* ========================================================================== */
/*                 Module init (optional, not required by vtable)             */
/* ========================================================================== */
int main(boot_info_t *boot_info)
{
	(void)boot_info;

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;

	int ret = init_global(gicd_base, gicr_base);
	if (ret) {
		serial.printf("init_global failed: %d\n", ret);
		return ret;
	}

	ret = init_core();
	if (ret) {
		serial.printf("init_core failed: %d\n", ret);
		return ret;
	}

	/* Use the EL1 Physical Timer (PPI 30) */
	const irq_vector_t timer_irq = 30;

	/* Configure and enable the timer interrupt */
	ret = configure(timer_irq, IRQ_TRIGGER_LEVEL, 0x80);
	if (ret) {
		serial.printf("configure failed: %d\n", ret);
		return ret;
	}

	ret = set_group(timer_irq, IRQ_GROUP_NON_SECURE);
	if (ret) {
		serial.printf("set_group failed: %d\n", ret);
		return ret;
	}

	ret = enable(timer_irq);
	if (ret) {
		serial.printf("enable failed: %d\n", ret);
		return ret;
	}

	set_core_priority_mask(0xFF);

	/* Program the EL1 physical timer to fire after ~10 ms */
	uint64_t cntfrq;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
	uint64_t cval;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(cval));
	cval += cntfrq / 100; /* ~10 ms */

	__asm__ volatile("msr cntp_cval_el0, %0" : : "r"(cval));

	/* Enable timer (bit 0) */
	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
	__asm__ volatile("isb");

	/* Poll the acknowledge register until we get the timer interrupt */
	irq_vector_t ack = 1023;
	for (int spin = 0; spin < 10000000; spin++) {
		__asm__ volatile("nop");
		ack = acknowledge();
		if (ack != 1023)
			break;
	}

	if (ack == timer_irq) {
		serial.printf(
			"[GIC Demo] Timer interrupt acknowledged (ID %u). Success!\n",
			ack);
		/* Signal end‑of‑interrupt */
		eoi(ack);
	} else {
		serial.printf(
			"[GIC Demo] acknowledge() returned %u (expected %u). FAILURE.\n",
			ack, timer_irq);
	}

	/* Disable timer */
	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(0UL));

	/* Disable the interrupt */
	ret = disable(timer_irq);
	if (ret) {
		serial.printf("disable failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/* ========================================================================== */
/*                 Export the vtable                                          */
/* ========================================================================== */
EXPORT_INTERFACE(interrupt_manager, gic_v3,
		 { .init_global = init_global,
		   .init_core = init_core,
		   .set_core_priority_mask = set_core_priority_mask,
		   .enable = enable,
		   .disable = disable,
		   .configure = configure,
		   .set_group = set_group,
		   .route_to_core = route_to_core,
		   .acknowledge = acknowledge,
		   .end_of_interrupt = eoi,
		   .register_handler = register_handler,
		   .unregister_handler = unregister_handler });

/* ========================================================================== */
/*                              Unit Tests                                    */
/* ========================================================================== */
#ifdef TESTING

TEST(GIC_InitLifecycle)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;

	int ret = init_global(gicd_base, gicr_base);
	EXPECT_EQ(ret, 0);
	ret = init_core();
	EXPECT_EQ(ret, 0);
	TEST_RESULT();
}

TEST(GIC_ConfigureSPI)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t uart_irq = 33;
	int ret = configure(uart_irq, IRQ_TRIGGER_EDGE, 0x80);
	EXPECT_EQ(ret, 0);

	uint32_t prio_reg =
		*(volatile uint32_t *)(gicd_base + 0x0400 + (uart_irq / 4) * 4);
	uint32_t shift = (uart_irq % 4) * 8;
	EXPECT_EQ((prio_reg >> shift) & 0xFF, 0x80);

	uint32_t cfg_reg = *(volatile uint32_t *)(gicd_base + 0x0C00 +
						  (uart_irq / 16) * 4);
	uint32_t cfg_shift = (uart_irq % 16) * 2;
	EXPECT_EQ((cfg_reg >> cfg_shift) & 0x3, 0x2);
	TEST_RESULT();
}

TEST(GIC_EnablePPI)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t timer_irq = 27;
	int ret = enable(timer_irq);
	EXPECT_EQ(ret, 0);

	uintptr_t redist_base = gicr_base;
	volatile uint32_t *isenabler0 =
		(volatile uint32_t *)(redist_base + 0x10000 + 0x0100);
	EXPECT_EQ((*isenabler0 >> timer_irq) & 1, 1);
	TEST_RESULT();
}

TEST(GIC_SGISelf)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	/* Enable SGI 0 */
	uintptr_t gicr_sgi = gicr_base + 0x10000;
	*(volatile uint32_t *)(gicr_sgi + 0x0100) = (1U << 0);
	*(volatile uint8_t *)(gicr_sgi + 0x0400) = 0x00;

	/* Unmask CPU priority */
	__asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(0xFFULL));
	__asm__ volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(1ULL));
	__asm__ volatile("isb");

	/* Get current MPIDR */
	uint64_t mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
	uint64_t aff0 = mpidr & 0xFF;
	uint64_t aff1 = (mpidr >> 8) & 0xFF;
	uint64_t aff2 = (mpidr >> 16) & 0xFF;
	uint64_t aff3 = (mpidr >> 32) & 0xFF;

	uint64_t sgi_reg = (aff3 << 48) | (aff2 << 32) | (aff1 << 24) |
			   ((uint64_t)(0 & 0x0F) << 24) |
			   ((aff0 & 0xF0) << 16) | (1ULL << (aff0 & 0x0F));
	__asm__ volatile("msr ICC_SGI1R_EL1, %0" : : "r"(sgi_reg));
	__asm__ volatile("dsb sy\n isb");

	irq_vector_t id = acknowledge();
	EXPECT_EQ(id, 0);
	int ret = eoi(id);
	EXPECT_EQ(ret, 0);
	TEST_RESULT();
}

TEST(GIC_RegisterHandler)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	void *dummy_ctx = (void *)0xDEADBEEF;
	isr_handler_t dummy_isr = (isr_handler_t)0x1234;

	int ret = register_handler(27, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, 0);
	ret = register_handler(27, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, EBUSY);
	ret = unregister_handler(27);
	EXPECT_EQ(ret, 0);
	TEST_RESULT();
}

TEST(GIC_Bounds)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	int ret = register_handler(2000, (isr_handler_t)0x1234, NULL);
	EXPECT_EQ(ret, EINVAL);
	ret = register_handler(27, NULL, NULL);
	EXPECT_EQ(ret, EINVAL);
	ret = unregister_handler(2000);
	EXPECT_EQ(ret, EINVAL);
	ret = set_group(2000, IRQ_GROUP_NON_SECURE);
	EXPECT_EQ(ret, EINVAL);
	TEST_RESULT();
}

TEST(GIC_MagicCorruption)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t v = 30;
	isr_handler_t f = (isr_handler_t)0xABCD;
	void *arg = (void *)0x12345678;

	int ret = register_handler(v, f, arg);
	EXPECT_EQ(ret, 0);
	ret = unregister_handler(v);
	EXPECT_EQ(ret, 0);
	TEST_RESULT();
}

TEST(GIC_RouteSPIToCore)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t spi = 50;
	uint64_t mpidr = get_current_mpidr();
	uint64_t expected = mpidr & ~(1ULL << 31);

	int ret = route_to_core(spi, mpidr);
	EXPECT_EQ(ret, 0);

	uint64_t router_val =
		*(volatile uint64_t *)(gicd_base + 0x6000ULL + (spi * 8));
	EXPECT_EQ(router_val, expected);
	TEST_RESULT();
}

TEST(GIC_RouteSGI_Failure)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	int ret = route_to_core(0, get_current_mpidr());
	EXPECT_EQ(ret, EINVAL);
	TEST_RESULT();
}

TEST(GIC_FullSPILifecycle)
{
	TEST_INIT();
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t spi = 51;
	uint64_t mpidr = get_current_mpidr();
	isr_handler_t dummy_isr = (isr_handler_t)0x1234;
	void *dummy_ctx = (void *)0xDEADBEEF;

	int ret = route_to_core(spi, mpidr);
	EXPECT_EQ(ret, 0);
	ret = configure(spi, IRQ_TRIGGER_EDGE, 0xA0);
	EXPECT_EQ(ret, 0);
	ret = enable(spi);
	EXPECT_EQ(ret, 0);
	ret = register_handler(spi, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, 0);

	uint32_t enabler =
		*(volatile uint32_t *)(gicd_base + 0x0100 + (spi / 32) * 4);
	EXPECT_EQ((enabler >> (spi % 32)) & 1, 1);

	uint32_t prio_reg =
		*(volatile uint32_t *)(gicd_base + 0x0400 + (spi / 4) * 4);
	uint32_t prio_shift = (spi % 4) * 8;
	EXPECT_EQ((prio_reg >> prio_shift) & 0xFF, 0xA0);

	uint32_t cfg_reg =
		*(volatile uint32_t *)(gicd_base + 0x0C00 + (spi / 16) * 4);
	uint32_t cfg_shift = (spi % 16) * 2;
	EXPECT_EQ((cfg_reg >> cfg_shift) & 0x3, 0x2);

	ret = unregister_handler(spi);
	EXPECT_EQ(ret, 0);
	ret = disable(spi);
	EXPECT_EQ(ret, 0);
	TEST_RESULT();
}

#endif /* TESTING */
