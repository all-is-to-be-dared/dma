#pragma once




#define assert(cond, ...) \
  do { __assert##__VA_OPT__(_fmt) (cond, #cond, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__); } while(0)

void __assert(bool cond, const char *condstr, const char *file, long line);
[[gnu::format(printf, 5, 6)]]
void __assert_fmt(bool cond, const char *condstr, const char *file, long line, const char *fmt, ...);




#define todo(fmt, ...) \
  do { __panic(__FILE__, __LINE__, "TODO: " fmt __VA_OPT__(,) __VA_ARGS__); } while(0)

#define panic(fmt, ...) \
  do { __panic(__FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)

[[noreturn]]
[[gnu::format(printf, 3, 4)]]
void __panic(const char *file, long line, const char *fmt, ...);

