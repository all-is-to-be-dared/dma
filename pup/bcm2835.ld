__LOAD_ADDR = 32K;
__WORD_ALIGN = 4;
__STACK_ALIGN = 8;
__STACK_SIZE = 32K;

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
  }
  .data : {
    . = ALIGN(__WORD_ALIGN);
    *(.data*)
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
