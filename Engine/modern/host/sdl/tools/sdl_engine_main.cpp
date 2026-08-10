#include "Playground/Host/DesktopHost.h"
#include "Playground/Runtime/DesktopPlatform.h"

#include "CPFInterface.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

struct EngineContext {
    std::string installRoot{"AppAssets"};
    std::string externalRoot{"playground-user"};
    std::unique_ptr<playground::runtime::DesktopPlatform> platform;
    bool initialized{};
};

bool onStart(void* opaque, PlaygroundDesktopHost* host)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    int width;
    int height;
    playgroundDesktopHostGetPixelSize(host, &width, &height);

    context.platform = std::make_unique<playground::runtime::DesktopPlatform>(
        context.installRoot, context.externalRoot,
        playgroundDesktopHostGetGLProcAddress);
    CPFInterface& interface = CPFInterface::getInstance();
    interface.setPlatformRequest(context.platform.get());
    if(!context.platform->init() || !GameSetup()) {
        std::fprintf(stderr, "Playground platform or client setup failed\n");
        return false;
    }

    IClientRequest& client = interface.client();
    client.setInitParam(0, nullptr);
    client.setScreenInfo(false, width, height);
    client.setFilePath(nullptr);
    context.initialized = client.initGame();
    if(!context.initialized) {
        std::fprintf(stderr,
            "Engine initialization failed (install root: %s)\n",
            context.installRoot.c_str());
        playgroundDesktopHostRequestQuit(host);
    }
    // Startup was entered successfully even when the game rejected its data.
    // Let the host run the matching shutdown callback before leaving.
    return true;
}

bool onFrame(void* opaque, PlaygroundDesktopHost* host, uint64_t deltaNs)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(!context.initialized) return false;
    u32 deltaMs = static_cast<u32>(std::clamp<uint64_t>(deltaNs / 1000000, 1, 250));
    if(!CPFInterface::getInstance().client().frameFlip(deltaMs)) return false;
    playgroundDesktopHostSwapBuffers(host);
    if(context.platform->quitRequested()) playgroundDesktopHostRequestQuit(host);
    return true;
}

void onStop(void* opaque, PlaygroundDesktopHost*)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(context.initialized) {
        CPFInterface::getInstance().client().finishGame(true);
        context.initialized = false;
    }
    if(context.platform) context.platform->shutdownAudioSystem();
}

void onPointer(void* opaque, PlaygroundDesktopHost*, int64_t id,
    PlaygroundPointerPhase phase, float x, float y)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(!context.initialized) return;
    IClientRequest::INPUT_TYPE type = phase == PLAYGROUND_POINTER_DOWN ? IClientRequest::I_CLICK
        : phase == PLAYGROUND_POINTER_MOVE ? IClientRequest::I_DRAG
        : phase == PLAYGROUND_POINTER_UP ? IClientRequest::I_RELEASE
        : IClientRequest::I_CANCEL;
    CPFInterface::getInstance().client().inputPoint(
        static_cast<int>(id), type, static_cast<int>(x), static_cast<int>(y));
}

void onKey(void* opaque, PlaygroundDesktopHost* host, int32_t key, int32_t,
    bool pressed, bool repeat)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(!context.initialized) return;
    if(key == SDLK_ESCAPE && pressed && !repeat) {
        CPFInterface::getInstance().client().inputDeviceKey(
            IClientRequest::KEY_BACK, IClientRequest::KEYEVENT_CLICK);
    }
    if(key == SDLK_F4 && pressed) playgroundDesktopHostRequestQuit(host);
}

void onActivity(void* opaque, PlaygroundDesktopHost*, bool active)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(!context.initialized) return;
    CPFInterface::getInstance().client().controlEvent(
        active ? IClientRequest::E_RESUME : IClientRequest::E_PAUSE,
        nullptr, 0, nullptr, 0, nullptr);
}

void onResize(void* opaque, PlaygroundDesktopHost*, int width, int height)
{
    auto& context = *static_cast<EngineContext*>(opaque);
    if(!context.initialized) return;
    CPFInterface::getInstance().client().setScreenInfo(false, width, height);
}

} // namespace

int main(int argc, char** argv)
{
    EngineContext context;
    PlaygroundDesktopHostConfig config = playgroundDesktopHostDefaultConfig();
    PlaygroundDesktopHostCallbacks callbacks{};
    callbacks.onStart = onStart;
    callbacks.onFrame = onFrame;
    callbacks.onStop = onStop;
    callbacks.onResize = onResize;
    callbacks.onActivity = onActivity;
    callbacks.onPointer = onPointer;
    callbacks.onKey = onKey;
    config.graphicsProfile = PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_ES;
    config.graphicsMajorVersion = 2;
    config.graphicsMinorVersion = 0;

    for(int i = 1; i < argc; ++i) {
        if(!std::strcmp(argv[i], "--install-root") && i + 1 < argc) {
            context.installRoot = argv[++i];
        } else if(!std::strcmp(argv[i], "--external-root") && i + 1 < argc) {
            context.externalRoot = argv[++i];
        } else if(!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            config.maximumFrames = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else {
            std::fprintf(stderr, "usage: %s [--install-root path] [--external-root path] [--frames n]\n", argv[0]);
            return 64;
        }
    }
    int result = playgroundDesktopHostRun(&config, &callbacks, &context);
    std::fflush(nullptr);
    // Several legacy static destructors assume a live registered client and
    // perform game shutdown themselves. The explicit lifecycle above already
    // owns shutdown, so do not run that second process-global teardown pass.
    std::_Exit(result);
}
