
#include <modules.h>
#include <api/gic_v3.h>
#include "gic_v3.h"
#include <api/serial_debug.h>
#include <errno.h>

IMPORT_INTERFACE_ANY(serial, serial);
IMPORT_INTERFACE_ANY(interrupt_manager, gic);

EXPORT_INTERFACE(
    interrupt_manager, gic_v3, {
        .init_global = init_global,
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
        .unregister_handler = unregister_handler
    });

#ifdef TESTING

/**
 * @brief Test: Init global and per-core with realistic addresses.
 */
TEST(GIC_InitLifecycle)
{
    TEST_INIT();

    // Use typical QEMU virt addresses (or real hardware values)
    uintptr_t gicd_base = 0x8000000ULL;   // QEMU virt platform GICD
    uintptr_t gicr_base = 0x80A0000ULL;   // GICR base (first redistributor)

    int ret = gic.init_global(gicd_base, gicr_base);
    EXPECT_EQ(ret, 0);

    ret = gic.init_core();
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    irq_vector_t uart_irq = 33;

    int ret = gic.configure(uart_irq, IRQ_TRIGGER_EDGE, 0x80);
    EXPECT_EQ(ret, 0);

    // Verify via MMIO: read IPRIORITYR and ICFGR
    uint32_t prio_reg = *(volatile uint32_t*)(gicd_base + 0x0400 + (uart_irq/4)*4);
    uint32_t prio_shift = (uart_irq % 4) * 8;
    uint32_t prio_val = (prio_reg >> prio_shift) & 0xFF;
    EXPECT_EQ(prio_val, 0x80);

    uint32_t cfg_reg = *(volatile uint32_t*)(gicd_base + 0x0C00 + (uart_irq/16)*4);
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    irq_vector_t timer_irq = 27;

    int ret = gic.enable(timer_irq);
    EXPECT_EQ(ret, 0);

    // Check redistributor ISENABLER0 (SGI/PPI enable register)
    uintptr_t redist_base = gicr_base; // first core
    volatile uint32_t *isenabler0 = (volatile uint32_t*)(redist_base + 0x10000 + 0x0100);
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    // Generate SGI 0 to self using ICC_SGI1R_EL1
    uint64_t self_mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(self_mpidr));
    uint64_t aff0 = self_mpidr & 0xFF;
    uint64_t aff1 = (self_mpidr >> 8) & 0xFF;
    uint64_t aff2 = (self_mpidr >> 16) & 0xFF;
    uint64_t aff3 = (self_mpidr >> 32) & 0xFF;

    // ICC_SGI1R_EL1 format: [63:48] Aff3, [47:44] RS, [43:32] Aff2, [31:24] Aff1, [23:16] Aff0, [15:8] target list, [7:0] SGI ID
    uint64_t sgi_reg = (aff3 << 48) | (aff2 << 32) | (aff1 << 24) | (aff0 << 16) | (1 << 8) | 0; // SGI ID 0, target list bit 0
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" : : "r"(sgi_reg));
    __asm__ volatile("dsb sy");

    // Acknowledge interrupt
    irq_vector_t id = gic.acknowledge();
    EXPECT_EQ(id, 0);  // SGI ID 0

    // End of interrupt
    int ret = gic.end_of_interrupt(id);
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    void* dummy_ctx = (void*)0xDEADBEEF;
    void (*dummy_isr)(void*) = (void (*)(void*))0x1234; // won't be called, just registration test

    int ret = gic.register_handler(27, dummy_isr, dummy_ctx);
    EXPECT_EQ(ret, 0);

    // Double registration should fail
    ret = gic.register_handler(27, dummy_isr, dummy_ctx);
    EXPECT_EQ(ret, EBUSY);

    // Unregister
    ret = gic.unregister_handler(27);
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    int ret = gic.register_handler(2000, (isr_handler_t)0x1234, NULL);
    EXPECT_EQ(ret, EINVAL);

    ret = gic.register_handler(27, NULL, NULL);
    EXPECT_EQ(ret, EINVAL);

    ret = gic.unregister_handler(2000);
    EXPECT_EQ(ret, EINVAL);

    ret = gic.set_group(2000, IRQ_GROUP_NON_SECURE);
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
    gic.init_global(gicd_base, gicr_base);
    gic.init_core();

    irq_vector_t v = 30;
    void (*f)(void*) = (void (*)(void*))0xABCD;
    void* arg = (void*)0x12345678;

    int ret = gic.register_handler(v, f, arg);
    EXPECT_EQ(ret, 0);

    // Unregister
    ret = gic.unregister_handler(v);
    EXPECT_EQ(ret, 0);

    TEST_RESULT();
}

#endif /* TESTING */

