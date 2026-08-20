#include "Playground/Host/SwitchHost.h"
#include "Playground/Platform/BuildTarget.h"

#include <GLES2/gl2.h>

#include <stdio.h>

static bool start(void *unused, PlaygroundSwitchHost *host) {
    (void)unused;
    int width;
    int height;
    playgroundSwitchHostGetPixelSize(host, &width, &height);
    glViewport(0, 0, width, height);
    const PlaygroundBuildTarget *target = playgroundGetBuildTarget();
    printf("Playground host: %s/%s/%u\n", target->platform,
           target->architecture, target->pointerBits);
    return true;
}

static bool frame(void *unused, PlaygroundSwitchHost *host, uint64_t deltaNs) {
    (void)unused;
    (void)deltaNs;
    glClearColor(0.07f, 0.16f, 0.24f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    playgroundSwitchHostSwapBuffers(host);
    return true;
}

static void resize(void *unused, PlaygroundSwitchHost *host, int width,
                   int height) {
    (void)unused;
    (void)host;
    glViewport(0, 0, width, height);
}

static void buttons(void *unused, PlaygroundSwitchHost *host,
                    uint64_t pressed, uint64_t released, uint64_t held) {
    (void)unused;
    (void)released;
    (void)held;
    /* Plus is bit 10 in the stable libnx HidNpadButton bitset. Keeping the
       probe header free of libnx types preserves the engine boundary. */
    if (pressed & (UINT64_C(1) << 10))
        playgroundSwitchHostRequestQuit(host);
}

int main(void) {
    PlaygroundSwitchHostConfig config = playgroundSwitchHostDefaultConfig();
    PlaygroundSwitchHostCallbacks callbacks = {0};
    callbacks.onStart = start;
    callbacks.onFrame = frame;
    callbacks.onResize = resize;
    callbacks.onButtons = buttons;
    int result = playgroundSwitchHostRun(&config, &callbacks, NULL);
    if (result && playgroundSwitchHostLastError())
        fprintf(stderr, "%s\n", playgroundSwitchHostLastError());
    return result;
}
