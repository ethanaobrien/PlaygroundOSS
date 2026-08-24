#include "Playground/WiiU/WiiURandom.h"

#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <mbedtls/hmac_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/entropy_poll.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace {

mbedtls_hmac_drbg_context g_drbg;
std::mutex g_mutex;
bool g_initialized;

bool readSeedFile(const std::filesystem::path &path,
                  std::array<unsigned char, 64> &seed) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  input.read(reinterpret_cast<char *>(seed.data()), seed.size());
  return input.gcount() == static_cast<std::streamsize>(seed.size());
}

bool readSeed(const std::filesystem::path &path,
              std::array<unsigned char, 64> &seed) {
  return readSeedFile(path, seed) ||
         readSeedFile(path.string() + ".previous", seed);
}

bool writeSeed(const std::filesystem::path &path,
               const std::array<unsigned char, 64> &seed) {
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      return false;
    output.write(reinterpret_cast<const char *>(seed.data()), seed.size());
    output.flush();
    if (!output)
      return false;
  }
  std::error_code ignored;
  const std::filesystem::path previous = path.string() + ".previous";
  std::filesystem::remove(previous, ignored);
  ignored.clear();
  if (std::filesystem::exists(path, ignored)) {
    ignored.clear();
    std::filesystem::rename(path, previous, ignored);
    if (ignored) {
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }
  ignored.clear();
  std::filesystem::rename(temporary, path, ignored);
  if (ignored) {
    std::error_code restoreError;
    std::filesystem::rename(previous, path, restoreError);
  }
  return !ignored;
}

bool randomBytesUnlocked(unsigned char *output, std::size_t size) {
  while (size) {
    const std::size_t count =
        std::min<std::size_t>(size, MBEDTLS_HMAC_DRBG_MAX_REQUEST);
    if (mbedtls_hmac_drbg_random(&g_drbg, output, count) != 0)
      return false;
    output += count;
    size -= count;
  }
  return true;
}

} // namespace

namespace playground::wiiu_runtime {

bool initializeRandom(const std::filesystem::path &seedPath,
                      const void *startupEntropy,
                      std::size_t startupEntropySize,
                      std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized)
    return true;
  if (!startupEntropy || startupEntropySize < 32) {
    error = "Wii U startup entropy was not collected";
    return false;
  }

  std::array<unsigned char, 64> storedSeed{};
  const bool existingSeed = readSeed(seedPath, storedSeed);
  std::vector<unsigned char> material;
  material.reserve(storedSeed.size() + startupEntropySize + 16);
  if (existingSeed)
    material.insert(material.end(), storedSeed.begin(), storedSeed.end());
  material.insert(material.end(), static_cast<const unsigned char *>(startupEntropy),
                  static_cast<const unsigned char *>(startupEntropy) +
                      startupEntropySize);
  const OSTime wallClock = OSGetTime();
  const OSTime systemClock = OSGetSystemTime();
  material.insert(material.end(),
                  reinterpret_cast<const unsigned char *>(&wallClock),
                  reinterpret_cast<const unsigned char *>(&wallClock) +
                      sizeof(wallClock));
  material.insert(material.end(),
                  reinterpret_cast<const unsigned char *>(&systemClock),
                  reinterpret_cast<const unsigned char *>(&systemClock) +
                      sizeof(systemClock));

  std::array<unsigned char, 32> digest{};
  if (mbedtls_sha256_ret(material.data(), material.size(), digest.data(), 0) !=
      0) {
    error = "Could not derive the Wii U random seed";
    return false;
  }
  mbedtls_hmac_drbg_init(&g_drbg);
  const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!sha256 || mbedtls_hmac_drbg_seed_buf(&g_drbg, sha256, digest.data(),
                                            digest.size()) != 0) {
    mbedtls_hmac_drbg_free(&g_drbg);
    error = "Could not initialize the Wii U random generator";
    return false;
  }
  mbedtls_hmac_drbg_set_prediction_resistance(
      &g_drbg, MBEDTLS_HMAC_DRBG_PR_OFF);
  mbedtls_hmac_drbg_set_reseed_interval(&g_drbg, 1 << 20);

  std::array<unsigned char, 64> replacement{};
  if (mbedtls_hmac_drbg_random(&g_drbg, replacement.data(),
                               replacement.size()) != 0 ||
      !writeSeed(seedPath, replacement)) {
    mbedtls_hmac_drbg_free(&g_drbg);
    error = "Could not rotate the per-user Wii U random seed";
    return false;
  }
  std::memset(replacement.data(), 0, replacement.size());
  std::memset(storedSeed.data(), 0, storedSeed.size());
  std::memset(digest.data(), 0, digest.size());
  g_initialized = true;
  return true;
}

void shutdownRandom() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized)
    return;
  mbedtls_hmac_drbg_free(&g_drbg);
  g_initialized = false;
}

} // namespace playground::wiiu_runtime

extern "C" bool playgroundWiiUGetRandomBytes(unsigned char *output,
                                                std::size_t size) {
  if (!output && size)
    return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_initialized && randomBytesUnlocked(output, size);
}

extern "C" int mbedtls_hardware_poll(void *, unsigned char *output,
                                      std::size_t length,
                                      std::size_t *outputLength) {
  if (outputLength)
    *outputLength = 0;
  if ((!output && length) || !outputLength)
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || !randomBytesUnlocked(output, length))
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  *outputLength = length;
  return 0;
}
