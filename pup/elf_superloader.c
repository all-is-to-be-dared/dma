#include <stdint.h>
#include <stdlib.h>
#include <elf.h>
#include <string.h>

#include "generic/printf.h"
#include "pup/common.h"
#include "generic/math.h"
#include "generic/assert.h"
#include "pup/trampoline.h"
#include "pup/protocol.h"

extern uint8_t __prog_start[], __prog_end[];

struct copy
{
  uintptr_t dst, src;
  size_t size;
};
struct zero_fill
{
  uintptr_t dst;
  size_t size;
};

static struct copy copies[ELF_MAX_SEGMENTS * 2];
static size_t dst_map[ELF_MAX_SEGMENTS * 2];
static size_t src_map[ELF_MAX_SEGMENTS * 2];
static size_t copy_count;

static struct zero_fill zero_fills[ELF_MAX_SEGMENTS];
static size_t zero_fill_count;

static bool
register_zero_fill(uintptr_t dst, size_t size)
{
  if (zero_fill_count >= ELF_MAX_SEGMENTS)
    return false;

  zero_fills[zero_fill_count++] = (struct zero_fill){ .dst = dst, .size = size };
  return true;
}

static bool
register_copy(uintptr_t dst, uintptr_t src, size_t size)
{
  size_t i, j;
  uintptr_t dst_end, src_end;
  uintptr_t cp_dst_start, cp_dst_end, cp_src_start, cp_src_end;

  dst_end = dst + size;
  src_end = src + size;

  if (copy_count >= ELF_MAX_SEGMENTS * 2) {
    printf(BOOT FUNC("register_copy") ERROR
           "can't insert copy [%p,%p)->[%p,%p) in memory map: too many segments",
           src,
           src_end,
           dst,
           dst_end);
    return false;
  }

  copies[copy_count].dst = dst;
  copies[copy_count].src = src;
  copies[copy_count].size = size;

  for (i = 0; i < copy_count; i++) {
    j = dst_map[i];
    cp_src_start = copies[j].src;
    cp_src_end = cp_src_start + copies[j].size;
    cp_dst_start = copies[j].dst;
    cp_dst_end = cp_dst_start + copies[j].size;
    if (dst < cp_dst_start) {
      if (dst_end <= cp_dst_start) {
        memmove(dst_map + i + 1, dst_map + i, (copy_count - i) * sizeof *dst_map);
        dst_map[i] = copy_count;
        break;
      }
    } else if (dst >= cp_dst_end)
      continue;

    goto insertion_failed;
  }

  for (i = 0; i < copy_count; i++) {
    j = src_map[i];
    cp_src_start = copies[j].src;
    cp_src_end = cp_src_start + copies[j].size;
    cp_dst_start = copies[j].dst;
    cp_dst_end = cp_dst_start + copies[j].size;
    if (src < cp_src_start) {
      if (src_end <= cp_src_start) {
        memmove(src_map + i + 1, src_map + i, (copy_count - i) * sizeof *src_map);
        src_map[i] = copy_count;
        break;
      }
    } else if (src >= cp_src_end)
      continue;

    goto insertion_failed;
  }

  copy_count += 1;
  return true;

insertion_failed:
  printf(BOOT FUNC("register_copy") ERROR "can't register copy [%p,%p)->[%p,%p) in memory map: "
                                          "overlaps previously inserted copy [%p,%p)->[%p,%p)\n",
         src,
         src_end,
         dst,
         dst_end,
         cp_src_start,
         cp_src_end,
         cp_dst_start,
         cp_dst_end);
  return false;
}

static void
remove_copy(size_t copy_idx)
{
  size_t i;
  assert(copy_idx < copy_count);
  memmove(copies + copy_idx, copies + copy_idx + 1, sizeof(struct copy) * (copy_count - copy_idx - 1));
  for (i = 0; i < copy_count; i++) {
    if (src_map[i] > copy_idx)
      src_map[i - 1] = src_map[i] - 1;
    if (dst_map[i] > copy_idx)
      dst_map[i - 1] = dst_map[i] - 1;
  }
  copy_count -= 1;
}

static void dump_copies(void)
{
  size_t i;
  printf(BOOT FUNC("dump_copies") "================ COPIES =================\n");
  for(i = 0;i < copy_count;i++) {
    printf(BOOT FUNC("dump_copies") "\t#%zu : [%p,%p)->[%p,%p)\n",
      i,
      copies[i].src,copies[i].src+copies[i].size,
      copies[i].dst,copies[i].dst+copies[i].size);
  }
}

enum
{
  FREE_DST = 1,
  FREE_SRC = 2,
  FREE_BOTH = 3,
};

static uintptr_t
find_free_space(size_t size, uintptr_t lo, uintptr_t hi, uint8_t mode)
{
  // This is a really stupid O(n^2) implementation.
  //
  // It's possible to do O(n) because src_map and dst_map are sorted, but the algorithm is a lot
  // more finicky.

  uintptr_t p, q, r, s;
  size_t i;
  bool ok;

  p = lo;

  while (p < hi) {
    // printf(BOOT FUNC("find_free_space") "p=%p\n", p);
    q = p + size;
    ok = true;
    for (i = 0; i < copy_count; i++) {
      if (mode & FREE_SRC) {
        r = copies[i].src;
        s = r + copies[i].size;
        if (p <= s && r <= q) {
          p = s;
          ok = false;
          break;
        }
      }
      if (mode & FREE_DST) {
        r = copies[i].dst;
        s = r + copies[i].size;
        if (p < s && r < q) {
          p = s;
          ok = false;
          break;
        }
      }
    }
    if (ok)
      return p;
  }

  return UINTPTR_MAX;
}

static void
normalize_buffers(void)
{
  size_t i;

  for (i = copy_count; i < ELF_MAX_SEGMENTS * 2; i++) {
    copies[i].size = 0;
    src_map[i] = dst_map[i] = 0;
  }
  for (i = zero_fill_count; i < ELF_MAX_SEGMENTS; i++) {
    zero_fills[i].size = 0;
  }
}


static bool
validate_elf_header(Elf32_Ehdr *ehdr, bool elf32_p, bool elfle_p, Elf32_Half machine)
{
#define BADELF(s, ...)                                                      \
  do {                                                                      \
    printf(BOOT FUNC("validate_elf_header") ERROR s __VA_OPT__(, ) __VA_ARGS__); \
    return false;                                                           \
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

void trampoline(void);

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
  Elf32_Shdr *shdr_sym, *shdr_str;
  size_t i, strtab_sh_i, symtab_sh_i, boot_size, cmdline_size;
  uintptr_t boot_alloc, symtab_ap, strtab_ap;

  ehdr = (Elf32_Ehdr *)img_start;

  // -----------------------------------------------------------------------------------------------
  // Validation

  if (!validate_elf_header(ehdr, elf32_p, elfle_p, machine))
    return false;

  // -----------------------------------------------------------------------------------------------
  // Parse program headers (segments) to get loadable segments; use these to populate the copies and
  // zero_fills arrays.

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
      if (phdr->p_filesz == 0) {
        if (!register_zero_fill(phdr->p_vaddr, phdr->p_memsz)) {
          printf(BOOT FUNC("load_elf_image") ERROR "failed to build zerofill map\n");
          return false;
        }
      } else {
        if (!register_copy(phdr->p_vaddr, img_start + phdr->p_offset, phdr->p_memsz)) {
          printf(BOOT FUNC("load_elf_image") ERROR "failed to build copy map\n");
          return false;
        }
      }
    }
  }

  // -----------------------------------------------------------------------------------------------
  // Parse section headers to get the symbol table and its corresponding string table, if the image
  // has them; if it does, then allocate space in the destination map and register copies.

  printf(BOOT FUNC("load_elf_image") "Program section count: %d (size %dB)\n",
         ehdr->e_shnum,
         ehdr->e_shentsize);

  strtab_sh_i = symtab_sh_i = SHN_UNDEF;
  for (i = 0; i < ehdr->e_shnum; i++) {
    shdr_sym = (Elf32_Shdr *)(img_start + ehdr->e_shoff + i * ehdr->e_shentsize);

    if (shdr_sym->sh_type == SHT_SYMTAB) {
      // There's at most one SHT_SYMTAB, and that has an sh_link pointing at the SHT_STRTAB it uses
      symtab_sh_i = i;

      strtab_sh_i = shdr_sym->sh_link;
      shdr_str = (Elf32_Shdr *)(img_start + ehdr->e_shoff + strtab_sh_i * ehdr->e_shentsize);

      break;
    }
  }

  if (strtab_sh_i) {
    printf(BOOT FUNC("load_elf_image") ".symtab: #%d, .strtab: #%d\n", symtab_sh_i, strtab_sh_i);
  } else if (symtab_sh_i) {
    printf(BOOT FUNC("load_elf_image") ".symtab: #%d, no strtab!\n", symtab_sh_i);
  } else {
    printf(BOOT FUNC("load_elf_image") "no symtab!\n");
  }

  if (strtab_sh_i) {
    symtab_ap = find_free_space(shdr_sym->sh_size, 0, mem_hi, FREE_DST);
    if (symtab_ap == UINTPTR_MAX) {
      printf(BOOT FUNC("load_elf_image") ERROR "no free space for symbol table\n");
      return false;
    }
    if (!register_copy(symtab_ap, img_start + shdr_sym->sh_offset, shdr_sym->sh_size)) {
      printf(BOOT FUNC("load_elf_image") ERROR "failed to register symbol table copy\n");
      return false;
    }
    printf(BOOT FUNC("load_elf_image") ".symtab: [%p,%p)->[%p,%p)\n",
           img_start + shdr_sym->sh_offset,
           img_start + shdr_sym->sh_offset + shdr_sym->sh_size,
           symtab_ap,
           symtab_ap + shdr_sym->sh_size);

    strtab_ap = find_free_space(shdr_str->sh_size, 0, mem_hi, FREE_DST);
    if (strtab_ap == UINTPTR_MAX) {
      printf(BOOT FUNC("load_elf_image") ERROR "no free space for string table\n");
      return false;
    }
    if (!register_copy(strtab_ap, img_start + shdr_str->sh_offset, shdr_str->sh_size)) {
      printf(BOOT FUNC("load_elf_image") ERROR "failed to register string table copy\n");
      return false;
    }
    printf(BOOT FUNC("load_elf_image") ".strtab: [%p,%p)->[%p,%p)\n",
           img_start + shdr_str->sh_offset,
           img_start + shdr_str->sh_offset + shdr_str->sh_size,
           strtab_ap,
           strtab_ap + shdr_str->sh_size);
  }

  // -----------------------------------------------------------------------------------------------
  // Calculate the size of the 2nd stage boot image and find space for it.

  boot_size = 0;

  cmdline_size = strlen(cmdline) + 1;

  boot_size += sizeof copies;
  boot_size += sizeof dst_map;
  boot_size += sizeof src_map;
  boot_size += sizeof zero_fills;
  boot_size += sizeof(struct elf_boot_args);
  boot_size += cmdline_size;
  boot_size += ELF_RELOCATION_BUFFER_SIZE;
  boot_size += (uintptr_t)ELF_TRAMPOLINE_END - (uintptr_t)ELF_TRAMPOLINE_START;

  boot_alloc = find_free_space(boot_size, 0, (uintptr_t)__prog_start, FREE_BOTH);
  if (boot_alloc == UINTPTR_MAX)
    boot_alloc = find_free_space(boot_size, (uintptr_t)__prog_end, mem_hi, FREE_BOTH);
  if (boot_alloc == UINTPTR_MAX) {
    printf(BOOT FUNC("load_elf_image") ERROR "no free space for boot image\n");
    return false;
  }
  if (!register_copy(boot_alloc, boot_alloc, boot_size)) {
    printf(BOOT FUNC("load_elf_image") ERROR "failed to register boot image copy\n");
    return false;
  }
  printf(
    BOOT FUNC("load_elf_image") "boot allocation: [%p,%p)\n", boot_alloc, boot_alloc + boot_size);


  // -----------------------------------------------------------------------------------------------
  // Populate 2nd stage boot image

  trampoline();

  // normalize_buffers();

  return true;
}

typedef struct {
  uintptr_t start, end;
} range;
static range src_range(struct copy cp) {
  return (range){.start = cp.src, .end = cp.src + cp.size};
}
static range dst_range(struct copy cp) {
  return (range){.start = cp.dst, .end = cp.dst + cp.size};
}
static bool overlaps(range lhs, range rhs) {
  return lhs.start < rhs.end &&  rhs.start < lhs.end;
}
static bool req(range lhs, range rhs) {
  return lhs.start == rhs.start && lhs.end == rhs.end;
}
static range isect(range lhs, range rhs) {
  range out;
  out = (range){
    .start = max(lhs.start, rhs.start),
    .end = min(lhs.end, rhs.end),
  };
  if(out.end <= out.start)
    out.start = out.end = 0;
  return out;
}
// returns the (lower) range of the difference lhs-rhs
static range rsub1(range lhs, range rhs) {
  if(!overlaps(lhs, rhs))
    return lhs;
  if(lhs.start < rhs.start)
    return (range){.start = lhs.start, .end = rhs.start};
  else
    return (range){.start = rhs.end, .end = max(rhs.end,lhs.end)};
}
// returns true if lhs is contained in rhs
static bool contained(range lhs, range rhs){
  return lhs.start >= rhs.start && lhs.end <= rhs.end;
}

// returns true if rhs cannot run until lhs does
//  (equivalently: if rhs' destination overalsp lhs' source)
static bool
blocks(struct copy lhs, struct copy rhs, range *r) {
  range rhs_dst, lhs_src;

  rhs_dst = dst_range(rhs);
  lhs_src = src_range(lhs);

  if(!overlaps(rhs_dst, lhs_src))
    return false;

  if(r)
    *r= isect(rhs_dst, lhs_src);

  return true;
}

void
trampoline(void)
{
  bool progress, icanrun;
  size_t i, j, off, passno;
  range r, idst, isrc;

  progress = true;
  passno = 0;
  while(progress) {
    printf(BOOT FUNC("trampoline") "pass: %d\n", passno);
    passno += 1;
    progress = false;
    dump_copies();

    for(i = 0;i < copy_count;) {
      icanrun = true;
      idst = dst_range(copies[i]);
      isrc = src_range(copies[i]);

      for(j = 0;j < copy_count;j++) {
        if(i == j) continue;

        if(blocks(copies[j], copies[i], nullptr)) {
          // cases:
          //  1. dst(i) <= src(j) - no part of i can run
          if(contained(idst, src_range(copies[i]))) {
            icanrun = false;
            break;
          }
          
          //  2. dst(i) > src(j) - part of i can run
          r = rsub1(idst, src_range(copies[j]));
          // there exists such a range
          assert(r.end > r.start);
          // it is the case that either r.start == i.start or r.end == i.end
          assert(r.start == idst.start || r.end == idst.end);
          if(r.start == idst.start) {
            off = r.end - idst.start;
            if(!register_copy(copies[i].dst + off, copies[i].src + off, copies[i].size - off)) {
              panic(BOOT FUNC("trampoline") ERROR "failed to register copy\n");
            }
            copies[i].size = off;
          } else if(r.end == idst.end) {
            off = r.start - idst.start;
            if(!register_copy(copies[i].dst, copies[i].src, off)) {
              panic(BOOT FUNC("trampoline") ERROR "failed to register copy\n");
            }
            copies[i].dst += off;
            copies[i].src += off;
            copies[i].size -= off;
          } else {
            panic("unreachable!\n");
          }
          dump_copies();
        }
      }

      if(icanrun) {
        progress = true;
        printf(BOOT FUNC("trampoline") "running copy: [%p,%p)->[%p,%p)\n", isrc.start, isrc.end, idst.start, idst.end);
        remove_copy(i);
        dump_copies();
      } else i++;
    }
  }
}
