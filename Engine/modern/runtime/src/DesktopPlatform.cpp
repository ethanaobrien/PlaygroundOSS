#include "Playground/Runtime/DesktopPlatform.h"

#include "DesktopStateStore.h"
#include "DesktopWidgets.h"

#include "CKLBCrypto.h"
#include "CKLBScriptEnv.h"
#include "CKLBUtility.h"
#include "FileSystem.h"
#include "FontRendering.h"
#include "ITmpFile.h"
#include "KLBBase64.h"
#include "MultithreadedNetwork.h"
#include "encryptFile.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

extern bool g_decompressBGM;

namespace playground::runtime {

struct DesktopScriptSource {
  std::string sourceName;
  std::string contentHash;
  u16 sourceId{0xffff};
};

class DesktopScriptRegistry {
public:
  DesktopScriptRegistry();
  void registerSource(const char *source, int sourceSize,
                      const char *sourceName);
  char *createHeader();

private:
  std::map<std::string, u16> m_sourceIds;
  std::map<std::string, DesktopScriptSource> m_sources;
  std::vector<std::string> m_pendingNames;
  std::vector<std::string> m_candidates;
};

namespace {

template <class T> void copyText(const T &text, char *buffer, int size) {
  if (!buffer || size <= 0)
    return;
  std::snprintf(buffer, static_cast<size_t>(size), "%s", text.c_str());
}

class DesktopReadStream final : public IReadStream {
public:
  DesktopReadStream(const std::string &path, const char *key, bool decrypt,
                    u32 allowedFormats)
      : m_file(std::fopen(path.c_str(), "rb")), m_decrypter(allowedFormats) {
    if (decrypt && m_file) {
      u8 header[4]{};
      u8 extendedHeader[128]{};
      std::fread(header, 1, sizeof(header), m_file);
      u32 headerSize = 0;
      m_decrypter.decryptSetup(reinterpret_cast<const u8 *>(key), header,
                               &headerSize);
      if (headerSize > sizeof(header)) {
        const u32 remaining = headerSize - sizeof(header);
        if (remaining > sizeof(extendedHeader) ||
            std::fread(extendedHeader, 1, remaining, m_file) != remaining) {
          std::fclose(m_file);
          m_file = nullptr;
          return;
        }
        m_decrypter.finishSetup(extendedHeader, key);
      }
      std::fseek(m_file, static_cast<long>(headerSize), SEEK_SET);
    }
  }
  ~DesktopReadStream() override {
    if (m_file)
      std::fclose(m_file);
  }
  bool isUserEncrypted() override { return m_decrypter.isUserEncrypted(); }
  s32 getSize() override {
    if (!m_file)
      return -1;
    long position = std::ftell(m_file);
    std::fseek(m_file, 0, SEEK_END);
    long size = std::ftell(m_file);
    std::fseek(m_file, position, SEEK_SET);
    return static_cast<s32>(size - m_decrypter.getHeaderSize());
  }
  s32 getPosition() override {
    return m_file ? static_cast<s32>(std::ftell(m_file) -
                                     m_decrypter.getHeaderSize())
                  : -1;
  }
  u8 readU8() override {
    u8 v = 0;
    readBlock(&v, 1);
    return v;
  }
  u16 readU16() override {
    u8 v[2]{};
    readBlock(v, 2);
    return (u16(v[0]) << 8) | v[1];
  }
  u32 readU32() override {
    u8 v[4]{};
    readBlock(v, 4);
    return (u32(v[0]) << 24) | (u32(v[1]) << 16) | (u32(v[2]) << 8) | v[3];
  }
  size_t readU16arr(u16 *p, size_t n) override {
    size_t c = m_file ? std::fread(p, sizeof(u16), n, m_file) : 0;
    m_decrypter.decryptBlck(p, c * sizeof(u16));
    return c;
  }
  size_t readU32arr(u32 *p, size_t n) override {
    size_t c = m_file ? std::fread(p, sizeof(u32), n, m_file) : 0;
    m_decrypter.decryptBlck(p, c * sizeof(u32));
    return c;
  }
  float readFloat() override {
    float v = 0;
    readBlock(&v, sizeof(v));
    return v;
  }
  bool readBlock(void *p, u32 n) override {
    if (!m_file)
      return false;
    size_t c = std::fread(p, 1, n, m_file);
    m_decrypter.decryptBlck(p, c);
    return c == n;
  }
  ESTATUS getStatus() override { return m_file ? NORMAL : NOT_FOUND; }
  IWriteStream *getWriteStream() override { return nullptr; }

private:
  FILE *m_file;
  CDecryptBaseClass m_decrypter;
};

class DesktopWriteStream final : public IWriteStream {
public:
  DesktopWriteStream(const std::string &path, const char *key, bool encrypt)
      : m_decrypter(0), m_encrypt(encrypt) {
    std::error_code error;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), error);
    m_file = std::fopen(path.c_str(), "wb");
    if (m_file && m_encrypt) {
      u8 header[16]{};
      u32 headerSize = 0;
      m_decrypter.encryptSetup(reinterpret_cast<const u8 *>(key), header,
                               &headerSize);
      if (std::fwrite(header, 1, headerSize, m_file) != headerSize) {
        m_failed = true;
        return;
      }
      m_decrypter.decryptSetup(reinterpret_cast<const u8 *>(key), header,
                               &headerSize);
      m_decrypter.finishSetup(header + 4, nullptr);
    }
  }
  ~DesktopWriteStream() override {
    if (m_file)
      std::fclose(m_file);
  }
  ESTATUS getStatus() override {
    return m_file && !m_failed ? NORMAL : CAN_NOT_WRITE;
  }
  s32 getPosition() override {
    return m_file ? static_cast<s32>(std::ftell(m_file) -
                                     m_decrypter.getHeaderSize())
                  : -1;
  }
  void writeU8(u8 v) override { writeBlock(&v, 1); }
  void writeU16(u16 v) override {
    u8 b[]{u8(v >> 8), u8(v)};
    writeBlock(b, 2);
  }
  void writeU32(u32 v) override {
    u8 b[]{u8(v >> 24), u8(v >> 16), u8(v >> 8), u8(v)};
    writeBlock(b, 4);
  }
  void writeFloat(float v) override { writeBlock(&v, sizeof(v)); }
  void writeBlock(void *p, u32 n) override {
    if (m_encrypt)
      m_decrypter.decryptBlck(p, n);
    if (m_file && std::fwrite(p, 1, n, m_file) != n)
      m_failed = true;
  }

private:
  FILE *m_file{};
  CDecryptBaseClass m_decrypter;
  bool m_encrypt{};
  bool m_failed{};
};

class DesktopFontSystem final : public IFontIF {
public:
  void *getFont(int size, u32) override {
    return FontObject::createFont(nullptr, static_cast<u32>(size));
  }
  void deleteFont(void *font) override {
    FontObject::destroyFont(static_cast<FontObject *>(font));
  }
  bool renderText(const char *text, void *font, u32 color, u16 width,
                  u16 height, u8 *buffer, s16 stride, s16 baseX, s16 baseY,
                  u32 pixelBytes, float scaleX, float scaleY) override {
    if (!font)
      return false;
    static_cast<FontObject *>(font)->renderText(baseX, baseY, text, buffer,
                                                color, width, height, stride,
                                                pixelBytes, scaleX, scaleY);
    return true;
  }
  bool getTextInfo(const char *text, void *font, STextInfo *info, float scaleX,
                   float scaleY) override {
    if (!font || !info)
      return false;
    static_cast<FontObject *>(font)->getTextInfo(text, info, scaleX, scaleY);
    return true;
  }
};

class DesktopTmpFile final : public ITmpFile {
public:
  explicit DesktopTmpFile(const std::string &path)
      : m_file(std::fopen(path.c_str(), "wb")) {}
  ~DesktopTmpFile() override { closeTmp(); }
  size_t writeTmp(void *p, size_t n) override {
    return m_file ? std::fwrite(p, 1, n, m_file) : 0;
  }
  int closeTmp() override {
    if (!m_file)
      return 0;
    int r = std::fclose(m_file);
    m_file = nullptr;
    return r;
  }
  bool ready() const { return m_file != nullptr; }

private:
  FILE *m_file;
};

struct DesktopThread {
  std::thread thread;
  std::atomic<bool> done{false};
  s32 result{};
};
struct DesktopEvent {
  std::mutex mutex;
  std::condition_variable condition;
  bool signaled{};
};

std::string withSeparator(std::string path) {
  if (!path.empty() && path.back() != '/' && path.back() != '\\')
    path.push_back('/');
  return path;
}

constexpr char PublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDBpUMUVjHWNI5q3ZRjF1vPnh+m\n"
    "aEGdbZkeosVvzLytBy9eYJ9qLYyFXxOY1LiggWyOLS+xEVMpV3A6frI3VewkVuCw\n"
    "na52ssCZcQSBA03Ykeb/cfHk5ChsDUP1vmAbloMb9f++Dow6Z4yubFWmBVMCHA6l\n"
    "fiUDPHjI8JqG56XJKQIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

std::string randomIdentifier() {
  unsigned char bytes[16];
  if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    return {};
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
  char output[37];
  std::snprintf(
      output, sizeof(output),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]);
  return output;
}

EVP_PKEY *loadPublicKey() {
  BIO *input = BIO_new_mem_buf(PublicKey, sizeof(PublicKey) - 1);
  if (!input)
    return nullptr;
  EVP_PKEY *key = PEM_read_bio_PUBKEY(input, nullptr, nullptr, nullptr);
  BIO_free(input);
  return key;
}

std::string stateKey(const char *category, const char *name,
                     const char *field = nullptr) {
  std::string key(category);
  key.push_back('/');
  key += name ? name : "";
  if (field) {
    key.push_back('/');
    key += field;
  }
  return key;
}

std::string sha1Hex(const char *data, size_t size) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char *>(data), size, hash);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned char byte : hash)
    output << std::setw(2) << static_cast<unsigned int>(byte);
  return output.str();
}

std::string sha512Hex(const char *data, size_t size) {
  unsigned char hash[SHA512_DIGEST_LENGTH];
  SHA512(reinterpret_cast<const unsigned char *>(data), size, hash);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned char byte : hash)
    output << std::setw(2) << static_cast<unsigned int>(byte);
  return output.str();
}

std::string jsonEscape(const std::string &value) {
  std::ostringstream output;
  for (unsigned char byte : value) {
    switch (byte) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (byte < 0x20)
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned int>(byte) << std::dec;
      else
        output << static_cast<char>(byte);
    }
  }
  return output.str();
}

std::string percentEncode(const char *value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string output;
  if (!value)
    return output;
  for (unsigned char byte : std::string(value)) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      output.push_back(static_cast<char>(byte));
    } else {
      output.push_back('%');
      output.push_back(hex[byte >> 4]);
      output.push_back(hex[byte & 15]);
    }
  }
  return output;
}

} // namespace

DesktopScriptRegistry::DesktopScriptRegistry() {
  static const char *const knownHashes[] = {
#include "CAndroidScriptSourceHashes.inc"
  };
  for (u16 index = 0;
       index < static_cast<u16>(sizeof(knownHashes) / sizeof(knownHashes[0]));
       ++index)
    m_sourceIds.emplace(knownHashes[index], index);
}

void DesktopScriptRegistry::registerSource(const char *source, int sourceSize,
                                           const char *sourceName) {
  if (!source || sourceSize < 0 || !sourceName)
    return;
  size_t prefixLength = 0;
  if (!std::strncmp(sourceName, "file://install/", 15))
    prefixLength = 15;
  else if (!std::strncmp(sourceName, "asset://", 8))
    prefixLength = 8;

  const std::string nameHash = sha1Hex(sourceName + prefixLength,
                                       std::strlen(sourceName + prefixLength));
  DesktopScriptSource record;
  record.sourceName = sourceName;
  record.contentHash = sha1Hex(source, static_cast<size_t>(sourceSize));
  auto id = m_sourceIds.find(nameHash);
  if (id != m_sourceIds.end())
    record.sourceId = id->second;

  const bool inserted = m_sources.find(record.sourceName) == m_sources.end();
  m_sources[record.sourceName] = record;
  if (inserted)
    m_pendingNames.push_back(record.sourceName);
}

char *DesktopScriptRegistry::createHeader() {
  constexpr int sourceCount = 30;
  constexpr int contentHashLength = 40;
  constexpr int hashTimestampLength = 16;
  constexpr int hashInputLength =
      hashTimestampLength + sourceCount * contentHashLength;
  constexpr int headerBinaryLength = 90;
  constexpr int headerPrefixLength = 13;

  std::vector<const DesktopScriptSource *> selected;
  selected.reserve(sourceCount);
  while (selected.size() < sourceCount && !m_pendingNames.empty()) {
    const std::string name = m_pendingNames.back();
    m_pendingNames.pop_back();
    m_candidates.push_back(name);
    selected.push_back(&m_sources.at(name));
  }
  if (m_candidates.empty()) {
    char *empty = new char[1];
    empty[0] = '\0';
    return empty;
  }
  while (selected.size() < sourceCount) {
    const std::string &name =
        m_candidates[static_cast<size_t>(std::rand()) % m_candidates.size()];
    selected.push_back(&m_sources.at(name));
  }

  std::array<unsigned char, headerBinaryLength> header{};
  header[0] = 2;
  const double timestamp = static_cast<double>(std::time(nullptr));
  std::memcpy(header.data() + 1, &timestamp, sizeof(timestamp));

  std::vector<char> work(hashInputLength + 1, 0);
  const unsigned char *timestampBytes =
      reinterpret_cast<const unsigned char *>(&timestamp);
  for (size_t index = 0; index < sizeof(timestamp); ++index)
    std::sprintf(work.data() + index * 2, "%02x", timestampBytes[index]);

  for (int index = 0; index < sourceCount; ++index) {
    const DesktopScriptSource &source = *selected[index];
    std::memcpy(work.data() + hashTimestampLength + index * contentHashLength,
                source.contentHash.data(), contentHashLength);
    std::memcpy(header.data() + 29 + index * sizeof(source.sourceId),
                &source.sourceId, sizeof(source.sourceId));
  }
  cryptoSHA1(header.data() + 9, work.data(), hashInputLength, 20);

  const int rounds = std::rand() % 5 + 1;
  const unsigned char *input = header.data();
  for (int round = 0; round < rounds; ++round) {
    for (int index = 0; index < headerBinaryLength; ++index)
      work[index] = static_cast<char>(((input[index] + 1) ^ (index + 1)) + 1);
    input = reinterpret_cast<const unsigned char *>(work.data());
  }
  work[headerBinaryLength - 1] = static_cast<char>(rounds);

  char *encoded = new char[256]();
  std::memcpy(encoded, "X-REQUEST-ID:", headerPrefixLength);
  u32 encodedLength = 180;
  KLBNetAPI_encodeBase64(work.data(), headerBinaryLength,
                         encoded + headerPrefixLength, &encodedLength);
  return encoded;
}

DesktopPlatform::DesktopPlatform(std::string installRoot,
                                 std::string externalRoot,
                                 GLProcResolver resolver)
    : m_installRoot(withSeparator(std::move(installRoot))),
      m_externalRoot(withSeparator(std::move(externalRoot))),
      m_glResolver(resolver) {
  std::error_code error;
  std::filesystem::create_directories(m_externalRoot, error);
  m_state = std::make_unique<DesktopStateStore>(
      std::filesystem::path(m_externalRoot) / ".playground-state");
  m_scriptRegistry = std::make_unique<DesktopScriptRegistry>();
  m_widgetManager = std::make_unique<DesktopWidgetManager>(this);
  m_deviceId = m_state->get("system/device-id");
  if (m_deviceId.empty()) {
    m_deviceId = randomIdentifier();
    if (!m_deviceId.empty())
      m_state->set("system/device-id", m_deviceId);
  }
}
DesktopPlatform::~DesktopPlatform() = default;

bool DesktopPlatform::init() {
  m_audio = getNewAudioImplementation();
  return m_audio && m_audio->init();
}

bool DesktopPlatform::useEncryption() { return true; }
void DesktopPlatform::validateEnvironment() {
  std::error_code error;
  if (!std::filesystem::is_directory(m_installRoot, error))
    logging("desktop install root is unavailable: %s", m_installRoot.c_str());
  error.clear();
  std::filesystem::create_directories(m_externalRoot, error);
  if (error)
    logging("desktop external root is unavailable: %s (%s)",
            m_externalRoot.c_str(), error.message().c_str());
}
void DesktopPlatform::detailedLogging(const char *file, const char *fn,
                                      int line, const char *fmt, ...) {
  std::fprintf(stderr, "%s:%d %s: ", file, line, fn);
  va_list a;
  va_start(a, fmt);
  std::vfprintf(stderr, fmt, a);
  va_end(a);
  std::fputc('\n', stderr);
}
void DesktopPlatform::logging(const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  std::vfprintf(stderr, fmt, a);
  va_end(a);
  std::fputc('\n', stderr);
}
void *DesktopPlatform::ifopen(const char *n, const char *m) {
  return std::fopen(n, m);
}
void DesktopPlatform::ifclose(void *f) {
  if (f)
    std::fclose(static_cast<FILE *>(f));
}
int DesktopPlatform::ifseek(void *f, long o, int w) {
  return std::fseek(static_cast<FILE *>(f), o, w);
}
u32 DesktopPlatform::ifread(void *p, u32 s, u32 n, void *f) {
  return static_cast<u32>(std::fread(p, s, n, static_cast<FILE *>(f)));
}
u32 DesktopPlatform::ifwrite(const void *p, u32 s, u32 n, void *f) {
  return static_cast<u32>(std::fwrite(p, s, n, static_cast<FILE *>(f)));
}
int DesktopPlatform::ifflush(void *f) {
  return std::fflush(static_cast<FILE *>(f));
}
long DesktopPlatform::iftell(void *f) {
  return std::ftell(static_cast<FILE *>(f));
}
bool DesktopPlatform::icreateEmptyFile(const char *n) {
  FILE *f = std::fopen(n, "wb");
  if (!f)
    return false;
  std::fclose(f);
  return true;
}
int DesktopPlatform::irename(const char *a, const char *b) {
  return std::rename(a, b);
}
const char *DesktopPlatform::getBundleVersion() { return "9.11-desktop"; }
const char *DesktopPlatform::getBundleId() {
  return "klb.android.lovelive.desktop";
}
s64 DesktopPlatform::nanotime() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string DesktopPlatform::resolvePath(const char *input,
                                         bool *readOnly) const {
  if (readOnly)
    *readOnly = false;
  if (!input)
    return {};
  std::string path(input);
  auto map = [&](const char *prefix, const std::string &root,
                 bool ro) -> std::string {
    size_t n = std::strlen(prefix);
    if (path.compare(0, n, prefix) == 0) {
      if (readOnly)
        *readOnly = ro;
      return root + path.substr(n);
    }
    return {};
  };
  if (auto v = map("file://external/", m_externalRoot, false); !v.empty())
    return v;
  if (auto v = map("file://install/", m_installRoot, true); !v.empty())
    return v;
  const char *assetPrefixes[]{"file://asset/", "asset://"};
  for (const char *prefix : assetPrefixes) {
    size_t n = std::strlen(prefix);
    if (path.compare(0, n, prefix) == 0) {
      std::string relative = path.substr(n);
      std::string external = m_externalRoot + relative;
      if (std::filesystem::exists(external))
        return external;
      if (readOnly)
        *readOnly = true;
      return m_installRoot + relative;
    }
  }
  return path;
}
IReadStream *DesktopPlatform::openReadStream(const char *n, bool decrypt,
                                             u32 mode) {
  if (!n)
    return nullptr;
  const char *key = n;
  if (!std::strncmp(n, "file://", 7))
    key = n + 7;
  else if (!std::strncmp(n, "asset://", 8))
    key = n + 8;
  return new DesktopReadStream(resolvePath(n, nullptr), key, decrypt, mode);
}
IReadStream *DesktopPlatform::openWriteStream(const char *n, bool encrypt,
                                              u32) {
  if (!n)
    return nullptr;
  const char *key = !std::strncmp(n, "file://", 7) ? n + 7 : n;
  return reinterpret_cast<IReadStream *>(
      new DesktopWriteStream(resolvePath(n, nullptr), key, encrypt));
}
void DesktopPlatform::beforeAssertFunction(const char *function, bool) {
  logging("assert callback: %s", function ? function : "");
  if (function && function[0])
    CKLBScriptEnv::getInstance().call_cbInt(function, 0);
}
void DesktopPlatform::addExtMsg(const char *key, const char *value,
                                bool sendImmediately) {
  if (!key || !key[0])
    return;
  m_state->set(stateKey("diagnostic", key), value ? value : "");
  if (sendImmediately)
    logging("diagnostic %s=%s", key, value ? value : "");
}
void DesktopPlatform::sendException(const char *m) {
  logging("exception: %s", m ? m : "");
}
void DesktopPlatform::leaveBreadcrumb(const char *message) {
  logging("script: %s", message ? message : "");
}
char *DesktopPlatform::createRequestIdHeader() {
  return m_scriptRegistry->createHeader();
}
void DesktopPlatform::copyToClipboard(const char *text) {
  if (!SDL_SetClipboardText(text ? text : ""))
    logging("clipboard write failed: %s", SDL_GetError());
}
double DesktopPlatform::getUsedMemorySize() {
  std::ifstream status("/proc/self/statm");
  uint64_t pages = 0;
  uint64_t resident = 0;
  if (!(status >> pages >> resident))
    return 0.0;
  return static_cast<double>(resident) *
         static_cast<double>(sysconf(_SC_PAGESIZE));
}
double DesktopPlatform::getFreeMemorySize() {
  const long pages = sysconf(_SC_AVPHYS_PAGES);
  const long pageSize = sysconf(_SC_PAGESIZE);
  return pages < 0 || pageSize < 0
             ? 0.0
             : static_cast<double>(pages) * static_cast<double>(pageSize);
}
bool DesktopPlatform::getSMode() { return false; }
void DesktopPlatform::getDateTimeNow(char *b, int n) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::strftime(b, n, "%Y-%m-%d %H:%M:%S", &tm);
}
double DesktopPlatform::getUNIXTimeNow() {
  return static_cast<double>(std::time(nullptr));
}
void DesktopPlatform::requestExtensionEvent(const char *,
                                            ExtensionEventArgs *) {
  logging("desktop extension event requested; no matching desktop extension is "
          "registered");
}
void DesktopPlatform::savePng2Album(const char *path) {
  if (!path || !path[0])
    return;
  const std::filesystem::path source(resolvePath(path, nullptr));
  const std::filesystem::path destinationDirectory =
      std::filesystem::path(m_externalRoot) / "Pictures";
  std::error_code error;
  std::filesystem::create_directories(destinationDirectory, error);
  if (error) {
    logging("screenshot directory creation failed: %s",
            error.message().c_str());
    return;
  }
  std::filesystem::path destination = destinationDirectory / source.filename();
  std::filesystem::copy_file(source, destination,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error)
    logging("screenshot export failed: %s", error.message().c_str());
  else
    logging("screenshot exported to %s", destination.c_str());
}
void DesktopPlatform::setIdleTimerActivity(bool active) {
  if (active)
    SDL_DisableScreenSaver();
  else
    SDL_EnableScreenSaver();
}
ITmpFile *DesktopPlatform::openTmpFile(const char *p) {
  auto *f = new DesktopTmpFile(resolvePath(p, nullptr));
  if (!f->ready()) {
    delete f;
    return nullptr;
  }
  return f;
}
int DesktopPlatform::removeTmpFile(const char *p) {
  return std::remove(resolvePath(p, nullptr).c_str());
}
bool DesktopPlatform::removeFileOrFolder(const char *p) {
  std::error_code e;
  std::filesystem::remove_all(resolvePath(p, nullptr), e);
  return !e;
}
void DesktopPlatform::excludePathFromBackup(const char *) {
  // Desktop save data is not managed by a mobile cloud-backup service.
}
u32 DesktopPlatform::getFreeSpaceExternalKB() {
  std::error_code e;
  auto s = std::filesystem::space(m_externalRoot, e);
  return e ? 0
           : static_cast<u32>(
                 std::min<uintmax_t>(s.available / 1024, UINT32_MAX));
}
u32 DesktopPlatform::getPhysicalMemKB() {
  const int megabytes = SDL_GetSystemRAM();
  return megabytes <= 0
             ? 0
             : static_cast<u32>(std::min<uint64_t>(
                   static_cast<uint64_t>(megabytes) * 1024, UINT32_MAX));
}
char *DesktopPlatform::getDeviceIntegrityInfo(const char *request) {
  struct utsname system{};
  uname(&system);
  char hostname[256]{};
  gethostname(hostname, sizeof(hostname) - 1);

  std::map<std::string, std::string> properties;
  properties["ro.build.version.release"] = system.release;
  properties["ro.product.name"] = "PlaygroundOSS Desktop";
  properties["ro.product.manufacturer"] = "Community";
  properties["ro.product.brand"] = "PlaygroundOSS";
  properties["ro.product.device"] = hostname;
  properties["ro.product.model"] = system.machine;
  properties["ro.product.board"] = system.machine;
  properties["ro.build.fingerprint"] =
      std::string(system.sysname) + "/" + system.release + "/" + system.machine;
  properties["ro.build.tags"] = "desktop-release";
  properties["Hardware"] = system.machine;
  properties["basePath"] = m_installRoot;
  properties["adbEnabled"] = "false";
  properties["device_id"] = m_deviceId;

  const std::string unitDatabase =
      resolvePath("asset://db/unit/unit.db_", nullptr);
  char databaseHash[64]{};
  CKLBUtility::sha1File(unitDatabase.c_str(), databaseHash, 40);
  properties["db_sha1"] = databaseHash[0] ? databaseHash : "NOT_FOUND";

  char executable[4096]{};
  const ssize_t executableLength =
      readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  char executableHash[64]{};
  if (executableLength > 0) {
    executable[executableLength] = '\0';
    CKLBUtility::sha1File(executable, executableHash, 40);
  }
  properties["GreatStockOption"] =
      executableHash[0] ? executableHash : "NOT_FOUND";
  properties["signature"] = sha512Hex(m_deviceId.data(), m_deviceId.size());

  if (request) {
    char encodedKey[32]{};
    u32 encodedLength = 0;
    KLBNetAPI_encodeBase64("wibs~d\x05", 7, encodedKey, &encodedLength);

    const size_t requestLength = std::strlen(request);
    std::vector<char> transformed(requestLength + 2, 0);
    const unsigned char *input =
        reinterpret_cast<const unsigned char *>(request);
    const int rounds = std::rand() % 5 + 1;
    for (int round = 0; round < rounds; ++round) {
      for (size_t index = 0; index <= requestLength; ++index)
        transformed[index] = static_cast<char>(
            ((input[index] + 1) ^ static_cast<unsigned char>(index + 1)) + 1);
      input = reinterpret_cast<const unsigned char *>(transformed.data());
    }
    transformed[requestLength] = static_cast<char>(rounds);
    transformed[requestLength + 1] = '\0';

    std::vector<char> encoded((requestLength + 2) * 2 + 8, 0);
    KLBNetAPI_encodeBase64(transformed.data(), requestLength + 2,
                           encoded.data(), &encodedLength);
    properties[encodedKey] = encoded.data();
  }

  std::ostringstream json;
  json << "{\n";
  for (auto iterator = properties.begin(); iterator != properties.end();
       ++iterator) {
    json << "\t\"" << jsonEscape(iterator->first) << "\":\""
         << jsonEscape(iterator->second) << "\",\n";
  }
  json << "\t\"SuspiciousElement\":[]\n}";
  const std::string result = json.str();
  char *output = new char[result.size() + 1];
  std::memcpy(output, result.c_str(), result.size() + 1);
  return output;
}
void DesktopPlatform::decompressBGM(bool decompress) {
  g_decompressBGM = decompress;
}
s64 DesktopPlatform::getElapsedTime() { return nanotime() / 1000000; }
void *DesktopPlatform::getFontSystem() {
  static DesktopFontSystem fontSystem;
  return &fontSystem;
}
void DesktopPlatform::deleteFontSystem(void *font) {
  FontObject::destroyFont(static_cast<FontObject *>(font));
}
void *DesktopPlatform::getFont(int s, const char *n) {
  return FontObject::createFont(n, static_cast<u32>(s));
}
void DesktopPlatform::deleteFont(void *f) {
  FontObject::destroyFont(static_cast<FontObject *>(f));
}
const char *DesktopPlatform::getFullPath(const char *p, bool *ro) {
  std::string v = resolvePath(p, ro);
  char *out = new char[v.size() + 1];
  std::memcpy(out, v.c_str(), v.size() + 1);
  return out;
}
const char *DesktopPlatform::getPlatform() {
  static const std::string platform = [] {
    struct utsname system{};
    if (uname(&system) != 0)
      return std::string("Linux");
    return std::string("Linux;") + system.machine + " " + system.release;
  }();
  return platform.c_str();
}
void *DesktopPlatform::getGLExtension(const char *n) {
  return m_glResolver ? m_glResolver(n) : nullptr;
}
const char *DesktopPlatform::getShaderExtension(int) { return ""; }
bool DesktopPlatform::setFrameRate(int n) {
  if (n <= 0)
    return false;
  m_frameRate = n;
  return true;
}
int DesktopPlatform::getMaxFrameRate() { return 240; }
IWidget *DesktopPlatform::createControl(IWidget::CONTROL type, int id,
                                        const char *caption, int x, int y,
                                        int width, int height, ...) {
  va_list arguments;
  va_start(arguments, height);
  IWidget *widget = m_widgetManager->create(type, id, caption, x, y, width,
                                            height, arguments);
  va_end(arguments);
  return widget;
}
void DesktopPlatform::destroyControl(IWidget *widget) {
  m_widgetManager->destroy(widget);
}
bool DesktopPlatform::callApplication(APP_TYPE type, ...) {
  va_list arguments;
  va_start(arguments, type);
  std::string url;
  switch (type) {
  case APP_MAIL: {
    const char *address = va_arg(arguments, const char *);
    const char *subject = va_arg(arguments, const char *);
    const char *body = va_arg(arguments, const char *);
    url = "mailto:" + percentEncode(address) +
          "?subject=" + percentEncode(subject) + "&body=" + percentEncode(body);
    break;
  }
  case APP_BROWSER: {
    const char *browserUrl = va_arg(arguments, const char *);
    (void)va_arg(arguments, const char *);
    url = browserUrl ? browserUrl : "";
    break;
  }
  case APP_UPDATE: {
    const char *search = va_arg(arguments, const char *);
    url = "https://github.com/ethanaobrien/PlaygroundOSS/releases";
    if (search && search[0])
      url += "?q=" + percentEncode(search);
    break;
  }
  case APP_MAP: {
    const double latitude = va_arg(arguments, double);
    const double longitude = va_arg(arguments, double);
    char mapUrl[256];
    std::snprintf(
        mapUrl, sizeof(mapUrl),
        "https://www.openstreetmap.org/?mlat=%.8f&mlon=%.8f#map=16/%.8f/%.8f",
        latitude, longitude, latitude, longitude);
    url = mapUrl;
    break;
  }
  case APP_COLLABORATION: {
    const char *application = va_arg(arguments, const char *);
    const char *argument = va_arg(arguments, const char *);
    if (application && std::strstr(application, "://"))
      url = application;
    else if (argument && std::strstr(argument, "://"))
      url = argument;
    break;
  }
  case APP_SHARE_CONTENTS: {
    (void)va_arg(arguments, const char *);
    const char *contents = va_arg(arguments, const char *);
    (void)va_arg(arguments, const char *);
    copyToClipboard(contents);
    va_end(arguments);
    return true;
  }
  case APP_SETTINGS:
  case APP_ATT:
  default:
    break;
  }
  va_end(arguments);
  return !url.empty() && SDL_OpenURL(url.c_str());
}
void DesktopPlatform::clearCookies() {
  std::error_code error;
  std::filesystem::remove(
      std::filesystem::path(m_externalRoot) / ".playground-cookies", error);
}
bool DesktopPlatform::readyDevID() { return !m_deviceId.empty(); }
int DesktopPlatform::getDevID(char *b, int n) {
  copyText(m_deviceId, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
void DesktopPlatform::exitGame() { m_quitRequested = true; }
bool DesktopPlatform::setSecureDataID(const char *s, const char *v) {
  return m_state->set(stateKey("secure", s, "user_id"), v ? v : "");
}
bool DesktopPlatform::setSecureDataPW(const char *s, const char *v) {
  return m_state->set(stateKey("secure", s, "passwd"), v ? v : "");
}
int DesktopPlatform::getSecureDataID(const char *s, char *b, int n) {
  const auto v = m_state->get(stateKey("secure", s, "user_id"));
  copyText(v, b, n);
  return static_cast<int>(v.size());
}
int DesktopPlatform::getSecureDataPW(const char *s, char *b, int n) {
  const auto v = m_state->get(stateKey("secure", s, "passwd"));
  copyText(v, b, n);
  return static_cast<int>(v.size());
}
bool DesktopPlatform::delSecureDataID(const char *s) {
  return m_state->erase(stateKey("secure", s, "user_id"));
}
bool DesktopPlatform::delSecureDataPW(const char *s) {
  return m_state->erase(stateKey("secure", s, "passwd"));
}
void DesktopPlatform::setUserDefaults(const char *k, bool v) {
  m_state->set(stateKey("default", k), v ? "TRUE" : "FALSE");
}
bool DesktopPlatform::getUserDefaults(const char *k) {
  return m_state->get(stateKey("default", k)) == "TRUE";
}
void DesktopPlatform::setUserDefaults(const char *k, const char *v) {
  m_state->set(stateKey("default", k), v ? v : "");
}
void DesktopPlatform::getUserDefaults(const char *k, char *b, int n) {
  copyText(m_state->get(stateKey("default", k)), b, n);
}
void *DesktopPlatform::createThread(s32 (*fn)(void *, void *), void *data) {
  auto *t = new DesktopThread;
  t->thread = std::thread([t, fn, data] {
    t->result = fn(t, data);
    t->done = true;
  });
  return t;
}
void DesktopPlatform::exitThread(void *h, s32 s) {
  static_cast<DesktopThread *>(h)->result = s;
}
bool DesktopPlatform::watchThread(void *h, s32 *s) {
  auto *t = static_cast<DesktopThread *>(h);
  if (!t->done)
    return true;
  if (s)
    *s = t->result;
  return false;
}
void DesktopPlatform::deleteThread(void *h) {
  auto *t = static_cast<DesktopThread *>(h);
  if (t->thread.joinable())
    t->thread.join();
  delete t;
}
void DesktopPlatform::breakThread(void *handle) {
  if (!handle)
    return;
  auto *thread = static_cast<DesktopThread *>(handle);
  const int result = pthread_cancel(thread->thread.native_handle());
  if (result != 0)
    logging("desktop worker cancellation failed: %d", result);
}
int DesktopPlatform::genUserID(char *b, int n) {
  const std::string id = randomIdentifier();
  copyText(id, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
int DesktopPlatform::genUserPW(const char *s, char *b, int n) {
  unsigned char randomBytes[4];
  if (RAND_bytes(randomBytes, sizeof(randomBytes)) != 1)
    return 0;
  u32 randomValue;
  std::memcpy(&randomValue, randomBytes, sizeof(randomValue));
  char input[1200];
  std::snprintf(input, sizeof(input), "%u.%u.%s", randomValue,
                static_cast<u32>(std::time(nullptr)), s ? s : "");
  unsigned char digest[SHA512_DIGEST_LENGTH];
  SHA512(reinterpret_cast<const unsigned char *>(input), std::strlen(input),
         digest);
  std::string value;
  value.reserve(SHA512_DIGEST_LENGTH * 2);
  char hex[3];
  for (unsigned char byte : digest) {
    std::snprintf(hex, sizeof(hex), "%02x", byte);
    value += hex;
  }
  copyText(value, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
void DesktopPlatform::registerScriptSource(const char *source, int sourceSize,
                                           const char *sourceName) {
  m_scriptRegistry->registerSource(source, sourceSize, sourceName);
}
void DesktopPlatform::initStoreTransactionObserver() {
  logging("desktop store observer initialized; purchases report unavailable");
}
void DesktopPlatform::releaseStoreTransactionObserver() {}
void DesktopPlatform::buyStoreItems(const char *item) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Purchases are unavailable on desktop";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_FAILED, nullptr,
      item && item[0] ? std::strlen(item) + 1 : 0,
      item && item[0] ? const_cast<char *>(item) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
void DesktopPlatform::getStoreProducts(const char *items, bool) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Store products are unavailable on desktop";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_GET_PRODUCTS_FAILED, nullptr,
      items && items[0] ? std::strlen(items) + 1 : 0,
      items && items[0] ? const_cast<char *>(items) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
void DesktopPlatform::finishStoreTransaction(const char *transaction) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Store restoration is unavailable on desktop";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_RESTORE_FAILED, nullptr,
      transaction && transaction[0] ? std::strlen(transaction) + 1 : 0,
      transaction && transaction[0] ? const_cast<char *>(transaction) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
bool DesktopPlatform::publicKeyVerify(unsigned char *message, int messageLength,
                                      unsigned char *signature,
                                      int signatureLength) {
  if (!message || messageLength < 0 || !signature || signatureLength < 0)
    return false;
  EVP_PKEY *key = loadPublicKey();
  if (!key)
    return false;
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  bool valid =
      context &&
      EVP_DigestVerifyInit(context, nullptr, EVP_sha1(), nullptr, key) == 1 &&
      EVP_DigestVerifyUpdate(context, message, messageLength) == 1 &&
      EVP_DigestVerifyFinal(context, signature, signatureLength) == 1;
  EVP_MD_CTX_free(context);
  EVP_PKEY_free(key);
  return valid;
}
int DesktopPlatform::publicKeyEncrypt(unsigned char *input, int inputLength,
                                      unsigned char *output, int outputLength) {
  if (!input || inputLength < 0 || !output || outputLength < 0)
    return -1;
  EVP_PKEY *key = loadPublicKey();
  if (!key)
    return -1;
  EVP_PKEY_CTX *context = EVP_PKEY_CTX_new(key, nullptr);
  size_t required = 0;
  bool valid =
      context && EVP_PKEY_encrypt_init(context) > 0 &&
      EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) > 0 &&
      EVP_PKEY_encrypt(context, nullptr, &required, input, inputLength) > 0 &&
      required <= static_cast<size_t>(outputLength) &&
      EVP_PKEY_encrypt(context, output, &required, input, inputLength) > 0;
  EVP_PKEY_CTX_free(context);
  EVP_PKEY_free(key);
  return valid ? static_cast<int>(required) : -1;
}
bool DesktopPlatform::randomBytes(unsigned char *o, int n) {
  return o && n >= 0 && (n == 0 || RAND_bytes(o, n) == 1);
}
int DesktopPlatform::encryptAES128CBC(unsigned char *output, int outputLength,
                                      const char *input, int inputLength,
                                      const char *key, int keyLength) {
  if (!output || outputLength < 0 || !input || inputLength < 0 || !key ||
      keyLength < 16)
    return -1;
  if (outputLength < inputLength + 32)
    return -8;
  if (RAND_bytes(output, 16) != 1)
    return -1;
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  int first = 0, last = 0;
  bool valid = context &&
               EVP_EncryptInit_ex(context, EVP_aes_128_cbc(), nullptr,
                                  reinterpret_cast<const unsigned char *>(key),
                                  output) == 1 &&
               EVP_EncryptUpdate(context, output + 16, &first,
                                 reinterpret_cast<const unsigned char *>(input),
                                 inputLength) == 1 &&
               EVP_EncryptFinal_ex(context, output + 16 + first, &last) == 1;
  EVP_CIPHER_CTX_free(context);
  return valid ? 16 + first + last : -7;
}
int DesktopPlatform::decryptAES128CBC(unsigned char *output, int outputLength,
                                      const char *input, int inputLength,
                                      const char *key, int keyLength) {
  if (!output || outputLength < 0 || !input || inputLength < 16 || !key ||
      keyLength < 16)
    return -1;
  if (outputLength < inputLength - 16)
    return -8;
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  int first = 0, last = 0;
  bool valid =
      context &&
      EVP_DecryptInit_ex(context, EVP_aes_128_cbc(), nullptr,
                         reinterpret_cast<const unsigned char *>(key),
                         reinterpret_cast<const unsigned char *>(input)) == 1 &&
      EVP_DecryptUpdate(context, output, &first,
                        reinterpret_cast<const unsigned char *>(input) + 16,
                        inputLength - 16) == 1 &&
      EVP_DecryptFinal_ex(context, output + first, &last) == 1;
  EVP_CIPHER_CTX_free(context);
  return valid ? first + last : -7;
}
bool DesktopPlatform::initNetwork() {
  return CurlObjectInternal::initializeLibrary();
}
void DesktopPlatform::shutdownNetwork() {
  CurlObjectInternal::shutdownLibrary();
}
CurlObjectInternal *DesktopPlatform::createNetworkOperation() {
  return CurlObjectInternal::create();
}
void DesktopPlatform::resetNetworkOperation(CurlObjectInternal *o) {
  o->reset();
}
void DesktopPlatform::cleanupNetworkOperation(CurlObjectInternal *o) {
  o->cleanup();
}
int DesktopPlatform::performNetworkOperation(CurlObjectInternal *o) {
  return o->perform();
}
void DesktopPlatform::freeNetworkFormHeaders(CurlObjectInternal *o) {
  o->freeFormHeaders();
}
void DesktopPlatform::destroyNetworkOperation(CurlObjectInternal *o) {
  CurlObjectInternal::destroy(o);
}
void DesktopPlatform::appendNetworkHeader(CurlObjectInternal *o,
                                          const char *h) {
  o->appendHeader(h);
}
void DesktopPlatform::setNetworkPostFields(CurlObjectInternal *o) {
  o->setPostFields();
}
void DesktopPlatform::setNetworkPostData(CurlObjectInternal *o, long n,
                                         const void *p) {
  o->setPostData(n, p);
}
void DesktopPlatform::addNetworkFormData(CurlObjectInternal *o, const char *n,
                                         long s, const void *p) {
  o->addFormData(n, s, p);
}
void DesktopPlatform::setupNetworkConnection(CurlObjectInternal *o,
                                             const char *u, const char *p,
                                             void *c, void *a, void *h,
                                             void *w) {
  o->setupConnection(u, p, c, a, h, w);
}
long DesktopPlatform::getNetworkHttpCode(CurlObjectInternal *o) {
  return o->getHttpCode();
}
void DesktopPlatform::startAlertDialog(const char *t, const char *m) {
  logging("%s: %s", t ? t : "Alert", m ? m : "");
}
void DesktopPlatform::forbidSleep(bool forbidden) {
  if (forbidden)
    SDL_DisableScreenSaver();
  else
    SDL_EnableScreenSaver();
}
float DesktopPlatform::getDeviceScale() { return 1.0f; }
void DesktopPlatform::quitGame() { m_quitRequested = true; }
void *DesktopPlatform::allocMutex() { return new std::mutex; }
void DesktopPlatform::freeMutex(void *p) {
  delete static_cast<std::mutex *>(p);
}
void DesktopPlatform::mutexLock(void *p) {
  static_cast<std::mutex *>(p)->lock();
}
void DesktopPlatform::mutexUnlock(void *p) {
  static_cast<std::mutex *>(p)->unlock();
}
void *DesktopPlatform::allocEventLock() { return new DesktopEvent; }
void DesktopPlatform::freeEventLock(void *p) {
  delete static_cast<DesktopEvent *>(p);
}
void DesktopPlatform::eventSleep(void *p) {
  auto *e = static_cast<DesktopEvent *>(p);
  std::unique_lock<std::mutex> l(e->mutex);
  e->condition.wait(l, [e] { return e->signaled; });
  e->signaled = false;
}
void DesktopPlatform::eventWakeup(void *p) {
  auto *e = static_cast<DesktopEvent *>(p);
  {
    std::lock_guard<std::mutex> l(e->mutex);
    e->signaled = true;
  }
  e->condition.notify_one();
}
const char *DesktopPlatform::getLangCodeRAW() {
  const char *configured = std::getenv("PLAYGROUND_LANGUAGE");
  return configured && configured[0] ? configured : "ja";
}
const char *DesktopPlatform::getCountryCodeRAW() {
  const char *configured = std::getenv("PLAYGROUND_COUNTRY");
  return configured && configured[0] ? configured : "JP";
}
const char *DesktopPlatform::getPreferredLangCodeRAW() {
  return getLangCodeRAW();
}
bool DesktopPlatform::getGyroPolar(float *a, float *e) {
  if (a)
    *a = 0;
  if (e)
    *e = 0;
  return false;
}
void *DesktopPlatform::getFont(int s, const char *n, float *a) {
  auto *f = static_cast<FontObject *>(getFont(s, n));
  if (a)
    *a = f ? f->getAscent() : 0;
  return f;
}
void *DesktopPlatform::getFontSystem(int s, const char *n) {
  return getFont(s, n);
}
bool DesktopPlatform::getTextInfo(const char *t, void *f, STextInfo *i) {
  if (!f || !i)
    return false;
  static_cast<FontObject *>(f)->getTextInfo(t, i, 1, 1);
  return true;
}

void DesktopPlatform::handleTextInput(const char *text) {
  m_widgetManager->inputText(text);
}

bool DesktopPlatform::handleEditingKey(int key, bool pressed) {
  return m_widgetManager->inputKey(key, pressed);
}

void DesktopPlatform::pumpPlatformEvents() { m_widgetManager->pumpEvents(); }

} // namespace playground::runtime
