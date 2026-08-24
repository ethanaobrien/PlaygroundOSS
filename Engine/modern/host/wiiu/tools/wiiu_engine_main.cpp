#include "Playground/Bootstrap/AssetBootstrap.h"
#include "Playground/Host/WiiUHost.h"
#include "Playground/Platform/BuildTargetConfig.h"
#include "Playground/Runtime/RuntimePlatform.h"
#include "Playground/WiiU/WiiUStorage.h"
#include "Playground/WiiU/WiiUArchive.h"
#include "Playground/WiiU/WiiURandom.h"
#include "Playground/WiiU/WiiUPath.h"
#include "Playground/WiiU/WiiUSqlite.h"

#include "CKLBDatabase.h"
#include "CPFInterface.h"
#include "NotificationManager.h"

#include <vpad/input.h>
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <coreinit/thread.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string g_startupError;

struct EngineContext {
  playground::wiiu_runtime::WiiUStorage storage;
  std::unique_ptr<playground::runtime::RuntimePlatform> platform;
  std::filesystem::path installRoot;
  bool initialized{};
  bool paused{};
  bool widgetPointerCaptured{};
};

void setPaused(EngineContext &context, bool paused) {
  if (!context.initialized || context.paused == paused)
    return;
  CPFInterface &interface = CPFInterface::getInstance();
  if (paused) {
    interface.client().pauseGame(true);
    if (interface.platform().getAudioSystem())
      interface.platform().getAudioSystem()->onActivityPause();
    std::string error;
    context.storage.commit(error);
  } else {
    if (interface.platform().getAudioSystem())
      interface.platform().getAudioSystem()->onActivityResume();
    if (auto *notification = INotificationManager::getInstance())
      notification->onActivityResume();
    interface.client().pauseGame(false);
  }
  context.paused = paused;
}

bool readText(const std::filesystem::path &path, std::string &text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input),
              std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

void reportFailure(const std::string &message) {
  g_startupError = message;
}

bool initializeRandom(EngineContext &context, std::string &error) {
  // CafeOS does not expose its hardware RNG to PPC applications. Mix the
  // per-user seed from the preceding launch with high-resolution timing and
  // raw GamePad sensor/touch samples, then rotate the seed before networking.
  // This avoids devkitPro mbedTLS's tick-seeded rand() entropy fallback.
  std::vector<unsigned char> entropy;
  entropy.reserve(128 * (sizeof(VPADStatus) + sizeof(OSTime)) + 16);
  const std::uint32_t persistentId = context.storage.persistentId();
  entropy.insert(entropy.end(),
                 reinterpret_cast<const unsigned char *>(&persistentId),
                 reinterpret_cast<const unsigned char *>(&persistentId) +
                     sizeof(persistentId));
  for (unsigned sample = 0; sample < 128; ++sample) {
    VPADStatus status{};
    VPADReadError readError = VPAD_READ_SUCCESS;
    const int count = VPADRead(VPAD_CHAN_0, &status, 1, &readError);
    const OSTime tick = OSGetTime();
    entropy.insert(entropy.end(),
                   reinterpret_cast<const unsigned char *>(&tick),
                   reinterpret_cast<const unsigned char *>(&tick) +
                       sizeof(tick));
    if (count == 1 && readError == VPAD_READ_SUCCESS)
      entropy.insert(entropy.end(),
                     reinterpret_cast<const unsigned char *>(&status),
                     reinterpret_cast<const unsigned char *>(&status) +
                         sizeof(status));
    OSSleepTicks(OSMillisecondsToTicks(2));
  }
  if (!playground::wiiu_runtime::initializeRandom(
          context.storage.roots().state / "entropy.seed", entropy.data(),
          entropy.size(), error))
    return false;
  if (!context.storage.commit(error)) {
    playground::wiiu_runtime::shutdownRandom();
    return false;
  }
  return true;
}

bool prepareAssets(EngineContext &context, PlaygroundWiiUHost *host,
                   std::filesystem::path &installRoot, std::string &error) {
  if (!context.storage.mount(error))
    return false;
  const std::filesystem::path metadataPath = "/vol/content/AppAssets.metadata";
  std::string metadataText;
  playground::bootstrap::AssetBundleMetadata metadata;
  if (!readText(metadataPath, metadataText)) {
    error = "Bundled AppAssets.metadata is missing";
    return false;
  }
  if (!playground::bootstrap::parseAssetBundleMetadata(metadataText, metadata,
                                                       error))
    return false;

  installRoot = "/vol/content/AppAssets";
  for (const auto &required : metadata.requiredFiles) {
    const std::filesystem::path path = installRoot / required;
    if (!playground::wiiu_runtime::isRegularFile(path)) {
      error = "Bundled AppAssets content is incomplete: " + path.string();
      return false;
    }
  }
  (void)host;
  return true;
}

bool initializeEngine(EngineContext &context) {
  std::string error;
  if (!initializeRandom(context, error)) {
    reportFailure(error);
    return false;
  }
  const auto &roots = context.storage.roots();
  context.platform = std::make_unique<playground::runtime::RuntimePlatform>(
      context.installRoot.string(), roots.content.string(), roots.state.string(),
      playgroundWiiUHostGetGLProcAddress,
      [&context] {
        std::string error;
        const bool result = context.storage.commit(error);
        if (!result)
          reportFailure(error);
        return result;
      },
      [&context] {
        std::string error;
        const bool result = context.storage.commit(error);
        if (!result)
          reportFailure(error);
        return result;
      },
      playground::wiiu_runtime::encodeStoragePath,
      playground::runtime::RuntimePlatform::StorageRemove{},
      PLAYGROUND_APP_INTEGRITY_DATABASE_URI);
  CPFInterface &interface = CPFInterface::getInstance();
  interface.setPlatformRequest(context.platform.get());
  if (!context.platform->init() || !GameSetup()) {
    reportFailure("Platform or client setup failed");
    return false;
  }
  // Register the Wii U encrypted and writable SQLite VFSes before the game
  // performs its first database open. init(nullptr) changes no global database
  // selection; it only initializes the engine's VFS registry.
  if (!CKLBDatabase::getInstance().init(nullptr)) {
    reportFailure("Could not initialize the Wii U SQLite VFS registry");
    return false;
  }
  IClientRequest &client = interface.client();
  client.setInitParam(0, nullptr);
  client.setScreenInfo(false, 960, 640);
  client.setFilePath(nullptr);
  context.initialized = client.initGame();
  if (!context.initialized)
    reportFailure("The engine rejected the installed AppAssets generation");
  return context.initialized;
}

bool onStart(void *opaque, PlaygroundWiiUHost *host) {
  auto &context = *static_cast<EngineContext *>(opaque);
  OSReport("PlaygroundOSS Wii U: presenting bootstrap screen\n");
  if (!playgroundWiiUHostPresentBootstrapProgress(host, 0, 100))
    return false;
  OSReport("PlaygroundOSS Wii U: preparing SaveData and AppAssets\n");
  std::string error;
  if (!prepareAssets(context, host, context.installRoot, error)) {
    reportFailure(error);
    return false;
  }
  if (!playgroundWiiUHostPresentBootstrapProgress(host, 100, 100))
    return false;
  return initializeEngine(context);
}

bool onFrame(void *opaque, PlaygroundWiiUHost *host, std::uint64_t deltaNs) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return false;
  if (context.paused)
    return true;
  context.platform->pumpPlatformEvents();
  const u32 deltaMs =
      static_cast<u32>(std::clamp<std::uint64_t>(deltaNs / 1000000, 1, 250));
  if (!CPFInterface::getInstance().client().frameFlip(deltaMs))
    return false;
  context.platform->renderWidgets();
  playgroundWiiUHostSwapBuffers(host);
  if (context.platform->quitRequested())
    playgroundWiiUHostRequestQuit(host);
  return true;
}

void onStop(void *opaque, PlaygroundWiiUHost *) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (context.initialized) {
    CPFInterface::getInstance().client().finishGame(true);
    context.initialized = false;
  }
  if (context.platform) {
    context.platform->shutdownAudioSystem();
    CPFInterface::getInstance().setClientRequest(nullptr);
    CPFInterface::getInstance().setPlatformRequest(nullptr);
    context.platform.reset();
  }
  std::string ignored;
  context.storage.commit(ignored);
  playground::wiiu_runtime::shutdownRandom();
  context.storage.unmount();
}

void onPointer(void *opaque, PlaygroundWiiUHost *,
               PlaygroundWiiUPointerPhase phase, float x, float y) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return;
  if (phase == PLAYGROUND_WIIU_POINTER_DOWN) {
    context.widgetPointerCaptured =
        context.platform->handleWidgetPointerDown(x, y);
    if (context.widgetPointerCaptured)
      return;
  } else if (context.widgetPointerCaptured) {
    if (phase == PLAYGROUND_WIIU_POINTER_UP ||
        phase == PLAYGROUND_WIIU_POINTER_CANCEL)
      context.widgetPointerCaptured = false;
    return;
  }
  const IClientRequest::INPUT_TYPE type =
      phase == PLAYGROUND_WIIU_POINTER_DOWN   ? IClientRequest::I_CLICK
      : phase == PLAYGROUND_WIIU_POINTER_MOVE ? IClientRequest::I_DRAG
      : phase == PLAYGROUND_WIIU_POINTER_UP   ? IClientRequest::I_RELEASE
                                              : IClientRequest::I_CANCEL;
  CPFInterface::getInstance().client().inputPoint(
      0, type, static_cast<int>(x), static_cast<int>(y));
}

void onButtons(void *opaque, PlaygroundWiiUHost *host, std::uint32_t pressed,
               std::uint32_t, std::uint32_t) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return;
  if (pressed & VPAD_BUTTON_B)
    CPFInterface::getInstance().client().inputDeviceKey(
        IClientRequest::KEY_BACK, IClientRequest::KEYEVENT_CLICK);
  if (pressed & VPAD_BUTTON_PLUS)
    playgroundWiiUHostRequestQuit(host);
}

void onVisibility(void *opaque, PlaygroundWiiUHost *, bool foreground) {
  setPaused(*static_cast<EngineContext *>(opaque), !foreground);
}

} // namespace

int main() {
  EngineContext context;
  PlaygroundWiiUHostConfig config = playgroundWiiUHostDefaultConfig();
  PlaygroundWiiUHostCallbacks callbacks{};
  callbacks.onStart = onStart;
  callbacks.onFrame = onFrame;
  callbacks.onStop = onStop;
  callbacks.onVisibility = onVisibility;
  callbacks.onPointer = onPointer;
  callbacks.onButtons = onButtons;
  const int result = playgroundWiiUHostRun(&config, &callbacks, &context);
  if (result || !g_startupError.empty()) {
    const char *error = playgroundWiiUHostLastError();
    OSFatal(!g_startupError.empty()
                ? g_startupError.c_str()
                : (error ? error : "PlaygroundOSS could not start"));
    for (;;)
      OSSleepTicks(OSMillisecondsToTicks(100));
  }
  return result ? result : (!g_startupError.empty() ? 3 : 0);
}
