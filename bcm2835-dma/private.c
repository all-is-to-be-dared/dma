#include <bcm2835-dma/private.h>

#include <printf/printf.h>

void run_initializers(void)
{
  extern struct initializer __inittab_start[];
  extern struct initializer __inittab_end[];

  static bool did_run_initializers = false;
  struct initializer *init;

  assert(!did_run_initializers, "initializers already ran!");
  did_run_initializers = true;

  init = __inittab_start;

  printf("Running initializers:\n");
  while(init < __inittab_end) {
    printf("\t[%s]\n", init->name);
    init->func();
    init++;
  }
  printf("Finished running initializers.\n");
}

