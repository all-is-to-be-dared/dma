#pragma once

#define TRAMPOLINE_START __trampoline_start
#define TRAMPOLINE_END __trampoline_end

#if !__ASSEMBLER__

#include <stdint.h>

extern uint8_t TRAMPOLINE_START[];
extern uint8_t TRAMPOLINE_END[];

#endif
