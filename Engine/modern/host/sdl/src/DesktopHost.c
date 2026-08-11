#include "Playground/Host/DesktopHost.h"

#include <SDL3/SDL.h>

#include <string.h>

struct PlaygroundDesktopHost {
    SDL_Window* window;
    SDL_GLContext graphicsContext;
    PlaygroundDesktopHostCallbacks callbacks;
    void* callbackContext;
    int pixelWidth;
    int pixelHeight;
    bool running;
    bool mouseDown;
    float mouseX;
    float mouseY;
    SDL_FingerID touchFingerIds[10];
    bool touchFingerUsed[10];
};

static void sendPointer(
    PlaygroundDesktopHost* host,
    int64_t pointerId,
    PlaygroundPointerPhase phase,
    float pixelX,
    float pixelY
);

static int touchSlot(PlaygroundDesktopHost* host, SDL_FingerID finger, bool create)
{
    int slot;
    for (slot = 1; slot < 10; ++slot) {
        if (host->touchFingerUsed[slot] && host->touchFingerIds[slot] == finger) {
            return slot;
        }
    }
    if (create) {
        for (slot = 1; slot < 10; ++slot) {
            if (!host->touchFingerUsed[slot]) {
                host->touchFingerUsed[slot] = true;
                host->touchFingerIds[slot] = finger;
                return slot;
            }
        }
    }
    return -1;
}

static void cancelMousePointer(PlaygroundDesktopHost* host)
{
    if (!host->mouseDown) {
        return;
    }
    host->mouseDown = false;
    SDL_CaptureMouse(false);
    sendPointer(host, 0, PLAYGROUND_POINTER_CANCEL, host->mouseX, host->mouseY);
}

static void refreshPixelSize(PlaygroundDesktopHost* host)
{
    if (host->window) {
        SDL_GetWindowSizeInPixels(host->window, &host->pixelWidth, &host->pixelHeight);
    }
}

static void logicalToPixels(
    PlaygroundDesktopHost* host,
    float logicalX,
    float logicalY,
    float* pixelX,
    float* pixelY
)
{
    int logicalWidth = host->pixelWidth;
    int logicalHeight = host->pixelHeight;
    if (host->window) {
        SDL_GetWindowSize(host->window, &logicalWidth, &logicalHeight);
    }
    *pixelX = logicalWidth > 0
        ? logicalX * (float)host->pixelWidth / (float)logicalWidth
        : logicalX;
    *pixelY = logicalHeight > 0
        ? logicalY * (float)host->pixelHeight / (float)logicalHeight
        : logicalY;
}

static void sendPointer(
    PlaygroundDesktopHost* host,
    int64_t pointerId,
    PlaygroundPointerPhase phase,
    float pixelX,
    float pixelY
)
{
    if (host->callbacks.onPointer) {
        host->callbacks.onPointer(
            host->callbackContext,
            host,
            pointerId,
            phase,
            pixelX,
            pixelY
        );
    }
}

static void processEvent(PlaygroundDesktopHost* host, const SDL_Event* event)
{
    float pixelX;
    float pixelY;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        host->running = false;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        refreshPixelSize(host);
        if (host->callbacks.onResize) {
            host->callbacks.onResize(
                host->callbackContext,
                host,
                host->pixelWidth,
                host->pixelHeight
            );
        }
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        cancelMousePointer(host);
        if (host->callbacks.onActivity) {
            host->callbacks.onActivity(host->callbackContext, host, false);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        cancelMousePointer(host);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        if (host->callbacks.onActivity) {
            host->callbacks.onActivity(host->callbackContext, host, true);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.which == SDL_TOUCH_MOUSEID ||
            event->button.button != SDL_BUTTON_LEFT) {
            break;
        }
        logicalToPixels(host, event->button.x, event->button.y, &pixelX, &pixelY);
        host->mouseX = pixelX;
        host->mouseY = pixelY;
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (host->mouseDown) {
                break;
            }
            host->mouseDown = true;
            SDL_CaptureMouse(true);
        } else {
            if (!host->mouseDown) {
                break;
            }
            host->mouseDown = false;
            SDL_CaptureMouse(false);
        }
        sendPointer(
            host,
            0,
            event->type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? PLAYGROUND_POINTER_DOWN
                : PLAYGROUND_POINTER_UP,
            pixelX,
            pixelY
        );
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.which == SDL_TOUCH_MOUSEID || !host->mouseDown ||
            !(event->motion.state & SDL_BUTTON_LMASK)) {
            break;
        }
        logicalToPixels(host, event->motion.x, event->motion.y, &pixelX, &pixelY);
        host->mouseX = pixelX;
        host->mouseY = pixelY;
        sendPointer(
            host,
            0,
            PLAYGROUND_POINTER_MOVE,
            pixelX,
            pixelY
        );
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
    {
        int slot = touchSlot(
            host,
            event->tfinger.fingerID,
            event->type == SDL_EVENT_FINGER_DOWN
        );
        if (slot < 0) {
            break;
        }
        pixelX = event->tfinger.x * (float)host->pixelWidth;
        pixelY = event->tfinger.y * (float)host->pixelHeight;
        sendPointer(
            host,
            slot,
            event->type == SDL_EVENT_FINGER_DOWN
                ? PLAYGROUND_POINTER_DOWN
                : event->type == SDL_EVENT_FINGER_MOTION
                    ? PLAYGROUND_POINTER_MOVE
                    : event->type == SDL_EVENT_FINGER_UP
                        ? PLAYGROUND_POINTER_UP
                        : PLAYGROUND_POINTER_CANCEL,
            pixelX,
            pixelY
        );
        if (event->type == SDL_EVENT_FINGER_UP ||
            event->type == SDL_EVENT_FINGER_CANCELED) {
            host->touchFingerUsed[slot] = false;
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (host->callbacks.onKey) {
            host->callbacks.onKey(
                host->callbackContext,
                host,
                (int32_t)event->key.key,
                (int32_t)event->key.scancode,
                event->type == SDL_EVENT_KEY_DOWN,
                event->key.repeat
            );
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (host->callbacks.onTextInput) {
            host->callbacks.onTextInput(
                host->callbackContext,
                host,
                event->text.text
            );
        }
        break;
    default:
        break;
    }
}

static bool createWindowAndContext(
    PlaygroundDesktopHost* host,
    const PlaygroundDesktopHostConfig* config
)
{
    SDL_WindowFlags flags = 0;
    if (config->resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config->highPixelDensity) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (config->graphicsProfile != PLAYGROUND_DESKTOP_GRAPHICS_NONE) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(
            SDL_GL_CONTEXT_PROFILE_MASK,
            config->graphicsProfile == PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_ES
                ? SDL_GL_CONTEXT_PROFILE_ES
                : SDL_GL_CONTEXT_PROFILE_CORE
        );
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, config->graphicsMajorVersion);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, config->graphicsMinorVersion);
    }

    host->window = SDL_CreateWindow(config->title, config->width, config->height, flags);
    if (!host->window) {
        return false;
    }
    if (config->graphicsProfile != PLAYGROUND_DESKTOP_GRAPHICS_NONE) {
        host->graphicsContext = SDL_GL_CreateContext(host->window);
        if (!host->graphicsContext) {
            SDL_DestroyWindow(host->window);
            host->window = NULL;
            return false;
        }
        SDL_GL_SetSwapInterval(config->verticalSync ? 1 : 0);
    }
    refreshPixelSize(host);
    return true;
}

PlaygroundDesktopHostConfig playgroundDesktopHostDefaultConfig(void)
{
    PlaygroundDesktopHostConfig config;
    memset(&config, 0, sizeof(config));
    config.title = "PlaygroundOSS";
    config.width = 960;
    config.height = 640;
    config.graphicsProfile = PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_CORE;
    config.graphicsMajorVersion = 3;
    config.graphicsMinorVersion = 2;
    config.highPixelDensity = true;
    config.resizable = true;
    config.verticalSync = true;
    return config;
}

int playgroundDesktopHostRun(
    const PlaygroundDesktopHostConfig* config,
    const PlaygroundDesktopHostCallbacks* callbacks,
    void* context
)
{
    PlaygroundDesktopHost host;
    SDL_Event event;
    uint64_t previousTick;
    uint32_t frames = 0;
    bool started = false;
    Uint32 initFlags;

    if (!config || !callbacks) {
        return 1;
    }
    memset(&host, 0, sizeof(host));
    host.callbacks = *callbacks;
    host.callbackContext = context;
    host.pixelWidth = config->width;
    host.pixelHeight = config->height;
    host.running = true;

    initFlags = SDL_INIT_EVENTS;
    if (config->audio) {
        initFlags |= SDL_INIT_AUDIO;
    }
    if (!config->headless) {
        initFlags |= SDL_INIT_VIDEO;
    }
    if (!SDL_Init(initFlags)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "SDL initialization failed: %s",
            SDL_GetError()
        );
        return 2;
    }
    if (!config->headless && !createWindowAndContext(&host, config)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "desktop host creation failed: %s",
            SDL_GetError()
        );
        SDL_Quit();
        return 3;
    }

    if (host.callbacks.onStart && !host.callbacks.onStart(context, &host)) {
        host.running = false;
    } else {
        started = true;
        if (host.window && host.callbacks.onTextInput) {
            SDL_StartTextInput(host.window);
        }
    }
    previousTick = SDL_GetTicksNS();
    while (host.running) {
        uint64_t now;
        while (SDL_PollEvent(&event)) {
            processEvent(&host, &event);
        }
        now = SDL_GetTicksNS();
        if (host.callbacks.onFrame
            && !host.callbacks.onFrame(context, &host, now - previousTick)) {
            host.running = false;
        }
        previousTick = now;
        ++frames;
        if (config->maximumFrames && frames >= config->maximumFrames) {
            host.running = false;
        }
        if (config->headless || !config->verticalSync) {
            SDL_Delay(1);
        }
    }

    if (started && host.callbacks.onStop) {
        host.callbacks.onStop(context, &host);
    }
    if (host.graphicsContext) {
        SDL_GL_DestroyContext(host.graphicsContext);
    }
    if (host.window) {
        if (host.callbacks.onTextInput) {
            SDL_StopTextInput(host.window);
        }
        SDL_DestroyWindow(host.window);
    }
    SDL_Quit();
    return 0;
}

void playgroundDesktopHostRequestQuit(PlaygroundDesktopHost* host)
{
    if (host) {
        host->running = false;
    }
}

void playgroundDesktopHostSwapBuffers(PlaygroundDesktopHost* host)
{
    if (host && host->window && host->graphicsContext) {
        SDL_GL_SwapWindow(host->window);
    }
}

void playgroundDesktopHostGetPixelSize(
    const PlaygroundDesktopHost* host,
    int* width,
    int* height
)
{
    if (width) {
        *width = host ? host->pixelWidth : 0;
    }
    if (height) {
        *height = host ? host->pixelHeight : 0;
    }
}

void* playgroundDesktopHostNativeWindow(const PlaygroundDesktopHost* host)
{
    return host ? host->window : NULL;
}

void* playgroundDesktopHostGetGLProcAddress(const char* name)
{
    return (void*)SDL_GL_GetProcAddress(name);
}
