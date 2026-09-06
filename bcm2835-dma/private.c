#include <bcm2835-dma/private.h>
#include <string.h>

#include <generic/printf.h>

void
run_initializers(void)
{
  extern struct initializer __inittab_start[];
  extern struct initializer __inittab_end[];

  static bool did_run_initializers = false;
  struct initializer* init;

  assert(!did_run_initializers, "initializers already ran!");
  did_run_initializers = true;

  init = __inittab_start;

  printf("Running initializers:\n");
  while (init < __inittab_end) {
    printf("\t[%s]\n", init->name);
    init->func();
    init++;
  }
  printf("Finished running initializers.\n");
}




struct mono
{
  void* key;
  void* val;
  struct mono* next;
};

struct mono_ctx
{
  long instance;
  size_t keysz;
  size_t valsz;
  struct mono* monos;
  struct mono_ctx* next;
};

enum
{
  MAX_MONO = 512,
  MAX_CONTEXT = 128,
};
static struct mono MONOS[MAX_MONO];
static struct mono_ctx CONTEXTS[MAX_CONTEXT];
static size_t curr_mono = 0, curr_context = 0;

struct mono_ctx* mono_root = NULL;

struct mono_ctx*
_mono_get_or_init_ctx(long i, size_t ks, size_t vs)
{
  struct mono_ctx* c;

  // printf("[%s:%ld,%zu,%zu]\n", __PRETTY_FUNCTION__, i, ks, vs);

  c = mono_root;
  while (c) {
    if (c->instance == i)
      return c;
    c = c->next;
  }

  // printf("!context not found!\n");
  assert(curr_context < MAX_CONTEXT);
  c = CONTEXTS + curr_context++;
  c->instance = i;
  c->keysz = ks;
  c->valsz = vs;
  c->monos = NULL;
  c->next = mono_root;
  mono_root = c;

  return c;
}

bool
_mono_get(struct mono_ctx* c, void* k, void* d)
{
  struct mono* m;

  m = c->monos;
  while (m) {
    if (!memcmp(m->key, k, c->keysz)) {
      memcpy(d, m->val, c->valsz);
      // printf("!found\n");
      return true;
    }
    m = m->next;
  }
  // printf("!not found\n");
  return false;
}

void
_mono_set(struct mono_ctx* c, void* k, void* d)
{
  struct mono* m;

  // printf("!set\n");

  assert(curr_mono < MAX_MONO);
  m = MONOS + curr_mono++;
  memcpy(m->key, k, c->keysz);
  memcpy(m->val, d, c->valsz);
  m->next = c->monos;

  c->monos = m;
}




static uint8_t alignas(1 << 16) ARENA_BACKING[1 << 16];
#include <bcm2835-dma/emit.h>
arena ARENA = {
  .start = ARENA_BACKING,
  .p = ARENA_BACKING,
  .end = ARENA_BACKING + (1 << 16),
  .util = 0,
};




enum
{
  PROBE_MAX = 32
};
struct probe probes[32] = {};
size_t probecnt = 0;

void
_Probe(const char* func, const char* name, volatile void* data, size_t width)
{
  if (probecnt >= PROBE_MAX)
    panic("no more probe slots to allocate (%zu>%d)\n", probecnt, PROBE_MAX);

  printf("Attaching probe #%zu: %s:%s to %p:<%zu>\n", probecnt, func, name, data, width);

  probes[probecnt++] = (struct probe){ func, name, data, width };
}

void
ReportProbeInfo(void)
{
  int i, j;
  struct probe* p;

  if (!probecnt)
    return;

  printf("Probe count: %d\n", probecnt);
  for (i = 0; i < probecnt; i++) {
    p = probes + i;
    printf("%s:%s (%p:<%zu>): ", p->func, p->name, p->data, p->size);
    for (j = 0; j < p->size; j++) {
      printf("%02hhx", *(volatile uint8_t*)(p->data + p->size - 1 - j));
    }
    printf("\n");
  }
}

void
ClearProbes(void)
{
  probecnt = 0;
}
