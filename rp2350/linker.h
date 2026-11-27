#pragma once

#include <stdint.h>

extern volatile uint8_t __bss_start[], __bss_end[];
extern volatile uint8_t __data_start[], __data_end[];
extern volatile uint8_t __data_start_flash[];
extern volatile uint8_t __stack_init[];
