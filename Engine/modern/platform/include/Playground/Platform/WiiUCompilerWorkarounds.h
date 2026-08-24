#ifndef PLAYGROUND_PLATFORM_WIIU_COMPILER_WORKAROUNDS_H
#define PLAYGROUND_PLATFORM_WIIU_COMPILER_WORKAROUNDS_H

#if defined(__WIIU__)
#ifdef __cplusplus
extern "C" {
#endif
void playground_wiiu_trap_abort(void);
#ifdef __cplusplus
}
#endif

// Cemu 2.6 cannot execute the PowerPC conditional-trap form GCC emits for an
// impossible __builtin_trap failure arm. Keep the failure call opaque to GCC
// so it cannot fold it straight back into the same conditional-trap opcode.
#define __builtin_trap() playground_wiiu_trap_abort()
#endif

#endif
