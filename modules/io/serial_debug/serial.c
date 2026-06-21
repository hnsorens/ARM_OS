/**
 * @file serial.c
 * @brief Core Implementation details of the ARM PrimeCell PL011 UART Controller Engine.
 *
 * Provides standalone Memory-Mapped I/O (MMIO) hardware register interaction loops,
 * internal string translation/reversal primitives, token string state parsers,
 * and standard C-library style formatted variadic string processing engine (`vprintf`).
 */

#include "serial.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <modules.h>
#include <type.h>

/* --- PL011 UART Hardware Layout Matrix (ARM Versatile Express Base) --- */
#define UART0_BASE \
	0x09000000 /**< Hardwired physical memory base coordinate for UART controller */

/* --- MMIO Register Offset Sub-Addresses --- */
#define UARTDR 0x00 /**< Data Register (Read/Write RX/TX FIFO packets) */
#define UARTFR \
	0x18 /**< Flag Register (Status bits evaluating current device queues) */
#define UARTIBRD 0x24 /**< Integer Baud Rate Divisor */
#define UARTFBRD 0x28 /**< Fractional Baud Rate Divisor */
#define UARTLCR_H 0x2C /**< Line Control Register */
#define UARTCR 0x30 /**< Control Register */
#define UARTIMSC 0x38 /**< Interrupt Mask Set/Clear Register */

/* --- Hardware Status Bitmasks --- */
#define UARTFR_TXFF (1 << 5) /**< Transmit FIFO Full bit flag indicator */
#define UARTFR_RXFE (1 << 4) /**< Receive FIFO Empty bit flag indicator */

#define MAX_NUMBER_LEN \
	32 /**< Static threshold tracking max width of numerical string transformations */

/* --- Core MMIO Primitive Abstractions --- */

/**
 * @brief Access Layer Helper: Writes a raw 32-bit unsigned word into volatile memory layout.
 */
static inline void mmio_write(u32 reg, u32 data)
{
	*(volatile u32 *)reg = data;
}

/**
 * @brief Access Layer Helper: Samples a raw 32-bit unsigned word from volatile memory layout.
 */
static inline u32 mmio_read(u32 reg)
{
	return *(volatile u32 *)reg;
}

/* --- Hardware Processing Operations --- */

/**
 * @brief Transmits a singular character byte out to the PL011 hardware FIFO queue.
 * * Blocks execution processing iteratively if the underlying transmitter pipeline 
 * indicates it is entirely full.
 * * @param[in] c Character token byte to dispatch.
 */
void uart_putc(char c)
{
	/* Spin-lock execution pattern while the transmit FIFO channel context remains entirely full */
	while (mmio_read(UART0_BASE + UARTFR) & UARTFR_TXFF)
		;

	/* Feed character data payload directly into hardware register matrix */
	mmio_write(UART0_BASE + UARTDR, c);
}

/**
 * @brief Transmits an absolute null-terminated ASCII string via serial connection.
 * * Automatically translates singular Unix newline characters (`\n`) into standard 
 * carriage return / line feed couples (`\r\n`) to preserve terminal tracking layouts.
 * * @param[in] str Constant base pointer tracking input character sequence.
 */
void uart_puts(const char *str)
{
	while (*str) {
		if (*str == '\n') {
			uart_putc('\r');
		}
		uart_putc(*str++);
	}
}

/* --- Interface Wrappers --- */

static void putc_wrapper(char c)
{
	uart_putc(c);
}

static void puts_wrapper(const char *str)
{
	while (*str) {
		putc_wrapper(*str++);
	}
}

/* --- Numerical String Serialization Routines --- */

/**
 * @brief Unsigned integer to absolute string transformation routine across arbitrary radices.
 * * Translates value patterns iteratively out using modulo divisions into an isolated buffer, 
 * ending with a character tracking inversion pass to resolve orientation properties.
 */
static char *utoa(u64 num, char *str, int base, bool uppercase)
{
	char *ptr = str;
	char *ptr1 = str;
	char tmp_char;

	if (num == 0) {
		*ptr++ = '0';
		*ptr = '\0';
		return str;
	}

	/* Process characters backwards by pulling numeric remainders via modulo arithmetic */
	while (num) {
		u64 remainder = num % base;
		*ptr++ = (remainder < 10) ? remainder + '0' :
					    (uppercase ? remainder - 10 + 'A' :
							 remainder - 10 + 'a');
		num /= base;
	}

	*ptr-- =
		'\0'; /* Tie off string payload safely and pull back tracker to final character symbol */

	/* Inversion loop phase matching structural ordering requirements */
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}

	return str;
}

/**
 * @brief Signed integer to absolute string transformation routine.
 * * Intercepts sign bits safely on Base-10 conversions, normalizes values to positive magnitudes, 
 * evaluates properties via `utoa`, and pushes missing symbols directly out via bit-shifting blocks.
 */
static char *itoa(int64_t num, char *str, int base, bool uppercase)
{
	char *ptr = str;

	if (num == 0) {
		*ptr++ = '0';
		*ptr = '\0';
		return str;
	}

	bool negative = false;
	if (num < 0 && base == 10) {
		negative = true;
		num = -num;
	}

	utoa((u64)num, ptr, base, uppercase);

	if (negative) {
		/* Open up initial array slots via right shift operation to prepend negative symbol markers */
		char *p = ptr;
		while (*p) {
			p++;
		}
		while (p >= ptr) {
			*(p + 1) = *p;
			p--;
		}
		*ptr = '-';
	}

	return str;
}

/* --- Custom Printf Parsing Engine Core Infrastructure --- */

/**
 * @struct format_flags
 * @brief Formatting properties tracking matrix parsed out from standard token specifiers.
 */
struct format_flags {
	bool left_justify; /**< '-' Left alignment layout logic token flag */
	bool force_sign; /**< '+' Explicit sign output forcing pattern token flag */
	bool space_sign; /**< ' ' Space allocation token logic flag for positive fields */
	bool alternate_form; /**< '#' Hex/Octal prefix insertion criteria marker */
	bool zero_pad; /**< '0' Zero prefix stuffing pattern variable flag */
	bool precision_set; /**< '.' Precision threshold specification toggle flag */
	int width; /**< Total minimum width bounding limits allocated for element representation */
	int precision; /**< Target fractional/string layout precision ceiling values */
	char length_modifier; /**< Structural sizing modifier tokens matching storage width properties ('h','l','z') */
	char specifier; /**< Final destination transformation variable code identifier token ('d','x','s','p') */
};

/**
 * @brief Evaluates string sequences to map out formatting rules flags.
 */
static const char *parse_format_specifier(const char *format,
					  struct format_flags *flags)
{
	*flags = (struct format_flags){ 0 };
	flags->precision = -1;

	/* Phase 1: Standard Flag Extraction */
	while (1) {
		switch (*format) {
		case '-':
			flags->left_justify = true;
			break;
		case '+':
			flags->force_sign = true;
			break;
		case ' ':
			flags->space_sign = true;
			break;
		case '#':
			flags->alternate_form = true;
			break;
		case '0':
			flags->zero_pad = true;
			break;
		default:
			goto parse_width;
		}
		format++;
	}

parse_width:
	/* Phase 2: Width Boundary Sizing Calculation */
	if (*format >= '0' && *format <= '9') {
		flags->width = 0;
		while (*format >= '0' && *format <= '9') {
			flags->width = flags->width * 10 + (*format - '0');
			format++;
		}
	}

	/* Phase 3: Precision Boundary Sizing Calculation */
	if (*format == '.') {
		flags->precision_set = true;
		flags->precision = 0;
		format++;

		if (*format >= '0' && *format <= '9') {
			while (*format >= '0' && *format <= '9') {
				flags->precision =
					flags->precision * 10 + (*format - '0');
				format++;
			}
		}
	}

	/* Phase 4: Size Length Modifier Evaluation Checks */
	switch (*format) {
	case 'h':
		if (*(format + 1) == 'h') {
			flags->length_modifier =
				'H'; /* Intercepts standard char sizes (hh tokens) */
			format += 2;
		} else {
			flags->length_modifier = 'h';
			format++;
		}
		break;
	case 'l':
		if (*(format + 1) == 'l') {
			flags->length_modifier =
				'q'; /* Intercepts quad long-long sizes (ll tokens) */
			format += 2;
		} else {
			flags->length_modifier = 'l';
			format++;
		}
		break;
	case 'L':
	case 'z':
	case 't':
	case 'j':
		flags->length_modifier = *format;
		format++;
		break;
	}

	/* Phase 5: Final Specifier Mapping Capture Step */
	flags->specifier = *format;

	return format + 1;
}

static void output_padding(int count, char pad_char)
{
	while (count-- > 0) {
		putc_wrapper(pad_char);
	}
}

/* --- Formatted Element Dispatch Operators --- */

static int print_formatted_string(const char *str, struct format_flags *flags)
{
	int count = 0;
	if (!str) {
		str = "(null)";
	}

	int len = 0;
	const char *p = str;
	while (*p++)
		len++;

	if (flags->precision_set && flags->precision < len) {
		len = flags->precision;
	}

	int padding = flags->width - len;
	if (padding < 0)
		padding = 0;

	if (!flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	for (int i = 0; i < len; i++) {
		putc_wrapper(str[i]);
		count++;
	}

	if (flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	return count;
}

static int print_formatted_char(char c, struct format_flags *flags)
{
	int count = 0;
	int padding = flags->width - 1;
	if (padding < 0)
		padding = 0;

	if (!flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	putc_wrapper(c);
	count++;

	if (flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	return count;
}

static int print_formatted_integer(int64_t num, struct format_flags *flags)
{
	int count = 0;
	char buffer[MAX_NUMBER_LEN];
	int base = 10;
	bool uppercase = false;

	/* Establish operational radices based on specifier properties mapping keys */
	switch (flags->specifier) {
	case 'd':
	case 'i':
	case 'u':
		base = 10;
		break;
	case 'x':
		base = 16;
		uppercase = false;
		break;
	case 'X':
		base = 16;
		uppercase = true;
		break;
	case 'o':
		base = 8;
		break;
	case 'b':
		base = 2;
		break;
	}

	bool is_negative = false;
	char sign_char = '\0';

	if (flags->specifier == 'd' || flags->specifier == 'i') {
		if (num < 0) {
			is_negative = true;
			num = -num;
			sign_char = '-';
		} else if (flags->force_sign) {
			sign_char = '+';
		} else if (flags->space_sign) {
			sign_char = ' ';
		}
	}

	if (flags->specifier == 'u') {
		utoa((u64)num, buffer, base, uppercase);
	} else {
		itoa(num, buffer, base, uppercase);
	}

	int len = 0;
	while (buffer[len])
		len++;

	int prefix_len = 0;
	char prefix[4] = { 0 };

	if (flags->alternate_form) {
		switch (flags->specifier) {
		case 'x':
			prefix[0] = '0';
			prefix[1] = 'x';
			prefix_len = 2;
			break;
		case 'X':
			prefix[0] = '0';
			prefix[1] = 'X';
			prefix_len = 2;
			break;
		case 'b':
			prefix[0] = '0';
			prefix[1] = 'b';
			prefix_len = 2;
			break;
		case 'o':
			if (num != 0 ||
			    (flags->precision_set && flags->precision == 0)) {
				prefix[0] = '0';
				prefix_len = 1;
			}
			break;
		}
	}

	if (sign_char) {
		prefix_len = 1;
	}

	if (flags->precision_set) {
		flags->zero_pad =
			false; /* Precision constraint updates disable classical zero-fill configurations */
		if (flags->precision == 0 && num == 0) {
			buffer[0] = '\0';
			len = 0;
		}
	}

	int total_len = (flags->precision > len) ? flags->precision : len;
	total_len += prefix_len;
	if (sign_char)
		total_len++;

	int padding = flags->width - total_len;
	if (padding < 0)
		padding = 0;

	char pad_char = (flags->zero_pad && !flags->left_justify &&
			 !flags->precision_set) ?
				'0' :
				' ';

	if (!flags->left_justify) {
		if (pad_char == '0' && sign_char) {
			putc_wrapper(sign_char);
			sign_char = '\0';
			count++;
		}
		if (pad_char == '0' && prefix_len > 0) {
			puts_wrapper(prefix);
			count += prefix_len;
			prefix_len = 0;
		}
		output_padding(padding, pad_char);
		count += padding;
	}

	if (sign_char) {
		putc_wrapper(sign_char);
		count++;
	}

	if (prefix_len > 0) {
		puts_wrapper(prefix);
		count += prefix_len;
	}

	if (flags->precision > len) {
		output_padding(flags->precision - len, '0');
		count += (flags->precision - len);
	}

	if (len > 0) {
		puts_wrapper(buffer);
		count += len;
	}

	if (flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	return count;
}

static int print_pointer(void *ptr, struct format_flags *flags)
{
	int count = 0;
	char buffer[MAX_NUMBER_LEN];

	utoa((uintptr_t)ptr, buffer, 16, false);

	int len = 0;
	while (buffer[len])
		len++;

	int total_len =
		len +
		2; /* Factoring hex address "0x" layout markers directly */
	int padding = flags->width - total_len;
	if (padding < 0)
		padding = 0;

	if (!flags->left_justify && !flags->zero_pad) {
		output_padding(padding, ' ');
		count += padding;
	}

	puts_wrapper("0x");
	count += 2;

	if (flags->zero_pad && !flags->left_justify) {
		output_padding(padding, '0');
		count += padding;
	}

	puts_wrapper(buffer);
	count += len;

	if (flags->left_justify) {
		output_padding(padding, ' ');
		count += padding;
	}

	return count;
}

/* --- Central Internal Dispatch Matrix Core Logic --- */

/**
 * @brief Standalone Backend Processor executing parsing routines over standard string lists.
 */
int vprintf(const char *format, va_list args)
{
	int count = 0;

	while (*format) {
		if (*format == '%') {
			format++;

			if (*format == '%') {
				putc_wrapper('%');
				count++;
				format++;
				continue;
			}

			struct format_flags flags;
			format = parse_format_specifier(format, &flags);

			switch (flags.specifier) {
			case 'c': {
				char c = (char)va_arg(args, int);
				count += print_formatted_char(c, &flags);
				break;
			}
			case 's': {
				char *str = va_arg(args, char *);
				count += print_formatted_string(str, &flags);
				break;
			}
			case 'd':
			case 'i': {
				int64_t num;
				switch (flags.length_modifier) {
				case 'h':
					num = (short)va_arg(args, int);
					break;
				case 'H':
					num = (char)va_arg(args, int);
					break;
				case 'l':
					num = va_arg(args, long);
					break;
				case 'q':
					num = va_arg(args, long long);
					break;
				case 'z':
					num = va_arg(args, u64);
					break;
				default:
					num = va_arg(args, int);
					break;
				}
				count += print_formatted_integer(num, &flags);
				break;
			}
			case 'u':
			case 'x':
			case 'X':
			case 'o':
			case 'b': {
				u64 num;
				switch (flags.length_modifier) {
				case 'h':
					num = (unsigned short)va_arg(
						args, unsigned int);
					break;
				case 'H':
					num = (unsigned char)va_arg(
						args, unsigned int);
					break;
				case 'l':
					num = va_arg(args, unsigned long);
					break;
				case 'q':
					num = va_arg(args, unsigned long long);
					break;
				case 'z':
					num = va_arg(args, u64);
					break;
				default:
					num = va_arg(args, unsigned int);
					break;
				}
				count += print_formatted_integer((int64_t)num,
								 &flags);
				break;
			}
			case 'p': {
				void *ptr = va_arg(args, void *);
				count += print_pointer(ptr, &flags);
				break;
			}
			default:
				putc_wrapper('%');
				putc_wrapper(flags.specifier);
				count += 2;
				break;
			}
		} else {
			putc_wrapper(*format);
			count++;
			format++;
		}
	}

	return count;
}

/**
 * @brief Public Variadic Entry Point. Passes args forward into internal processing engine.
 */
int serial_debug_serial_printf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = vprintf(format, args);
	va_end(args);
	return count;
}
