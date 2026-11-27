#pragma once

#define min(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _b : _a;      \
  })
#define max(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })
#define abs(a) \
  ({ \
    __typeof__(a) _a = (a); \
    __typeof__(a) _out; \
    __asm__ ("add %0, %1, %1, asr #31; eor %0, %0, %1, asr #31;" :"=r"(_out):"r"(_a)); \
    _out; \
  })
