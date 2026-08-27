#pragma once

#include <stdint.h>
#include "bcm2835/platform.h"
#include "bcm2835/extra.h"




/* -------------------------------------------------------------------------------------------------
   BUS ADDRESSES */




typedef struct
{
  uint32_t _0;
} busad;

enum
{
  ARM_DRAM_BASE = 0x0000'0000,
  ARM_DRAM_END = 0x2000'0000,
  BUS_DRAM_BASE = 0x4000'0000,
  BUS_DRAM_END = 0x6000'0000,

  ARM_PERI_BASE = 0x2000'0000,
  ARM_PERI_END = 0x2100'0000,
  BUS_PERI_BASE = 0x7E00'0000,
  BUS_PERI_END = 0x8000'0000,
};

static const inline bool
arm_is_dram(uintptr_t addr)
{
  return ARM_DRAM_BASE <= addr && addr < ARM_DRAM_END;
}
static const inline bool
arm_is_peri(uintptr_t addr)
{
  return ARM_PERI_BASE <= addr && addr < ARM_PERI_END;
}
static const inline busad
arm_to_bus(uintptr_t addr)
{
  if (arm_is_dram(addr)) {
    addr -= ARM_DRAM_BASE;
    addr += BUS_DRAM_BASE;
  } else if (arm_is_peri(addr)) {
    addr -= ARM_PERI_BASE;
    addr += BUS_PERI_BASE;
  } else {
    panic("arm_to_bus: invalid address %x\n", addr);
  }
  return (busad){ ._0 = addr };
}

static const inline bool
bus_is_dram(busad bus)
{
  return BUS_DRAM_BASE <= bus._0 && bus._0 < BUS_DRAM_END;
}
static const inline bool
bus_is_peri(busad bus)
{
  return BUS_PERI_BASE <= bus._0 && bus._0 < BUS_PERI_END;
}
static const inline uintptr_t
bus_to_arm(busad bus)
{
  uintptr_t addr = bus._0;
  if (bus_is_dram(bus)) {
    addr -= BUS_DRAM_BASE;
    addr += ARM_DRAM_BASE;
  } else if (bus_is_peri(bus)) {
    addr -= BUS_PERI_BASE;
    addr += ARM_PERI_BASE;
  } else {
    panic("bus_to_arm: invalid address %lx\n", bus._0);
  }
  return addr;
}




/* -------------------------------------------------------------------------------------------------
   CONTROL BLOCKS */




typedef struct
{
  volatile uint32_t ti;
  volatile busad src_addr;
  volatile busad dst_addr;
  volatile uint32_t txfr_len;
  volatile uint32_t stride;
  volatile busad next_cb;

  volatile uint32_t _ignore0, _ignore1;
} __attribute__((aligned(32))) cblk;

enum
{
  TI_2DMODE = 1 << 1,
  TI_DST_INC = 1 << 4,
  TI_DST_INC_WIDE128 = 1 << 5,
  TI_SRC_INC = 1 << 8,
  TI_SRC_INC_WIDE128 = 1 << 9,
};

static inline uint32_t
stride(uint16_t dst, uint16_t src)
{
  return ((uint32_t)dst) << 16 | (uint32_t)src;
}

static inline uint32_t
length(uint16_t rows, uint16_t cols)
{
  assert(rows < 0x4000);
  return ((uint32_t)rows) << 16 | (uint32_t)cols;
}




/* -------------------------------------------------------------------------------------------------
   DMA */




enum dma_channel_no
{
  DMA_CHAN0,
  DMA_CHAN1,
  DMA_CHAN2,
  DMA_CHAN3,
  DMA_CHAN4,
  DMA_CHAN5,
  DMA_CHAN6,
  DMA_CHAN7,
  DMA_CHAN8,
  DMA_CHAN9,
  DMA_CHAN10,
  DMA_CHAN11,
  DMA_CHAN12,
  DMA_CHAN13,
  DMA_CHAN14,
  DMA_CHAN15,
};
enum
{
  DMA_CHAN_COUNT = DMA_CHAN15 + 1,
};




enum
{
  DMA_CS_ERROR = (1 << 8),
  DMA_CS_OUTSTANDING_WRITES = (1 << 6),
  DMA_CS_PAUSED_BY_DREQ = (1 << 5),
  DMA_CS_IS_ACTIVE = (1 << 0),
  DMA_CS_START = (1 << 0),

  DMA_DEBUG_ERROR_READ = (1 << 2),
  DMA_DEBUG_ERROR_FIFO = (1 << 1),
  DMA_DEBUG_ERROR_AXI_READ_LAST = (1 << 0),
};

typedef struct
{
  io32 cs;
  volatile busad conblk_ad;
  io32 _ti;
  io32 _source_ad;
  io32 _dest_ad;
  io32 _txfr_len;
  io32 _stride;
  io32 _nextconbk;
  io32 debug;
} hw_dmachan_t __attribute__((aligned(0x100)));
static_assert(offsetof(hw_dmachan_t, debug) == 0x20);

hw_dmachan_t*
__dma_channel(enum dma_channel_no no);




inline bool
__dma_is_active(hw_dmachan_t* chan)
{
  return (chan->cs & DMA_CS_IS_ACTIVE) != 0;
}

inline bool
__dma_is_error(hw_dmachan_t* chan)
{
  return (chan->cs & DMA_CS_ERROR) != 0;
}




enum
{
  DMA_ERROR_AXI_READ_LAST = 1 << 0,
  DMA_ERROR_FIFO = 1 << 1,
  DMA_ERROR_READ = 1 << 2,
  DMA_ERROR_ANY = 1 << 3,

  DMA_PAUSED_FOR_DREQ = 1 << 8,
  DMA_OUTSTANDING_WRITES = 1 << 9,
};

struct dma_run_info
{
  uint32_t cycle_start, cycle_end;
  uint64_t us_start, us_end;
  uint32_t status;
};

struct dma_run_info
__dma_timed_run(hw_dmachan_t* chan, cblk* init_blk);




/* -------------------------------------------------------------------------------------------------
   DMA KIT */




struct dmakit_reservation
{
  enum dma_channel_no chan;
};




void
dmakit_init_channels(void);

bool
dmakit_channel_acquire(struct dmakit_reservation* rsv, bool allow_lite, const char* who);

void
dmakit_channel_release(struct dmakit_reservation rsv, const char* who);

void
dmakit_dump_channel_reservations(void);




bool dmakit_channel_is_lite(enum dma_channel_no);

bool dmakit_channel_is_active(enum dma_channel_no);




struct dma_run_info
dmakit_timed_run(struct dmakit_reservation rsv, cblk* init_blk);




/* -------------------------------------------------------------------------------------------------
   INTEGRITY CHECKING */




// One of the things that one needs to be careful with when using DMA this extensively is to ensure
// that memory corruption does not occur. To this end, we have an 'integrity checking' system in
// place, that allows recording the checksums of certain regions of memory and then checking later
// if the regions have changed. This allows a crude check to ensure that the DMA isn't scribbling
// on the walls.




// XXX: If guards are inside a guarded region, then they need to be processed before the guards of
//      enclosing regions, otherwise false positives may occur.
struct guard
{
  // Next guard in the guard chain
  struct guard* next;
  // Pointer to region
  volatile uint8_t* ptr;
  // Size (in bytes) of region.
  size_t size;
  // CRC of the guarded region
  uint32_t crc;
  // FCS of the guard itself: if the CRC in the guard disagrees with the calculated one, then we
  // want to be able to tell whether the guard is wrong, or if memory is wrong.
  //
  // This is calculated from _all_ fields of the guard.
  uint16_t fcs;
};

void
guard_chain_init(void);
void
guard_chain_add(struct guard* g);
void
guard_chain_reset(void);

// Records the current state of memory into the guard chain.
void
guard_chain_update(void);
// Check the current state of memory against the guard chain.
// Returns `true` if they match, `false` otherwise.
bool
guard_chain_check(bool print_diagnostics);




/* -------------------------------------------------------------------------------------------------
   INITIALIZERS */




struct initializer
{
  const char* name;
  void (*func)(void);
};

void
run_initializers(void);

#define INITIALIZER(_name)                                                     \
  void _name(void);                                                            \
  [[gnu::used, gnu::section(".inittab")]]                                      \
  const struct initializer _Init__##_name = { .name = #_name, .func = _name }; \
  void _name(void)




/* -------------------------------------------------------------------------------------------------
   PROBES */




struct probe
{
  const char* func;
  const char* name;
  volatile uint8_t* data;
  size_t size;
};

#define Probe(name, data, width) _Probe(__PRETTY_FUNCTION__, name, data, width)
void
_Probe(const char* func, const char* name, volatile void* data, size_t width);

void
ReportProbeInfo(void);

void
ClearProbes(void);




/* -------------------------------------------------------------------------------------------------
   RUNTIME MONO */



#define MonomorphizeOn(_Value, _Expr)                       \
  ({                                                                   \
    __typeof__((_Expr)) t;                                             \
    auto v = (_Value);                                                 \
    auto ctx = _mono_get_or_init_ctx(__COUNTER__, sizeof t, sizeof v); \
    if (!_mono_get(ctx, &v, &t)) {                                     \
      t = (_Expr);                                                     \
      _mono_set(ctx, &v, &t);                                          \
    }                                                                  \
    t;                                                                 \
  })

struct mono_ctx;

struct mono_ctx*
_mono_get_or_init_ctx(long i, size_t ks, size_t vs);

bool
_mono_get(struct mono_ctx*, void* value, void* data);

void
_mono_set(struct mono_ctx*, void* value, void* data);
