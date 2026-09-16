#include "pup/protocol.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>
#include <generic/printf.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>

// -------------------------------------------------------------------------------------------------

// Allowed instruction subset

typedef enum
{
  // Generic instruction that can be coded with a cold bits/hot bits approach.
  IK_Generic,
  // Branch instructions are a bit special because just strumming the hot bits won't actually give
  // you anything useful; you have to consider what the actual 'jumpable' range of the program is
  // and constrain to that instead.
  IK_Branch,
} InsnKind;
typedef struct
{
  uint32_t cold_bits;
  uint32_t hot_bits;
  bool (*_Nullable permit)(uint32_t hot_bits);
} InsnArith;
typedef struct
{
  uint32_t (*_Nonnull gen)(uint32_t branch_from, uint32_t branch_to);
} InsnBranch;
typedef struct
{
  InsnKind kind;
  union
  {
    InsnArith arith;
    InsnBranch branch;
  };
} Insn;
typedef struct
{
  Insn *insns;
  size_t count;
} ISel;

// Probabilistic tests

typedef struct
{
  void *sandbox_data;
  void *extra;
} PTestEnv;
typedef struct
{
  bool (*test)(PTestEnv env);
  void (*init)(PTestEnv env);
} PTest;
typedef struct
{
  PTest *tests;
  size_t count;
} PTestSet;

// Superoptimizer input program

typedef struct
{
  ISel isel;
  PTestSet ptestset;
  void *testenv;
} Program;

// -------------------------------------------------------------------------------------------------

typedef enum
{
  // 16MB supersection
  RK_Supersection = 0,
  // 1MB section
  RK_Section = 1,
  // 64KB large page
  RK_LargePage = 2,
  // 4KB small page
  RK_SmallPage = 3,
} RegionKind;
typedef struct
{
  uint16_t bitmap;
  uint16_t next;
  uint16_t prev;
} RegionInfo;
enum : uint16_t
{
  REGION_COUNT = 0x2220,
  CHAIN_END = 0xffff,
};
typedef struct
{
  RegionInfo regions[REGION_COUNT];
  uint32_t supersection_mask;
  size_t floating_lists[3];
  size_t floating_counts[3];
  size_t allocated_counts[4];
} PhysicalPageAllocator;

// Initialize a PhysicalPageAllocator.
void
ppa_init(PhysicalPageAllocator *ppa, size_t preclaim_count, uintptr_t preclaims[preclaim_count][2]);
// Allocate a memory region of kind `rk`.
uintptr_t
ppa_alloc(PhysicalPageAllocator *ppa, RegionKind rk);
// Free a memory region.
//
// Note that this works both for memory regions claimed with [`ppa_claim`] and also for memory
// regions allocated with [`ppa_alloc`].
void
ppa_free(PhysicalPageAllocator *ppa, uintptr_t p, RegionKind rk);

constexpr uintptr_t REGION_SCALES[] = {
  [RK_Supersection] = 16 * 1024 * 1024,
  [RK_Section] = 1 * 1024 * 1024,
  [RK_LargePage] = 64 * 1024,
  [RK_SmallPage] = 4 * 1024,
};
static uintptr_t
get_region_ptr(size_t no, RegionKind rk)
{
  return REGION_SCALES[rk] * no;
}
constexpr size_t INDEX_OFFSETS[] = {
  [RK_Supersection] = 0x2200,
  [RK_Section] = 0x2000,
  [RK_LargePage] = 0x0,
  [RK_SmallPage] = 0xdeaddead,
};
static size_t
region_index(size_t i, RegionKind rk)
{
  assert(rk != RK_Supersection);
  return i + INDEX_OFFSETS[rk];
}
static void float_region(PhysicalPageAllocator *ppa, size_t idx_in_kind, RegionKind rk)
{
  size_t idx;

  idx = region_index(idx_in_kind, rk);

  ppa->floating_counts[rk] += 1;
  ppa->regions[idx].prev = CHAIN_END;
  ppa->regions[idx].next = ppa->floating_lists[rk] == SIZE_MAX ? CHAIN_END : ppa->floating_lists[rk];
  ppa->floating_lists[rk] = idx;
}
static void unfloat_region(PhysicalPageAllocator *ppa, size_t region_idx, RegionKind rk)
{
  size_t next, prev;

  ppa->floating_counts[rk] -= 1;
  next = ppa->regions[region_idx].next;
  if (next != CHAIN_END)
    ppa->regions[next].prev = CHAIN_END;
  prev = ppa->regions[region_idx].prev;
  if (prev != CHAIN_END)
    ppa->regions[prev].next = next;
  else
    ppa->floating_lists[region_idx] = next == CHAIN_END ? SIZE_MAX : next;
}

void
ppa_init(PhysicalPageAllocator *ppa, size_t preclaim_count, uintptr_t preclaims[preclaim_count][2])
{
  size_t i, j;
  uintptr_t r1;

  for (i = 0; i < REGION_COUNT; i++)
    ppa->regions[i] = (RegionInfo){ 0xffff, CHAIN_END, CHAIN_END };
  ppa->supersection_mask = 0xffff'ffff;
  memset(ppa->floating_lists, 0, sizeof ppa->floating_lists);
  memset(ppa->floating_counts, 0, sizeof ppa->floating_counts);
  memset(ppa->allocated_counts, 0, sizeof ppa->allocated_counts);

  j = 0;
  for (i = 0; i < 0x2'0000; i++) {
    r1 = get_region_ptr(i + 1, RK_SmallPage);
    if(r1 <= preclaims[0][0]) {
      continue;
    } else {
      ppa->allocated_counts[RK_SmallPage] += 1;
      ppa->regions[region_index(i/16, RK_LargePage)].bitmap &= ~(1 << (i % 16));
      while(preclaims[j][1] <= r1) {
        if(j++ >= preclaim_count) {
          goto finished_preclaims;
        }
      }
    }
  }

finished_preclaims:
  for(i = 0;i < 0x200;i++) {
    for(j = i*16;j < i*16+16;j++) {
      if(ppa->regions[region_index(j, RK_LargePage)].bitmap != 0xffff) {
        ppa->allocated_counts[RK_LargePage] += 1;
        ppa->regions[region_index(i, RK_Section)].bitmap &= ~(1 << (j % 16));
        if (ppa->regions[region_index(j, RK_LargePage)].bitmap)
          float_region(ppa, j, RK_LargePage);
      }
    }
  }

  for(i = 0;i < 0x20;i++) {
    for(j = i * 16;j < i*16+16;j++) {
      if(ppa->regions[region_index(j, RK_Section)].bitmap != 0xffff) {
        ppa->allocated_counts[RK_Section] += 1;
        ppa->regions[region_index(i, RK_Supersection)].bitmap &= ~(1 << (j % 16));
        if (ppa->regions[region_index(j, RK_Section)].bitmap)
          float_region(ppa, j, RK_Section);
      }
    }
  }

  for(i=0;i < 32;i++) {
    if(ppa->regions[region_index(i, RK_Supersection)].bitmap != 0xffff) {
      ppa->allocated_counts[RK_Supersection] += 1;
      ppa->supersection_mask &= ~(1 << i);
      if (ppa->regions[region_index(i, RK_Supersection)].bitmap)
        float_region(ppa, i, RK_Supersection);
    }
  }
}

static uintptr_t commit_region(
  PhysicalPageAllocator *ppa,
  size_t from_region_idx,
  RegionKind from_region_kind,
  RegionKind alloc_kind
)
{
  uint16_t bitmap;
  uint32_t lz, child_no, from_region_no, region_no;

  assert(alloc_kind <= RK_SmallPage);
  assert(from_region_kind < alloc_kind);

  bitmap = ppa->regions[from_region_idx].bitmap;
  lz = bitmap ? __builtin_clz(bitmap) : 0;
  bitmap &= ~(0x8000 >> lz);
  ppa->regions[from_region_idx].bitmap = bitmap;

  if(!bitmap)
    unfloat_region(ppa, from_region_idx, from_region_kind);

  child_no = 16 - lz;

  from_region_no = from_region_idx - INDEX_OFFSETS[from_region_kind];
  region_no = child_no + (from_region_no * 16);
  if(from_region_kind == alloc_kind + 1) {
    return get_region_ptr(region_no, alloc_kind);
  } else {
    return commit_region(
      ppa,
      region_index(region_no, from_region_kind + 1),
      from_region_kind + 1,
      alloc_kind
    );
  }
}

uintptr_t ppa_alloc(PhysicalPageAllocator *ppa, RegionKind rk)
{
  RegionKind rki;
  uintptr_t ix;
  uint32_t lz, ss_no, ss_idx;

  rki = rk;
  while(rki >= RK_Section) {
    rki -= 1;

    if(ppa->floating_lists[rki] != SIZE_MAX) {
      ix = commit_region(ppa, ppa->floating_lists[rki], rki, rk);
      ppa->allocated_counts[rk] += 1;
      return ix;
    }
  }

  // no supersections with sufficient free space, OR rk==RK_Supersection
  // in either case, we have to go back to the global supersection mask and try to allocate an
  // entire supersection, which we will either completely allocate or float.

  if (!ppa->supersection_mask)
    return UINTPTR_MAX;

  lz = __builtin_clz(ppa->supersection_mask);
  ppa->supersection_mask &= ~(0x8000'0000 >> lz);
  ss_no = 31 - lz;
  if(rk == RK_Supersection) {
    ix = get_region_ptr(ss_no, RK_Supersection);
  } else {
    ss_idx = region_index(ss_no, RK_Supersection);
    float_region(ppa, ss_no, rk);
    ix = commit_region(ppa, ss_idx, RK_Supersection, rk);
  }
  ppa->allocated_counts[rk] += 1;
  return ix;
}

static void
free_region_recursive(PhysicalPageAllocator *ppa, size_t region_no, RegionKind rk)
{
  uint32_t parent_no, parent_idx, parent_kind;
  uint16_t bitmap;

  if(rk == RK_Supersection) {
    ppa->supersection_mask |= 1 << region_no;
    return;
  }

  parent_no = region_no / 16;
  parent_kind = rk - 1;
  parent_idx = region_index(parent_no, parent_kind);

  bitmap = (ppa->regions[parent_idx].bitmap |= (1 << (region_no % 16)));
  if(bitmap == 0xffff){
    unfloat_region(ppa, parent_idx, parent_kind);
    free_region_recursive(ppa, parent_no, parent_kind);
  } else if (!bitmap) {
    float_region(ppa, parent_idx, parent_kind);
  }
}

void
ppa_free(PhysicalPageAllocator *ppa, uintptr_t p, RegionKind rk)
{
  uint32_t region_no;
  
  assert(!(p & (REGION_SCALES[rk] - 1)));
  region_no = p / REGION_SCALES[rk];

  free_region_recursive(ppa, region_no, rk);
  ppa->allocated_counts[rk] -= 1;
}

// -------------------------------------------------------------------------------------------------

typedef struct
{
  uint32_t state[4];
} RngState;

typedef struct
{
  // PRNG used for checking memory integrity (see: [`data`]).
  RngState rng;

  // Location at which code is loaded.
  //
  // The sandbox will load this page as X,!W (all other pages as !X) and will have all instructions
  // not part of the loaded code as traps.
  void *code;
  // Location at which data is loaded.
  //
  // The sandbox will load this page(s) as R,W,!X. All bytes not part of the demarcated data area
  // will initially be written with random numbers, and checked after execution. Said random numbers
  // will change with every test execution, and a small number of bits in the random seed will vary
  // according to hardware entropy between each test, to prevent the superoptimizer from 'learning'
  // the PRNG.
  void *data;
  // Location at which the stack can be found.
  //
  // As above: R,W,!X.
  void *stack;

  // State of the registers when the sandbox starts.
  uint32_t registers_initial[16];
  // State of the registers when the sandbox finished.
  uint32_t registers_final[16];

  // Time at which the sandbox started running, in microseconds and cycles.
  uint64_t start_us;
  uint32_t start_cy;

  size_t code_size, code_pages;
  size_t data_size, data_pages;
  size_t stack_pages;
} Sandbox;

void
sandbox_init(Sandbox *sbox);

// -------------------------------------------------------------------------------------------------

typedef struct {
  uint32_t icnt;
  uint32_t isel;
  uint32_t ihot;
} Superoptimizer;

void sopter_run(Superoptimizer *sopter, Program *prg, Sandbox *sbox);

// -------------------------------------------------------------------------------------------------

void
main(struct elf_boot_args *boot_args)
{
  ;
}
