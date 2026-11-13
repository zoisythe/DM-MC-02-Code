ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
    SRAM4 (rw) : ORIGIN = 0x38000000, LENGTH = 16K
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : {
        . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
    } > DTCM AT > FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > DTCM
    .dma_sram4 (NOLOAD) : ALIGN(4) { *(.dma_sram4) } > SRAM4
    /DISCARD/ : { *(.comment*) *(.note*) }
    ASSERT(_ebss < _estack - 4096, "Insufficient stack space")
}
