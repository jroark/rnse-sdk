ENTRY(app_init)
SECTIONS {
 . = 0xac410040;
 .text : { *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 . = ALIGN(4); _app_image_end = .;
 .bss : { *(.bss*) *(COMMON) }
 _app_bss_end = .;
 ASSERT(. <= 0xac490000, "app arena exceeded")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
