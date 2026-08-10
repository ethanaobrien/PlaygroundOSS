#include "Playground/Host/DesktopHost.h"
#include "Playground/Platform/BuildTarget.h"

#include <SDL3/SDL_keycode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool onStart(void* context, PlaygroundDesktopHost* host)
{
    const PlaygroundBuildTarget* target = playgroundGetBuildTarget();
    int width;
    int height;
    (void)context;
    playgroundDesktopHostGetPixelSize(host, &width, &height);
    printf(
        "desktop-host platform=%s architecture=%s framebuffer=%dx%d\n",
        target->platform,
        target->architecture,
        width,
        height
    );
    return true;
}

static bool onFrame(
    void* context,
    PlaygroundDesktopHost* host,
    uint64_t deltaNanoseconds
)
{
    (void)context;
    (void)deltaNanoseconds;
    playgroundDesktopHostSwapBuffers(host);
    return true;
}

static void onStop(void* context, PlaygroundDesktopHost* host)
{
    (void)context;
    (void)host;
    puts("desktop-host stopped");
}

static void onKey(
    void* context,
    PlaygroundDesktopHost* host,
    int32_t key,
    int32_t scanCode,
    bool pressed,
    bool repeat
)
{
    (void)context;
    (void)scanCode;
    (void)repeat;
    if (pressed && key == SDLK_ESCAPE) {
        playgroundDesktopHostRequestQuit(host);
    }
}

int main(int argc, char** argv)
{
    PlaygroundDesktopHostConfig config = playgroundDesktopHostDefaultConfig();
    PlaygroundDesktopHostCallbacks callbacks;
    int index;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.onStart = onStart;
    callbacks.onFrame = onFrame;
    callbacks.onStop = onStop;
    callbacks.onKey = onKey;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--headless") == 0) {
            config.headless = true;
            config.graphicsProfile = PLAYGROUND_DESKTOP_GRAPHICS_NONE;
        } else if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            config.maximumFrames = (uint32_t)strtoul(argv[++index], NULL, 10);
        } else {
            fprintf(stderr, "usage: %s [--headless] [--frames count]\n", argv[0]);
            return 64;
        }
    }

    return playgroundDesktopHostRun(&config, &callbacks, NULL);
}
