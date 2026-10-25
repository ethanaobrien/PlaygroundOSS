#include "Playground/Bootstrap/AssetBootstrap.h"
#include "Playground/Host/SwitchHost.h"
#include "Playground/Platform/BuildTargetConfig.h"
#include "Playground/Runtime/RuntimePlatform.h"
#include "Playground/Switch/SwitchNetwork.h"
#include "Playground/Switch/SwitchPath.h"
#include "Playground/Switch/SwitchCrypto.h"
#include "Playground/Switch/SwitchStorage.h"
#include "Playground/Switch/SwitchDatabase.h"
#include "Playground/Switch/SwitchArchiveWriters.h"
#include "Playground/Switch/SwitchAudioOutput.h"
#include "Playground/Switch/SwitchDiagnostics.h"
#include "Playground/Switch/SwitchSystem.h"

#include "CPFInterface.h"
#include "CKLBDatabase.h"
#include "CKLBLuaDB.h"
#include "CKLBLanguageDatabase.h"
#include "NotificationManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>

namespace {

// Horizon commits are archive-wide and close every writer, so committing per
// file or per SQLite transaction stalls downloads and frames. Content commits
// are bounded by journal usage; SaveData commits are coalesced off the frame.
constexpr unsigned long long CacheCommitBytes = 16ull * 1024 * 1024;
constexpr unsigned long long CacheCommitOps = 256;
constexpr auto StateCommitInterval = std::chrono::seconds(2);

class DurabilityThread {
public:
  ~DurabilityThread() { stop(); }
  void start(playground::switch_runtime::SwitchDatabase& database) {
    stop();
    m_stopping = false;
    m_worker = std::thread([this, &database] {
      std::unique_lock<std::mutex> lock(m_mutex);
      while (!m_wake.wait_for(lock, StateCommitInterval, [this] { return m_stopping; })) {
        if (m_paused) continue; // Lifecycle commits own the suspended state.
        lock.unlock();
        std::string error;
        if (!database.commitPending(error))
          std::fprintf(stderr, "Deferred SaveData commit failed: %s\n", error.c_str());
        lock.lock();
      }
    });
  }
  void setPaused(bool paused) { m_paused = paused; }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopping = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable()) m_worker.join();
  }
private:
  std::mutex m_mutex;
  std::condition_variable m_wake;
  std::thread m_worker;
  bool m_stopping{};
  std::atomic<bool> m_paused{false};
};

struct EngineContext {
  playground::switch_runtime::SwitchStorage storage;
  std::filesystem::path developmentRoot{"sdmc:/switch/PlaygroundOSS-SIF"};
  std::filesystem::path archive;
  std::filesystem::path metadata;
  std::unique_ptr<playground::runtime::RuntimePlatform> platform;
  std::unique_ptr<playground::switch_runtime::SwitchDatabase> database;
  std::unique_ptr<playground::switch_runtime::SwitchArchiveWriters> cacheWriters;
  playground::switch_runtime::SwitchDiagnostics diagnostics;
  DurabilityThread durability;
  bool initialized{};
  bool paused{};
  bool foreground{true};
  bool romfs{};
  std::unordered_set<std::int64_t> widgetCapturedPointers;
};

bool commitState(EngineContext& context, std::string& error) {
  return context.database ? context.database->commitState(error)
                          : context.storage.commitState(error);
}
bool commitCache(EngineContext& context, std::string& error) {
  return context.cacheWriters ? context.cacheWriters->commit(error)
                              : context.storage.commitCache(error);
}

bool readText(const std::filesystem::path &path, std::string &text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input),
              std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

void fatal(const std::string &message) {
  std::fprintf(stderr, "Switch startup failed: %s\n", message.c_str());
  playground::switch_runtime::showApplicationMessage(
      "PlaygroundOSS could not start", message.c_str());
}

void releasePlatformResources(EngineContext &context) {
  context.diagnostics.stop();
  // Pending SaveData is committed by the final unmount after databases close.
  context.durability.stop();
  // Startup failures can bypass the engine's ordinary shutdownNetwork call.
  // Enforce the same lease boundary here before destroying the platform or
  // unmounting either filesystem.
  playground::switch_runtime::sharedSwitchNetwork().shutdownAndWait();
  // Also cover partial startup: no database may outlive its native adapter.
  if (context.database) {
    CKLBLuaDB::closeAll();
    CKLBDatabase::release();
    CKLBLanguageDatabase::release();
  }
  context.database.reset();
  CPFInterface::getInstance().setClientRequest(nullptr);
  CPFInterface::getInstance().setPlatformRequest(nullptr);
  context.platform.reset();
  context.cacheWriters.reset();
  playground::switch_runtime::shutdownCryptoService();
  std::fflush(nullptr);
  if (context.romfs) {
    playground::switch_runtime::shutdownRomFs();
    context.romfs = false;
  }
  std::string storageError;
  if (!context.storage.unmount(&storageError))
    std::fprintf(stderr, "Switch storage unmount failure: %s\n",
                 storageError.c_str());
}

bool prepareStorageAndAssets(EngineContext &context, PlaygroundSwitchHost *host,
                             std::filesystem::path &installRoot,
                             std::string &error) {
  const bool installed = playground::switch_runtime::isInstalledApplication();
  if (installed) {
    if (!context.storage.mountInstalledTitle(error))
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
  if (installed)
    options.mapStoragePath = playground::switch_runtime::encodeStoragePath;
  if (installed) {
    options.describeFileError =
        playground::switch_runtime::describeLastFileError;
  }
  options.commitStorage = [&context](std::string &commitError) {
    return context.storage.commitCache(commitError);
  };
  if (installed) {
    // Horizon SaveData/CacheStorage journals use a bounded mapping table.
    // AppAssets contains thousands of small files; one transaction exhausts
    // that table (FS 2-6706) even though data space remains. Staging has no
    // completion marker, so periodic commits remain power-loss safe.
    options.extractionCommitInterval = 256;
  }
  if (installed) {
    // libstdc++ remove_all reports a generic EIO for large CacheStorage trees
    // on Horizon. Use the mounted IFileSystem's native recursive operation.
    options.removeTree = [&context](const std::filesystem::path &path,
                                    std::string &removeError) {
      return context.storage.removeCacheTree(path, removeError);
    };
    options.resetReplaceableStorage = [&context](std::string &resetError) {
      return context.storage.resetCache(resetError);
    };
    // Keep bootstrap-owned path components compact on Horizon. The immutable
    // directory remains the complete 64-character SHA-256; only descriptive
    // prefixes and the disposable staging suffix are omitted.
    options.maximumInternalNameLength = 64;
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
  context.diagnostics.phase(paused ? 6 : 5);
  if (context.paused == paused)
    return;
  if (!context.initialized) {
    // onStart performs first-run extraction before the engine exists, but the
    // bootstrap progress pump still receives HOME/sleep focus transitions.
    // Commit any mounted transactional storage at that safe between-entry
    // boundary instead of silently skipping lifecycle durability merely
    // because client/audio/network initialization has not happened yet.
    if (paused && context.storage.mounted()) {
      std::string error;
      if (!commitCache(context, error))
        std::fprintf(stderr,
                     "Switch bootstrap suspend CacheStorage commit failed: %s\n",
                     error.c_str());
      error.clear();
      if (!commitState(context, error))
        std::fprintf(stderr,
                     "Switch bootstrap suspend SaveData commit failed: %s\n",
                     error.c_str());
    }
    context.paused = paused;
    return;
  }
  CPFInterface &interface = CPFInterface::getInstance();
  context.durability.setPaused(paused);
  if (paused) {
    interface.client().pauseGame(true);
    if (interface.platform().getAudioSystem())
      interface.platform().getAudioSystem()->onActivityPause();
    std::string error;
    const bool networkIdle =
        playground::switch_runtime::sharedSwitchNetwork().suspend(error);
    if (!networkIdle)
      std::fprintf(stderr, "Switch suspend network failure: %s\n",
                   error.c_str());
    // CacheStorage may still have a temporary download writer after a bounded
    // cancellation timeout. Never commit that filesystem concurrently. Commit
    // content before SaveData so durable state cannot reference lost content.
    if (networkIdle) {
      error.clear();
      if (!commitCache(context, error))
        std::fprintf(stderr, "Switch suspend CacheStorage commit failed: %s\n",
                     error.c_str());
    }
    error.clear();
    if (!commitState(context, error))
      std::fprintf(stderr, "Switch suspend SaveData commit failed: %s\n",
                   error.c_str());
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
  if (!playground::switch_runtime::initializeCryptoService(error)) {
    fatal(error);
    releasePlatformResources(context);
    return false;
  }
  if (!prepareStorageAndAssets(context, host, installRoot, error)) {
    fatal(error);
    releasePlatformResources(context);
    return false;
  }
  const auto &roots = context.storage.roots();
  const bool installed =
      roots.mode == playground::switch_runtime::StorageMode::InstalledTitle;
  playground::runtime::RuntimePlatform::StorageCommit commitState;
  playground::runtime::RuntimePlatform::StorageCommit commitContent;
  if (roots.mode == playground::switch_runtime::StorageMode::InstalledTitle) {
    context.cacheWriters = std::make_unique<playground::switch_runtime::SwitchArchiveWriters>(
        roots.cache.generic_string(),
        [&context](std::string& error) { return context.storage.commitCache(error); });
    context.cacheWriters->setCommitPolicy(CacheCommitBytes, CacheCommitOps);
    commitState = [&context] {
      std::string commitError;
      const bool committed = ::commitState(context, commitError);
      if (!committed)
        std::fprintf(stderr, "SaveData commit failed: %s\n",
                     commitError.c_str());
      return committed;
    };
    commitContent = [&context] {
      std::string commitError;
      const bool committed = context.cacheWriters->requestCommit(commitError);
      if (!committed)
        std::fprintf(stderr, "CacheStorage commit failed: %s\n",
                     commitError.c_str());
      return committed;
    };
  }
  context.platform = std::make_unique<playground::runtime::RuntimePlatform>(
      installRoot.string(), roots.content.string(), roots.state.string(),
      playgroundSwitchHostGetGLProcAddress,
      std::move(commitState), std::move(commitContent),
      installed
          ? playground::runtime::RuntimePlatform::PathMapper{
                playground::switch_runtime::encodeStoragePath}
          : playground::runtime::RuntimePlatform::PathMapper{},
      [&context](const std::filesystem::path &path,
                 std::string &removeError) {
        return context.storage.removeTree(path, removeError);
      },
      PLAYGROUND_APP_INTEGRITY_DATABASE_URI,
      installed
          ? playground::runtime::RuntimePlatform::StorageList{
                playground::switch_runtime::listStorageDirectory}
          : playground::runtime::RuntimePlatform::StorageList{});

  if (context.cacheWriters) {
    context.platform->setContentWriterFactory([&context](const std::string& path) {
      return context.cacheWriters->open(path);
    });
    context.platform->setContentWriteBatch([&context](bool begin, bool success) {
      std::string error;
      const bool result = begin ? context.cacheWriters->beginBatch(error)
                                : context.cacheWriters->endBatch(success, error);
      if (!result) std::fprintf(stderr, "CacheStorage write batch failed: %s\n", error.c_str());
      return result;
    });
  }
  CPFInterface &interface = CPFInterface::getInstance();
  interface.setPlatformRequest(context.platform.get());
  context.database = std::make_unique<playground::switch_runtime::SwitchDatabase>(
      installRoot, roots.content, roots.state, installed,
      [&context](std::string& failure) { return context.storage.commitState(failure); });
  context.database->setNativeError(playground::switch_runtime::describeLastFileError);
  context.database->setMainThread(std::this_thread::get_id());
  if (context.cacheWriters) {
    context.database->setBeforeCommit([&context](std::string& failure) {
      return context.cacheWriters->commitIfDirty(failure);
    });
    context.database->setDeferredCommit(true);
  }
  if (!context.database->initialize()) {
    fatal(context.database->error());
    releasePlatformResources(context);
    return false;
  }
  if (context.cacheWriters)
    context.durability.start(*context.database);
  context.diagnostics.start([&context] {
    std::string result = context.cacheWriters ? context.cacheWriters->profile() : "No cache coordinator\n";
    if (context.database) result += context.database->profile();
    result += playground::switch_runtime::audioTimingProfile();
    playground::switch_runtime::NetworkMetrics metrics;
    if (playground::switch_runtime::sharedSwitchNetwork().tryMetrics(metrics)) {
      char text[192];
      std::snprintf(text, sizeof(text), "Network: completed=%llu bytes=%llu active=%u peak=%u\n",
          static_cast<unsigned long long>(metrics.transferCount),
          static_cast<unsigned long long>(metrics.bytesReceived),
          metrics.activeTransfers, metrics.peakActiveTransfers);
      result += text;
    } else result += "Network metrics busy\n";
    return result;
  });
  if (!context.platform->init()) {
    fatal("Platform initialization failed: " +
          context.platform->initializationError());
    context.platform->shutdownAudioSystem();
    releasePlatformResources(context);
    return false;
  }
  if (!GameSetup()) {
    fatal("Client setup failed");
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
  context.diagnostics.phase(1);
  context.platform->pumpPlatformEvents();
  const u32 deltaMs =
      static_cast<u32>(std::clamp<std::uint64_t>(deltaNs / 1000000, 1, 250));
  context.diagnostics.phase(2);
  if (!CPFInterface::getInstance().client().frameFlip(deltaMs))
    return false;
  context.diagnostics.phase(3);
  context.platform->renderWidgets();
  context.diagnostics.phase(4);
  playgroundSwitchHostSwapBuffers(host);
  context.diagnostics.frame();
  context.diagnostics.phase(5);
  if (context.platform->quitRequested())
    playgroundSwitchHostRequestQuit(host);
  return true;
}

void onStop(void *opaque, PlaygroundSwitchHost *) {
  auto &context = *static_cast<EngineContext *>(opaque);
  context.diagnostics.phase(7);
  if (context.initialized) {
    CPFInterface::getInstance().client().finishGame(true);
    context.initialized = false;
  }
  if (context.platform)
    context.platform->shutdownAudioSystem();
  // releasePlatformResources drains transfer leases and performs the single
  // checked final filesystem commit/unmount boundary.
  releasePlatformResources(context);
}

void showDownloadProfile(EngineContext& context) {
    // Temporary profiling UI. Capture before opening the system applet: its
    // own time on-screen must not enter the displayed measurement interval.
    const std::string storage = context.cacheWriters->profile();
    const auto network = playground::switch_runtime::sharedSwitchNetwork().metrics();
    char summary[512];
    std::snprintf(summary, sizeof(summary),
        "\nFile log status=%d (0 off,1 starting,2 active,-1 failed,3 stopped)\n"
        "Network (completed transfers)\ncount=%llu bytes=%llu\n"
        "summed_transfer_seconds=%.3f active=%u\n"
        "Timings overlap across threads; not a wire-only speed measurement.",
        context.diagnostics.status(), static_cast<unsigned long long>(network.transferCount),
        static_cast<unsigned long long>(network.bytesReceived),
        network.transferNanoseconds / 1000000000.0, network.activeTransfers);
    playground::switch_runtime::showApplicationMessage(
        "Download timing snapshot (diagnostic, not a game error)",
        (storage + summary + playground::switch_runtime::audioTimingProfile()).c_str());
}

void onPointer(void *opaque, PlaygroundSwitchHost *, std::int64_t id,
               PlaygroundSwitchPointerPhase phase, float x, float y) {
  auto &context = *static_cast<EngineContext *>(opaque);
  if (!context.initialized || context.paused)
    return;
  if (phase == PLAYGROUND_SWITCH_POINTER_DOWN) {
    if (context.platform->handleWidgetPointerDown(x, y)) {
      context.widgetCapturedPointers.insert(id);
      return;
    }
  } else if (context.widgetCapturedPointers.count(id)) {
    if (phase == PLAYGROUND_SWITCH_POINTER_UP ||
        phase == PLAYGROUND_SWITCH_POINTER_CANCEL)
      context.widgetCapturedPointers.erase(id);
    return;
  }
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
  if ((pressed & PLAYGROUND_SWITCH_BUTTON_MINUS) && context.cacheWriters) {
    showDownloadProfile(context);
    return;
  }
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
