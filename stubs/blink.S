.arm

.globl _start

;@ note: slightly broken: the program this is based on was built for an LED on 0, not 47, so the
;@       FSEL code is completely fucked here

_start:
        mov r0, #(1 << 15)
        mov r1, #0x20000000
        str r0, [r1, +r1, asr #8]!
.L0:
        subs r2, r2, #(1 << 9)
        strmi r0, [r1, #0x20]
        strpl r0, [r1, #0x2c]
        b .L0
