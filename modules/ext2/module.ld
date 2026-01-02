SECTIONS
{
    . = 0x0;                 /* Start at address 0 */
    .text : { 
      *(.text._entry) 
      *(.text*) 
    }    /* First thing is .text section */
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
