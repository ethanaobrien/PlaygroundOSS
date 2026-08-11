#include "Playground/Runtime/DesktopPlatform.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "platform-services-probe: %s\n", message);
    std::fflush(nullptr);
    std::_Exit(1);
  }
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s external-root\n", argv[0]);
    return 64;
  }

  std::string firstDeviceId;
  {
    playground::runtime::DesktopPlatform platform(".", argv[1]);
    std::array<char, 256> buffer{};
    if (!check(platform.readyDevID(), "device ID unavailable") ||
        !check(platform.getDevID(buffer.data(), buffer.size()) == 36,
               "device ID is not a UUID"))
      return 1;
    firstDeviceId = buffer.data();

    if (!check(platform.setSecureDataID("probe", "user"),
               "secure ID write failed") ||
        !check(platform.setSecureDataPW("probe", "password"),
               "secure password write failed"))
      return 1;
    platform.setUserDefaults("boolean", true);
    platform.setUserDefaults("string", "persistent-value");

    const char plain[] = "AES-CBC interoperability payload";
    const char key[] = "0123456789abcdef";
    std::array<unsigned char, 128> encrypted{};
    std::array<unsigned char, 128> decrypted{};
    int encryptedLength =
        platform.encryptAES128CBC(encrypted.data(), encrypted.size(), plain,
                                  sizeof(plain) - 1, key, sizeof(key) - 1);
    int decryptedLength = platform.decryptAES128CBC(
        decrypted.data(), decrypted.size(),
        reinterpret_cast<const char *>(encrypted.data()), encryptedLength, key,
        sizeof(key) - 1);
    if (!check(encryptedLength > 16, "AES encryption failed") ||
        !check(decryptedLength == sizeof(plain) - 1,
               "AES decrypted length differs") ||
        !check(std::memcmp(decrypted.data(), plain, decryptedLength) == 0,
               "AES round trip differs"))
      return 1;

    std::array<unsigned char, 128> rsa{};
    if (!check(platform.publicKeyEncrypt(
                   reinterpret_cast<unsigned char *>(const_cast<char *>(plain)),
                   sizeof(plain) - 1, rsa.data(), rsa.size()) == 128,
               "RSA public encryption failed"))
      return 1;
  }

  {
    playground::runtime::DesktopPlatform platform(".", argv[1]);
    std::array<char, 256> buffer{};
    platform.getDevID(buffer.data(), buffer.size());
    if (!check(firstDeviceId == buffer.data(), "device ID did not persist") ||
        !check(platform.getSecureDataID("probe", buffer.data(),
                                        buffer.size()) == 4,
               "secure ID did not persist") ||
        !check(std::string(buffer.data()) == "user", "secure ID value differs"))
      return 1;
    platform.getSecureDataPW("probe", buffer.data(), buffer.size());
    if (!check(std::string(buffer.data()) == "password",
               "secure password value differs") ||
        !check(platform.getUserDefaults("boolean"),
               "boolean default did not persist"))
      return 1;
    platform.getUserDefaults("string", buffer.data(), buffer.size());
    if (!check(std::string(buffer.data()) == "persistent-value",
               "string default did not persist"))
      return 1;
  }

  std::puts("platform-services-probe passed");
  std::fflush(nullptr);
  // The legacy engine still has process-global destructors that assume a
  // registered game client. This focused platform probe never creates one.
  std::_Exit(0);
}
