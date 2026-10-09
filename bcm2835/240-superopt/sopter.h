#pragma once

#include <inttypes.h>
#include <stddef.h>

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

