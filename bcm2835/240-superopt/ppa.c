#include <string.h>

#include <generic/assert.h>
#include <generic/printf.h>

#include "sopter.h"

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
static const char * REGION_NAMES[] = {
  [RK_Supersection] = "16MB supersection",
  [RK_Section] = "1MB section",
  [RK_LargePage] = "64KB large page",
  [RK_SmallPage] = "4KB small page",
};
static size_t
region_index(size_t i, RegionKind rk)
{
  // assert(rk != RK_Supersection);
  assert(rk != RK_SmallPage);
  return i + INDEX_OFFSETS[rk];
}
static void float_region(PhysicalPageAllocator *ppa, size_t idx_in_kind, RegionKind rk)
{
  size_t idx;

  printf("float_region(#%d,%s)\n", idx_in_kind, REGION_NAMES[rk]);

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
  memset(ppa->floating_lists, 0xff, sizeof ppa->floating_lists);
  memset(ppa->floating_counts, 0, sizeof ppa->floating_counts);
  memset(ppa->allocated_counts, 0, sizeof ppa->allocated_counts);

  if(!preclaim_count)
    return;

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
  printf("commit(%d,%s,%s)\n",
    from_region_idx,
    REGION_NAMES[from_region_kind],
    REGION_NAMES[alloc_kind]
  );
  uint16_t bitmap;
  uint32_t lz, child_no, from_region_no, region_no;

  assert(alloc_kind <= RK_SmallPage);
  assert(from_region_kind < alloc_kind,
    "committing %s from %s\n",
    REGION_NAMES[alloc_kind],
    REGION_NAMES[from_region_kind]
  );

  bitmap = ppa->regions[from_region_idx].bitmap;
  lz = bitmap ? __builtin_clz(bitmap) : 0;
  bitmap &= ~(0x8000 >> lz);
  ppa->regions[from_region_idx].bitmap = bitmap;

  if(!bitmap)
    unfloat_region(ppa, from_region_idx, from_region_kind);

  child_no = 16 - lz;

  from_region_no = from_region_idx - INDEX_OFFSETS[from_region_kind];
  region_no = child_no + (from_region_no * 16);
  if(from_region_kind == alloc_kind - 1) {
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
  uint32_t lz, ss_no /*, ss_idx */;

  rki = rk;
  while(rki >= RK_Section) {
    rki -= 1;

    if(ppa->floating_lists[rki] != SIZE_MAX) {
      ix = commit_region(ppa, ppa->floating_lists[rki], rki, rk);
      ppa->allocated_counts[rk] += 1;
      return ix;
    }
  }

  // no floating supersections with sufficient free space, OR rk==RK_Supersection
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
    // ss_idx = region_index(ss_no, RK_Supersection);
    float_region(ppa, ss_no, RK_Supersection);
    printf("floated supersection #%d\n", ss_no);
    // ix = commit_region(ppa, ss_idx, RK_Supersection, rk);
    [[clang::musttail]]
    return ppa_alloc(ppa, rk);
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

