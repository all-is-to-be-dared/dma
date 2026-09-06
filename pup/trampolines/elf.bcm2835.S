#include "pup/trampoline.h"

#define _prefetch_flush(reg)    \
    mov reg, #0;                 \
    mcr p15, 0, reg, c7, c5, 4

#define _cln_inv_dcache_entire(reg)     \
    mov reg, #0;                         \
    mcr p15, 0, reg, c7, c14, 0

#define _inv_both_caches_entire(reg)    \
    mov reg, #0;                         \
    mcr p15, 0, reg, c7, c7, 0

#define _btac_flush(reg)        \
    mov reg, #0;                 \
    mcr p15, 0, reg, c7, c5, 6

#define _dsb(reg) \
    mov reg, #0; \
    mcr p15, 0, reg, c7, c10, 4

.globl ELF_TRAMPOLINE_START
.globl ELF_TRAMPOLINE_END
.globl ELF_TRAMPOLINE_PARAMS

@# ===== ALGORITHM =====
@#
@#   1. carry out all (partial) copies that can be completed until convergence.
@#   2. at this point, all remaining copies form cycles, so we methodically run each cycle until
@#      none remain
@#   3. carry out all zero fill operations

ELF_TRAMPOLINE_START:

    @ ----------------------------------------------------------------------------------------------
    @ 0. We're moving a lot of bytes. Therefore, we want to use fast routines. Thus, we need to pull
    @    in the optimized libc mem*** routines. However, this poses a small problem: the main
    @    trampoline code needs to touch the ELF_TRAMPOLINE_PARAMS area, and pc-relative offsets
    @    only work with small offsets. Thus: we insert the mem*** routines at the start, and branch
    @    past them.
    b real_start

@ ASM_ALIAS is used by the includes below. We don't actually want it to do anything though (because
@ it would pollute the symbol namespace), so we neutralize it.
.macro	ASM_ALIAS new old
.endm

@ because the .S files use .global, we need to ensure that we don't create duplicate symbols!
#define __aeabi_memset __elf_trampoline__aeabi_memset
#define __aeabi_memmove __elf_trampoline__aeabi_memmove

@# __aeabi_memset: void *dst, int c, size_t sz
#include "libc/newlib-arm/aeabi_memset-arm.S"
@# __aeabi_memmove: void *dst, void *src, size_t sz
#include "libc/newlib-arm/aeabi_memmove-arm.S"

@ Unfortunately, newlib'c memcpy for armv6 is defined in C, so we have to make our own here:
@# memcpy: void *dst, void *src, size_t sz
@# assumption: (src&3)==(dst&3)
memcpy:
    @ if sz == 0, return
    tst r2, r2
    beq memcpy.done

    @ if sz<16, bytecopy
    cmp r2, #16
    blt memcpy.x1

    @ if (src&3)!=(dst&3), bytecopy
    lsl r4, r1, #29
    teq r4, r0, lsl #29
    bne memcpy.x1

    @ otherwise, align to 4 and start schmovin'
memcpy.pre1:
    tst r0, #3
    beq memcpy.x16
    ldrb r4, [r1], #1
    strb r4, [r1], #1
    sub r2, r2, #1
    b memcpy.pre1

    @ word x4 copier
memcpy.x16:
    lsrs r3, r2, #4
    beq memcpy.x4

memcpy.x16.2:
    ldmia r1!, {r4-r7}
    stmia r0!, {r4-r7}
    subs r3, r3, #1
    bne memcpy.x16.2

    subs r2, r3, lsl #4

    @ word x1 copier
memcpy.x4:
    lsrs r3, r2, #2
    beq memcpy.x1

memcpy.x4.2:
    ldr r4, [r1], #4
    str r4, [r0], #4
    subs r3, r3, #1
    bne memcpy.x4.2

    subs r2, r3, lsl #2

    @ byte copier    
memcpy.x1:
    ldrb r4, [r1], #1
    strb r4, [r0], #1
    subs r2, r2, #1
    bne memcpy.x1

    @ epilogue
memcpy.done:
    bx lr

@ Having a printf equivalent is really tremendously useful
@# printx: const char *s, ...
@#   parameters are passed on the stack, with [sp]=%0, [sp,#4]=%1, etc.
@#   format: %0, %1, %2, ..., %f, %10, etc. are all printed in hex with leading 0x
@#           and %% is literal '%'
@# clobber: r0, r1, r2, r3, r4, r5, r6
printx:
    push {lr}
    mov r3, r0
    mov r5, #0
printx.loop:
    ldrb r0, [r3], #1
    tst r0, r0
    beq printx.done
    teq r0, #'%'
    beq printx.fmt

printx.raw:
    bl putc
    b printx.loop

printx.fmt:
    ldrb r4, [r3], #1

    @ if r4 == '\0', print the %; by decrementing r3, we point it back at the '\0' and printx.loop
    @ will exit for us
    tst r4, r4
    subeq r3, r3, #1
    beq printx.raw

    @ if r4 == '%', print one % and continue
    teq r4, #'%'
    moveq r0, r4
    beq printx.raw

    @ if '0' <= r4 <= '9'
    sub r1, r4, #'0'
    cmp r1, #10
    blo printx.fmt.0_9

    @ if 'a' <= r4 <= 'f'
    sub r1, r4, #'a'
    cmp r1, #6
    blo printx.fmt.a_f

    @ if 'A' <= r4 <= 'F'
    sub r1, r4, #'A'
    cmp r1, #6
    blo printx.fmt.a_f

    @ else print %X where X is the 2nd character
    mov r0, #'%'
    bl putc
    mov r0, r4
    bl putc
    b printx.loop

printx.fmt.2:
    ldrb r4, [r3], #1

    @ if r4 == '\0', print out what's already been accumulated. the step-back in printx.fmt.3.cont
    @ will let printx.loop do the exit
    tst r4, r4
    b printx.fmt.3

    sub r1, r4, #'0'
    cmp r1, #10
    blo printx.fmt.0_9
    sub r1, r4, #'a'
    cmp r1, #6
    blo printx.fmt.a_f
    sub r1, r4, #'A'
    cmp r1, #6
    blo printx.fmt.a_f

    b printx.fmt.3

printx.fmt.0_9:
    lsl r5, r5, #4
    add r5, r5, r1
    b printx.fmt.2
    
printx.fmt.a_f:
    lsl r5, r5, #4
    add r1, r1, #10
    add r5, r5, r1
    b printx.fmt.2

printx.fmt.3:
    add r5, r5, #1 @ we pushed lr
    @ ldr r4, [sp, r5, lsl #2]
    @ mov r4, sp
    @ add r5, r5, #1 @ we pushed lr
    ldr r4, [sp, r5, lsl #2]

    mov r0, #'0'
    bl putc
    mov r0, #'x'
    bl putc

    mov r6, #8
    adr r5, S_PRINT_HEX
printx.fmt.3.loop:
    ldrb r0, [r5, r4, lsr #28]
    bl putc
    lsl r4, r4, #4
    subs r6, r6, #1
    bne printx.fmt.3.loop

printx.fmt.3.cont:
    sub r3, r3, #1
    mov r5, #0
    b printx.loop

printx.done:
    pop {pc}

@ okay because the string length is 16 bytes
S_PRINT_HEX: .ascii "0123456789ABCDEF"

.macro PRINTX_IMPL s:req args:vararg
    adr r0, S_\+
    .ifnb \args
        push { \args }
    .endif
    bl printx
    .ifnb \args
        pop { \args }
    .endif
    b P_\+
    S_\+: .asciz "\s"
    .align 2
    P_\+:
.endm

#define PRINTX(s, ...) PRINTX_IMPL s __VA_ARGS__

@# putc: char c
@# clobber: r1, r2
putc:
    _dsb(r2)
    ldr r1, =muart_base

putc.wait:
    ldr r2, [r1, #muart_stat]
    tst r2, #muart_stat_spav
    beq putc.wait

    str r0, [r1, #muart_io]
    _dsb(r2)
    bx lr

.equ muart_base      , 0x20215040
.equ muart_io        , 0x0
.equ muart_stat      , 0x24
.equ muart_stat_spav , (1 << 1)

@# pwrman_reset:
@# NORETURN
pwrman_reset:
    _dsb(r0)

    ldr r1, =pwrman_base

    @ wdog = cookie | 0x0000f
    mov r0, #pwrman_cookie
    orr r0, r0, #0x0000f
    str r0, [r1, #pwrman_wdog]

    @ rstc = cookie | reset_full
    mov r0, #pwrman_cookie
    orr r0, r0, #pwrman_rstc_full
    str r0, [r1, #pwrman_rstc]

pwrman_reset.loop:
    b pwrman_reset.loop

.equ pwrman_base     , 0x20100000
.equ pwrman_rstc     , 0x1c
.equ pwrman_wdog     , 0x24
.equ pwrman_cookie   , 0x5a000000
.equ pwrman_rstc_full, (1 << 5)

@# panic: const char *s, ...
@# NORETURN
@# parameters identical to `printx`
panic:
    bl printx
    b pwrman_reset

@# cksum: void* p, size_t sz
@# clobbers: r0, r1, r2, r3
cksum:
    @ PRECONDITION: aligned(p, 4)
    @ PRECONDITION: divides(4, sz)
    mov r3, #0
cksum.x4:
    ldr r2, [r0], #4
    add r3, r3, r2
    subs r1, r1, #4
    bne cksum.x4
cksum.done:
    mov r0, r3
    bx lr

stack:
    .skip 256
stack_hi:

@ index, opcode
S_INVALID_OPCODE: .asciz "TRAMPOLINE ERROR: INVALID OPCODE %1 AT INDEX %0\n"

.align 5
real_start:
    @ need our own stack in here
    adr sp, stack_hi

    PRINTX("TRAMPOLINE: entered\n")

    @ ----------------------------------------------------------------------------------------------
    @ 1. Move the ELF

    ldr r0, elf_dst
    ldr r1, elf_src
    ldr r2, elf_size
    bl __aeabi_memmove

    # ldr r1, elf_dst
    # ldr r2, elf_src
    # ldr r3, elf_size
    # PRINTX("TRAMPOLINE: moved ELF from %1:<%2> to %0:<%2>\n", r1, r2, r3)

    @ ----------------------------------------------------------------------------------------------
    @ 2. Run all the operations

    @ PRECONDITION: loader_op_count > 0

    adr r8, loader_ops
    mov r9, #0
    ldr r10, loader_op_count

op_loop:
    cmp r9, r10
    beq post_ops
    
    ldr r11, [r8, #op_kind]
    cmp r11, #OP_COPY
    beq op_loop.copy
    cmp r11, #OP_ZERO
    beq op_loop.zero

    @ if control reaches this point, it's all screwed up
    ldr r0, S_INVALID_OPCODE
    push {r9, r11}
    b panic

op_loop.copy:
    add r8, r8, #op_dst

    # ldmia r8, {r1-r3}
    # PRINTX("TRAMPOLINE: copied %2 bytes from %1 to %0\n", r1, r2, r3)

    ldmia r8!, {r0-r2}
    bl __aeabi_memmove

    add r9, r9, #1
    b op_loop

op_loop.zero:
    add r8, r8, #op_dst

    # ldmia r8, {r1-r3}
    # PRINTX("TRAMPOLINE: zerofilled %2 bytes at %0\n", r1, r2, r3)
    
    ldmia r8!, {r0-r2}
    bl __aeabi_memset

    add r9, r9, #1
    b op_loop

    @ ----------------------------------------------------------------------------------------------
    @ 3. Process state maintenance

post_ops:

    # ldr r1, elf_entry
    # adr r2, elf_boot_args
    # PRINTX("TRAMPOLINE: Performed %2 operations; booting at %0 with args at %1\n", r1, r2, r10)

    @ TODO: this counts as self-modifying code, so we should probably do the whole shebang from the
    @       manual

    _dsb(r0)

    _cln_inv_dcache_entire(r0)
    _inv_both_caches_entire(r0)
    _btac_flush(r0)
    _prefetch_flush(r0)

    _dsb(r0)

    @ ----------------------------------------------------------------------------------------------
    @ 4. Finally, do the actual trampolining

    ldr r9, elf_entry
    adr r0, elf_boot_args

    bx r9

@ Force constant pool to appear here
.ltorg

    
ELF_TRAMPOLINE_PARAMS:

.equ OP_COPY, 1
.equ OP_ZERO, 2
.equ op_kind, 0
.equ op_dst, 4
.equ op_src, 8
.equ op_size, 12

params:
loader_ops: .skip (ELF_MAX_SEGMENTS * 16), 0
loader_op_count: .word 4
elf_src: .word 0
elf_entry: .word 0
    elf_boot_args:
elf_dst: .word 0
elf_size: .word 0
cmdline_len: .word 4
cmdline:

@ WARNING: DO NOT PUT ANYTHING BETWEEN `cmdline` AND ELF_TRAMPOLINE_END

ELF_TRAMPOLINE_END:
