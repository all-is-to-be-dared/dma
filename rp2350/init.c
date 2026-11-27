//! RP2350 start sequence.

#include <rp2350/linker.h>

[[gnu::naked, gnu::section(".text.start")]]
[[noreturn]]
void _start() {
  __asm__ (
    // disable interrupts
    "csrw mie, zero\n"
    "csrci mstatus, 8\n"

    // clear BSS
    "la t0, %[bss_start]\n"
    "la t1, %[bss_end]\n"
    "1:\n"
    "bgeu t0, t1, 2f\n"
    "sw zero, 0(t0)\n"
    "addi t0, t0, 8\n"
    "j 1b\n"
    "2:\n"

    // load data segment 
    "la t0, %[ramloader_dst]\n"
    "la t1, %[ramloader_dst_end]\n"
    "la t2, %[ramloader_src]\n"
    "3:\n"
    "bgeu t0, t1, 4f\n"
    "lw t3, 0(t2)\n"
    "sw t3, 0(t0)\n"
    "addi t0, t0, 4\n"
    "addi t2, t2, 4\n"
    "j 3b\n"
    "4:\n"

    // initialize stack and frame pointer
    "la sp, %[stack_init]\n"
    "andi sp, sp, -16\n"
    "add fp, sp, zero\n"

    // jump to main
    "j main"
    ::
      [bss_start]"s" (__bss_start),
      [bss_end]"s" (__bss_end),
      [ramloader_dst]"s" (__data_start),
      [ramloader_dst_end]"s" (__data_end),
      [ramloader_src]"s" (__data_start_flash),
      [stack_init]"s" (__stack_init)
  );
}
