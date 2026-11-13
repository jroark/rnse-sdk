/* A program in the update loader's place.  LOAD is the address of the
 * loader's module header on the unit (given on the command line); the
 * image starts 0x80 bytes after it with the four entry points.  Code,
 * data and stack all stay inside the room the stock loader's image
 * takes, SIZE bytes from LOAD, so that nothing is assumed about the
 * memory around it. */
ENTRY(_start)
SECTIONS
{
    . = LOAD + 0x80;
    .text : { KEEP(*(.entry)) *(.text*) . = ALIGN(0x1000); KEEP(*(.vectors)) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4);
    _image_end = .;
    _bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(4);
    _bss_end = .;
    _stack_top = LOAD + SIZE - 16;
    ASSERT(_bss_end + 0x4000 < _stack_top, "the program does not fit in the loader's place")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
