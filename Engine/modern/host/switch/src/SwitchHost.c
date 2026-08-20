#include "Playground/Host/SwitchHost.h"

#include <switch.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <string.h>

#define PLAYGROUND_TOUCH_WIDTH 1280.0f
#define PLAYGROUND_TOUCH_HEIGHT 720.0f
#define PLAYGROUND_MAX_TOUCHES 10

typedef struct PlaygroundTouch {
  uint32_t fingerId;
  float x;
  float y;
  bool active;
  bool seen;
} PlaygroundTouch;

struct PlaygroundSwitchHost {
  PlaygroundSwitchHostCallbacks callbacks;
  PlaygroundSwitchHostConfig config;
  void *callbackContext;
  AppletHookCookie appletHook;
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
  EGLConfig eglConfig;
  PadState pad;
  PlaygroundTouch touches[PLAYGROUND_MAX_TOUCHES];
  float controllerX;
  float controllerY;
  bool controllerPointerDown;
  AppletFocusState focus;
  AppletOperationMode operationMode;
  ApmPerformanceMode performanceMode;
  int width;
  int height;
  bool running;
  bool exitRequested;
  bool resumePending;
  bool focusPending;
  bool operationModePending;
  bool performanceModePending;
  uint64_t bootstrapLastTick;
  unsigned bootstrapLastPercent;
  bool bootstrapPresented;
  int failureCode;
};

static char g_error[256];

static void setError(const char *message) {
  snprintf(g_error, sizeof(g_error), "%s (EGL 0x%04x)", message,
           (unsigned)eglGetError());
}

const char *playgroundSwitchHostLastError(void) {
  return g_error[0] ? g_error : NULL;
}

static void appletEvent(AppletHookType event, void *opaque) {
  PlaygroundSwitchHost *host = opaque;
  switch (event) {
  case AppletHookType_OnFocusState:
    host->focusPending = true;
    break;
  case AppletHookType_OnOperationMode:
    host->operationModePending = true;
    break;
  case AppletHookType_OnPerformanceMode:
    host->performanceModePending = true;
    break;
  case AppletHookType_OnResume:
    host->resumePending = true;
    break;
  case AppletHookType_OnExitRequest:
    host->exitRequested = true;
    break;
  default:
    break;
  }
}

static void dimensions(const PlaygroundSwitchHost *host,
                       AppletOperationMode mode, int *width, int *height) {
  if (mode == AppletOperationMode_Console) {
    *width = (int)host->config.dockedWidth;
    *height = (int)host->config.dockedHeight;
  } else {
    *width = (int)host->config.handheldWidth;
    *height = (int)host->config.handheldHeight;
  }
}

static bool createSurface(PlaygroundSwitchHost *host,
                          AppletOperationMode mode) {
  const EGLint attributes[] = {EGL_NONE};
  int width;
  int height;
  dimensions(host, mode, &width, &height);
  Result result = nwindowSetDimensions(nwindowGetDefault(), (uint32_t)width,
                                       (uint32_t)height);
  if (R_FAILED(result)) {
    snprintf(g_error, sizeof(g_error), "nwindowSetDimensions failed: 0x%08x",
             result);
    return false;
  }
  host->surface = eglCreateWindowSurface(host->display, host->eglConfig,
                                         nwindowGetDefault(), attributes);
  if (host->surface == EGL_NO_SURFACE) {
    setError("eglCreateWindowSurface failed");
    return false;
  }
  if (!eglMakeCurrent(host->display, host->surface, host->surface,
                      host->context)) {
    setError("eglMakeCurrent failed");
    return false;
  }
  eglSwapInterval(host->display, host->config.verticalSync ? 1 : 0);
  if (host->width > 0 && host->height > 0) {
    host->controllerX *= (float)width / (float)host->width;
    host->controllerY *= (float)height / (float)host->height;
  } else {
    host->controllerX = width * 0.5f;
    host->controllerY = height * 0.5f;
  }
  host->width = width;
  host->height = height;
  host->operationMode = mode;
  return true;
}

static void destroySurface(PlaygroundSwitchHost *host) {
  if (host->surface == EGL_NO_SURFACE)
    return;
  eglMakeCurrent(host->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(host->display, host->surface);
  host->surface = EGL_NO_SURFACE;
}

static bool initializeGraphics(PlaygroundSwitchHost *host) {
  const EGLint configAttributes[] = {EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_ES2_BIT,
                                     EGL_SURFACE_TYPE,
                                     EGL_WINDOW_BIT,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_ALPHA_SIZE,
                                     8,
                                     EGL_DEPTH_SIZE,
                                     24,
                                     EGL_STENCIL_SIZE,
                                     8,
                                     EGL_NONE};
  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLint configCount = 0;
  host->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (host->display == EGL_NO_DISPLAY ||
      !eglInitialize(host->display, NULL, NULL) ||
      !eglBindAPI(EGL_OPENGL_ES_API) ||
      !eglChooseConfig(host->display, configAttributes, &host->eglConfig, 1,
                       &configCount) ||
      configCount != 1) {
    setError("EGL display initialization failed");
    return false;
  }
  host->context = eglCreateContext(host->display, host->eglConfig,
                                   EGL_NO_CONTEXT, contextAttributes);
  if (host->context == EGL_NO_CONTEXT) {
    setError("eglCreateContext failed");
    return false;
  }
  return createSurface(host, appletGetOperationMode());
}

static void shutdownGraphics(PlaygroundSwitchHost *host) {
  destroySurface(host);
  if (host->context != EGL_NO_CONTEXT) {
    eglDestroyContext(host->display, host->context);
    host->context = EGL_NO_CONTEXT;
  }
  if (host->display != EGL_NO_DISPLAY) {
    eglTerminate(host->display);
    host->display = EGL_NO_DISPLAY;
  }
}

static PlaygroundSwitchVisibility visibility(AppletFocusState state) {
  if (state == AppletFocusState_InFocus)
    return PLAYGROUND_SWITCH_VISIBILITY_FOREGROUND;
  if (state == AppletFocusState_OutOfFocus)
    return PLAYGROUND_SWITCH_VISIBILITY_OBSCURED;
  return PLAYGROUND_SWITCH_VISIBILITY_BACKGROUND;
}

static void cancelTouches(PlaygroundSwitchHost *host) {
  for (int i = 0; i < PLAYGROUND_MAX_TOUCHES; ++i) {
    PlaygroundTouch *touch = &host->touches[i];
    if (!touch->active)
      continue;
    if (host->callbacks.onPointer)
      host->callbacks.onPointer(host->callbackContext, host, i,
                                PLAYGROUND_SWITCH_POINTER_CANCEL, touch->x,
                                touch->y);
    touch->active = false;
  }
  if (host->controllerPointerDown) {
    if (host->callbacks.onPointer)
      host->callbacks.onPointer(host->callbackContext, host, 0,
                                PLAYGROUND_SWITCH_POINTER_CANCEL,
                                host->controllerX, host->controllerY);
    host->controllerPointerDown = false;
  }
}

static void dispatchAppletEvents(PlaygroundSwitchHost *host) {
  if (host->exitRequested) {
    host->exitRequested = false;
    host->running = false;
  }
  if (host->focusPending) {
    AppletFocusState next = appletGetFocusState();
    host->focusPending = false;
    if (next != host->focus) {
      host->focus = next;
      if (next != AppletFocusState_InFocus)
        cancelTouches(host);
      if (host->callbacks.onVisibility)
        host->callbacks.onVisibility(host->callbackContext, host,
                                     visibility(next));
    }
  }
  if (host->resumePending) {
    host->resumePending = false;
    if (host->callbacks.onResume)
      host->callbacks.onResume(host->callbackContext, host);
  }
  if (host->operationModePending) {
    AppletOperationMode next = appletGetOperationMode();
    host->operationModePending = false;
    if (next != host->operationMode) {
      cancelTouches(host);
      destroySurface(host);
      if (!createSurface(host, next)) {
        host->failureCode = 5;
        host->running = false;
      } else if (host->callbacks.onResize) {
        host->callbacks.onResize(host->callbackContext, host, host->width,
                                 host->height);
      }
    }
  }
  if (host->performanceModePending) {
    ApmPerformanceMode next = appletGetPerformanceMode();
    host->performanceModePending = false;
    if (next != host->performanceMode) {
      host->performanceMode = next;
      if (host->callbacks.onPerformanceMode)
        host->callbacks.onPerformanceMode(host->callbackContext, host,
                                          (int)next);
    }
  }
}

static int touchSlot(PlaygroundSwitchHost *host, uint32_t id, bool create) {
  int available = -1;
  for (int i = 0; i < PLAYGROUND_MAX_TOUCHES; ++i) {
    if (host->touches[i].active && host->touches[i].fingerId == id)
      return i;
    if (!host->touches[i].active && available < 0)
      available = i;
  }
  if (create && available >= 0) {
    host->touches[available].active = true;
    host->touches[available].fingerId = id;
    return available;
  }
  return -1;
}

static void pumpInput(PlaygroundSwitchHost *host) {
  padUpdate(&host->pad);
  const u64 pressed = padGetButtonsDown(&host->pad);
  const u64 released = padGetButtonsUp(&host->pad);
  const u64 held = padGetButtons(&host->pad);
  if (host->callbacks.onButtons)
    host->callbacks.onButtons(host->callbackContext, host, pressed, released,
                              held);

  if (host->operationMode == AppletOperationMode_Console) {
    const HidAnalogStickState stick = padGetStickPos(&host->pad, 0);
    const float scale = (float)host->width / PLAYGROUND_TOUCH_WIDTH;
    float dx = 0.0f;
    float dy = 0.0f;
    if (stick.x < -4096 || stick.x > 4096)
      dx += ((float)stick.x / 32768.0f) * 24.0f * scale;
    if (stick.y < -4096 || stick.y > 4096)
      dy -= ((float)stick.y / 32768.0f) * 24.0f * scale;
    if (held & HidNpadButton_Left)
      dx -= 12.0f * scale;
    if (held & HidNpadButton_Right)
      dx += 12.0f * scale;
    if (held & HidNpadButton_Up)
      dy -= 12.0f * scale;
    if (held & HidNpadButton_Down)
      dy += 12.0f * scale;
    const float oldX = host->controllerX;
    const float oldY = host->controllerY;
    host->controllerX += dx;
    host->controllerY += dy;
    if (host->controllerX < 0.0f)
      host->controllerX = 0.0f;
    if (host->controllerY < 0.0f)
      host->controllerY = 0.0f;
    if (host->controllerX > (float)(host->width - 1))
      host->controllerX = (float)(host->width - 1);
    if (host->controllerY > (float)(host->height - 1))
      host->controllerY = (float)(host->height - 1);
    const bool moved = oldX != host->controllerX || oldY != host->controllerY;
    if ((pressed & HidNpadButton_A) && host->callbacks.onPointer) {
      host->controllerPointerDown = true;
      host->callbacks.onPointer(host->callbackContext, host, 0,
                                PLAYGROUND_SWITCH_POINTER_DOWN,
                                host->controllerX, host->controllerY);
    } else if (host->controllerPointerDown && moved &&
               host->callbacks.onPointer) {
      host->callbacks.onPointer(host->callbackContext, host, 0,
                                PLAYGROUND_SWITCH_POINTER_MOVE,
                                host->controllerX, host->controllerY);
    }
    if ((released & HidNpadButton_A) && host->controllerPointerDown) {
      if (host->callbacks.onPointer)
        host->callbacks.onPointer(host->callbackContext, host, 0,
                                  PLAYGROUND_SWITCH_POINTER_UP,
                                  host->controllerX, host->controllerY);
      host->controllerPointerDown = false;
    }
  }

  for (int i = 0; i < PLAYGROUND_MAX_TOUCHES; ++i)
    host->touches[i].seen = false;
  HidTouchScreenState state = {0};
  size_t count = hidGetTouchScreenStates(&state, 1);
  if (count) {
    float scaleX = (float)host->width / PLAYGROUND_TOUCH_WIDTH;
    float scaleY = (float)host->height / PLAYGROUND_TOUCH_HEIGHT;
    for (int i = 0; i < state.count && i < PLAYGROUND_MAX_TOUCHES; ++i) {
      const HidTouchState *source = &state.touches[i];
      int slot = touchSlot(host, source->finger_id, false);
      bool down = slot < 0;
      if (down)
        slot = touchSlot(host, source->finger_id, true);
      if (slot < 0)
        continue;
      PlaygroundTouch *touch = &host->touches[slot];
      touch->seen = true;
      touch->x = source->x * scaleX;
      touch->y = source->y * scaleY;
      if (host->callbacks.onPointer)
        host->callbacks.onPointer(host->callbackContext, host, slot,
                                  down ? PLAYGROUND_SWITCH_POINTER_DOWN
                                       : PLAYGROUND_SWITCH_POINTER_MOVE,
                                  touch->x, touch->y);
    }
  }
  for (int i = 0; i < PLAYGROUND_MAX_TOUCHES; ++i) {
    PlaygroundTouch *touch = &host->touches[i];
    if (!touch->active || touch->seen)
      continue;
    if (host->callbacks.onPointer)
      host->callbacks.onPointer(host->callbackContext, host, i,
                                PLAYGROUND_SWITCH_POINTER_UP, touch->x,
                                touch->y);
    touch->active = false;
  }
}

static void clearCursorRectangle(PlaygroundSwitchHost *host, int x, int y,
                                 int width, int height, float red, float green,
                                 float blue) {
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x + width > host->width)
    width = host->width - x;
  if (y + height > host->height)
    height = host->height - y;
  if (width <= 0 || height <= 0)
    return;
  glScissor(x, host->height - y - height, width, height);
  glClearColor(red, green, blue, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

static void drawControllerCursor(PlaygroundSwitchHost *host) {
  if (host->operationMode != AppletOperationMode_Console)
    return;
  const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  GLint scissor[4];
  GLint framebuffer = 0;
  GLfloat clearColor[4];
  GLboolean colorMask[4];
  glGetIntegerv(GL_SCISSOR_BOX, scissor);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
  glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
  glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  const int x = (int)host->controllerX;
  const int y = (int)host->controllerY;
  clearCursorRectangle(host, x - 12, y - 2, 25, 5, 0.05f, 0.05f, 0.05f);
  clearCursorRectangle(host, x - 2, y - 12, 5, 25, 0.05f, 0.05f, 0.05f);
  clearCursorRectangle(host, x - 10, y - 1, 21, 3, 1.0f, 0.22f, 0.55f);
  clearCursorRectangle(host, x - 1, y - 10, 3, 21, 1.0f, 0.22f, 0.55f);
  glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
  glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
  glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
  if (!scissorEnabled)
    glDisable(GL_SCISSOR_TEST);
}

PlaygroundSwitchHostConfig playgroundSwitchHostDefaultConfig(void) {
  PlaygroundSwitchHostConfig config = {0};
  config.handheldWidth = 1280;
  config.handheldHeight = 720;
  config.dockedWidth = 1920;
  config.dockedHeight = 1080;
  config.verticalSync = true;
  return config;
}

int playgroundSwitchHostRun(const PlaygroundSwitchHostConfig *config,
                            const PlaygroundSwitchHostCallbacks *callbacks,
                            void *context) {
  if (!config || !callbacks)
    return 64;
  PlaygroundSwitchHost host = {0};
  host.config = *config;
  host.callbacks = *callbacks;
  host.callbackContext = context;
  host.display = EGL_NO_DISPLAY;
  host.context = EGL_NO_CONTEXT;
  host.surface = EGL_NO_SURFACE;
  host.focus = appletGetFocusState();
  host.operationMode = appletGetOperationMode();
  host.performanceMode = appletGetPerformanceMode();
  host.running = true;

  Result focusResult = appletSetFocusHandlingMode(
      AppletFocusHandlingMode_SuspendHomeSleepNotify);
  if (R_FAILED(focusResult)) {
    snprintf(g_error, sizeof(g_error),
             "appletSetFocusHandlingMode failed: 0x%08x", focusResult);
    return 2;
  }
  appletHook(&host.appletHook, appletEvent, &host);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&host.pad);
  hidInitializeTouchScreen();

  bool started = false;
  int result = 0;
  if (!initializeGraphics(&host)) {
    result = 3;
  } else if (host.callbacks.onStart &&
             !host.callbacks.onStart(context, &host)) {
    result = 4;
  } else {
    started = true;
    if (host.callbacks.onVisibility)
      host.callbacks.onVisibility(host.callbackContext, &host,
                                  visibility(host.focus));
    uint64_t previous = armGetSystemTick();
    uint32_t frames = 0;
    while (host.running && appletMainLoop()) {
      dispatchAppletEvents(&host);
      if (!host.running)
        break;
      if (host.focus != AppletFocusState_InFocus) {
        /*
         * Out-of-focus library applets do not necessarily suspend the
         * process.  Do not run the engine or spin without EGL's vsync
         * while HOME, the keyboard, or another system UI owns focus.
         * Reset the clock so resume never receives the entire pause as
         * one giant frame delta.
         */
        previous = armGetSystemTick();
        svcSleepThread(16666667L);
        continue;
      }
      pumpInput(&host);
      uint64_t now = armGetSystemTick();
      if (host.callbacks.onFrame &&
          !host.callbacks.onFrame(context, &host, armTicksToNs(now - previous)))
        host.running = false;
      previous = now;
      if (config->maximumFrames && ++frames >= config->maximumFrames)
        host.running = false;
    }
  }
  if (started && host.callbacks.onStop)
    host.callbacks.onStop(context, &host);
  cancelTouches(&host);
  shutdownGraphics(&host);
  appletUnhook(&host.appletHook);
  return result ? result : host.failureCode;
}

void playgroundSwitchHostRequestQuit(PlaygroundSwitchHost *host) {
  if (host)
    host->running = false;
}

void playgroundSwitchHostSwapBuffers(PlaygroundSwitchHost *host) {
  if (host && host->surface != EGL_NO_SURFACE) {
    drawControllerCursor(host);
  }
  if (host && host->surface != EGL_NO_SURFACE &&
      !eglSwapBuffers(host->display, host->surface)) {
    setError("eglSwapBuffers failed");
    host->failureCode = 6;
    host->running = false;
  }
}

static void clearBootstrapRectangle(PlaygroundSwitchHost *host, int x, int y,
                                    int width, int height, float red,
                                    float green, float blue) {
  if (width <= 0 || height <= 0)
    return;
  glScissor(x, host->height - y - height, width, height);
  glClearColor(red, green, blue, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

static bool presentBootstrapFrame(PlaygroundSwitchHost *host,
                                  unsigned percent) {
  const int cardX = host->width / 8;
  const int cardY = host->height * 3 / 8;
  const int cardWidth = host->width * 3 / 4;
  const int cardHeight = host->height / 4;
  const int padding = host->width / 32;
  const int trackHeight = host->height / 28;
  const int trackX = cardX + padding;
  const int trackWidth = cardWidth - padding * 2;
  const int trackY = cardY + cardHeight / 2;
  const int fillWidth = (int)(((uint64_t)trackWidth * percent) / 100);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, host->width, host->height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.055f, 0.045f, 0.075f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  clearBootstrapRectangle(host, cardX, cardY, cardWidth, cardHeight, 0.16f,
                          0.14f, 0.19f);
  clearBootstrapRectangle(host, trackX, trackY, trackWidth, trackHeight, 0.29f,
                          0.27f, 0.32f);
  clearBootstrapRectangle(host, trackX, trackY, fillWidth, trackHeight, 1.0f,
                          0.22f, 0.55f);
  // A small activity marker makes even the initial 0% frame visibly distinct
  // from a hung black launch screen without depending on fonts or AppAssets.
  const int marker = host->height / 40;
  clearBootstrapRectangle(host, cardX + cardWidth / 2 - marker / 2,
                          cardY + cardHeight / 4 - marker / 2, marker, marker,
                          1.0f, 0.72f, 0.86f);
  glDisable(GL_SCISSOR_TEST);
  if (!eglSwapBuffers(host->display, host->surface)) {
    setError("eglSwapBuffers failed during AppAssets installation");
    host->failureCode = 6;
    host->running = false;
    return false;
  }
  return true;
}

bool playgroundSwitchHostPresentBootstrapProgress(PlaygroundSwitchHost *host,
                                                  uint64_t completed,
                                                  uint64_t total) {
  if (!host || !host->running)
    return false;

  // onStart runs before the ordinary frame loop. Service the same applet
  // lifecycle here so a long first install remains suspend/HOME/dock safe.
  for (;;) {
    if (!appletMainLoop()) {
      host->running = false;
      return false;
    }
    dispatchAppletEvents(host);
    if (!host->running || host->surface == EGL_NO_SURFACE)
      return false;
    if (host->focus == AppletFocusState_InFocus)
      break;
    svcSleepThread(16666667L);
  }

  unsigned percent = total ? (unsigned)((completed * 100) / total) : 100;
  if (percent > 100)
    percent = 100;
  const uint64_t now = armGetSystemTick();
  const bool elapsed =
      !host->bootstrapPresented ||
      armTicksToNs(now - host->bootstrapLastTick) >= UINT64_C(100000000);
  if (host->bootstrapPresented && percent == host->bootstrapLastPercent &&
      !elapsed)
    return true;

  host->bootstrapPresented = true;
  host->bootstrapLastPercent = percent;
  host->bootstrapLastTick = now;
  return presentBootstrapFrame(host, percent);
}

void playgroundSwitchHostGetPixelSize(const PlaygroundSwitchHost *host,
                                      int *width, int *height) {
  if (width)
    *width = host ? host->width : 0;
  if (height)
    *height = host ? host->height : 0;
}

void *playgroundSwitchHostGetGLProcAddress(const char *name) {
  return (void *)eglGetProcAddress(name);
}
