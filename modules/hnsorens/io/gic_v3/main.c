
#include <modules.h>
#include <api/gic_v3.h>
#include "gic_v3.h"
#include <api/serial_debug.h>
#include <errno.h>
#include <test.h>
#include <type.h>

IMPORT_INTERFACE_ANY(serial, serial);

int main()
{
	// Use typical QEMU virt addresses for now
	uintptr_t gicd_base = 0x8000000ULL; // QEMU virt platform GICD
	uintptr_t gicr_base = 0x80A0000ULL; // GICR base (first redistributor)

	//init_global(gicd_base, gicr_base);
}

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

#ifdef TESTING

/**
 * @brief Test: Init global and per-core with realistic addresses.
 */
TEST(GIC_InitLifecycle)
{
	TEST_INIT();

	// Use typical QEMU virt addresses (or real hardware values)
	uintptr_t gicd_base = 0x8000000ULL; // QEMU virt platform GICD
	uintptr_t gicr_base = 0x80A0000ULL; // GICR base (first redistributor)

	int ret = init_global(gicd_base, gicr_base);
	EXPECT_EQ(ret, 0);

	ret = init_core();
	EXPECT_EQ(ret, 0);

	TEST_RESULT();
}

/**
 * @brief Test: Configure a SPI (UART IRQ 33) as edge-triggered, priority 0x80, Non-Secure.
 */
TEST(GIC_ConfigureSPI)
{
	TEST_INIT();

	// Ensure init has been done (depends on previous test or re-init)
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t uart_irq = 33;

	int ret = configure(uart_irq, IRQ_TRIGGER_EDGE, 0x80);
	EXPECT_EQ(ret, 0);

	// Verify via MMIO: read IPRIORITYR and ICFGR
	uint32_t prio_reg =
		*(volatile uint32_t *)(gicd_base + 0x0400 + (uart_irq / 4) * 4);
	uint32_t prio_shift = (uart_irq % 4) * 8;
	uint32_t prio_val = (prio_reg >> prio_shift) & 0xFF;
	EXPECT_EQ(prio_val, 0x80);

	uint32_t cfg_reg = *(volatile uint32_t *)(gicd_base + 0x0C00 +
						  (uart_irq / 16) * 4);
	uint32_t cfg_shift = (uart_irq % 16) * 2;
	uint32_t cfg_val = (cfg_reg >> cfg_shift) & 0x3;
	EXPECT_EQ(cfg_val, 0x2); // Edge-triggered

	TEST_RESULT();
}

/**
 * @brief Test: Enable a PPI (Timer IRQ 27) and verify ISENABLER0.
 */
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

	// Check redistributor ISENABLER0 (SGI/PPI enable register)
	uintptr_t redist_base = gicr_base; // first core
	volatile uint32_t *isenabler0 =
		(volatile uint32_t *)(redist_base + 0x10000 + 0x0100);
	EXPECT_EQ((*isenabler0 >> timer_irq) & 1, 1);

	TEST_RESULT();
}

/**
 * @brief Test: SGI signaling – send SGI #0 to self and verify IAR1.
 */
TEST(GIC_SGISelf)
{
	TEST_INIT();

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;

	init_global(gicd_base, gicr_base);
	init_core();

	// 1. Enable SGI 0 in the Redistributor (GICR_ISENABLER0)
	// Core SGI/PPI registers are at offset +0x10000 in the Redistributor frame
	uintptr_t gicr_sgi_base = gicr_base + 0x10000;
	*(volatile uint32_t *)(gicr_sgi_base + 0x0100) =
		(1U << 0); // Set bit 0 to enable SGI 0

	// 2. Set SGI 0 Priority to highest (0x00)
	*(volatile uint8_t *)(gicr_sgi_base + 0x0400) = 0x00;

	// 3. Unmask CPU Priority Filter & Enable Group 1 Interrupts
	uint64_t pmr = 0xFF;
	uint64_t igrpen1 = 1;
	__asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(pmr));
	__asm__ volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(igrpen1));
	__asm__ volatile(
		"isb"); // Ensure system registers update before SGI trigger

	// 4. Read MPIDR_EL1 to extract core affinity fields
	uint64_t self_mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(self_mpidr));

	uint64_t aff0 = self_mpidr & 0xFF;
	uint64_t aff1 = (self_mpidr >> 8) & 0xFF;
	uint64_t aff2 = (self_mpidr >> 16) & 0xFF;
	uint64_t aff3 = (self_mpidr >> 32) & 0xFF;

	// 5. Construct GICv3 ICC_SGI1R_EL1 Value
	// Bit layout:
	// [63:48] Aff3
	// [43:32] Aff2
	// [31:24] Aff1
	// [27:24] SGI INTID (0..15)
	// [23:16] Aff0 (Cluster level)
	// [15:0]  Target List (Bit mask for target PE inside cluster)
	uint32_t sgi_id = 0;
	uint64_t target_pe_mask =
		1ULL << (aff0 & 0x0F); // Core position within Aff0 cluster
	uint64_t cluster_aff0 = (aff0 & 0xF0) >> 4;

	uint64_t sgi_reg = (aff3 << 48) | (aff2 << 32) | (aff1 << 24) |
			   ((uint64_t)(sgi_id & 0x0F) << 24) |
			   (cluster_aff0 << 16) | (target_pe_mask & 0xFFFF);

	__asm__ volatile("msr ICC_SGI1R_EL1, %0" : : "r"(sgi_reg));
	__asm__ volatile("dsb sy");
	__asm__ volatile("isb");

	// 6. Acknowledge Interrupt (Reads ICC_IAR1_EL1)
	irq_vector_t id = acknowledge();
	EXPECT_EQ(id, 0); // Should return INTID 0 (SGI 0), NOT 1023

	// 7. End of Interrupt (Writes ICC_EOIR1_EL1)
	int ret = eoi(id);
	EXPECT_EQ(ret, 0);

	TEST_RESULT();
}

/**
 * @brief Test: Register handler and assert EBUSY on duplicate.
 */
TEST(GIC_RegisterHandler)
{
	TEST_INIT();

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	void *dummy_ctx = (void *)0xDEADBEEF;
	void (*dummy_isr)(void *) = (void (*)(
		void *))0x1234; // won't be called, just registration test

	int ret = register_handler(27, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, 0);

	// Double registration should fail
	ret = register_handler(27, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, EBUSY);

	// Unregister
	ret = unregister_handler(27);
	EXPECT_EQ(ret, 0);

	TEST_RESULT();
}

/**
 * @brief Test: Parameter bounds – invalid vector (>1023), NULL handler.
 */
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
	EXPECT_NE(ret, 0); // should fail

	TEST_RESULT();
}

/**
 * @brief Test: Magic corruption detection (if any) – ensure handlers table doesn't get corrupted.
 */
TEST(GIC_MagicCorruption)
{
	TEST_INIT();

	// The handler table doesn't have magic tokens currently, but we can test that
	// a write to the table does not produce spurious changes to adjacent entries.
	// This is a placement test; if magic fields are added later, they must be checked.
	// For now, we just exercise the table.
	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t v = 30;
	void (*f)(void *) = (void (*)(void *))0xABCD;
	void *arg = (void *)0x12345678;

	int ret = register_handler(v, f, arg);
	EXPECT_EQ(ret, 0);

	// Unregister
	ret = unregister_handler(v);
	EXPECT_EQ(ret, 0);

	TEST_RESULT();
}

/**
 * @brief Test: Route SPI 50 to the current core and verify IROUTER content.
 */
TEST(GIC_RouteSPIToCore)
{
	TEST_INIT();

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t spi = 50;
	uint64_t mpidr = get_current_mpidr();

	// Clear IRM bit (bit 31) – matching what route_to_core does
	uint64_t expected = mpidr & ~(1ULL << 31);

	int ret = route_to_core(spi, mpidr);
	EXPECT_EQ(ret, 0);

	// Read IROUTER at offset 0x6000 + (spi * 8)
	uint64_t router_val =
		*(volatile uint64_t *)(gicd_base + 0x6000ULL + (spi * 8));
	EXPECT_EQ(router_val, expected);

	TEST_RESULT();
}

/**
 * @brief Test: Routing an SGI (<32) must fail with EINVAL.
 */
TEST(GIC_RouteSGI_Failure)
{
	TEST_INIT();

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t sgi = 0; // SGI 0
	uint64_t mpidr = get_current_mpidr();

	int ret = route_to_core(sgi, mpidr);
	EXPECT_EQ(ret, EINVAL);

	TEST_RESULT();
}

/**
 * @brief Test: Full SPI cycle – route, configure, enable, register handler.
 */
TEST(GIC_FullSPILifecycle)
{
	TEST_INIT();

	uintptr_t gicd_base = 0x8000000ULL;
	uintptr_t gicr_base = 0x80A0000ULL;
	init_global(gicd_base, gicr_base);
	init_core();

	irq_vector_t spi = 51;
	uint64_t mpidr = get_current_mpidr();
	void *dummy_ctx = (void *)0xDEADBEEF;
	void (*dummy_isr)(void *) = (void (*)(void *))0x1234;

	// 1. Route to current core
	int ret = route_to_core(spi, mpidr);
	EXPECT_EQ(ret, 0);

	// 2. Configure as edge-triggered, priority 0xA0
	ret = configure(spi, IRQ_TRIGGER_EDGE, 0xA0);
	EXPECT_EQ(ret, 0);

	// 3. Enable the interrupt
	ret = enable(spi);
	EXPECT_EQ(ret, 0);

	// 4. Register handler
	ret = register_handler(spi, dummy_isr, dummy_ctx);
	EXPECT_EQ(ret, 0);

	// 5. Verify ISENABLER (distributor offset for SPIs)
	uint32_t enabler =
		*(volatile uint32_t *)(gicd_base + 0x0100 + (spi / 32) * 4);
	uint32_t bit = (enabler >> (spi % 32)) & 1;
	EXPECT_EQ(bit, 1);

	// 6. Verify IPRIORITYR
	uint32_t prio_reg =
		*(volatile uint32_t *)(gicd_base + 0x0400 + (spi / 4) * 4);
	uint32_t prio_shift = (spi % 4) * 8;
	uint32_t prio_val = (prio_reg >> prio_shift) & 0xFF;
	EXPECT_EQ(prio_val, 0xA0);

	// 7. Verify ICFGR (edge = 0x2)
	uint32_t cfg_reg =
		*(volatile uint32_t *)(gicd_base + 0x0C00 + (spi / 16) * 4);
	uint32_t cfg_shift = (spi % 16) * 2;
	uint32_t cfg_val = (cfg_reg >> cfg_shift) & 0x3;
	EXPECT_EQ(cfg_val, 0x2);

	// 8. Clean up: unregister and disable
	ret = unregister_handler(spi);
	EXPECT_EQ(ret, 0);
	ret = disable(spi);
	EXPECT_EQ(ret, 0);

	TEST_RESULT();
}

#endif /* TESTING */
