#include "WebBootstrap.h"

#include "Playground/Host/DesktopHost.h"
#include "Playground/Runtime/RuntimePlatform.h"

#include "CPFInterface.h"

#include <SDL3/SDL_keycode.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace {

struct EngineContext {
  std::string installRoot;
  std::string externalRoot;
  std::unique_ptr<playground::runtime::RuntimePlatform> platform;
  std::uint64_t frames{};
  bool initialized{};
};

EngineContext Context;

bool onStart(void *opaque, PlaygroundDesktopHost *host) {
  auto &context = *static_cast<EngineContext *>(opaque);
  int width = 0;
  int height = 0;
  playgroundDesktopHostGetPixelSize(host, &width, &height);
  context.platform = std::make_unique<playground::runtime::RuntimePlatform>(
      context.installRoot, context.externalRoot,
      playgroundDesktopHostGetGLProcAddress);
  CPFInterface &interface = CPFInterface::getInstance();
  interface.setPlatformRequest(context.platform.get());
  if (!context.platform->init() || !GameSetup()) {
    playground::web::showFatalError("Platform or client setup failed");
    return false;
  }
  IClientRequest &client = interface.client();
  client.setInitParam(0, nullptr);
  client.setScreenInfo(false, width, height);
  client.setFilePath(nullptr);
  context.initialized = client.initGame();
  if (!context.initialized) {
    playground::web::showFatalError(
        "The engine rejected the installed AppAssets generation");
    playgroundDesktopHostRequestQuit(host);
  }
  return true;
}

bool onFrame(void *opaque, PlaygroundDesktopHost *host, std::uint64_t deltaNs) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return false;
  context.platform->pumpPlatformEvents();
  const u32 deltaMs = static_cast<u32>(
      std::clamp<std::uint64_t>(deltaNs / 1000000, 1, 250));
  if (!CPFInterface::getInstance().client().frameFlip(deltaMs))
    return false;
  playgroundDesktopHostSwapBuffers(host);
  if (++context.frames == 1) {
    int width = 0;
    int height = 0;
    playgroundDesktopHostGetPixelSize(host, &width, &height);
    MAIN_THREAD_EM_ASM({
      globalThis.playgroundEngineReady = {};
      globalThis.playgroundEngineReady.frames = 1;
      globalThis.playgroundEngineReady.width = $0;
      globalThis.playgroundEngineReady.height = $1;
    }, width, height);
  }
  if (context.platform->quitRequested())
    playgroundDesktopHostRequestQuit(host);
  return true;
}

void onStop(void *opaque, PlaygroundDesktopHost *) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (context.initialized) {
    CPFInterface::getInstance().client().finishGame(true);
    context.initialized = false;
  }
  context.frames = 0;
  if (context.platform)
    context.platform->shutdownAudioSystem();
}

void onPointer(void *opaque, PlaygroundDesktopHost *, std::int64_t id,
               PlaygroundPointerPhase phase, float x, float y) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return;
  const IClientRequest::INPUT_TYPE type =
      phase == PLAYGROUND_POINTER_DOWN
          ? IClientRequest::I_CLICK
          : phase == PLAYGROUND_POINTER_MOVE
                ? IClientRequest::I_DRAG
                : phase == PLAYGROUND_POINTER_UP ? IClientRequest::I_RELEASE
                                                 : IClientRequest::I_CANCEL;
  CPFInterface::getInstance().client().inputPoint(
      static_cast<int>(id), type, static_cast<int>(x), static_cast<int>(y));
}

void onKey(void *opaque, PlaygroundDesktopHost *, std::int32_t key,
           std::int32_t, bool pressed, bool repeat) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return;
  if (context.platform->handleEditingKey(key, pressed))
    return;
  if (key == SDLK_ESCAPE && pressed && !repeat) {
    CPFInterface::getInstance().client().inputDeviceKey(
        IClientRequest::KEY_BACK, IClientRequest::KEYEVENT_CLICK);
  }
}

void onTextInput(void *opaque, PlaygroundDesktopHost *, const char *text) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (context.initialized)
    context.platform->handleTextInput(text);
}

void onActivity(void *opaque, PlaygroundDesktopHost *, bool active) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return;
  CPFInterface::getInstance().client().controlEvent(
      active ? IClientRequest::E_RESUME : IClientRequest::E_PAUSE, nullptr, 0,
      nullptr, 0, nullptr);
}

void onResize(void *opaque, PlaygroundDesktopHost *, int width, int height) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return;
  IClientRequest &client = CPFInterface::getInstance().client();
  client.setScreenInfo(false, width, height);
  client.changeProjectionMatrix();
  MAIN_THREAD_EM_ASM({
    if (globalThis.playgroundEngineReady) {
      globalThis.playgroundEngineReady.width = $0;
      globalThis.playgroundEngineReady.height = $1;
      globalThis.playgroundEngineReady.resizes =
          (globalThis.playgroundEngineReady.resizes || 0) + 1;
    }
  }, width, height);
}

} // namespace

int main() {
  std::string error;
  playground::web::RuntimePaths paths;
  if (!playground::web::prepareBrowserStorage(paths, error)) {
    playground::web::showFatalError(error);
    return 1;
  }
  Context.installRoot = std::move(paths.installRoot);
  Context.externalRoot = std::move(paths.externalRoot);

  PlaygroundDesktopHostConfig config = playgroundDesktopHostDefaultConfig();
  PlaygroundDesktopHostCallbacks callbacks{};
  callbacks.onStart = onStart;
  callbacks.onFrame = onFrame;
  callbacks.onStop = onStop;
  callbacks.onResize = onResize;
  callbacks.onActivity = onActivity;
  callbacks.onPointer = onPointer;
  callbacks.onKey = onKey;
  callbacks.onTextInput = onTextInput;
  config.graphicsProfile = PLAYGROUND_DESKTOP_GRAPHICS_OPENGL_ES;
  config.graphicsMajorVersion = 2;
  config.graphicsMinorVersion = 0;
  config.audio = true;
  return playgroundDesktopHostRun(&config, &callbacks, &Context);
}
