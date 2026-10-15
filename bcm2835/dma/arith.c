#include <bcm2835/dma/private.h>
#include <bcm2835/dma/emit.h>
#include <bcm2835/dma/arith.h>




_TableSymbol volatile uint8_t alignas(256) ZERO[256] = {};
_TableSymbol volatile uint8_t alignas(1 << 24) HYPER[1 << 24];
_TableSymbol volatile uint8_t alignas(1 << 16) SUPER[1 << 16];

