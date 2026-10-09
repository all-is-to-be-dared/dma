#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <generic/printf.h>
#include <generic/assert.h>
#include <generic/backtrace.h>
#include <generic/macros.h>

#include <bcm2835/arch.h>
#include <bcm2835/mmu.h>
#include <bcm2835/platform.h>

#include <pup/protocol.h>

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
