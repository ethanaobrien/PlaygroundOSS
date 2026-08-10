#ifndef PLAYGROUND_HOST_DESKTOP_HOST_H
#define PLAYGROUND_HOST_DESKTOP_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlaygroundDesktopHost PlaygroundDesktopHost;

typedef enum PlaygroundDesktopGraphicsProfile {
    PLAYGROUND_DESKTOP_GRAPHICS_NONE = 0,
    PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_CORE = 1,
    PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_ES = 2
} PlaygroundDesktopGraphicsProfile;

typedef enum PlaygroundPointerPhase {
    PLAYGROUND_POINTER_DOWN = 0,
    PLAYGROUND_POINTER_MOVE = 1,
    PLAYGROUND_POINTER_UP = 2,
    PLAYGROUND_POINTER_CANCEL = 3
} PlaygroundPointerPhase;

typedef struct PlaygroundDesktopHostConfig {
    const char* title;
    int width;
    int height;
    PlaygroundDesktopGraphicsProfile graphicsProfile;
    int graphicsMajorVersion;
    int graphicsMinorVersion;
    uint32_t maximumFrames;
    bool highPixelDensity;
    bool resizable;
    bool verticalSync;
    bool headless;
} PlaygroundDesktopHostConfig;

typedef struct PlaygroundDesktopHostCallbacks {
    bool (*onStart)(void* context, PlaygroundDesktopHost* host);
    bool (*onFrame)(void* context, PlaygroundDesktopHost* host, uint64_t deltaNanoseconds);
    void (*onStop)(void* context, PlaygroundDesktopHost* host);
    void (*onResize)(
        void* context,
        PlaygroundDesktopHost* host,
        int pixelWidth,
        int pixelHeight
    );
    void (*onActivity)(void* context, PlaygroundDesktopHost* host, bool active);
    void (*onPointer)(
        void* context,
        PlaygroundDesktopHost* host,
        int64_t pointerId,
        PlaygroundPointerPhase phase,
        float pixelX,
        float pixelY
    );
    void (*onKey)(
        void* context,
        PlaygroundDesktopHost* host,
        int32_t key,
        int32_t scanCode,
        bool pressed,
        bool repeat
    );
    void (*onTextInput)(
        void* context,
        PlaygroundDesktopHost* host,
        const char* utf8Text
    );
} PlaygroundDesktopHostCallbacks;

PlaygroundDesktopHostConfig playgroundDesktopHostDefaultConfig(void);

int playgroundDesktopHostRun(
    const PlaygroundDesktopHostConfig* config,
    const PlaygroundDesktopHostCallbacks* callbacks,
    void* context
);

void playgroundDesktopHostRequestQuit(PlaygroundDesktopHost* host);
void playgroundDesktopHostSwapBuffers(PlaygroundDesktopHost* host);
void playgroundDesktopHostGetPixelSize(
    const PlaygroundDesktopHost* host,
    int* width,
    int* height
);
void* playgroundDesktopHostNativeWindow(const PlaygroundDesktopHost* host);

#ifdef __cplusplus
}
#endif

#endif
