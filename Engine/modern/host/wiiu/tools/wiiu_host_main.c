#include "Playground/Host/WiiUHost.h"
#include "Playground/Platform/BuildTarget.h"

#include <vpad/input.h>
#include <GLES2/gl2.h>

typedef struct ProbeState {
  float touchX;
  float touchY;
  bool hasInputSamples;
  bool touching;
} ProbeState;

static bool start(void *opaque, PlaygroundWiiUHost *host) {
  (void)opaque;
  (void)host;
  (void)playgroundGetBuildTarget();
  return true;
}

static bool frame(void *opaque, PlaygroundWiiUHost *host, uint64_t deltaNs) {
  (void)opaque;
  (void)host;
  (void)deltaNs;
  return true;
}

static void render(void *opaque, PlaygroundWiiUHost *host,
                   int width, int height) {
  const ProbeState *state = opaque;
  glViewport(0, 0, width, height);
  if (state->touching)
    glClearColor(0.75f, 0.05f, 0.75f, 1.0f);
  else if (state->hasInputSamples)
    glClearColor(0.05f, 0.65f, 0.10f, 1.0f);
  else
    glClearColor(0.05f, 0.14f, 0.24f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  playgroundWiiUHostSwapBuffers(host);
}

static void pointer(void *opaque, PlaygroundWiiUHost *host,
                    PlaygroundWiiUPointerPhase phase, float x, float y) {
  (void)host;
  ProbeState *state = opaque;
  state->touchX = x;
  state->touchY = y;
  state->touching = phase == PLAYGROUND_WIIU_POINTER_DOWN ||
                    phase == PLAYGROUND_WIIU_POINTER_MOVE;
}

static void buttons(void *opaque, PlaygroundWiiUHost *host,
                    uint32_t pressed, uint32_t released, uint32_t held) {
  ProbeState *state = opaque;
  state->hasInputSamples = true;
  (void)released;
  (void)held;
  if (pressed & VPAD_BUTTON_PLUS)
    playgroundWiiUHostRequestQuit(host);
}

int main(void) {
  PlaygroundWiiUHostConfig config = playgroundWiiUHostDefaultConfig();
  PlaygroundWiiUHostCallbacks callbacks = {0};
  ProbeState state = {0};
  callbacks.onStart = start;
  callbacks.onFrame = frame;
  callbacks.onRender = render;
  callbacks.onPointer = pointer;
  callbacks.onButtons = buttons;
  return playgroundWiiUHostRun(&config, &callbacks, &state);
}
