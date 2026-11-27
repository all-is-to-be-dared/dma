__XIP_BASE = 0x10000000;
__XIP_SIZE = 32M;

__SRAM_BASE = 0x20000000;
__SRAM_SIZE = 512K; /* technically, 520K */

__STACK_SIZE = 32K;
__HEAP_SIZE = 32K;

__WORD_ALIGN = 4;
__STACK_ALIGN = 16;

ENTRY(_start)
ASSERT(DEFINED(_start), "_start MUST be defined")

MEMORY
{
  RAM (rwx)  : ORIGIN = __SRAM_BASE, LENGTH = __SRAM_SIZE
  FLASH (rx) : ORIGIN = __XIP_BASE,  LENGTH = __XIP_SIZE
}

PHDRS
{
  text   PT_LOAD FLAGS(5);
  data   PT_LOAD FLAGS(6);
  bss    PT_LOAD FLAGS(6);
  stack  PT_LOAD FLAGS(6);
}

SECTIONS
{
  .text : {
    . = ALIGN(__WORD_ALIGN);
    KEEP(*(.rp2350.boot_header))
    *(.text*)
    . = ALIGN(__WORD_ALIGN);
    *(.rodata*)
    . = ALIGN(__WORD_ALIGN);
  } >FLASH :text

  __data_start_flash = LOADADDR(.data)
  
  .data : {
    __global_pointer$ = . + 0x800;
    __data_start = .;
    *(.data*)
    *(.sdata*)
    . = ALIGN(__WORD_ALIGN);
    __data_end = .;
  } >RAM AT> FLASH :data
  .bss : {
    . = ALIGN(__WORD_ALIGN);
    __bss_start = .;
    *(*.bss*)
    *(*.sbss*)
    *(COMMON)
    . = ALIGN(__WORD_ALIGN);
    __bss_end = .;
  } >RAM :bss
  .stack (NOLOAD) (TYPE=SHT_NOBITS) : {
    . = ALIGN(__STACK_ALIGN);
    __stack_start = .;
    . += __STACK_SIZE;
    . = ALIGN(__STACK_ALIGN);
    __stack_init = .;
    __stack_end = .;
  } >RAM :stack
}
