#include <stdlib.h>

// This separate translation unit is intentionally opaque to callers. GCC's
// PowerPC backend otherwise turns a branch to abort() back into a conditional
// trap, which Cemu 2.6 cannot execute correctly.
__attribute__((noinline, noreturn)) void playground_wiiu_trap_abort(void) {
  abort();
}
