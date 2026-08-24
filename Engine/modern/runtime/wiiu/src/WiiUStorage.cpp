#include "Playground/WiiU/WiiUStorage.h"

#include <mbedtls/sha256.h>
#include <nn/act/client_cpp.h>
#include <nn/save/save.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

namespace playground::wiiu_runtime {
namespace {

bool createRoots(StorageRoots &roots, std::string &error) {
  std::error_code fsError;
  std::filesystem::create_directories(roots.installContainer, fsError);
  if (!fsError)
    std::filesystem::create_directories(roots.content, fsError);
  if (!fsError)
    std::filesystem::create_directories(roots.state, fsError);
  if (!fsError)
    return true;
  error = "Could not create Wii U user storage: " + fsError.message();
  return false;
}

} // namespace

WiiUStorage::~WiiUStorage() { unmount(); }

bool WiiUStorage::mount(std::string &error) {
  unmount();
  const nn::Result actResult = nn::act::Initialize();
  if (actResult.IsFailure()) {
    const NNResult nativeResult = actResult;
    char message[128];
    std::snprintf(message, sizeof(message),
                  "nn::act::Initialize failed with result 0x%08x",
                  static_cast<unsigned>(nativeResult.value));
    error = message;
    return false;
  }
  m_actInitialized = true;
  m_slot = nn::act::GetSlotNo();
  m_persistentId = nn::act::GetPersistentIdEx(m_slot);
  if (!m_slot || !m_persistentId) {
    error = "The title was launched without a usable Wii U user account";
    unmount();
    return false;
  }

  const SAVEStatus initResult = SAVEInit();
  if (initResult != SAVE_STATUS_OK) {
    error = "SAVEInit failed with status " + std::to_string(initResult);
    unmount();
    return false;
  }
  m_saveInitialized = true;
  const SAVEStatus directoryResult = SAVEInitSaveDir(m_slot);
  if (directoryResult != SAVE_STATUS_OK) {
    error = "SAVEInitSaveDir failed with status " +
            std::to_string(directoryResult);
    unmount();
    return false;
  }

  char userPath[64];
  std::snprintf(userPath, sizeof(userPath), "/vol/save/%08x",
                static_cast<unsigned>(m_persistentId));
  m_roots.user = userPath;
  char cachePath[128];
  std::snprintf(cachePath, sizeof(cachePath),
                "fs:/vol/external01/wiiu/apps/PlaygroundOSS/users/%08x",
                static_cast<unsigned>(m_persistentId));
  const std::filesystem::path cacheRoot = cachePath;
  m_roots.installContainer = cacheRoot / "appassets";
  m_roots.content = cacheRoot / "external";
  m_roots.state = m_roots.user / "state";
  if (!createRoots(m_roots, error) || !commit(error)) {
    unmount();
    return false;
  }
  return true;
}

bool WiiUStorage::commit(std::string &error) {
  if (!m_saveInitialized) {
    error = "Wii U user SaveData is not initialized";
    return false;
  }
  const SAVEStatus result = SAVEUpdateSaveDir();
  if (result == SAVE_STATUS_OK)
    return true;
  error = "SAVEUpdateSaveDir failed with status " + std::to_string(result);
  return false;
}

void WiiUStorage::unmount() {
  if (m_saveInitialized)
    SAVEShutdown();
  if (m_actInitialized)
    nn::act::Finalize();
  m_roots = {};
  m_slot = 0;
  m_persistentId = 0;
  m_saveInitialized = false;
  m_actInitialized = false;
}

bool hashFileSha256(const std::filesystem::path &path,
                    std::string &hexDigest, std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open file for SHA-256: " + path.string();
    return false;
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts_ret(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    error = "Could not initialize SHA-256";
    return false;
  }
  // AppAssets is over 100 MiB. Hash in large chunks without consuming 64 KiB
  // of the Wii U main-thread stack during first-launch bootstrap.
  std::vector<unsigned char> buffer(64 * 1024);
  while (input) {
    input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0 && mbedtls_sha256_update_ret(
                         &context, buffer.data(),
                         static_cast<std::size_t>(count)) != 0) {
      mbedtls_sha256_free(&context);
      error = "SHA-256 update failed";
      return false;
    }
  }
  if (input.bad()) {
    mbedtls_sha256_free(&context);
    error = "Could not read file for SHA-256: " + path.string();
    return false;
  }
  std::array<unsigned char, 32> digest{};
  const int result = mbedtls_sha256_finish_ret(&context, digest.data());
  mbedtls_sha256_free(&context);
  if (result != 0) {
    error = "SHA-256 finalization failed";
    return false;
  }
  static constexpr char Hex[] = "0123456789abcdef";
  hexDigest.resize(digest.size() * 2);
  for (std::size_t index = 0; index < digest.size(); ++index) {
    hexDigest[index * 2] = Hex[digest[index] >> 4];
    hexDigest[index * 2 + 1] = Hex[digest[index] & 0xf];
  }
  return true;
}

} // namespace playground::wiiu_runtime
