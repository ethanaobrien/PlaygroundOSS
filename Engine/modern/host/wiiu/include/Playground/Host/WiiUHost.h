#ifndef PLAYGROUND_HOST_WIIU_HOST_H
#define PLAYGROUND_HOST_WIIU_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlaygroundWiiUHost PlaygroundWiiUHost;

typedef enum PlaygroundWiiUPointerPhase {
  PLAYGROUND_WIIU_POINTER_DOWN,
  PLAYGROUND_WIIU_POINTER_MOVE,
  PLAYGROUND_WIIU_POINTER_UP,
  PLAYGROUND_WIIU_POINTER_CANCEL
} PlaygroundWiiUPointerPhase;

typedef struct PlaygroundWiiUHostConfig {
  uint32_t renderWidth;
  uint32_t renderHeight;
  uint32_t maximumFrames;
} PlaygroundWiiUHostConfig;

typedef struct PlaygroundWiiUHostCallbacks {
  bool (*onStart)(void *, PlaygroundWiiUHost *);
  bool (*onFrame)(void *, PlaygroundWiiUHost *, uint64_t deltaNs);
  void (*onRender)(void *, PlaygroundWiiUHost *, int width, int height);
  void (*onStop)(void *, PlaygroundWiiUHost *);
  void (*onVisibility)(void *, PlaygroundWiiUHost *, bool foreground);
  void (*onPointer)(void *, PlaygroundWiiUHost *, PlaygroundWiiUPointerPhase,
                    float x, float y);
  void (*onButtons)(void *, PlaygroundWiiUHost *, uint32_t pressed,
                    uint32_t released, uint32_t held);
} PlaygroundWiiUHostCallbacks;

PlaygroundWiiUHostConfig playgroundWiiUHostDefaultConfig(void);
int playgroundWiiUHostRun(const PlaygroundWiiUHostConfig *,
                          const PlaygroundWiiUHostCallbacks *, void *);
void playgroundWiiUHostRequestQuit(PlaygroundWiiUHost *);
void playgroundWiiUHostSwapBuffers(PlaygroundWiiUHost *);
// Draws a dependency-free first-run progress screen while servicing ProcUI.
// Returns false if the title is exiting or presenting the frame failed.
bool playgroundWiiUHostPresentBootstrapProgress(PlaygroundWiiUHost *,
                                                uint64_t completed,
                                                uint64_t total);
bool playgroundWiiUHostPumpBootstrapProgress(PlaygroundWiiUHost *,
                                             uint64_t completed,
                                             uint64_t total);
PlaygroundWiiUHost *playgroundWiiUHostGetActive(void);
bool playgroundWiiUHostGetMotionAngles(float *azimuth, float *elevation);
bool playgroundWiiUHostShowSoftwareKeyboard(
    PlaygroundWiiUHost *, bool password, const char *initialText,
    const char *hintText, uint32_t maximumCharacters, char *output,
    uint32_t outputBytes);
void *playgroundWiiUHostGetGLProcAddress(const char *name);
const char *playgroundWiiUHostLastError(void);

#ifdef __cplusplus
}
#endif

#endif
