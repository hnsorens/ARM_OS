SECTIONS
{
    . = 0x0;                 /* Start at address 0 */

    
    .text : { 
      *(.text._entry) 
      *(.text*) 
    }    /* First thing is .text section */
    .vectors : ALIGN(2048) {
        *(.vectors)
    }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
