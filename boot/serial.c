#include "serial.h"

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

// Forward declaration - you provide this
void uart_putc(char c);

// Local buffer for number conversions
#define MAX_NUMBER_LEN 32

// Helper: Output a character
static void putc_wrapper(char c) {
    uart_putc(c);
}

// Helper: Output a string
static void puts_wrapper(const char *str) {
    while (*str) {
        putc_wrapper(*str++);
    }
}

// Convert unsigned integer to string with base
static char *utoa(uint64_t num, char *str, int base, BOOLEAN uppercase) {
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    
    if (num == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }
    
    // Convert number
    while (num) {
        uint64_t remainder = num % base;
        *ptr++ = (remainder < 10) ? remainder + '0' : 
                (uppercase ? remainder - 10 + 'A' : remainder - 10 + 'a');
        num /= base;
    }
    
    // Terminate string
    *ptr-- = '\0';
    
    // Reverse string
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

// Convert signed integer to string with base
static char *itoa(int64_t num, char *str, int base, BOOLEAN uppercase) {
    char *ptr = str;
    
    if (num == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }
    
    // Handle negative numbers
    BOOLEAN negative = FALSE;
    if (num < 0 && base == 10) {
        negative = TRUE;
        num = -num;
    }
    
    // Convert using unsigned function
    utoa((uint64_t)num, ptr, base, uppercase);
    
    // Add minus sign if needed
    if (negative) {
        // Shift string right and add '-'
        char *p = ptr;
        while (*p) p++;
        while (p >= ptr) {
            *(p + 1) = *p;
            p--;
        }
        *ptr = '-';
    }
    
    return str;
}

// Format flags
typedef struct {
    BOOLEAN left_justify;     // '-'
    BOOLEAN force_sign;       // '+'
    BOOLEAN space_sign;       // ' '
    BOOLEAN alternate_form;   // '#'
    BOOLEAN zero_pad;         // '0'
    BOOLEAN precision_set;    // '.' was specified
    int width;
    int precision;
    char length_modifier;  // 'h', 'l', 'L', etc.
    char specifier;        // 'd', 'x', 's', etc.
} format_flags;

// Parse format specifier
static const char *parse_format_specifier(const char *format, format_flags *flags) {
    // Reset flags
    *flags = (format_flags){0};
    flags->precision = -1;  // -1 means not specified
    
    // Parse flags
    while (1) {
        switch (*format) {
            case '-': flags->left_justify = TRUE; break;
            case '+': flags->force_sign = TRUE; break;
            case ' ': flags->space_sign = TRUE; break;
            case '#': flags->alternate_form = TRUE; break;
            case '0': flags->zero_pad = TRUE; break;
            default: goto parse_width;
        }
        format++;
    }
    
parse_width:
    // Parse width
    if (*format >= '0' && *format <= '9') {
        flags->width = 0;
        while (*format >= '0' && *format <= '9') {
            flags->width = flags->width * 10 + (*format - '0');
            format++;
        }
    }
    
    // Parse precision
    if (*format == '.') {
        flags->precision_set = TRUE;
        flags->precision = 0;
        format++;
        
        if (*format >= '0' && *format <= '9') {
            flags->precision = 0;
            while (*format >= '0' && *format <= '9') {
                flags->precision = flags->precision * 10 + (*format - '0');
                format++;
            }
        }
    }
    
    // Parse length modifier
    switch (*format) {
        case 'h':
            if (*(format + 1) == 'h') {
                flags->length_modifier = 'H';  // 'hh'
                format += 2;
            } else {
                flags->length_modifier = 'h';
                format++;
            }
            break;
        case 'l':
            if (*(format + 1) == 'l') {
                flags->length_modifier = 'q';  // 'll'
                format += 2;
            } else {
                flags->length_modifier = 'l';
                format++;
            }
            break;
        case 'L':
            flags->length_modifier = 'L';
            format++;
            break;
        case 'z':
            flags->length_modifier = 'z';
            format++;
            break;
        case 't':
            flags->length_modifier = 't';
            format++;
            break;
        case 'j':
            flags->length_modifier = 'j';
            format++;
            break;
    }
    
    // Parse specifier
    flags->specifier = *format;
    
    return format + 1;
}

// Output padding
static void output_padding(int count, char pad_char) {
    while (count-- > 0) {
        putc_wrapper(pad_char);
    }
}

// Print string with formatting
static int print_formatted_string(const char *str, format_flags *flags) {
    int count = 0;
    
    if (!str) {
        str = "(null)";
    }
    
    // Calculate string length
    int len = 0;
    const char *p = str;
    while (*p++) len++;
    
    // Apply precision
    if (flags->precision_set && flags->precision < len) {
        len = flags->precision;
    }
    
    // Calculate padding
    int padding = flags->width - len;
    if (padding < 0) padding = 0;
    
    // Right justify (pad left)
    if (!flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    // Output string
    for (int i = 0; i < len; i++) {
        putc_wrapper(str[i]);
        count++;
    }
    
    // Left justify (pad right)
    if (flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    return count;
}

// Print character with formatting
static int print_formatted_char(char c, format_flags *flags) {
    int count = 0;
    
    // Calculate padding
    int padding = flags->width - 1;
    if (padding < 0) padding = 0;
    
    // Right justify (pad left)
    if (!flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    // Output character
    putc_wrapper(c);
    count++;
    
    // Left justify (pad right)
    if (flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    return count;
}

// Print integer with formatting
static int print_formatted_integer(int64_t num, format_flags *flags) {
    int count = 0;
    char buffer[MAX_NUMBER_LEN];
    
    // Get base
    int base = 10;
    BOOLEAN uppercase = FALSE;
    
    switch (flags->specifier) {
        case 'd':
        case 'i':
        case 'u':
            base = 10;
            break;
        case 'x':
            base = 16;
            uppercase = FALSE;
            break;
        case 'X':
            base = 16;
            uppercase = TRUE;
            break;
        case 'o':
            base = 8;
            break;
        case 'b':
            base = 2;
            break;
    }
    
    // Handle sign
    BOOLEAN is_negative = FALSE;
    char sign_char = '\0';
    
    if (flags->specifier == 'd' || flags->specifier == 'i') {
        if (num < 0) {
            is_negative = TRUE;
            num = -num;
            sign_char = '-';
        } else if (flags->force_sign) {
            sign_char = '+';
        } else if (flags->space_sign) {
            sign_char = ' ';
        }
    }
    
    // Convert to string
    if (flags->specifier == 'u') {
        utoa((uint64_t)num, buffer, base, uppercase);
    } else {
        itoa(num, buffer, base, uppercase);
    }
    
    // Calculate length
    int len = 0;
    while (buffer[len]) len++;
    
    // Handle alternate form
    int prefix_len = 0;
    char prefix[4] = {0};
    
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
            case 'o':
                // Octal prefix only if number is not zero
                if (num != 0 || (flags->precision_set && flags->precision == 0)) {
                    prefix[0] = '0';
                    prefix_len = 1;
                }
                break;
            case 'b':
                prefix[0] = '0';
                prefix[1] = 'b';
                prefix_len = 2;
                break;
        }
    }
    
    // Sign takes space too
    if (sign_char) {
        prefix_len = 1;  // Override, sign is separate
    }
    
    // Apply precision for integers
    if (flags->precision_set) {
        flags->zero_pad = FALSE;  // Precision overrides zero padding
        
        // Special case: precision 0 and number is 0 means print nothing
        if (flags->precision == 0 && num == 0) {
            buffer[0] = '\0';
            len = 0;
        }
    }
    
    // Calculate total length
    int total_len = len;
    if (flags->precision > len) {
        total_len = flags->precision;
    }
    total_len += prefix_len;
    if (sign_char) total_len++;
    
    // Calculate padding
    int padding = flags->width - total_len;
    if (padding < 0) padding = 0;
    
    // Determine pad character
    char pad_char = ' ';
    if (flags->zero_pad && !flags->left_justify && !flags->precision_set) {
        pad_char = '0';
    }
    
    // Right justify (pad left)
    if (!flags->left_justify) {
        if (pad_char == '0' && sign_char) {
            putc_wrapper(sign_char);
            sign_char = '\0';  // Already printed
            count++;
        }
        
        if (pad_char == '0' && prefix_len > 0) {
            puts_wrapper(prefix);
            count += prefix_len;
            prefix_len = 0;  // Already printed
        }
        
        output_padding(padding, pad_char);
        count += padding;
    }
    
    // Print sign (if not already printed)
    if (sign_char) {
        putc_wrapper(sign_char);
        count++;
    }
    
    // Print prefix (if not already printed)
    if (prefix_len > 0) {
        puts_wrapper(prefix);
        count += prefix_len;
    }
    
    // Print leading zeros from precision
    if (flags->precision > len) {
        output_padding(flags->precision - len, '0');
        count += (flags->precision - len);
    }
    
    // Print the number
    if (len > 0) {
        puts_wrapper(buffer);
        count += len;
    }
    
    // Left justify (pad right)
    if (flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    return count;
}

// Print pointer
static int print_pointer(void *ptr, format_flags *flags) {
    int count = 0;
    char buffer[MAX_NUMBER_LEN];
    
    // Convert pointer to hex
    utoa((uintptr_t)ptr, buffer, 16, FALSE);
    
    // Always add 0x prefix for pointers
    int len = 0;
    while (buffer[len]) len++;
    
    // Calculate padding
    int total_len = len + 2;  // +2 for "0x"
    int padding = flags->width - total_len;
    if (padding < 0) padding = 0;
    
    // Right justify (pad left)
    if (!flags->left_justify && !flags->zero_pad) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    // Print 0x prefix
    puts_wrapper("0x");
    count += 2;
    
    // Pad with zeros if needed
    if (flags->zero_pad && !flags->left_justify) {
        output_padding(padding, '0');
        count += padding;
    }
    
    // Print pointer value
    puts_wrapper(buffer);
    count += len;
    
    // Left justify (pad right)
    if (flags->left_justify) {
        output_padding(padding, ' ');
        count += padding;
    }
    
    return count;
}

int vprintf(const char *format, va_list args) {
    int count = 0;
    
    while (*format) {
        if (*format == '%') {
            format++;
            
            // Handle literal '%'
            if (*format == '%') {
                putc_wrapper('%');
                count++;
                format++;
                continue;
            }
            
            // Parse format specifier
            format_flags flags;
            format = parse_format_specifier(format, &flags);
            
            // Process based on specifier
            switch (flags.specifier) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    count += print_formatted_char(c, &flags);
                    break;
                }
                
                case 's': {
                    char *str = va_arg(args, char*);
                    count += print_formatted_string(str, &flags);
                    break;
                }
                
                case 'd':
                case 'i': {
                    int64_t num;
                    
                    // Handle different integer sizes
                    switch (flags.length_modifier) {
                        case 'h':  // short
                            num = (short)va_arg(args, int);
                            break;
                        case 'H':  // char (hh)
                            num = (char)va_arg(args, int);
                            break;
                        case 'l':  // long
                            num = va_arg(args, long);
                            break;
                        case 'q':  // long long (ll)
                            num = va_arg(args, long long);
                            break;
                        case 'z':  // size_t
                            num = va_arg(args, size_t);
                            break;
                        default:  // int
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
                    uint64_t num;
                    
                    // Handle different unsigned integer sizes
                    switch (flags.length_modifier) {
                        case 'h':  // unsigned short
                            num = (unsigned short)va_arg(args, unsigned int);
                            break;
                        case 'H':  // unsigned char (hh)
                            num = (unsigned char)va_arg(args, unsigned int);
                            break;
                        case 'l':  // unsigned long
                            num = va_arg(args, unsigned long);
                            break;
                        case 'q':  // unsigned long long (ll)
                            num = va_arg(args, unsigned long long);
                            break;
                        case 'z':  // size_t
                            num = va_arg(args, size_t);
                            break;
                        default:  // unsigned int
                            num = va_arg(args, unsigned int);
                            break;
                    }
                    
                    count += print_formatted_integer((int64_t)num, &flags);
                    break;
                }
                
                case 'p': {
                    void *ptr = va_arg(args, void*);
                    count += print_pointer(ptr, &flags);
                    break;
                }
                
                // Add more specifiers as needed...
                
                default:
                    // Unknown specifier, just print as-is
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

int serial_debug_serial_printf(char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vprintf(format, args);
    va_end(args);
    return count;
}

void serial_debug_start()
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

  serial_debug_serial_printf("UART Serial Out Initialized!\n");
}

