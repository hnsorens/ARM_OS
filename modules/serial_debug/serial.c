
#include "../module_debug.h"
#include "../module_vtables.h"
#include "../module.h"

#include <stdint.h>
#include "kprintf.h"

// PL011 UART Registers (ARM Versatile Express base)
#define UART0_BASE 0x09000000

// Register offsets
#define UARTDR     0x00    // Data register
#define UARTFR     0x18    // Flag register
#define UARTIBRD   0x24    // Integer baud rate divisor
#define UARTFBRD   0x28    // Fractional baud rate divisor
#define UARTLCR_H  0x2C    // Line control register
#define UARTCR     0x30    // Control register
#define UARTIMSC   0x38    // Interrupt mask set/clear register

// Flag bits
#define UARTFR_TXFF (1 << 5)  // Transmit FIFO full
#define UARTFR_RXFE (1 << 4)  // Receive FIFO empty

static inline void mmio_write(uint32_t reg, uint32_t data) {
    *(volatile uint32_t*)reg = data;
}

static inline uint32_t mmio_read(uint32_t reg) {
    return *(volatile uint32_t*)reg;
}

vtable(serial_vtable_t);
start(init, serial_init);

void uart_putc(char c) {
    // Wait until transmit FIFO has space
    while (mmio_read(UART0_BASE + UARTFR) & UARTFR_TXFF);
    
    // Write character
    mmio_write(UART0_BASE + UARTDR, c);
}

void uart_puts(const char* str) {
    while (*str) {
        if (*str == '\n')
            uart_putc('\r');
        uart_putc(*str++);
    }
}

void init(serial_vtable_t *vtable)
{
  vtable->serial_printf = kprintf;
}

void serial_init(kernel_vtable_t *kvtable, unsigned long load)
{
    // Disable UART
  mmio_write(UART0_BASE + UARTCR, 0);
  
  // Set baud rate to 115200
  // (UARTCLK = 24MHz, baud = 115200)
  mmio_write(UART0_BASE + UARTIBRD, 13);
  mmio_write(UART0_BASE + UARTFBRD, 1);
  
  // Set line control: 8 bits, no parity, 1 stop bit, FIFOs enabled
  mmio_write(UART0_BASE + UARTLCR_H, (1 << 4) | (1 << 5) | (1 << 6));
  
  // Enable UART, enable transmit & receive
  mmio_write(UART0_BASE + UARTCR, (1 << 0) | (1 << 8) | (1 << 9));

  kprintf("UART Serial Out Initialized!\n");
}