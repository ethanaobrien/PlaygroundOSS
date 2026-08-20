#include "Playground/Switch/SwitchStorage.h"

#include <mbedtls/sha256.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace playground::switch_runtime {

bool hashFileSha256(const std::filesystem::path &path, std::string &digest,
                    std::string &error) {
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
  std::array<unsigned char, 256 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0 && mbedtls_sha256_update_ret(
                         &context, buffer.data(),
                         static_cast<std::size_t>(count)) != 0) {
      mbedtls_sha256_free(&context);
      error = "Could not update SHA-256";
      return false;
    }
  }
  if (!input.eof()) {
    mbedtls_sha256_free(&context);
    error = "Could not read file for SHA-256: " + path.string();
    return false;
  }
  std::array<unsigned char, 32> output{};
  if (mbedtls_sha256_finish_ret(&context, output.data()) != 0) {
    mbedtls_sha256_free(&context);
    error = "Could not finish SHA-256";
    return false;
  }
  mbedtls_sha256_free(&context);
  std::ostringstream text;
  text << std::hex << std::setfill('0');
  for (unsigned char byte : output)
    text << std::setw(2) << static_cast<unsigned int>(byte);
  digest = text.str();
  return true;
}

} // namespace playground::switch_runtime
