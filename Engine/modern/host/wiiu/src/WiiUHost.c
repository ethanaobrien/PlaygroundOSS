#include "Playground/Host/WiiUHost.h"

#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <coreinit/debug.h>
#include <proc_ui/procui.h>
#include <EGL/egl.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/sdcard.h>
#include <wiiu_egl.h>
#include <GLES2/gl2.h>

#include <errno.h>
#include <stdio.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <string.h>

#define PLAYGROUND_WIIU_TOUCH_WIDTH 1280.0f
#define PLAYGROUND_WIIU_TOUCH_HEIGHT 720.0f

struct PlaygroundWiiUHost {
  PlaygroundWiiUHostConfig config;
  PlaygroundWiiUHostCallbacks callbacks;
  void *callbackContext;
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
  wiiu_egl_window *window;
  bool running;
  bool touchDown;
  float touchX;
  float touchY;
  bool vpadInitialized;
  bool sdMounted;
  bool foreground;
  GLuint bootstrapTextProgram;
  GLint bootstrapTextPosition;
  GLint bootstrapTextViewport;
};

static char g_error[256];
static float g_motionAzimuth;
static float g_motionElevation;
static bool g_motionAvailable;
static PlaygroundWiiUHost *g_activeHost;
static void cancelTouch(PlaygroundWiiUHost *host);

static const unsigned char *bootstrapGlyph(char character) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  static const unsigned char percent[7] = {0x19, 0x1a, 0x04, 0x04,
                                            0x0b, 0x13, 0x00};
  static const unsigned char digits[10][7] = {
      {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
      {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
      {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
      {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
      {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
      {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
      {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e},
      {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
      {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
      {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}};
  static const unsigned char letters[26][7] = {
      {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
      {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
      {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
      {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
      {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
      {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
      {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
      {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
      {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
      {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e},
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
      {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
      {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
      {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
      {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
      {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
      {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
      {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
      {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
      {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
      {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
      {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
      {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}};
  if (character >= '0' && character <= '9')
    return digits[character - '0'];
  if (character >= 'A' && character <= 'Z')
    return letters[character - 'A'];
  if (character == '%')
    return percent;
  return blank;
}

static GLuint compileBootstrapShader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static bool ensureBootstrapTextProgram(PlaygroundWiiUHost *host) {
  if (host->bootstrapTextProgram)
    return true;
  static const char vertexSource[] =
      "attribute vec2 position;\n"
      "uniform vec2 viewportSize;\n"
      "void main() {\n"
      "  vec2 clip = vec2(position.x * 2.0 / viewportSize.x - 1.0,\n"
      "                   1.0 - position.y * 2.0 / viewportSize.y);\n"
      "  gl_Position = vec4(clip, 0.0, 1.0);\n"
      "}\n";
  static const char fragmentSource[] =
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }\n";
  GLuint vertex = compileBootstrapShader(GL_VERTEX_SHADER, vertexSource);
  GLuint fragment = compileBootstrapShader(GL_FRAGMENT_SHADER, fragmentSource);
  if (!vertex || !fragment) {
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    return false;
  }
  host->bootstrapTextProgram = glCreateProgram();
  glAttachShader(host->bootstrapTextProgram, vertex);
  glAttachShader(host->bootstrapTextProgram, fragment);
  glLinkProgram(host->bootstrapTextProgram);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(host->bootstrapTextProgram, GL_LINK_STATUS, &linked);
  if (!linked) {
    glDeleteProgram(host->bootstrapTextProgram);
    host->bootstrapTextProgram = 0;
    return false;
  }
  host->bootstrapTextPosition =
      glGetAttribLocation(host->bootstrapTextProgram, "position");
  host->bootstrapTextViewport =
      glGetUniformLocation(host->bootstrapTextProgram, "viewportSize");
  return host->bootstrapTextPosition >= 0 && host->bootstrapTextViewport >= 0;
}

static void drawBootstrapText(PlaygroundWiiUHost *host, const char *text,
                              float centerX, float topY) {
  GLfloat vertices[8192];
  unsigned vertexCount = 0;
  const float spacing = 6.0f;
  const size_t length = strlen(text);
  float left = centerX - ((float)length * spacing - 1.0f) * 2.5f;
  for (size_t character = 0; character < length; ++character) {
    const unsigned char *rows = bootstrapGlyph(text[character]);
    for (unsigned row = 0; row < 7; ++row) {
      for (unsigned column = 0; column < 5; ++column) {
        if ((rows[row] & (1u << (4u - column))) == 0)
          continue;
        if (vertexCount + 6 > sizeof(vertices) / (sizeof(vertices[0]) * 2))
          break;
        const float x = left + (float)column * 5.0f;
        const float y = topY + (float)row * 5.0f;
        const GLfloat square[12] = {x,        y,        x + 4.0f, y,
                                    x + 4.0f, y + 4.0f, x,        y,
                                    x + 4.0f, y + 4.0f, x,        y + 4.0f};
        memcpy(&vertices[vertexCount * 2], square, sizeof(square));
        vertexCount += 6;
      }
    }
    left += spacing * 5.0f;
  }
  if (!vertexCount || !ensureBootstrapTextProgram(host))
    return;
  glUseProgram(host->bootstrapTextProgram);
  glUniform2f(host->bootstrapTextViewport, (GLfloat)host->config.renderWidth,
              (GLfloat)host->config.renderHeight);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray((GLuint)host->bootstrapTextPosition);
  glVertexAttribPointer((GLuint)host->bootstrapTextPosition, 2, GL_FLOAT,
                        GL_FALSE, 0, vertices);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertexCount);
  glDisableVertexAttribArray((GLuint)host->bootstrapTextPosition);
}

static uint32_t onAcquireForeground(void *opaque) {
  PlaygroundWiiUHost *host = (PlaygroundWiiUHost *)opaque;
  if (host) {
    host->foreground = true;
    if (host->callbacks.onVisibility)
      host->callbacks.onVisibility(host->callbackContext, host, true);
  }
  return 0;
}

static uint32_t onReleaseForeground(void *opaque) {
  PlaygroundWiiUHost *host = (PlaygroundWiiUHost *)opaque;
  if (host) {
    host->foreground = false;
    cancelTouch(host);
    if (host->callbacks.onVisibility)
      host->callbacks.onVisibility(host->callbackContext, host, false);
  }
  return 0;
}

static bool ensureDirectory(const char *path) {
  return mkdir(path, 0777) == 0 || errno == EEXIST;
}

static bool filesEqual(const char *leftPath, const char *rightPath) {
  FILE *left = fopen(leftPath, "rb");
  FILE *right = fopen(rightPath, "rb");
  if (!left || !right) {
    if (left)
      fclose(left);
    if (right)
      fclose(right);
    return false;
  }

  unsigned char leftBuffer[4096];
  unsigned char rightBuffer[4096];
  bool equal = true;
  for (;;) {
    const size_t leftCount = fread(leftBuffer, 1, sizeof(leftBuffer), left);
    const size_t rightCount =
        fread(rightBuffer, 1, sizeof(rightBuffer), right);
    if (leftCount != rightCount ||
        (leftCount && memcmp(leftBuffer, rightBuffer, leftCount) != 0)) {
      equal = false;
      break;
    }
    if (!leftCount)
      break;
  }
  if (ferror(left) || ferror(right))
    equal = false;
  fclose(left);
  fclose(right);
  return equal;
}

static bool installShaderCompiler(PlaygroundWiiUHost *host) {
  host->sdMounted = WHBMountSdCard();
  if (!host->sdMounted)
    return false;

  const char *root = WHBGetSdCardMountPath();
  char wiiuPath[512];
  char libsPath[512];
  char destination[512];
  char temporary[512];
  snprintf(wiiuPath, sizeof(wiiuPath), "%s/wiiu", root);
  snprintf(libsPath, sizeof(libsPath), "%s/wiiu/libs", root);
  snprintf(destination, sizeof(destination), "%s/glslcompiler.rpl", libsPath);
  snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
  if (!ensureDirectory(wiiuPath) || !ensureDirectory(libsPath))
    return false;

  struct stat sourceInfo;
  if (stat("/vol/content/glslcompiler.rpl", &sourceInfo) != 0)
    return false;
  if (filesEqual("/vol/content/glslcompiler.rpl", destination))
    return true;

  FILE *source = fopen("/vol/content/glslcompiler.rpl", "rb");
  FILE *output = fopen(temporary, "wb");
  if (!source || !output) {
    if (source)
      fclose(source);
    if (output)
      fclose(output);
    return false;
  }
  unsigned char buffer[4096];
  bool success = true;
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
    if (fwrite(buffer, 1, count, output) != count) {
      success = false;
      break;
    }
  }
  if (ferror(source) || fflush(output) != 0)
    success = false;
  fclose(source);
  if (fclose(output) != 0)
    success = false;
  if (!success || rename(temporary, destination) != 0) {
    remove(temporary);
    return false;
  }
  return true;
}

static void redirectStandardStreamsToSdCard(const PlaygroundWiiUHost *host) {
  if (!host || !host->sdMounted)
    return;

  const char *root = WHBGetSdCardMountPath();
  if (!root)
    return;

  char stdoutPath[512];
  char stderrPath[512];
  snprintf(stdoutPath, sizeof(stdoutPath), "%s/playground-wiiu-stdout.log",
           root);
  snprintf(stderrPath, sizeof(stderrPath), "%s/playground-wiiu-stderr.log",
           root);

  /* Cemu cannot safely execute the console devoptab callback reached by
     newlib's fwrite path. Lua print() eventually uses that path, as do a few
     third-party diagnostics. Keep those streams useful without routing worker
     output through OSReport's non-thread-safe HLE implementation. */
  if (freopen(stdoutPath, "a", stdout))
    setvbuf(stdout, NULL, _IOLBF, 0);
  if (freopen(stderrPath, "a", stderr))
    setvbuf(stderr, NULL, _IOLBF, 0);
}

const char *playgroundWiiUHostLastError(void) {
  return g_error[0] ? g_error : NULL;
}

PlaygroundWiiUHostConfig playgroundWiiUHostDefaultConfig(void) {
  PlaygroundWiiUHostConfig config;
  config.renderWidth = 960;
  config.renderHeight = 640;
  config.maximumFrames = 0;
  return config;
}

void playgroundWiiUHostRequestQuit(PlaygroundWiiUHost *host) {
  if (host)
    host->running = false;
}

PlaygroundWiiUHost *playgroundWiiUHostGetActive(void) { return g_activeHost; }

static void cancelTouch(PlaygroundWiiUHost *host) {
  if (!host->touchDown)
    return;
  host->touchDown = false;
  if (host->callbacks.onPointer)
    host->callbacks.onPointer(host->callbackContext, host,
                              PLAYGROUND_WIIU_POINTER_CANCEL, host->touchX,
                              host->touchY);
}

static void pumpInput(PlaygroundWiiUHost *host) {
  VPADStatus status;
  VPADReadError error = VPAD_READ_SUCCESS;
  const int readCount = VPADRead(VPAD_CHAN_0, &status, 1, &error);
  if (error == VPAD_READ_NO_SAMPLES || error == VPAD_READ_BUSY)
    return;
  if (readCount != 1 || error != VPAD_READ_SUCCESS) {
    cancelTouch(host);
    return;
  }

  if (host->callbacks.onButtons)
    host->callbacks.onButtons(host->callbackContext, host, status.trigger,
                              status.release, status.hold);
  g_motionAzimuth = status.angle.z;
  g_motionElevation = status.angle.x;
  g_motionAvailable = true;

  VPADTouchData touch;
  VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_1280X720, &touch,
                             &status.tpNormal);
  const bool validX = (touch.validity & VPAD_INVALID_X) == 0;
  const bool validY = (touch.validity & VPAD_INVALID_Y) == 0;
  float x = (float)touch.x * ((float)host->config.renderWidth /
                              PLAYGROUND_WIIU_TOUCH_WIDTH);
  float y = (float)touch.y * ((float)host->config.renderHeight /
                              PLAYGROUND_WIIU_TOUCH_HEIGHT);
  if (x < 0.0f)
    x = 0.0f;
  else if (x >= (float)host->config.renderWidth)
    x = (float)host->config.renderWidth - 1.0f;
  if (y < 0.0f)
    y = 0.0f;
  else if (y >= (float)host->config.renderHeight)
    y = (float)host->config.renderHeight - 1.0f;
  if (touch.touched && validX && validY) {
    const PlaygroundWiiUPointerPhase phase =
        host->touchDown ? PLAYGROUND_WIIU_POINTER_MOVE
                        : PLAYGROUND_WIIU_POINTER_DOWN;
    host->touchDown = true;
    host->touchX = x;
    host->touchY = y;
    if (host->callbacks.onPointer)
      host->callbacks.onPointer(host->callbackContext, host, phase, x, y);
  } else if (host->touchDown) {
    host->touchDown = false;
    if (host->callbacks.onPointer)
      host->callbacks.onPointer(host->callbackContext, host,
                                PLAYGROUND_WIIU_POINTER_UP, host->touchX,
                                host->touchY);
  }
}

static bool initializeGraphics(PlaygroundWiiUHost *host) {
  const EGLint configAttributes[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
      EGL_WINDOW_BIT,      EGL_RED_SIZE,       8,
      EGL_GREEN_SIZE,      8,                  EGL_BLUE_SIZE,
      8,                   EGL_ALPHA_SIZE,     8,
      EGL_DEPTH_SIZE,      24,                 EGL_STENCIL_SIZE,
      8,                   EGL_NONE};
  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  host->window = wiiu_egl_window_create(host->config.renderWidth,
                                        host->config.renderHeight,
                                        WIIU_EGL_WINDOW_FLAG_TV |
                                            WIIU_EGL_WINDOW_FLAG_DRC);
  host->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  const EGLBoolean initialized =
      host->display != EGL_NO_DISPLAY &&
      eglInitialize(host->display, NULL, NULL);
  const EGLBoolean bound = initialized && eglBindAPI(EGL_OPENGL_ES_API);
  const EGLBoolean configured =
      bound && eglChooseConfig(host->display, configAttributes, &config, 1,
                               &count);
  if (!host->window || host->display == EGL_NO_DISPLAY || !initialized ||
      !bound || !configured || count != 1) {
    snprintf(g_error, sizeof(g_error),
             "Wii U EGL display initialization failed (0x%04x; window=%p "
             "display=%p initialized=%u bound=%u configured=%u count=%d)",
             (unsigned)eglGetError(), host->window, host->display,
             (unsigned)initialized, (unsigned)bound, (unsigned)configured,
             (int)count);
    return false;
  }
  host->surface = eglCreateWindowSurface(host->display, config,
                                         (EGLNativeWindowType)host->window,
                                         NULL);
  host->context = eglCreateContext(host->display, config, EGL_NO_CONTEXT,
                                   contextAttributes);
  const EGLBoolean current =
      host->context != EGL_NO_CONTEXT && host->surface != EGL_NO_SURFACE &&
      eglMakeCurrent(host->display, host->surface, host->surface,
                     host->context);
  if (host->context == EGL_NO_CONTEXT || host->surface == EGL_NO_SURFACE ||
      !current) {
    snprintf(g_error, sizeof(g_error),
             "Wii U EGL context initialization failed (0x%04x)",
             (unsigned)eglGetError());
    return false;
  }
  eglSwapInterval(host->display, 1);
  return true;
}

static void shutdownGraphics(PlaygroundWiiUHost *host) {
  if (host->bootstrapTextProgram) {
    glDeleteProgram(host->bootstrapTextProgram);
    host->bootstrapTextProgram = 0;
  }
  if (host->display != EGL_NO_DISPLAY)
    eglMakeCurrent(host->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
  if (host->surface != EGL_NO_SURFACE)
    eglDestroySurface(host->display, host->surface);
  if (host->context != EGL_NO_CONTEXT)
    eglDestroyContext(host->display, host->context);
  if (host->display != EGL_NO_DISPLAY)
    eglTerminate(host->display);
  if (host->window)
    wiiu_egl_window_destroy(host->window);
}

int playgroundWiiUHostRun(const PlaygroundWiiUHostConfig *config,
                          const PlaygroundWiiUHostCallbacks *callbacks,
                          void *context) {
  g_error[0] = '\0';
  if (!callbacks) {
    snprintf(g_error, sizeof(g_error), "Wii U host callbacks are required");
    return 2;
  }

  PlaygroundWiiUHost host;
  memset(&host, 0, sizeof(host));
  host.config = config ? *config : playgroundWiiUHostDefaultConfig();
  host.callbacks = *callbacks;
  host.callbackContext = context;
  host.display = EGL_NO_DISPLAY;
  host.context = EGL_NO_CONTEXT;
  host.surface = EGL_NO_SURFACE;
  host.running = true;
  host.foreground = true;
  g_activeHost = &host;

  WHBProcInit();
  /* VPADInit is a no-op on current Cafe OS, but remains required by older
     system versions. Keep the matching shutdown so input has a complete
     lifetime on both environments. */
  VPADInit();
  host.vpadInitialized = true;
  OSReport("PlaygroundOSS Wii U: ProcUI initialized\n");
  ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, onAcquireForeground, &host,
                         0);
  ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, onReleaseForeground, &host,
                         0);
  /* CafeGLSL is a shared platform runtime, not user data. A WUHB cannot make
     an RPL visible to the loader directly, so install the pinned bundled copy
     into the standard Aroma library directory before EGL initializes. Cemu
     and installed RPX packages may already resolve the sibling module. */
  if (installShaderCompiler(&host)) {
    redirectStandardStreamsToSdCard(&host);
    OSReport("PlaygroundOSS Wii U: CafeGLSL runtime available\n");
  } else {
    OSReport("PlaygroundOSS Wii U: CafeGLSL runtime installation failed\n");
  }

  int result = 0;
  if (!initializeGraphics(&host)) {
    result = 3;
    host.running = false;
  } else if (host.callbacks.onStart &&
      !host.callbacks.onStart(host.callbackContext, &host)) {
    snprintf(g_error, sizeof(g_error), "Wii U host startup callback failed");
    result = 3;
    host.running = false;
  } else {
    OSReport("PlaygroundOSS Wii U: startup callback complete\n");
  }

  uint32_t frameCount = 0;
  OSTime previous = OSGetTime();
  while (host.running && WHBProcIsRunning()) {
    const OSTime now = OSGetTime();
    const uint64_t deltaNs = OSTicksToNanoseconds(now - previous);
    previous = now;
    pumpInput(&host);
    if (host.callbacks.onFrame &&
        !host.callbacks.onFrame(host.callbackContext, &host, deltaNs))
      host.running = false;
    if (host.running && host.callbacks.onRender)
      host.callbacks.onRender(host.callbackContext, &host,
                              (int)host.config.renderWidth,
                              (int)host.config.renderHeight);
    ++frameCount;
    if (host.config.maximumFrames &&
        frameCount >= host.config.maximumFrames)
      host.running = false;
  }

  cancelTouch(&host);
  if (host.callbacks.onStop)
    host.callbacks.onStop(host.callbackContext, &host);
  shutdownGraphics(&host);
  fflush(stdout);
  fflush(stderr);
  if (host.sdMounted)
    WHBUnmountSdCard();
  if (host.vpadInitialized)
    VPADShutdown();
  WHBProcShutdown();
  g_activeHost = NULL;
  return result;
}
void playgroundWiiUHostSwapBuffers(PlaygroundWiiUHost *host) {
  if (host && host->display != EGL_NO_DISPLAY &&
      host->surface != EGL_NO_SURFACE)
    eglSwapBuffers(host->display, host->surface);
}

bool playgroundWiiUHostPresentBootstrapProgress(PlaygroundWiiUHost *host,
                                                uint64_t completed,
                                                uint64_t total) {
  if (!host || !host->running || host->surface == EGL_NO_SURFACE)
    return false;
  /* Lifecycle processing belongs to the normal WHB host loop; progress
     presentation only draws the latest installer state. */
  if (!host->foreground)
    return true;

  unsigned percent = total ? (unsigned)((completed * 100) / total) : 100;
  if (percent > 100)
    percent = 100;
  const int width = (int)host->config.renderWidth;
  const int height = (int)host->config.renderHeight;

  GLint previousFramebuffer = 0;
  GLint previousProgram = 0;
  GLint previousArrayBuffer = 0;
  GLint previousViewport[4] = {0, 0, 0, 0};
  GLint previousScissorBox[4] = {0, 0, 0, 0};
  GLboolean previousColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
  GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean previousDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean previousCullEnabled = glIsEnabled(GL_CULL_FACE);
  const GLboolean previousBlendEnabled = glIsEnabled(GL_BLEND);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
  glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_SCISSOR_TEST);
  /* Cemu's Wii U ANGLE path expands scissored clears to the whole target.
     Show progress as a steadily brightening blue/teal field instead. It is
     intentionally shader- and geometry-free so it also works before the
     engine renderer and bundled GLSL compiler are initialized. */
  const float progress = (float)percent / 100.0f;
  glClearColor(0.04f + 0.04f * progress, 0.08f + 0.35f * progress,
               0.12f + 0.25f * progress, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%u%%", percent);
  drawBootstrapText(host, "STARTING PLAYGROUNDOSS", (float)width * 0.5f,
                    (float)height * 0.43f);
  drawBootstrapText(host, percentage, (float)width * 0.5f,
                    (float)height * 0.54f);
  const EGLBoolean swapped = eglSwapBuffers(host->display, host->surface);

  glUseProgram((GLuint)previousProgram);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
             previousViewport[3]);
  glColorMask(previousColorMask[0], previousColorMask[1],
              previousColorMask[2], previousColorMask[3]);
  glScissor(previousScissorBox[0], previousScissorBox[1],
            previousScissorBox[2], previousScissorBox[3]);
  if (previousScissorEnabled)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
  if (previousDepthEnabled)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (previousCullEnabled)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  if (previousBlendEnabled)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  glClearColor(previousClearColor[0], previousClearColor[1],
               previousClearColor[2], previousClearColor[3]);

  if (!swapped) {
    snprintf(g_error, sizeof(g_error),
             "Wii U AppAssets progress presentation failed (0x%04x)",
             (unsigned)eglGetError());
    host->running = false;
    return false;
  }
  return true;
}

bool playgroundWiiUHostPumpBootstrapProgress(PlaygroundWiiUHost *host,
                                             uint64_t completed,
                                             uint64_t total) {
  if (!host || !host->running || !WHBProcIsRunning()) {
    if (host)
      host->running = false;
    return false;
  }
  pumpInput(host);
  return playgroundWiiUHostPresentBootstrapProgress(host, completed, total);
}

bool playgroundWiiUHostGetMotionAngles(float *azimuth, float *elevation) {
  if (azimuth)
    *azimuth = g_motionAzimuth;
  if (elevation)
    *elevation = g_motionElevation;
  return g_motionAvailable;
}

void *playgroundWiiUHostGetGLProcAddress(const char *name) {
  return (void *)eglGetProcAddress(name);
}
