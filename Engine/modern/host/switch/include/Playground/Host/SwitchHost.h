#ifndef PLAYGROUND_HOST_SWITCH_HOST_H
#define PLAYGROUND_HOST_SWITCH_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlaygroundSwitchHost PlaygroundSwitchHost;

// Stable libnx Npad bit values exposed without including switch.h in engine
// translation units, whose legacy BaseType aliases conflict with libnx.
#define PLAYGROUND_SWITCH_BUTTON_B (UINT64_C(1) << 1)
#define PLAYGROUND_SWITCH_BUTTON_PLUS (UINT64_C(1) << 10)

typedef enum PlaygroundSwitchVisibility {
  PLAYGROUND_SWITCH_VISIBILITY_FOREGROUND,
  PLAYGROUND_SWITCH_VISIBILITY_OBSCURED,
  PLAYGROUND_SWITCH_VISIBILITY_BACKGROUND
} PlaygroundSwitchVisibility;

typedef enum PlaygroundSwitchPointerPhase {
  PLAYGROUND_SWITCH_POINTER_DOWN,
  PLAYGROUND_SWITCH_POINTER_MOVE,
  PLAYGROUND_SWITCH_POINTER_UP,
  PLAYGROUND_SWITCH_POINTER_CANCEL
} PlaygroundSwitchPointerPhase;

typedef struct PlaygroundSwitchHostConfig {
  uint32_t handheldWidth;
  uint32_t handheldHeight;
  uint32_t dockedWidth;
  uint32_t dockedHeight;
  uint32_t maximumFrames;
  bool verticalSync;
} PlaygroundSwitchHostConfig;

typedef struct PlaygroundSwitchHostCallbacks {
  bool (*onStart)(void *, PlaygroundSwitchHost *);
  bool (*onFrame)(void *, PlaygroundSwitchHost *, uint64_t deltaNs);
  void (*onStop)(void *, PlaygroundSwitchHost *);
  void (*onVisibility)(void *, PlaygroundSwitchHost *,
                       PlaygroundSwitchVisibility);
  void (*onResume)(void *, PlaygroundSwitchHost *);
  void (*onResize)(void *, PlaygroundSwitchHost *, int width, int height);
  void (*onPerformanceMode)(void *, PlaygroundSwitchHost *, int mode);
  void (*onPointer)(void *, PlaygroundSwitchHost *, int64_t id,
                    PlaygroundSwitchPointerPhase, float x, float y);
  void (*onButtons)(void *, PlaygroundSwitchHost *, uint64_t pressed,
                    uint64_t released, uint64_t held);
} PlaygroundSwitchHostCallbacks;

PlaygroundSwitchHostConfig playgroundSwitchHostDefaultConfig(void);
int playgroundSwitchHostRun(const PlaygroundSwitchHostConfig *,
                            const PlaygroundSwitchHostCallbacks *, void *);
void playgroundSwitchHostRequestQuit(PlaygroundSwitchHost *);
void playgroundSwitchHostSwapBuffers(PlaygroundSwitchHost *);
// Presents a dependency-free first-run progress screen while continuing to
// service applet focus, suspend, exit, and dock/handheld mode transitions.
// Returns false when startup work should be cancelled because the applet is
// exiting or the graphics surface failed.
bool playgroundSwitchHostPresentBootstrapProgress(PlaygroundSwitchHost *,
                                                  uint64_t completed,
                                                  uint64_t total);
void playgroundSwitchHostGetPixelSize(const PlaygroundSwitchHost *, int *width,
                                      int *height);
void *playgroundSwitchHostGetGLProcAddress(const char *name);
const char *playgroundSwitchHostLastError(void);

#ifdef __cplusplus
}
#endif

#endif
