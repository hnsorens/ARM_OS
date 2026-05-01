#include "serial.h"

// PL011 UART Registers (ARM Versatile Express base)
#define UART0_BASE 0x09000000

// Register offsets
#define UARTDR 0x00 // Data register
#define UARTFR 0x18 // Flag register
#define UARTIBRD 0x24 // Integer baud rate divisor
#define UARTFBRD 0x28 // Fractional baud rate divisor
#define UARTLCR_H 0x2C // Line control register
#define UARTCR 0x30 // Control register
#define UARTIMSC 0x38 // Interrupt mask set/clear register

// Flag bits
#define UARTFR_TXFF (1 << 5) // Transmit FIFO full
#define UARTFR_RXFE (1 << 4) // Receive FIFO empty

static inline VOID MMIO_Write(IN EFI_PHYSICAL_ADDRESS Reg, IN UINT32 Data)
{
	*(volatile UINT32 *)Reg = Data;
}

static inline UINT32 MMIO_Read(IN EFI_PHYSICAL_ADDRESS Reg)
{
	return *(volatile UINT32 *)Reg;
}

static VOID Uart_PutC(IN CHAR8 C)
{
	// Wait until transmit FIFO has space
	while (MMIO_Read(UART0_BASE + UARTFR) & UARTFR_TXFF)
		;
	// Write character
	MMIO_Write(UART0_BASE + UARTDR, C);
}

static VOID Uart_PutS(IN CONST CHAR8 *IN Str, IN UINTN N)
{
	for (UINTN I = 0; I < N; I++) {
		Uart_PutC(Str[I]);
	}
}

UINTN
Boot_Log(IN CONST CHAR8 *Str, IN UINTN N)
{
	Uart_PutS("[Boot] ", 7);
	Uart_PutS(Str, N);
	return N + 7;
}

VOID Boot_Log_Start()
{
	// Disable UART
	MMIO_Write(UART0_BASE + UARTCR, 0);

	// Set baud rate to 115200
	// (UARTCLK = 24MHz, baud = 115200)
	MMIO_Write(UART0_BASE + UARTIBRD, 13);
	MMIO_Write(UART0_BASE + UARTFBRD, 1);

	// Set line control: 8 bits, no parity, 1 stop bit, FIFOs enabled
	MMIO_Write(UART0_BASE + UARTLCR_H, (1 << 4) | (1 << 5) | (1 << 6));

	// Enable UART, enable transmit & receive
	MMIO_Write(UART0_BASE + UARTCR, (1 << 0) | (1 << 8) | (1 << 9));

	Boot_Log("UART serial out initialized\n", 28);
}
