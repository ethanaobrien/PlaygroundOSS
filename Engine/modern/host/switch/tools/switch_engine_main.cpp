#include "Playground/Bootstrap/AssetBootstrap.h"
#include "Playground/Host/SwitchHost.h"
#include "Playground/Runtime/RuntimePlatform.h"
#include "Playground/Switch/SwitchNetwork.h"
#include "Playground/Switch/SwitchSqlite.h"
#include "Playground/Switch/SwitchStorage.h"
#include "Playground/Switch/SwitchSystem.h"

#include "CPFInterface.h"
#include "NotificationManager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

struct EngineContext {
  playground::switch_runtime::SwitchStorage storage;
  std::filesystem::path developmentRoot{"sdmc:/switch/PlaygroundOSS-SIF"};
  std::filesystem::path archive;
  std::filesystem::path metadata;
  std::unique_ptr<playground::runtime::RuntimePlatform> platform;
  bool initialized{};
  bool paused{};
  bool foreground{true};
  bool romfs{};
  bool logRedirected{};
};

bool readText(const std::filesystem::path &path, std::string &text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input),
              std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool publishForYuzu1734(const std::filesystem::path &staging,
                        const std::filesystem::path &generation,
                        std::string &error) {
  constexpr const char *CompletionMarker = ".appassets-version";
  std::error_code fsError;
  std::filesystem::remove_all(generation, fsError);
  fsError.clear();
  std::filesystem::create_directories(generation, fsError);
  if (fsError) {
    error =
        "Could not create the yuzu AppAssets generation: " + fsError.message();
    return false;
  }

  auto streamFile = [&error](const std::filesystem::path &inputPath,
                             const std::filesystem::path &output,
                             const std::filesystem::path &relative) {
    std::ifstream input(inputPath, std::ios::binary);
    std::ofstream destination(output, std::ios::binary | std::ios::trunc);
    if (!input || !destination) {
      error =
          "Could not open AppAssets file for yuzu 1734: " + relative.string();
      return false;
    }
    destination << input.rdbuf();
    destination.close();
    if (input.bad() || !destination) {
      error =
          "Could not stream AppAssets file for yuzu 1734: " + relative.string();
      return false;
    }
    return true;
  };

  std::filesystem::path markerSource;
  std::filesystem::recursive_directory_iterator iterator(staging, fsError);
  const std::filesystem::recursive_directory_iterator end;
  while (!fsError && iterator != end) {
    const auto &entry = *iterator;
    const std::filesystem::path relative =
        entry.path().lexically_relative(staging);
    const std::filesystem::path output = generation / relative;
    if (entry.is_directory(fsError)) {
      std::filesystem::create_directories(output, fsError);
    } else if (!fsError && entry.is_regular_file(fsError)) {
      if (relative == CompletionMarker)
        markerSource = entry.path();
      else {
        std::filesystem::create_directories(output.parent_path(), fsError);
        if (!fsError && !streamFile(entry.path(), output, relative))
          return false;
      }
    } else if (!fsError) {
      error = "Unexpected entry while publishing AppAssets for yuzu 1734: " +
              relative.string();
      return false;
    }
    if (!fsError)
      iterator.increment(fsError);
  }
  if (fsError) {
    error = "Could not publish AppAssets on yuzu 1734: " + fsError.message();
    return false;
  }

  // This is the commit record checked on the next launch. Copy it only after
  // every payload file so interruption can never authenticate a partial tree.
  if (markerSource.empty() ||
      !streamFile(markerSource, generation / CompletionMarker,
                  CompletionMarker)) {
    if (error.empty())
      error = "The yuzu AppAssets staging tree has no completion marker";
    return false;
  }

  std::filesystem::remove_all(staging, fsError);
  if (fsError) {
    error = "Could not remove yuzu AppAssets staging: " + fsError.message();
    return false;
  }
  return true;
}

void fatal(const std::string &message) {
  std::fprintf(stderr, "Switch startup failed: %s\n", message.c_str());
  playground::switch_runtime::showApplicationMessage(
      "PlaygroundOSS could not start", message.c_str());
}

bool redirectInstalledLog(EngineContext &context, std::string &error) {
  if (!playground::switch_runtime::isInstalledApplication())
    return true;
  const std::filesystem::path directory =
      context.storage.roots().state / "logs";
  const std::filesystem::path current = directory / "runtime.log";
  const std::filesystem::path previous = directory / "runtime.previous.log";
  std::error_code fsError;
  std::filesystem::create_directories(directory, fsError);
  if (fsError) {
    error = "Could not create the installed-title log directory: " +
            fsError.message();
    return false;
  }
  if (std::filesystem::is_regular_file(current, fsError) &&
      std::filesystem::file_size(current, fsError) > 4 * 1024 * 1024) {
    std::filesystem::remove(previous, fsError);
    fsError.clear();
    std::filesystem::rename(current, previous, fsError);
    if (fsError) {
      error = "Could not rotate the installed-title log: " + fsError.message();
      return false;
    }
  }
  FILE *out = std::freopen(current.string().c_str(), "ab", stdout);
  FILE *err = std::freopen(current.string().c_str(), "ab", stderr);
  if (!out || !err) {
    error = "Could not open the installed-title runtime log";
    return false;
  }
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IOLBF, 0);
  context.logRedirected = true;
  std::printf("\n=== PlaygroundOSS installed-title launch ===\n");
  return true;
}

void releasePlatformResources(EngineContext &context) {
  playground::switch_runtime::sharedSwitchNetwork().shutdown();
  CPFInterface::getInstance().setClientRequest(nullptr);
  CPFInterface::getInstance().setPlatformRequest(nullptr);
  context.platform.reset();
  playground::switch_runtime::shutdownSwitchSqliteDurability();
  std::fflush(nullptr);
  if (context.logRedirected) {
    std::fclose(stdout);
    std::fclose(stderr);
    context.logRedirected = false;
  }
  if (context.romfs) {
    playground::switch_runtime::shutdownRomFs();
    context.romfs = false;
  }
  context.storage.unmount();
}

bool prepareStorageAndAssets(EngineContext &context, PlaygroundSwitchHost *host,
                             std::filesystem::path &installRoot,
                             std::string &error) {
  const bool installed = playground::switch_runtime::isInstalledApplication();
  if (installed) {
    if (!context.storage.mountInstalledTitle(error))
      return false;
    if (!redirectInstalledLog(context, error))
      return false;
#if !defined(PLAYGROUND_SWITCH_BUNDLED_ASSETS)
    error = "Installed builds require AppAssets.zip in RomFS";
    return false;
#endif
  } else if (!context.storage.mountDevelopmentNro(context.developmentRoot,
                                                  error)) {
    return false;
  }

#if defined(PLAYGROUND_SWITCH_BUNDLED_ASSETS)
  if (!playground::switch_runtime::initializeRomFs()) {
    error = "RomFS could not be mounted";
    return false;
  }
  context.romfs = true;
  context.archive = "romfs:/AppAssets.zip";
  context.metadata = "romfs:/AppAssets.metadata";
#else
  if (context.archive.empty())
    context.archive = context.developmentRoot / "AppAssets.zip";
  if (context.metadata.empty())
    context.metadata = context.developmentRoot / "AppAssets.metadata";
#endif

  std::string metadataText;
  playground::bootstrap::AssetBundleMetadata metadata;
  if (!readText(context.metadata, metadataText)) {
    error = "AppAssets metadata is missing: " + context.metadata.string();
    return false;
  }
  if (!playground::bootstrap::parseAssetBundleMetadata(metadataText, metadata,
                                                       error))
    return false;

  playground::bootstrap::AssetBootstrapOptions options;
  options.archive = context.archive;
  options.cacheRoot = context.storage.roots().installContainer;
  options.metadata = std::move(metadata);
  options.hashFile = playground::switch_runtime::hashFileSha256;
  options.commitStorage = [&context](std::string &commitError) {
    return context.storage.commitCache(commitError);
  };
  if (context.storage.yuzu1734CompatibilityMode()) {
    options.publishGeneration = publishForYuzu1734;
  }
  options.maximumEntries = options.metadata.entryCount;
  options.maximumExpandedBytes = options.metadata.expandedSize;
  unsigned lastLoggedPercent = 101;
  options.progress = [host, lastLoggedPercent](
                         std::uint64_t complete, std::uint64_t total,
                         const std::string &entry) mutable {
    const unsigned percent =
        total ? static_cast<unsigned>(complete * 100 / total) : 100;
    if (percent != lastLoggedPercent) {
      std::printf("Installing AppAssets: %u%% %s\n", percent, entry.c_str());
      lastLoggedPercent = percent;
    }
    return playgroundSwitchHostPresentBootstrapProgress(host, complete, total);
  };
  playground::bootstrap::AssetBootstrapResult result;
  if (!playground::bootstrap::prepareAssetBundle(options, result, error))
    return false;
  installRoot = result.installRoot;
  std::printf("AppAssets %s at %s\n",
              result.installed ? "installed" : "validated",
              installRoot.c_str());
  return true;
}

void setPaused(EngineContext &context, bool paused) {
  if (!context.initialized || context.paused == paused)
    return;
  CPFInterface &interface = CPFInterface::getInstance();
  if (paused) {
    interface.client().pauseGame(true);
    if (interface.platform().getAudioSystem())
      interface.platform().getAudioSystem()->onActivityPause();
    playground::switch_runtime::sharedSwitchNetwork().suspend();
    std::string error;
    if (!context.storage.commitState(error) ||
        !context.storage.commitCache(error))
      std::fprintf(stderr, "Switch suspend commit failed: %s\n", error.c_str());
  } else {
    std::string error;
    if (!playground::switch_runtime::sharedSwitchNetwork().resume(error))
      std::fprintf(stderr, "Switch network resume failed: %s\n", error.c_str());
    if (interface.platform().getAudioSystem())
      interface.platform().getAudioSystem()->onActivityResume();
    if (auto *notification = INotificationManager::getInstance())
      notification->onActivityResume();
    interface.client().pauseGame(false);
  }
  context.paused = paused;
}

bool onStart(void *opaque, PlaygroundSwitchHost *host) {
  auto &context = *static_cast<EngineContext *>(opaque);
  std::filesystem::path installRoot;
  std::string error;
  // Display something before SaveData/CacheStorage provisioning as those
  // service calls may themselves take noticeable time on first launch.
  if (!playgroundSwitchHostPresentBootstrapProgress(host, 0, 100))
    return false;
  if (!prepareStorageAndAssets(context, host, installRoot, error)) {
    fatal(error);
    releasePlatformResources(context);
    return false;
  }
  if (!playground::switch_runtime::initializeSwitchSqliteDurability(error)) {
    fatal(error);
    releasePlatformResources(context);
    return false;
  }

  const auto &roots = context.storage.roots();
  context.platform = std::make_unique<playground::runtime::RuntimePlatform>(
      installRoot.string(), roots.content.string(), roots.state.string(),
      playgroundSwitchHostGetGLProcAddress,
      [&context] {
        std::string commitError;
        const bool committed = context.storage.commitState(commitError);
        if (!committed)
          std::fprintf(stderr, "SaveData commit failed: %s\n",
                       commitError.c_str());
        return committed;
      },
      [&context] {
        std::string commitError;
        const bool committed = context.storage.commitCache(commitError);
        if (!committed)
          std::fprintf(stderr, "CacheStorage commit failed: %s\n",
                       commitError.c_str());
        return committed;
      });

  CPFInterface &interface = CPFInterface::getInstance();
  interface.setPlatformRequest(context.platform.get());
  if (!context.platform->init() || !GameSetup()) {
    fatal("Platform or client setup failed");
    context.platform->shutdownAudioSystem();
    releasePlatformResources(context);
    return false;
  }
  int width = 0;
  int height = 0;
  playgroundSwitchHostGetPixelSize(host, &width, &height);
  IClientRequest &client = interface.client();
  client.setInitParam(0, nullptr);
  client.setScreenInfo(false, width, height);
  client.setFilePath(nullptr);
  context.initialized = client.initGame();
  if (!context.initialized) {
    fatal("The engine rejected the installed AppAssets generation");
    context.platform->shutdownAudioSystem();
    releasePlatformResources(context);
  }
  return context.initialized;
}

bool onFrame(void *opaque, PlaygroundSwitchHost *host, std::uint64_t deltaNs) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return context.initialized;
  context.platform->pumpPlatformEvents();
  const u32 deltaMs =
      static_cast<u32>(std::clamp<std::uint64_t>(deltaNs / 1000000, 1, 250));
  if (!CPFInterface::getInstance().client().frameFlip(deltaMs))
    return false;
  playgroundSwitchHostSwapBuffers(host);
  if (context.platform->quitRequested())
    playgroundSwitchHostRequestQuit(host);
  return true;
}

void onStop(void *opaque, PlaygroundSwitchHost *) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (context.initialized) {
    CPFInterface::getInstance().client().finishGame(true);
    context.initialized = false;
  }
  if (context.platform)
    context.platform->shutdownAudioSystem();
  std::string ignored;
  std::fflush(nullptr);
  context.storage.commitState(ignored);
  context.storage.commitCache(ignored);
  releasePlatformResources(context);
}

void onPointer(void *opaque, PlaygroundSwitchHost *, std::int64_t id,
               PlaygroundSwitchPointerPhase phase, float x, float y) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return;
  const IClientRequest::INPUT_TYPE type =
      phase == PLAYGROUND_SWITCH_POINTER_DOWN   ? IClientRequest::I_CLICK
      : phase == PLAYGROUND_SWITCH_POINTER_MOVE ? IClientRequest::I_DRAG
      : phase == PLAYGROUND_SWITCH_POINTER_UP   ? IClientRequest::I_RELEASE
                                                : IClientRequest::I_CANCEL;
  CPFInterface::getInstance().client().inputPoint(
      static_cast<int>(id), type, static_cast<int>(x), static_cast<int>(y));
}

void onButtons(void *opaque, PlaygroundSwitchHost *host, std::uint64_t pressed,
               std::uint64_t, std::uint64_t) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return;
  if (pressed & PLAYGROUND_SWITCH_BUTTON_B)
    CPFInterface::getInstance().client().inputDeviceKey(
        IClientRequest::KEY_BACK, IClientRequest::KEYEVENT_CLICK);
  if (pressed & PLAYGROUND_SWITCH_BUTTON_PLUS)
    playgroundSwitchHostRequestQuit(host);
}

void onVisibility(void *opaque, PlaygroundSwitchHost *,
                  PlaygroundSwitchVisibility visibility) {
  auto &context = *static_cast<EngineContext *>(opaque);
  context.foreground = visibility == PLAYGROUND_SWITCH_VISIBILITY_FOREGROUND;
  setPaused(context, visibility != PLAYGROUND_SWITCH_VISIBILITY_FOREGROUND);
}

void onResume(void *opaque, PlaygroundSwitchHost *) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (context.foreground)
    setPaused(context, false);
}

void onPerformanceMode(void *, PlaygroundSwitchHost *, int mode) {
  std::printf("Switch performance mode changed: %d\n", mode);
}

void onResize(void *opaque, PlaygroundSwitchHost *, int width, int height) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized)
    return;
  auto &client = CPFInterface::getInstance().client();
  client.setScreenInfo(false, width, height);
  client.resetViewport();
}

} // namespace

int main(int argc, char **argv) {
  EngineContext context;
  for (int index = 1; index < argc; ++index) {
    if (!std::strcmp(argv[index], "--storage-root") && index + 1 < argc)
      context.developmentRoot = argv[++index];
    else if (!std::strcmp(argv[index], "--asset-archive") && index + 1 < argc)
      context.archive = argv[++index];
    else if (!std::strcmp(argv[index], "--asset-metadata") && index + 1 < argc)
      context.metadata = argv[++index];
    else {
      std::fprintf(stderr,
                   "usage: %s [--storage-root sdmc:/path] "
                   "[--asset-archive path] [--asset-metadata path]\n",
                   argv[0]);
      return 64;
    }
  }

  PlaygroundSwitchHostConfig config = playgroundSwitchHostDefaultConfig();
  PlaygroundSwitchHostCallbacks callbacks{};
  callbacks.onStart = onStart;
  callbacks.onFrame = onFrame;
  callbacks.onStop = onStop;
  callbacks.onVisibility = onVisibility;
  callbacks.onResume = onResume;
  callbacks.onResize = onResize;
  callbacks.onPerformanceMode = onPerformanceMode;
  callbacks.onPointer = onPointer;
  callbacks.onButtons = onButtons;
  const int result = playgroundSwitchHostRun(&config, &callbacks, &context);
  if (result == 2 || result == 3 || result == 5 || result == 6) {
    const char *error = playgroundSwitchHostLastError();
    playground::switch_runtime::showApplicationMessage(
        "PlaygroundOSS platform failure",
        error ? error : "The Switch host stopped after a platform failure");
  }
  return result;
}
