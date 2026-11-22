/* side193: a short piece in the update loader's place (what the stock
 * software calls), and the resident it copies to RAM the stock
 * software leaves alone (docs/side193.md), linked for that address and
 * kept in the image right behind the first piece. */
ENTRY(_entry)
RES = 0x8bff0000;
RES_SIZE = 0x10000;
SECTIONS
{
    _load = LOAD;
    . = LOAD + 0x80;
    .inject : { KEEP(*(.entry)) *(.inject*) . = ALIGN(4); }
    _res_load = .;
    .res RES : AT(_res_load) {
        _res_start = .;
        KEEP(*(.res.head))
        *(.text*) *(.rodata*) *(.data*)
        . = ALIGN(4);
        _res_end = .;
    }
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); _bss_end = .; }
    _res_top = RES + RES_SIZE;
    ASSERT(_bss_end <= _res_top, "the resident does not fit in its place")
    ASSERT(_res_load + (_res_end - _res_start) < LOAD + SIZE, "the image does not fit in the loader's place")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
