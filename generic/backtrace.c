#include <elf.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>
#include <generic/printf.h>
#include <inttypes.h>
#include <string.h>

struct trace32_sections {
  void *image_base;
  Elf32_Shdr *symtab, *strtab;
};
static bool find_trace32_sections(void *elf_image_base,
                                  struct trace32_sections *sections) {
  Elf32_Ehdr *ehdr;
  Elf32_Shdr *shdr, *shdr_base;
  Elf32_Word shidx, shnum;

  sections->image_base = elf_image_base;
  sections->symtab = nullptr;
  sections->strtab = nullptr;

  ehdr = elf_image_base;
  if (!ehdr->e_shoff)
    return false;

  shdr = shdr_base = elf_image_base + ehdr->e_shoff;

  shnum = ehdr->e_shnum;
  if (!shnum)
    shnum = shdr->sh_size;

  assert(ehdr->e_shentsize == sizeof(Elf32_Shdr));

  shidx = 0;
  while (shidx < shnum) {
    if (shdr->sh_type == SHT_SYMTAB) {
      sections->symtab = shdr;
      sections->strtab = shdr_base + shdr->sh_link;
      if (sections->strtab->sh_type != SHT_STRTAB)
        return false;
      return true;
    }
    shdr++;
    shidx++;
  }

  // couldn't find SHT_SYMTAB
  return false;
}
[[gnu::used]]
static void dump_symbol_table_elf32(struct trace32_sections *sections) {
  Elf32_Sym *sym;
  const char *strtab, *pub, *typs, *binds;
  uint32_t i, cnt, bind, typ;

  strtab = sections->image_base + sections->strtab->sh_offset;

  sym = sections->image_base + sections->symtab->sh_offset;
  assert(sections->symtab->sh_entsize,
         "SHT_SYMTAB should be a table of fixed-size entries\n");
  assert(sizeof *sym == sections->symtab->sh_entsize,
         "SHT_SYMTAB entries have unexpected size\n");

  assert((sections->symtab->sh_size % sections->symtab->sh_entsize) == 0,
         "SHT_SYMTAB size is not a multiple of %d\n", sizeof *sym);
  cnt = sections->symtab->sh_size / sections->symtab->sh_entsize;

  const char *VIS[] = {
      [STV_DEFAULT] = "Def",
      [STV_INTERNAL] = "Int",
      [STV_HIDDEN] = "Hid",
      [STV_PROTECTED] = "Prt",
  };
  const char *BIND[] = {
      [STB_LOCAL] = "Loc",  [STB_GLOBAL] = "Glo",     [STB_WEAK] = "Wk.",
      [STB_NUM] = "Num", // ?
      [STB_LOOS] = "Os0",   [STB_LOOS + 1] = "Os1",   [STB_LOOS + 2] = "Os2",
      [STB_LOPROC] = "Pr0", [STB_LOPROC + 1] = "Pr1", [STB_LOPROC + 2] = "Pr2",
  };
  const char *TYP[] = {
      [STT_NOTYPE] = "NoTy",     [STT_OBJECT] = "Obj.",
      [STT_FUNC] = "Func",       [STT_SECTION] = "Sect",
      [STT_FILE] = "File",       [STT_COMMON] = "Com.",
      [STT_TLS] = "TLS.",        [STT_NUM] = "NUM?",
      [STT_LOOS] = "Os0+",       [STT_LOOS + 1] = "Os1+",
      [STT_LOOS + 2] = "Os2+",   [STT_LOPROC] = "Prc0",
      [STT_LOPROC + 1] = "Prc1", [STT_LOPROC + 2] = "Prc2",
  };

  for (i = 0; i < cnt; i++) {
    pub = VIS[ELF32_ST_VISIBILITY(sym->st_other)];
    bind = ELF32_ST_BIND(sym->st_info);
    binds = BIND[bind];
    typ = ELF32_ST_TYPE(sym->st_info);
    typs = TYP[typ];
    printf("Symbol #%d: %s (%s) %s '%s' = %p <%zu>\n", i, pub, binds, typs,
           sym->st_name ? strtab + sym->st_name : "<UNNAMED>",
           (void *)sym->st_value, sym->st_size);
    sym += 1;
  }
}
static const char *backtrace_get_symbol_elf32(struct trace32_sections *sections,
                                              uintptr_t pc) {
  Elf32_Sym *sym;
  const char *strtab;
  uint32_t i, cnt;

  strtab = sections->image_base + sections->strtab->sh_offset;

  sym = sections->image_base + sections->symtab->sh_offset;
  assert(sections->symtab->sh_entsize,
         "SHT_SYMTAB should be a table of fixed-size entries\n");
  assert(sizeof *sym == sections->symtab->sh_entsize,
         "SHT_SYMTAB entries have unexpected size\n");

  assert((sections->symtab->sh_size % sections->symtab->sh_entsize) == 0,
         "SHT_SYMTAB size is not a multiple of %d\n", sizeof *sym);
  cnt = sections->symtab->sh_size / sections->symtab->sh_entsize;

  for (i = 0; i < cnt; i++, sym++) {
    if (ELF32_ST_TYPE(sym->st_info) != STT_FUNC)
      continue;

    if (pc >= sym->st_value && pc < (sym->st_value + sym->st_size))
      return sym->st_name ? strtab + sym->st_name : "<unnamed>";
  }

  return nullptr;
}

union {
  struct trace32_sections trace32_sections;
} trace_info;
enum trace_cap { TCAP_NONE = 0, TCAP_ELF32 = 1 };
static enum trace_cap trace_cap = TCAP_NONE;

bool backtrace_enable(void *elf_image_base) {
  Elf32_Ehdr *ehdr;
  bool has_frame_pointer;

  ehdr = elf_image_base;
  trace_cap = TCAP_NONE;

  // Currently, the only backtrace supported is the frame-pointer based one that
  // works off the symbol table. However, this only works if the compiler has
  // been told not to omit frame pointer codegen. Thus: we check that
  // frame-pointer is not omitted!
  if (strstr(_CFLAGS_, "-fno-omit-frame-pointer")) {
    has_frame_pointer = true;
  } else if (strstr(_CFLAGS_, "-fomit-frame-pointer")) {
    has_frame_pointer = false;
  } else {
    has_frame_pointer = __OPTIMIZE__ > 0;
  }

  if (ehdr->e_ident[EI_CLASS] == ELFCLASS32) {
    if (has_frame_pointer)
      if (find_trace32_sections(elf_image_base, &trace_info.trace32_sections))
        trace_cap = TCAP_ELF32;
  }

  return trace_cap != TCAP_NONE;
}

bool backtrace_enabled() { return trace_cap != TCAP_NONE; }

// -------------------------------------------------------------------------------------------------
// ARCHITECTURE-SPECIFIC INTERNALS

// -------------------------------------------------------------------------------------------------
// ARCHITECTURE : ARMv6

#if __ARM_ARCH == 6

struct stack_frame {
  uint32_t fp;
  uint32_t lr;
};

[[gnu::noinline]]
backtrace_cursor backtrace_cursor_new() {
  // gets:
  //  - address of instruction calling this function
  //  - pointer to the stack frame owned by the CALLING function

  uintptr_t ra;
  struct stack_frame *sframe;

  ra =
      (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)) - 4;
  sframe = __builtin_frame_address(0);
  return (backtrace_cursor){.pc = ra, .fp = sframe->fp};
}

void backtrace_cursor_read(backtrace_cursor *curs, uintptr_t *pc,
                           const char **sym) {
  struct trace_sections;

  if (pc)
    *pc = curs->pc;

  if (!sym)
    return;

  switch (trace_cap) {
  case TCAP_ELF32:
    *sym = backtrace_get_symbol_elf32(&trace_info.trace32_sections, curs->pc);
    break;

  case TCAP_NONE:
  default:
    *sym = nullptr;
    break;
  }
}

bool backtrace_cursor_next(backtrace_cursor *curs) {
  struct stack_frame *sframe;

  if (!curs->fp)
    // FP=0 marks _start
    return false;

  sframe = (struct stack_frame *)curs->fp;

  curs->pc = sframe->lr - 4;
  curs->fp = sframe->fp;

  return true;
}

#else
#error "Unsupported architecture for backtrace functionality"
#endif
