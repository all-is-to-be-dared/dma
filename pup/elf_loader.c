#include <stdint.h>
#include <stdlib.h>
#include <elf.h>
#include <string.h>

#include "generic/printf.h"
#include "pup/common.h"
#include "generic/assert.h"
#include "pup/trampoline.h"
#include "pup/protocol.h"

extern uint8_t __prog_start[], __prog_end[];

static bool
validate_elf_header(Elf32_Ehdr *ehdr, bool elf32_p, bool elfle_p, Elf32_Half machine)
{
#define BADELF(s, ...)                                                           \
  do {                                                                           \
    printf(BOOT FUNC("validate_elf_header") ERROR s __VA_OPT__(, ) __VA_ARGS__); \
    return false;                                                                \
  } while (0)

  if (memcmp(ehdr, ELFMAG, SELFMAG))
    BADELF("bad magic number: %hhx%hhx%hhx%hhx, expected '\\x7ELF'\n",
           ehdr->e_ident[0],
           ehdr->e_ident[1],
           ehdr->e_ident[2],
           ehdr->e_ident[3]);
  if (ehdr->e_ident[EI_CLASS] != (elf32_p ? ELFCLASS32 : ELFCLASS64))
    BADELF("ELF image must be 32-bit\n");
  if (ehdr->e_ident[EI_DATA] != (elfle_p ? ELFDATA2LSB : ELFDATA2MSB))
    BADELF("ELF image must be little-endian\n");
  if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
    BADELF("ELF image must have header version %d\n", EV_CURRENT);
  if (ehdr->e_ident[EI_OSABI] != ELFOSABI_SYSV)
    BADELF("ELF image must use System V ABI\n");
  if (ehdr->e_ident[EI_ABIVERSION] != 0)
    BADELF("ELF image must have ABI version 0\n");
  if (ehdr->e_type != ET_EXEC)
    BADELF("ELF image must be executable\n");
  if (ehdr->e_machine != machine)
    BADELF("ELF image must have machine type: %d\n", machine);
  if (ehdr->e_version != EV_CURRENT)
    BADELF("ELF image must have object file version %d\n", EV_CURRENT);

  return true;
#undef BADELF
}

typedef struct
{
  uintptr_t start, end;
} range;

// all segments + new elf location
// INVARIANT: sorted
static range memory_map[ELF_MAX_SEGMENTS + 2];
static size_t range_count;
static bool
claim_space(range r, bool force)
{
  size_t i;

  if (range_count >= ELF_MAX_SEGMENTS && !force)
    return false;

  range_count += 1;
  assert(range_count <= ELF_MAX_SEGMENTS + 2);

  for (i = 0; i < range_count; i++) {
    if (r.start < memory_map[i].start) {
      memmove(memory_map + i + 1, memory_map + i, sizeof(range) * (range_count - i));
      memory_map[i] = r;
      return true;
    }
  }
  memory_map[range_count-1] = r;
  return true;
}
static uintptr_t
allocate_space(size_t size, range allowed, bool force)
{
  uintptr_t p;
  size_t i;

  assert(size);

  p = allowed.start;
  for (i = 0; i < range_count; i++) {
    // if allowed.start>0, then we need to skip forward in the memory map, otherwise the search will
    // short circuit and misbehave
    if (p > memory_map[i].start)
      continue;
    // if p goes to far, quit
    if (p + size > allowed.end)
      return UINTPTR_MAX;

    // correct as long as p<=start, which is ensured above
    if ((memory_map[i].start - p) >= size)
      goto success;
    else
      p = memory_map[i].end;
  }

  // it's possible that even if there are no gaps among registered ranges, there still exists space
  // up in the high end of the allowed range
  if ((allowed.end - p) >= size)
    goto success;

  return UINTPTR_MAX;

success:

  if(!claim_space((range){ p, p + size }, force))
    return UINTPTR_MAX;

  return p;
}

static struct elf_loader_op loader_ops[ELF_MAX_SEGMENTS];
static size_t op_count;
static bool
register_loader_op(enum elf_loader_op_kind kind, uintptr_t dst, uintptr_t src, size_t size)
{
  if (op_count >= ELF_MAX_SEGMENTS)
    return false;

  loader_ops[op_count++] =
    (struct elf_loader_op){ .kind = kind, .dst = dst, .src = src, .size = size };
  return true;
}

static uintptr_t elf_trampoline_ptr;

bool
load_elf_image(uintptr_t img_start,
               uintptr_t img_end,
               bool elf32_p,
               bool elfle_p,
               uint16_t machine,
               uintptr_t mem_hi,
               const char *cmdline)
{
  Elf32_Ehdr *ehdr;
  Elf32_Phdr *phdr;
  size_t i, boot_size, elf_size, cmdline_len;
  uintptr_t boot_alloc, elf_alloc;
  struct elf_trampoline_params *params;


  ehdr = (Elf32_Ehdr *)img_start;

  // -----------------------------------------------------------------------------------------------
  // Validation

  if (!validate_elf_header(ehdr, elf32_p, elfle_p, machine))
    return false;

  // -----------------------------------------------------------------------------------------------
  // Parse program headers (segments) to get loadable segments and build the memory map.
  range_count = 0;
  op_count = 0;

  printf(BOOT FUNC("load_elf_image") "Program header count: %d (size: %dB)\n",
         ehdr->e_phnum,
         ehdr->e_phentsize);
  for (i = 0; i < ehdr->e_phnum; i++) {
    phdr = (Elf32_Phdr *)(img_start + ehdr->e_phoff + i * ehdr->e_phentsize);
    printf(BOOT FUNC("load_elf_image") "T=%d O=%x VA=%x PA=%x FZ=%x MZ=%x F=%x AL=%x\n",
           phdr->p_type,
           phdr->p_offset,
           phdr->p_vaddr,
           phdr->p_paddr,
           phdr->p_filesz,
           phdr->p_memsz,
           phdr->p_flags,
           phdr->p_align);

    if (phdr->p_type == PT_LOAD) {
      claim_space((range){ phdr->p_vaddr, phdr->p_vaddr + phdr->p_memsz }, false);
    }
  }

  // -----------------------------------------------------------------------------------------------
  // Allocate space for the ELF image

  elf_size = img_end - img_start;
  elf_alloc = allocate_space(elf_size, (range){ 0, mem_hi }, true);
  if (elf_alloc == UINTPTR_MAX) {
    printf(BOOT FUNC("load_elf_image") ERROR "no free space for ELF image");
    return false;
  }
  printf(BOOT FUNC("load_elf_image") "ELF allocation: [%p,%p)\n",
         (void *)elf_alloc,
         (void *)elf_alloc + elf_size);
  
  // -----------------------------------------------------------------------------------------------
  // Now that we know the source address for the copies, fill out the loader_ops array

  for (i = 0; i < ehdr->e_phnum; i++) {
    phdr = (Elf32_Phdr *)(img_start + ehdr->e_phoff + i * ehdr->e_phentsize);
    if (phdr->p_type == PT_LOAD) {
      if(phdr->p_filesz > 0) {
        if (!register_loader_op(OP_COPY, phdr->p_vaddr, elf_alloc + phdr->p_offset, phdr->p_filesz)) {
          printf(BOOT FUNC("load_elf_image") ERROR "failed to register loader operation\n");
          return false;
        }
      }
      if(phdr->p_filesz < phdr->p_memsz) {
        if(!register_loader_op(OP_ZERO, phdr->p_vaddr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz)) {
          printf(BOOT FUNC("load_elf_image") ERROR "failed to register loader operation\n");
          return false;
        }
      }
    }
  }

  // -----------------------------------------------------------------------------------------------
  // Calculate the size of the 2nd stage boot image and find space for it.

  cmdline_len = strlen(cmdline);

  boot_size = 0;
  boot_size += (uintptr_t)ELF_TRAMPOLINE_END - (uintptr_t)ELF_TRAMPOLINE_START;
  boot_size += cmdline_len;

  boot_alloc = allocate_space(boot_size, (range){ 0, (uintptr_t)__prog_start }, true);
  if (boot_alloc == UINTPTR_MAX)
    boot_alloc = allocate_space(boot_size, (range){ (uintptr_t)__prog_end, mem_hi }, true);
  if (boot_alloc == UINTPTR_MAX) {
    printf(BOOT FUNC("load_elf_image") ERROR "no free space for boot image\n");
    return false;
  }
  printf(BOOT FUNC("load_elf_image") "boot allocation: [%p,%p)\n",
         (void *)boot_alloc,
         (void *)boot_alloc + boot_size);

  // -----------------------------------------------------------------------------------------------
  // Populate the 2nd stage boot image

  memcpy((void*)boot_alloc, ELF_TRAMPOLINE_START, ELF_TRAMPOLINE_END - ELF_TRAMPOLINE_START);
  elf_trampoline_ptr = boot_alloc;

  params = (struct elf_trampoline_params *)(boot_alloc + ELF_TRAMPOLINE_PARAMS - ELF_TRAMPOLINE_START);
  // printf(BOOT FUNC("load_elf_image") "located parameter block at: %p\n", params);

  memcpy(params->loader_ops, loader_ops, sizeof loader_ops);
  params->loader_op_count = op_count;
  params->elf_src = img_start;
  params->elf_entry = ehdr->e_entry;

  params->boot_args.elf = (void *)elf_alloc;
  params->boot_args.elf_size = elf_size;
  params->boot_args.cmdline_len = cmdline_len;
  memcpy(params->boot_args.cmdline, cmdline, cmdline_len);

  return true;
}

uintptr_t
elf_trampoline() {
  return elf_trampoline_ptr;
}
