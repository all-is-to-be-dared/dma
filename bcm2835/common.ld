__LOAD_ADDR = 32K;
__WORD_ALIGN = 4;
__STACK_ALIGN = 8;
__STACK_SIZE = 640K;

ENTRY(_start)
ASSERT(DEFINED(_start), "_start MUST be defined")

SECTIONS {
  .text __LOAD_ADDR : {
    __prog_start = .;
    __exec_start = .;
    KEEP(*(.text.start))
    *(.text*)
    . = ALIGN(__WORD_ALIGN);
    __exec_end = .;
  }
  .rodata : {
    . = ALIGN(__WORD_ALIGN);
    *(.rodata*)
    . = ALIGN(__WORD_ALIGN);
    __inittab_start = .;
    /* Nothing else in the program references these symbols so we use KEEP to force the linker not
       to GC them*/
    KEEP(*(.inittab*))
    __inittab_end = .;

    . = ALIGN(__WORD_ALIGN);
  }
  .data : {
    . = ALIGN(__WORD_ALIGN);
    *(.data*)
    . = ALIGN(__WORD_ALIGN);
  }

  /* IMPORTANT: The linker/objcopy have strong order dependencies. In particular:

      If there are SHT_NOBITS sections in between any of the loadable sections, then the resulting
      BIN file will have that space filled with zeroes, creating unnecessary bloat.

     Therefore, after this point, all sections MUST be SHT_NOBITS, and before this point, all
     sections MUST NOT be so.
   */
  .dma (TYPE=SHT_NOBITS): {
    . = ALIGN(__WORD_ALIGN);
    __dma_tables_start = .;
    KEEP(*(.dma.tables*))
    __dma_tables_end = .;
    . = ALIGN(__WORD_ALIGN);
  }
  .bss : {
    . = ALIGN(__WORD_ALIGN);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(__WORD_ALIGN);
    __bss_end = .;
  }
  .stack (TYPE=SHT_NOBITS) : {
    . = ALIGN(__STACK_ALIGN);
    __stack_start = .;
    . += __STACK_SIZE;
    . = ALIGN(__STACK_ALIGN);
    __stack_init = .;
    __stack_end = .;
    __prog_end = .;
  }
}
