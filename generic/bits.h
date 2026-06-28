#pragma once

#define COMPILER_BARRIER do { __asm__ volatile ("" ::: "memory"); } while(0)
