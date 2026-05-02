
ENTRY(_start)

SECTIONS
{
    . = 1M;

    _kernel_virtual_base = 0xFFFF800000000000;

    .text (_kernel_virtual_base + .) : AT(ADDR(.text) - _kernel_virtual_base)
    {
        _code = .;
        *(.multiboot) /* Ensure 32-bit entry header is here if using Multiboot */
        *(.text)
        . = ALIGN(4K);
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - _kernel_virtual_base)
    {
        _rodata = .;
        *(.rodata*)
        . = ALIGN(4K);
    }

    . = ALIGN(4K);
    .k_vtables : {
        __vtable_all_start = .;
        KEEP(*(.k_vtables.all))
        . = ALIGN(8);
        __vtable_all_end = .;
     }

    .data ALIGN(4K) : AT(ADDR(.data) - _kernel_virtual_base)
    {
        _data = .;
        *(COMMON)
        *(.bss)
        . = ALIGN(4K);
    }

    kernel_end = .;
}
