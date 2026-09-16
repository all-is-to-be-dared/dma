//! BCM2835 start sequence.

[[gnu::naked, gnu::section(".text.start")]]
[[noreturn]]
void _start() {
  __asm__ (
    "mrs r1, cpsr\n"
    "and r1, r1, #(~0x1f)\n"
    "orr r1, r1, #0x13\n"
    "orr r1, r1, #((1 << 7) | (1 << 6))\n"
    "msr cpsr, r1\n"

    "mov r1, #0\n"
    "mcr p15, 0, r1, c7, c5, 4\n"

    "mov r3, #0\n"
    "ldr r1, =__bss_start\n"
    "ldr r2, =__bss_end\n"
    "subs r2, r2, r1\n"
    "beq 3f\n"
    "2:\n"
    "strb r3, [r1], #1\n"
    "subs r2, r2, #1\n"
    "bne 2b\n"
    "3:\n"

    "ldr sp, =__stack_init\n"
    "and sp, sp, #-16\n"
    "mov fp, #0\n"
    "bl main\n"
    "bl pwrman_reset\n"
  );
}
