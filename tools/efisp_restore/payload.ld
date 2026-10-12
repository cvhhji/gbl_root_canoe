ENTRY(efisp_entry)
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "Payload must not contain writable globals")
