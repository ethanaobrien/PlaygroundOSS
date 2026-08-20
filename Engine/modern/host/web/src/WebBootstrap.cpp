#include "WebBootstrap.h"

#include "Playground/Bootstrap/AssetBootstrap.h"

#include <emscripten.h>
#include <emscripten/atomic.h>
#include <emscripten/fetch.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>
#include <mbedtls/sha256.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifndef PLAYGROUND_WEB_APP_ASSETS_URL
#define PLAYGROUND_WEB_APP_ASSETS_URL "AppAssets.zip"
#endif
#ifndef PLAYGROUND_WEB_APP_ASSETS_METADATA_URL
#define PLAYGROUND_WEB_APP_ASSETS_METADATA_URL "AppAssets.metadata"
#endif

namespace playground::web {
namespace {

constexpr const char *Root = "/playground-opfs";
constexpr const char *CacheRoot = "/playground-opfs/cache/appassets";
constexpr const char *StateRoot = "/playground-opfs/user";
constexpr const char *Archive = "/playground-opfs/cache/AppAssets.zip";
constexpr const char *Metadata = "/playground-opfs/cache/AppAssets.metadata";
constexpr const char *StateGeneration =
    "/playground-opfs/user/.appassets-generation";

void setStatus(const char *phase, const std::string &detail, double progress) {
  MAIN_THREAD_EM_ASM({
    const phase = UTF8ToString($0);
    const detail = UTF8ToString($1);
    if (globalThis.playgroundSetStatus)
      globalThis.playgroundSetStatus(phase, detail, $2);
  }, phase, detail.c_str(), progress);
}

bool download(const char *url, const std::filesystem::path &destination,
              std::string &error) {
  emscripten_fetch_attr_t attributes;
  emscripten_fetch_attr_init(&attributes);
  std::strcpy(attributes.requestMethod, "GET");
  attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY |
                          EMSCRIPTEN_FETCH_SYNCHRONOUS;
  attributes.withCredentials = EM_TRUE;
  emscripten_fetch_t *fetch = emscripten_fetch(&attributes, url);
  if (!fetch || fetch->status < 200 || fetch->status >= 300) {
    error = "Could not download " + std::string(url) + " (HTTP " +
            std::to_string(fetch ? fetch->status : 0) + ")";
    if (fetch)
      emscripten_fetch_close(fetch);
    return false;
  }
  std::filesystem::create_directories(destination.parent_path());
  const auto temporary = destination.string() + ".part";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output ||
      !output.write(fetch->data,
                    static_cast<std::streamsize>(fetch->numBytes)) ||
      !output.flush()) {
    error = "Could not write downloaded " + destination.string();
    emscripten_fetch_close(fetch);
    return false;
  }
  output.close();
  emscripten_fetch_close(fetch);
  std::error_code fsError;
  std::filesystem::remove(destination, fsError);
  fsError.clear();
  std::filesystem::rename(temporary, destination, fsError);
  if (fsError) {
    error = "Could not publish downloaded file: " + fsError.message();
    return false;
  }
  return true;
}

enum StreamState : std::uint32_t {
  StreamEmpty,
  StreamChunk,
  StreamDone,
  StreamError,
  StreamCancel,
};

struct alignas(4) StreamControl {
  std::uint32_t state{};
  std::uint32_t length{};
  std::uint32_t httpStatus{};
};

std::string formatDigest(const std::array<unsigned char, 32> &bytes) {
  std::ostringstream formatted;
  formatted << std::hex << std::setfill('0');
  for (unsigned char byte : bytes)
    formatted << std::setw(2) << static_cast<unsigned int>(byte);
  return formatted.str();
}

std::string byteProgress(std::uint64_t done, std::uint64_t total) {
  constexpr double MiB = 1024.0 * 1024.0;
  std::ostringstream text;
  text << std::fixed << std::setprecision(1)
       << static_cast<double>(done) / MiB << " / "
       << static_cast<double>(total) / MiB << " MiB";
  return text.str();
}

bool streamDownload(const char *url, const std::filesystem::path &destination,
                    std::uint64_t expectedSize,
                    const std::string &expectedDigest, std::string &digest,
                    std::string &error) {
  constexpr std::size_t BufferSize = 1024 * 1024;
  std::vector<unsigned char> buffer(BufferSize);
  StreamControl control;
  const std::string temporary = destination.string() + ".part";
  std::filesystem::create_directories(destination.parent_path());
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Could not create downloaded " + destination.string();
    return false;
  }

  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  if (mbedtls_sha256_starts(&hash, 0) != 0) {
    mbedtls_sha256_free(&hash);
    error = "Could not initialize AppAssets SHA-256";
    return false;
  }

  // Start the asynchronous browser stream on the main runtime thread. The
  // engine pthread consumes one bounded shared-memory chunk at a time and
  // writes it directly to OPFS, avoiding Fetch's full-response Wasm-heap copy.
  MAIN_THREAD_EM_ASM({
    const control = $0 >>> 2;
    const buffer = $1;
    const capacity = $2;
    const url = UTF8ToString($3);
    const waitForEmpty = async () => {
      for (;;) {
        const state = Atomics.load(HEAP32, control);
        if (state === 0)
          return true;
        if (state === 4)
          return false;
        const waiter = Atomics.waitAsync(HEAP32, control, state, 1000);
        if (waiter.async)
          await waiter.value;
        else
          await new Promise(resolve => setTimeout(resolve, 0));
      }
    };
    (async () => {
      try {
        const response = await fetch(url, { credentials: 'include' });
        Atomics.store(HEAP32, control + 2, response.status);
        if (!response.ok || !response.body)
          throw new Error(`HTTP ${response.status}`);
        const reader = response.body.getReader();
        for (;;) {
          const result = await reader.read();
          if (result.done)
            break;
          let offset = 0;
          while (offset < result.value.byteLength) {
            if (!await waitForEmpty())
              throw new Error('download cancelled');
            const length = Math.min(capacity,
              result.value.byteLength - offset);
            HEAPU8.set(result.value.subarray(offset, offset + length), buffer);
            Atomics.store(HEAP32, control + 1, length);
            Atomics.store(HEAP32, control, 1);
            Atomics.notify(HEAP32, control);
            offset += length;
          }
        }
        if (!await waitForEmpty())
          throw new Error('download cancelled');
        Atomics.store(HEAP32, control, 2);
        Atomics.notify(HEAP32, control);
      } catch (exception) {
        console.error(`AppAssets download failed: ${exception}`);
        Atomics.store(HEAP32, control, 3);
        Atomics.notify(HEAP32, control);
      }
    })();
  }, &control, buffer.data(), buffer.size(), url);

  std::uint64_t received = 0;
  std::uint64_t nextUpdate = 0;
  bool success = true;
  for (;;) {
    const auto state = static_cast<StreamState>(
        emscripten_atomic_load_u32(&control.state));
    if (state == StreamEmpty) {
      emscripten_futex_wait(&control.state, StreamEmpty, 1000.0);
      continue;
    }
    if (state == StreamDone)
      break;
    if (state == StreamError) {
      error = "Could not download AppAssets.zip (HTTP " +
              std::to_string(control.httpStatus) + ")";
      success = false;
      break;
    }
    if (state != StreamChunk || control.length > buffer.size()) {
      error = "Browser supplied an invalid AppAssets download chunk";
      success = false;
      break;
    }
    output.write(reinterpret_cast<const char *>(buffer.data()), control.length);
    if (!output || mbedtls_sha256_update(&hash, buffer.data(), control.length) !=
                       0) {
      error = "Could not store downloaded AppAssets.zip";
      success = false;
      break;
    }
    received += control.length;
    if (received >= nextUpdate || received == expectedSize) {
      setStatus("Downloading AppAssets.zip",
                byteProgress(received, expectedSize),
                expectedSize ? static_cast<double>(received) / expectedSize
                             : 0.0);
      nextUpdate = received + 1024 * 1024;
    }
    emscripten_atomic_store_u32(&control.state, StreamEmpty);
    emscripten_futex_wake(&control.state, 1);
  }

  if (!success) {
    emscripten_atomic_store_u32(&control.state, StreamCancel);
    emscripten_futex_wake(&control.state, 1);
  } else {
    setStatus("Finalizing AppAssets.zip",
              "Committing the archive to browser storage", 1.0);
  }
  output.flush();
  output.close();
  std::array<unsigned char, 32> bytes{};
  success = success && received == expectedSize &&
            mbedtls_sha256_finish(&hash, bytes.data()) == 0;
  mbedtls_sha256_free(&hash);
  if (success) {
    digest = formatDigest(bytes);
    success = digest == expectedDigest;
  }
  if (!success) {
    if (error.empty())
      error = received != expectedSize
                  ? "Downloaded AppAssets.zip has the wrong size"
                  : "Downloaded AppAssets.zip failed SHA-256 validation";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }

  std::error_code fsError;
  std::filesystem::remove(destination, fsError);
  fsError.clear();
  std::filesystem::rename(temporary, destination, fsError);
  if (fsError) {
    error = "Could not publish downloaded file: " + fsError.message();
    return false;
  }
  return true;
}

bool readText(const std::filesystem::path &path, std::string &text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input),
              std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool reconcileMutableConfiguration(const std::string &generation,
                                   std::string &error) {
  std::string previousGeneration;
  if (readText(StateGeneration, previousGeneration) &&
      previousGeneration == generation + "\n")
    return true;

  // server_info and client_info are mutable bootstrap configuration, not user
  // progress. They may have been downloaded by a previous AppAssets
  // distribution and otherwise outrank the new install copy by their embedded
  // date. Reset only these files when the immutable bundle changes; preserve
  // account state, databases, packages, and downloaded assets.
  const std::filesystem::path stateRoot(StateRoot);
  const std::filesystem::path mutableConfiguration[] = {
      stateRoot / "config/server_info.json",
      stateRoot / "config/client_info.json",
  };
  for (const auto &path : mutableConfiguration) {
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    if (removeError) {
      error = "Could not invalidate stale browser configuration: " +
              removeError.message();
      return false;
    }
  }

  const std::string temporary = std::string(StateGeneration) + ".part";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !(output << generation << '\n') || !output.flush()) {
      error = "Could not record the browser AppAssets generation";
      return false;
    }
  }
  std::error_code publishError;
  std::filesystem::remove(StateGeneration, publishError);
  publishError.clear();
  std::filesystem::rename(temporary, StateGeneration, publishError);
  if (publishError) {
    error = "Could not publish the browser AppAssets generation: " +
            publishError.message();
    return false;
  }
  return true;
}

bool hashFile(const std::filesystem::path &path, std::string &digest,
              std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open file for SHA-256: " + path.string();
    return false;
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    error = "Could not initialize SHA-256";
    return false;
  }
  std::array<unsigned char, 1024 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0 &&
        mbedtls_sha256_update(&context, buffer.data(), count) != 0) {
      mbedtls_sha256_free(&context);
      error = "Could not hash AppAssets.zip";
      return false;
    }
  }
  std::array<unsigned char, 32> bytes{};
  const bool success = input.eof() &&
                       mbedtls_sha256_finish(&context, bytes.data()) == 0;
  mbedtls_sha256_free(&context);
  if (!success) {
    error = "Could not finish SHA-256 for AppAssets.zip";
    return false;
  }
  digest = formatDigest(bytes);
  return true;
}

} // namespace

bool prepareBrowserStorage(RuntimePaths &paths, std::string &error) {
  backend_t opfs = wasmfs_create_opfs_backend();
  if (!opfs || wasmfs_create_directory(Root, 0777, opfs) != 0) {
    error = "Could not mount the browser's origin-private filesystem";
    return false;
  }
  std::error_code fsError;
  std::filesystem::create_directories(CacheRoot, fsError);
  std::filesystem::create_directories(StateRoot, fsError);
  if (fsError) {
    error = "Could not create browser storage roots: " + fsError.message();
    return false;
  }

  setStatus("Preparing game data", "Downloading integrity metadata", 0.0);
  if (!download(PLAYGROUND_WEB_APP_ASSETS_METADATA_URL, Metadata, error))
    return false;
  std::string metadataText;
  playground::bootstrap::AssetBundleMetadata metadata;
  if (!readText(Metadata, metadataText) ||
      !playground::bootstrap::parseAssetBundleMetadata(metadataText, metadata,
                                                       error)) {
    if (error.empty())
      error = "Downloaded AppAssets metadata could not be read";
    return false;
  }
  const std::string generationDigest = metadata.sha256;

  const auto generation = std::filesystem::path(CacheRoot) /
      ("install-" + metadata.sha256);
  const bool installed =
      std::filesystem::is_regular_file(generation / ".appassets-version");
  std::string downloadedDigest;
  if (!installed) {
    setStatus("Preparing game data", "Downloading AppAssets.zip", 0.01);
    if (!streamDownload(PLAYGROUND_WEB_APP_ASSETS_URL, Archive,
                        metadata.archiveSize, metadata.sha256,
                        downloadedDigest, error))
      return false;
  }

  playground::bootstrap::AssetBootstrapOptions options;
  options.archive = Archive;
  options.cacheRoot = CacheRoot;
  options.metadata = std::move(metadata);
  options.hashFile = [downloadedDigest](const std::filesystem::path &path,
                                        std::string &digest,
                                        std::string &hashError) {
    if (!downloadedDigest.empty()) {
      digest = downloadedDigest;
      return true;
    }
    return hashFile(path, digest, hashError);
  };
  options.publishWithCompletionMarker = true;
  options.progress = [](std::uint64_t done, std::uint64_t total,
                        const std::string &entry) {
    setStatus("Installing game data", entry,
              total ? static_cast<double>(done) / total : 0.0);
    return true;
  };
  playground::bootstrap::AssetBootstrapResult result;
  if (!playground::bootstrap::prepareAssetBundle(options, result, error))
    return false;
  if (!reconcileMutableConfiguration(generationDigest, error))
    return false;
  std::filesystem::remove(Archive, fsError);
  std::filesystem::remove(Metadata, fsError);
  paths.installRoot = result.installRoot.string();
  paths.externalRoot = StateRoot;
  setStatus("Starting", "Game data is ready", 1.0);
  return true;
}

void showFatalError(const std::string &error) {
  std::fprintf(stderr, "PlaygroundOSS web startup failed: %s\n", error.c_str());
  MAIN_THREAD_EM_ASM({
    if (globalThis.playgroundFatal)
      globalThis.playgroundFatal(UTF8ToString($0));
  }, error.c_str());
}

} // namespace playground::web
