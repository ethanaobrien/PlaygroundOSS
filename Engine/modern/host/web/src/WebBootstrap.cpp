#include "WebBootstrap.h"

#include "Playground/Bootstrap/AssetBootstrap.h"

#include <emscripten.h>
#include <emscripten/atomic.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>
#include <mbedtls/sha256.h>

#include <array>
#include <cstdio>
#include <cstdlib>
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

enum XhrState : std::uint32_t {
  XhrPending,
  XhrProgress,
  XhrDone,
  XhrError,
};

struct alignas(4) XhrControl {
  std::uint32_t state{};
  std::uint32_t httpStatus{};
  std::uint32_t data{};
  std::uint32_t length{};
  std::uint32_t transferred{};
  std::uint32_t total{};
};

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
  XhrControl control;
  MAIN_THREAD_EM_ASM({
    const control = $0 >>> 2;
    const request = new XMLHttpRequest();
    const publishState = state => {
      Atomics.store(HEAP32, control, state);
      Atomics.notify(HEAP32, control);
    };
    request.open('GET', UTF8ToString($1), true);
    request.responseType = 'arraybuffer';
    request.withCredentials = false;
    request.onreadystatechange = () => {
      if (request.readyState >= XMLHttpRequest.HEADERS_RECEIVED) {
        try {
          Atomics.store(HEAP32, control + 1, request.status);
        } catch (_) {}
      }
    };
    request.onprogress = event => {
      Atomics.store(HEAP32, control + 4,
        Math.min(event.loaded, 0xffffffff));
      Atomics.store(HEAP32, control + 5,
        event.lengthComputable ? Math.min(event.total, 0xffffffff) : 0);
      if (Atomics.load(HEAP32, control) < 2)
        publishState(1);
    };
    request.onload = () => {
      Atomics.store(HEAP32, control + 1, request.status);
      if (request.status < 200 || request.status >= 300 || !request.response) {
        publishState(3);
        return;
      }
      const bytes = new Uint8Array(request.response);
      const data = _malloc(bytes.byteLength || 1);
      if (!data) {
        publishState(3);
        return;
      }
      HEAPU8.set(bytes, data);
      Atomics.store(HEAP32, control + 2, data);
      Atomics.store(HEAP32, control + 3, bytes.byteLength);
      Atomics.store(HEAP32, control + 4, bytes.byteLength);
      Atomics.store(HEAP32, control + 5, bytes.byteLength);
      publishState(2);
    };
    request.onerror = request.onabort = request.ontimeout = () =>
      publishState(3);
    request.send();
  }, &control, url);

  for (;;) {
    const auto state = static_cast<XhrState>(
        emscripten_atomic_load_u32(&control.state));
    if (state == XhrPending) {
      emscripten_futex_wait(&control.state, XhrPending, 1000.0);
      continue;
    }
    if (state == XhrProgress) {
      setStatus("Preparing game data", "Downloading integrity metadata",
                control.total ? static_cast<double>(control.transferred) /
                                    control.total
                              : 0.0);
      std::uint32_t expected = XhrProgress;
      emscripten_atomic_cas_u32(&control.state, expected, XhrPending);
      continue;
    }
    break;
  }

  if (control.state != XhrDone) {
    error = "Could not download " + std::string(url) + " (HTTP " +
            std::to_string(control.httpStatus) + ")";
    return false;
  }
  std::filesystem::create_directories(destination.parent_path());
  const auto temporary = destination.string() + ".part";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output ||
      !output.write(reinterpret_cast<const char *>(control.data),
                    static_cast<std::streamsize>(control.length)) ||
      !output.flush()) {
    error = "Could not write downloaded " + destination.string();
    std::free(reinterpret_cast<void *>(control.data));
    return false;
  }
  output.close();
  std::free(reinterpret_cast<void *>(control.data));
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
  StreamProgress,
  StreamChunk,
  StreamDone,
  StreamError,
  StreamCancel,
};

struct alignas(4) StreamControl {
  std::uint32_t state{};
  std::uint32_t length{};
  std::uint32_t httpStatus{};
  std::uint32_t transferred{};
  std::uint32_t total{};
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

  // Start an asynchronous XMLHttpRequest on the browser thread. XHR progress
  // events update the startup UI while the engine pthread sleeps; once loaded,
  // the response is copied to OPFS through one bounded shared-memory chunk.
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
        if (state === 5)
          return false;
        const waiter = Atomics.waitAsync(HEAP32, control, state, 1000);
        if (waiter.async)
          await waiter.value;
        else
          await new Promise(resolve => setTimeout(resolve, 0));
      }
    };
    const fail = exception => {
      console.error(`AppAssets download failed: ${exception}`);
      Atomics.store(HEAP32, control, 4);
      Atomics.notify(HEAP32, control);
    };
    const request = new XMLHttpRequest();
    request.open('GET', url, true);
    request.responseType = 'arraybuffer';
    request.withCredentials = false;
    request.onreadystatechange = () => {
      if (request.readyState >= XMLHttpRequest.HEADERS_RECEIVED) {
        try {
          Atomics.store(HEAP32, control + 2, request.status);
        } catch (_) {}
      }
    };
    request.onprogress = event => {
      Atomics.store(HEAP32, control + 3,
        Math.min(event.loaded, 0xffffffff));
      Atomics.store(HEAP32, control + 4,
        event.lengthComputable ? Math.min(event.total, 0xffffffff) : 0);
      const state = Atomics.load(HEAP32, control);
      if (state === 0 || state === 1) {
        Atomics.store(HEAP32, control, 1);
        Atomics.notify(HEAP32, control);
      }
    };
    request.onload = async () => {
      try {
        Atomics.store(HEAP32, control + 2, request.status);
        if (request.status < 200 || request.status >= 300 || !request.response)
          throw new Error(`HTTP ${request.status}`);
        const bytes = new Uint8Array(request.response);
        let offset = 0;
        while (offset < bytes.byteLength) {
          if (!await waitForEmpty())
            throw new Error('download cancelled');
          const length = Math.min(capacity, bytes.byteLength - offset);
          HEAPU8.set(bytes.subarray(offset, offset + length), buffer);
          Atomics.store(HEAP32, control + 1, length);
          Atomics.store(HEAP32, control, 2);
          Atomics.notify(HEAP32, control);
          offset += length;
        }
        if (!await waitForEmpty())
          throw new Error('download cancelled');
        Atomics.store(HEAP32, control, 3);
        Atomics.notify(HEAP32, control);
      } catch (exception) {
        fail(exception);
      }
    };
    request.onerror = request.onabort = request.ontimeout = () =>
      fail(`network status ${request.status || 0}`);
    request.send();
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
    if (state == StreamProgress) {
      const std::uint64_t transferred = control.transferred;
      const std::uint64_t total = control.total ? control.total : expectedSize;
      setStatus("Downloading AppAssets.zip",
                byteProgress(transferred, total),
                total ? static_cast<double>(transferred) / total : 0.0);
      std::uint32_t expected = StreamProgress;
      emscripten_atomic_cas_u32(&control.state, expected, StreamEmpty);
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
