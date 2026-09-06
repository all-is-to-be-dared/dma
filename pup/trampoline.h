#pragma once




// -------------------------------------------------------------------------------------------------
// FLAT BINARY TRAMPOLINES




#define TRAMPOLINE_START __trampoline_start
#define TRAMPOLINE_END __trampoline_end

#if !__ASSEMBLER__

#include <stdint.h>

extern uint8_t TRAMPOLINE_START[];
extern uint8_t TRAMPOLINE_END[];

#endif




// -------------------------------------------------------------------------------------------------
// ELF TRAMPOLINES




#define ELF_RELOCATION_BUFFER_SIZE 4096
#define ELF_MAX_SEGMENTS 16

#define ELF_TRAMPOLINE_START __elf_trampoline_start
#define ELF_TRAMPOLINE_END __elf_trampoline_end
#define ELF_TRAMPOLINE_PARAMS __elf_trampoline_params

#if !__ASSEMBLER__

#include <pup/protocol.h>

enum elf_loader_op_kind : uint32_t
{
  OP_COPY = 1,
  OP_ZERO = 2,
};
struct elf_loader_op
{
  enum elf_loader_op_kind kind;
  // if kind==OP_ZERO, then the low byte of 'src' is the fill character
  uintptr_t dst, src;
  size_t size;
};

struct elf_trampoline_params {
  struct elf_loader_op loader_ops[ELF_MAX_SEGMENTS];
  size_t loader_op_count;
  uintptr_t elf_src;
  uintptr_t elf_entry;

  struct elf_boot_args boot_args;
};

extern uint8_t ELF_TRAMPOLINE_PARAMS[];
extern uint8_t ELF_TRAMPOLINE_START[];
extern uint8_t ELF_TRAMPOLINE_END[];

#endif
