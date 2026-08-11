#include "BaseType.h"
#include "FileSystem.h"
#include "Playground/Runtime/DesktopPlatform.h"
#include "encryptFile.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "platform-services-probe: %s\n", message);
    std::fflush(nullptr);
    std::_Exit(1);
  }
  return condition;
}

s32 probeThread(void *, void *) {
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  return 73;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s external-root\n", argv[0]);
    std::fflush(nullptr);
    std::_Exit(64);
  }

  std::string firstDeviceId;
  {
    playground::runtime::DesktopPlatform platform(".", argv[1]);
    initNMAsset(0);
    std::array<char, 256> buffer{};
    if (!check(platform.readyDevID(), "device ID unavailable") ||
        !check(platform.getDevID(buffer.data(), buffer.size()) == 36,
               "device ID is not a UUID"))
      return 1;
    firstDeviceId = buffer.data();

    const std::string platformDescription = platform.getPlatform();
    if (!check(platformDescription.rfind("Android;", 0) == 0,
               "platform description does not use the SIF Android family"))
      return 1;
    if (!check(std::count(platformDescription.begin(),
                          platformDescription.end(), ';') == 2,
               "platform description does not contain OS/version/timezone"))
      return 1;

    char encryptedPayload[] = "encrypted desktop stream";
    const char expectedPayload[] = "encrypted desktop stream";
    IReadStream *writeHandle = platform.openWriteStream(
        "file://external/probe/encrypted.bin", true, 0);
    IWriteStream *writer = reinterpret_cast<IWriteStream *>(writeHandle);
    if (!check(writer && writer->getStatus() == IWriteStream::NORMAL,
               "encrypted writer unavailable"))
      return 1;
    writer->writeBlock(encryptedPayload, sizeof(encryptedPayload));
    delete writer;
    IReadStream *reader =
        platform.openReadStream("file://external/probe/encrypted.bin", true, 0);
    std::array<char, sizeof(encryptedPayload)> decoded{};
    if (!check(reader && reader->getStatus() == IReadStream::NORMAL &&
                   reader->readBlock(decoded.data(), decoded.size()) &&
                   std::memcmp(decoded.data(), expectedPayload,
                               sizeof(expectedPayload)) == 0,
               "encrypted stream round trip differs"))
      return 1;
    delete reader;

    const char downloadedPayload[] = "published on-demand asset";
    ITmpFile *temporary =
        platform.openTmpFile("file://external/probe/downloaded.bin_");
    if (!check(temporary &&
                   temporary->writeTmp(
                       const_cast<char *>(downloadedPayload),
                       sizeof(downloadedPayload)) == sizeof(downloadedPayload) &&
                   temporary->closeTmp() == 0,
               "temporary download write failed"))
      return 1;
    delete temporary;
    if (!check(platform.irename("file://external/probe/downloaded.bin_",
                                "file://external/probe/downloaded.bin") == 0,
               "virtual-path download publication failed"))
      return 1;
    reader = platform.openReadStream(
        "file://external/probe/downloaded.bin", false, 0);
    std::array<char, sizeof(downloadedPayload)> downloaded{};
    if (!check(reader && reader->getStatus() == IReadStream::NORMAL &&
                   reader->readBlock(downloaded.data(), downloaded.size()) &&
                   std::memcmp(downloaded.data(), downloadedPayload,
                               sizeof(downloadedPayload)) == 0,
               "published download differs"))
      return 1;
    delete reader;

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

    for (int index = 0; index < 30; ++index) {
      const std::string name =
          "asset://probe/" + std::to_string(index) + ".lua";
      const std::string source = "return " + std::to_string(index);
      platform.registerScriptSource(source.data(), source.size(), name.c_str());
    }
    char *requestHeader = platform.createRequestIdHeader();
    if (!check(requestHeader &&
                   std::strncmp(requestHeader, "X-REQUEST-ID:", 13) == 0,
               "request ID header was not generated"))
      return 1;
    delete[] requestHeader;

    char *integrity = platform.getDeviceIntegrityInfo("probe-request");
    if (!check(integrity && std::strstr(integrity, "device_id") &&
                   std::strstr(integrity, "SuspiciousElement"),
               "desktop integrity payload is incomplete"))
      return 1;
    delete[] integrity;

    void *thread = platform.createThread(probeThread, nullptr);
    s32 status = 0;
    if (!check(thread && platform.watchThread(thread, &status),
               "live thread was reported complete"))
      return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (!check(!platform.watchThread(thread, &status) && status == 73,
               "completed thread status differs"))
      return 1;
    platform.deleteThread(thread);

    if (!check(platform.getPhysicalMemKB() > 0,
               "physical memory metric unavailable") ||
        !check(platform.getFreeMemorySize() > 0,
               "free memory metric unavailable") ||
        !check(platform.getUsedMemorySize() > 0,
               "process memory metric unavailable"))
      return 1;

    IWidget *text =
        platform.createControl(IWidget::TEXTBOX, 7, "seed", 10, 20, 300, 40, 8);
    if (!check(text != nullptr && text->getTextMaxLength() == 8,
               "desktop text control unavailable"))
      return 1;
    platform.handleTextInput("-payload");
    std::array<char, 32> widgetText{};
    text->getText(widgetText.data(), widgetText.size());
    if (!check(std::string(widgetText.data()) == "seed-pay",
               "text input/max-length behavior differs") ||
        !check(platform.handleEditingKey(SDLK_BACKSPACE, true),
               "text backspace was not consumed"))
      return 1;
    text->getText(widgetText.data(), widgetText.size());
    if (!check(std::string(widgetText.data()) == "seed-pa",
               "text backspace behavior differs"))
      return 1;
    platform.destroyControl(text);
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
