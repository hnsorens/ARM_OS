SECTIONS
{
    . = 0x80000; /* Example load address for aarch64 */
    
    .text : { *(.text) }

    .init_data : {
        . = ALIGN(8);
        __init_start = .;
        KEEP(*(.init_array))
        __init_end = .;
    }
    
    .data : { *(.data) }
    .bss : { *(.bss) }
}
