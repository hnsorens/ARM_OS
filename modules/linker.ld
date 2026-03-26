SECTIONS
{
    . = 0x1000;
    .init_data : {
        . = ALIGN(8);
        KEEP(*(.entry_point))
        __init_start = .;
        KEEP(*(.init_array))
        __init_end = .;
    }
    
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
