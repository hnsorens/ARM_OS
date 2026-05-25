provider_entry = DEFINED(_start) ? _start : (DEFINED(main) ? main : 0);
ENTRY(provider_entry)

SECTIONS
{
    . = 1M;

    _kernel_virtual_base = 0;

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

    .data ALIGN(4K) : AT(ADDR(.data) - _kernel_virtual_base)
    {
        _data = .;
        *(COMMON)
        *(.bss)
        . = ALIGN(4K);
    }

    kernel_end = .;
}
