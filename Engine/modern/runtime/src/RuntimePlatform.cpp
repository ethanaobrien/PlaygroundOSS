#include "Playground/Runtime/RuntimePlatform.h"

#include "RuntimeCrypto.h"
#include "RuntimeStateStore.h"
#include "RuntimeWidgets.h"

#include "CKLBCrypto.h"
#include "CKLBScriptEnv.h"
#include "CKLBUtility.h"
#include "FileSystem.h"
#include "FontRendering.h"
#include "ITmpFile.h"
#include "KLBBase64.h"
#include "MultithreadedNetwork.h"
#include "encryptFile.h"

#if defined(__SWITCH__)
#include "Playground/Switch/SwitchAlbum.h"
#include "Playground/Switch/SwitchNetwork.h"
#include "Playground/Switch/SwitchStorage.h"
#include "Playground/Switch/SwitchSystem.h"
#elif defined(__WIIU__)
#include "Playground/Host/WiiUHost.h"
#include "Playground/WiiU/WiiUPath.h"
#include <coreinit/debug.h>
#include <coreinit/energysaver.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memory.h>
#include <sysapp/switch.h>
#else
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_video.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
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
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#if defined(__SWITCH__) || defined(__WIIU__)
#include <pthread.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <psapi.h>
#include <windows.h>
#else
#include <pthread.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

extern bool g_decompressBGM;

namespace playground::runtime {

#if defined(__EMSCRIPTEN__)
void pumpWebPlatformServices();
#endif

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

bool uriUsesExplicitStateRoot(const char *uri) {
  if (!uri)
    return false;
  constexpr const char *statePrefix = "file://state/";
  return std::strncmp(uri, statePrefix, std::strlen(statePrefix)) == 0;
}

template <class T> void copyText(const T &text, char *buffer, int size) {
  if (!buffer || size <= 0)
    return;
  std::snprintf(buffer, static_cast<size_t>(size), "%s", text.c_str());
}

struct DesktopHostInfo {
  std::string system;
  std::string release;
  std::string machine;
  std::string hostname;
};

bool getLocalTime(std::time_t time, std::tm &result) {
#if defined(_WIN32)
  return localtime_s(&result, &time) == 0;
#else
  return localtime_r(&time, &result) != nullptr;
#endif
}

DesktopHostInfo getDesktopHostInfo() {
  DesktopHostInfo result;
#if defined(__SWITCH__)
  const auto version = switch_runtime::systemVersion();
  result.system = "Horizon";
  result.release = std::to_string(version.major) + "." +
                   std::to_string(version.minor) + "." +
                   std::to_string(version.micro);
  result.machine = "aarch64";
  result.hostname = "nintendo-switch";
#elif defined(__WIIU__)
  result.system = "CafeOS";
  result.release = "Wii U";
  result.machine = "powerpc";
  result.hostname = "nintendo-wiiu";
#elif defined(_WIN32)
  result.system = "Windows";
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  using RtlGetVersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFunction>(
                                   GetProcAddress(ntdll, "RtlGetVersion"))
                             : nullptr;
  if (rtlGetVersion && rtlGetVersion(&version) == 0) {
    result.release = std::to_string(version.dwMajorVersion) + "." +
                     std::to_string(version.dwMinorVersion) + "." +
                     std::to_string(version.dwBuildNumber);
  } else {
    result.release = "unknown";
  }
  SYSTEM_INFO systemInfo{};
  GetNativeSystemInfo(&systemInfo);
  switch (systemInfo.wProcessorArchitecture) {
  case PROCESSOR_ARCHITECTURE_AMD64:
    result.machine = "x86_64";
    break;
  case PROCESSOR_ARCHITECTURE_ARM64:
    result.machine = "arm64";
    break;
  case PROCESSOR_ARCHITECTURE_INTEL:
    result.machine = "x86";
    break;
  default:
    result.machine = "unknown";
    break;
  }
  char hostname[MAX_COMPUTERNAME_LENGTH + 1]{};
  DWORD hostnameLength = sizeof(hostname);
  if (GetComputerNameA(hostname, &hostnameLength))
    result.hostname.assign(hostname, hostnameLength);
  else
    result.hostname = "windows-desktop";
#else
  struct utsname system{};
  if (uname(&system) == 0) {
    result.system = system.sysname;
    result.release = system.release;
    result.machine = system.machine;
  } else {
    result.system = "Linux";
    result.release = "unknown";
    result.machine = "unknown";
  }
  char hostname[256]{};
  if (gethostname(hostname, sizeof(hostname) - 1) == 0)
    result.hostname = hostname;
  else
    result.hostname = "linux-desktop";
#endif
  return result;
}

std::string getExecutablePath() {
#if defined(__SWITCH__) || defined(__WIIU__)
  // Horizon does not expose a stable filesystem path for an installed NSO.
  // The package identity is covered by NACP/NPDM and the content archive.
  return {};
#elif defined(_WIN32)
  std::vector<char> path(4096);
  while (true) {
    DWORD length = GetModuleFileNameA(nullptr, path.data(),
                                      static_cast<DWORD>(path.size()));
    if (!length)
      return {};
    if (length < path.size() - 1)
      return std::string(path.data(), length);
    path.resize(path.size() * 2);
  }
#else
  char path[4096]{};
  const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return length > 0 ? std::string(path, static_cast<size_t>(length))
                    : std::string();
#endif
}

std::filesystem::path cookieStoragePath(const std::string &stateRoot) {
  return std::filesystem::path(stateRoot) / ".playground-cookies";
}

void restrictStateFile(const std::filesystem::path &path) {
#if !defined(_WIN32) && !defined(__SWITCH__) && !defined(__WIIU__)
  chmod(path.c_str(), S_IRUSR | S_IWUSR);
#else
  (void)path;
#endif
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
  DesktopWriteStream(const std::string &path, const char *key, bool encrypt,
                     RuntimePlatform::StorageCommit commit,
                     const RuntimePlatform::WriterFactory& factory)
      : m_decrypter(0), m_encrypt(encrypt), m_commit(std::move(commit)) {
    std::error_code error;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), error);
    if (!error) m_file = factory ? factory(path) : openStorageWriter(path);
    if (m_file && m_encrypt) {
      u8 header[16]{};
      u32 headerSize = 0;
      m_decrypter.encryptSetup(reinterpret_cast<const u8 *>(key), header,
                               &headerSize);
      if (m_file->write(header, headerSize) != headerSize) {
        m_failed = true;
        return;
      }
      m_decrypter.decryptSetup(reinterpret_cast<const u8 *>(key), header,
                               &headerSize);
      m_decrypter.finishSetup(header + 4, nullptr);
    }
  }
  ~DesktopWriteStream() override {
    if (m_file) {
      const bool closed = m_file->close() == 0;
      m_file = nullptr;
      if (!closed)
        m_failed = true;
      // Never publish a partial stream. Transactional mounted filesystems
      // commit only after the complete file has closed successfully.
      if (!m_failed && m_commit && !m_commit())
        m_failed = true;
    }
  }
  ESTATUS getStatus() override {
    return m_file && !m_failed ? NORMAL : CAN_NOT_WRITE;
  }
  s32 getPosition() override {
    return m_file ? static_cast<s32>(m_file->position() -
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
    if (m_file && m_file->write(p, n) != n)
      m_failed = true;
  }

private:
  std::unique_ptr<StorageWriter> m_file;
  CDecryptBaseClass m_decrypter;
  bool m_encrypt{};
  bool m_failed{};
  RuntimePlatform::StorageCommit m_commit;
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
  DesktopTmpFile(const std::string &path, RuntimePlatform::StorageCommit commit,
                 const RuntimePlatform::WriterFactory& factory)
      : m_path(path), m_commit(std::move(commit)),
        m_started(std::chrono::steady_clock::now()) {
    std::error_code error;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), error);
    if (!error) m_file = factory ? factory(path) : openStorageWriter(path);
    if (!m_file) {
#if !defined(__WIIU__)
      std::fprintf(stderr,
                   "asset download: cannot open temporary file %s: %s\n",
                   m_path.c_str(),
                   error ? error.message().c_str() : std::strerror(errno));
#endif
    }
  }
  ~DesktopTmpFile() override { closeTmp(); }
  size_t writeTmp(void *p, size_t n) override {
    if (!m_file)
      return 0;
    const size_t written = m_file->write(p, n);
    m_bytesWritten += written;
    if (written != n) {
#if !defined(__WIIU__)
      std::fprintf(stderr,
                   "asset download: short write to %s: requested=%zu "
                   "written=%zu error=%s\n",
                   m_path.c_str(), n, written, std::strerror(errno));
#endif
    }
    return written;
  }
  int closeTmp() override {
    if (!m_file)
      return 0;
    int r = m_file->close();
    m_file = nullptr;
    if (!r && m_commit && !m_commit())
      r = -1;
#if !defined(__WIIU__)
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                     m_started)
            .count();
    // Packages hold thousands of small files; report only the notable ones.
    if (r != 0 || seconds >= 0.05)
      std::fprintf(stderr,
                 "asset download write: path=%s bytes=%zu elapsed=%.3fs "
                 "result=%d\n",
                 m_path.c_str(), m_bytesWritten, seconds, r);
#endif
    return r;
  }
  bool ready() const { return m_file != nullptr; }

private:
  std::string m_path;
  std::unique_ptr<StorageWriter> m_file;
  RuntimePlatform::StorageCommit m_commit;
  std::chrono::steady_clock::time_point m_started;
  std::size_t m_bytesWritten{};
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

bool pathUsesRoot(const std::string &path, const std::string &root) {
  if (path.empty() || root.empty())
    return false;
  if (path == root)
    return true;
  if (path.size() <= root.size() || path.compare(0, root.size(), root) != 0)
    return false;
  // RuntimePlatform normalizes its roots with a trailing separator. Keep the
  // boundary check explicit as this helper is also used for mapped native
  // paths, where a lexical prefix is not sufficient ownership evidence.
  return root.back() == '/' || root.back() == '\\' ||
         path[root.size()] == '/' || path[root.size()] == '\\';
}

std::string joinUriPath(const std::string &root, const std::string &relative) {
  if (relative.empty())
    return root;
  if (relative.front() == '/' || relative.front() == '\\' ||
      relative.find('\\') != std::string::npos ||
      relative.find(':') != std::string::npos)
    return {};
  size_t begin = 0;
  while (begin <= relative.size()) {
    const size_t end = relative.find('/', begin);
    const std::string component = relative.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (component == "." || component == "..")
      return {};
    for (unsigned char byte : component) {
      if (byte < 0x20 || byte == 0x7f)
        return {};
    }
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return root + relative;
}

std::string randomIdentifier() {
  unsigned char bytes[16];
  if (!cryptoRandom(bytes, sizeof(bytes)))
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
  unsigned char hash[20];
  if (!cryptoSha1(data, size, hash))
    return {};
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned char byte : hash)
    output << std::setw(2) << static_cast<unsigned int>(byte);
  return output.str();
}

#if defined(PLAYGROUND_WEB)
std::string sha1FileForWeb(const std::string &path) {
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return {};
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return {};
  }
  const long length = std::ftell(file);
  if (length < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return {};
  }
  std::vector<char> contents(static_cast<size_t>(length));
  const bool read = contents.empty() ||
                    std::fread(contents.data(), 1, contents.size(), file) ==
                        contents.size();
  std::fclose(file);
  return read ? sha1Hex(contents.data(), contents.size()) : std::string{};
}
#endif

std::string sha512Hex(const char *data, size_t size) {
  unsigned char hash[64];
  if (!cryptoSha512(data, size, hash))
    return {};
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

RuntimePlatform::RuntimePlatform(std::string installRoot,
                                 std::string contentRoot,
                                 GLProcResolver resolver,
                                 std::string integrityDatabaseUri)
    : RuntimePlatform(std::move(installRoot), contentRoot, contentRoot,
                      resolver, {}, {}, {}, {},
                      std::move(integrityDatabaseUri)) {}

RuntimePlatform::RuntimePlatform(std::string installRoot,
                                 std::string contentRoot, std::string stateRoot,
                                 GLProcResolver resolver,
                                 StorageCommit commitState,
                                 StorageCommit commitContent,
                                 PathMapper pathMapper,
                                 StorageRemove storageRemove,
                                 std::string integrityDatabaseUri,
                                 StorageList storageList)
    : m_installRoot(withSeparator(std::move(installRoot))),
      m_contentRoot(withSeparator(std::move(contentRoot))),
      m_stateRoot(withSeparator(std::move(stateRoot))),
      m_integrityDatabaseUri(std::move(integrityDatabaseUri)),
      m_commitState(std::move(commitState)),
      m_commitContent(std::move(commitContent)),
      m_pathMapper(std::move(pathMapper)),
      m_storageRemove(std::move(storageRemove)),
      m_storageList(std::move(storageList)), m_glResolver(resolver) {
  std::error_code error;
  std::filesystem::create_directories(m_contentRoot, error);
  if (!error)
    std::filesystem::create_directories(m_stateRoot, error);
  m_state = std::make_unique<RuntimeStateStore>(
      std::filesystem::path(m_stateRoot) / ".playground-state", m_commitState);
  m_scriptRegistry = std::make_unique<DesktopScriptRegistry>();
  m_widgetManager = std::make_unique<RuntimeWidgetManager>(this);
  if (m_state->valid()) {
    m_deviceId = m_state->get("system/device-id");
    if (m_deviceId.empty()) {
      m_deviceId = randomIdentifier();
      if (m_deviceId.empty() ||
          !m_state->set("system/device-id", m_deviceId))
        m_deviceId.clear();
    }
  }
}
RuntimePlatform::~RuntimePlatform() = default;

bool RuntimePlatform::usesStateStorage(const char *uri) const {
  if (uriUsesExplicitStateRoot(uri))
    return true;
  const std::string resolved = resolvePath(uri, nullptr);
  return pathUsesRoot(resolved, m_stateRoot);
}

bool RuntimePlatform::init() {
  m_initializationError.clear();
  if (!m_state || !m_state->valid()) {
    m_initializationError = "persistent state is corrupt or unreadable";
    logging("platform %s", m_initializationError.c_str());
    return false;
  }
  if (m_deviceId.empty()) {
    m_initializationError =
        "persistent device identity could not be loaded or committed";
    logging("platform %s", m_initializationError.c_str());
    return false;
  }
  m_audio = getNewAudioImplementation();
  if (!m_audio || !m_audio->init()) {
    m_initializationError = "audio system initialization failed";
    return false;
  }
  return true;
}

bool RuntimePlatform::useEncryption() { return true; }
void RuntimePlatform::validateEnvironment() {
  std::error_code error;
  if (!std::filesystem::is_directory(m_installRoot, error))
    logging("platform install root is unavailable: %s", m_installRoot.c_str());
  error.clear();
  std::filesystem::create_directories(m_contentRoot, error);
  if (error)
    logging("platform content root is unavailable: %s (%s)",
            m_contentRoot.c_str(), error.message().c_str());
  error.clear();
  std::filesystem::create_directories(m_stateRoot, error);
  if (error)
    logging("platform state root is unavailable: %s (%s)",
            m_stateRoot.c_str(), error.message().c_str());
}
void RuntimePlatform::detailedLogging(const char *file, const char *fn,
                                      int line, const char *fmt, ...) {
#if defined(__WIIU__)
  (void)file;
  (void)fn;
  (void)line;
  (void)fmt;
#else
  std::fprintf(stderr, "%s:%d %s: ", file, line, fn);
  va_list a;
  va_start(a, fmt);
  std::vfprintf(stderr, fmt, a);
  va_end(a);
  std::fputc('\n', stderr);
#endif
}
void RuntimePlatform::logging(const char *fmt, ...) {
#if defined(__WIIU__)
  (void)fmt;
#else
  va_list a;
  va_start(a, fmt);
  std::vfprintf(stderr, fmt, a);
  va_end(a);
  std::fputc('\n', stderr);
#endif
}
void *RuntimePlatform::ifopen(const char *n, const char *m) {
  return std::fopen(n, m);
}
void RuntimePlatform::ifclose(void *f) {
  if (f)
    std::fclose(static_cast<FILE *>(f));
}
int RuntimePlatform::ifseek(void *f, long o, int w) {
  return std::fseek(static_cast<FILE *>(f), o, w);
}
u32 RuntimePlatform::ifread(void *p, u32 s, u32 n, void *f) {
  return static_cast<u32>(std::fread(p, s, n, static_cast<FILE *>(f)));
}
u32 RuntimePlatform::ifwrite(const void *p, u32 s, u32 n, void *f) {
  return static_cast<u32>(std::fwrite(p, s, n, static_cast<FILE *>(f)));
}
int RuntimePlatform::ifflush(void *f) {
  return std::fflush(static_cast<FILE *>(f));
}
long RuntimePlatform::iftell(void *f) {
  return std::ftell(static_cast<FILE *>(f));
}
bool RuntimePlatform::icreateEmptyFile(const char *n) {
  // Match the original Android/Win32 hosts: ensure existence without truncating
  // an existing file if creation races another opener.
  FILE *f = std::fopen(n, "ab");
  if (!f)
    return false;
  return std::fclose(f) == 0;
}
int RuntimePlatform::irename(const char *a, const char *b) {
  std::lock_guard<std::mutex> mutationLock(m_storageMutationMutex);
  const auto started = std::chrono::steady_clock::now();
  const std::string source = resolvePath(a, nullptr);
  const bool stateSource = pathUsesRoot(source, m_stateRoot);
  std::string destination = resolvePath(b, nullptr);
  constexpr const char *externalPrefix = "file://external/";
  const std::size_t externalPrefixLength = std::strlen(externalPrefix);
  const bool externalSource =
      a && std::strncmp(a, externalPrefix, externalPrefixLength) == 0;
  const bool externalDestination =
      b && std::strncmp(b, externalPrefix, externalPrefixLength) == 0;
  if (stateSource && externalSource && b &&
      std::strncmp(b, externalPrefix, externalPrefixLength) == 0) {
    // Renaming within the legacy unified namespace preserves the source
    // layer, including when a lower-layer destination already exists. This
    // makes account-state files behave like ordinary files even though
    // SaveData and CacheStorage are separate Horizon filesystems.
    const std::string stateUri =
        std::string("file://state/") + (b + externalPrefixLength);
    destination = resolvePath(stateUri.c_str(), nullptr);
  }
  const bool stateDestination = pathUsesRoot(destination, m_stateRoot);
  if (stateSource != stateDestination) {
    errno = EXDEV;
    return -1;
  }
  // A platform path codec may represent one logical component with private
  // intermediate directories. Ensure the destination representation exists
  // before the ordinary atomic leaf rename; this is inert for native paths
  // whose parent already exists.
  std::error_code destinationError;
  std::filesystem::create_directories(
      std::filesystem::path(destination).parent_path(), destinationError);
  if (destinationError) {
    errno = EIO;
    return -1;
  }
  if (stateSource && externalSource && externalDestination &&
      m_contentRoot != m_stateRoot) {
    // A copy-up leaves the replaceable source underneath the account copy.
    // Remove and commit both lower names before publishing the upper rename.
    // If CacheStorage cleanup fails, the caller receives failure while the
    // authoritative SaveData source is still present at its original name.
    // The inverse order reported failure after the logical rename was already
    // durable and could expose the stale lower source on the next lookup.
    const auto physical = [this](const std::string &value) {
      return m_pathMapper
                 ? m_pathMapper(std::filesystem::path(value))
                 : std::filesystem::path(value);
    };
    const std::filesystem::path lowerSource = physical(
        joinUriPath(m_contentRoot, a + externalPrefixLength));
    const std::filesystem::path lowerDestination = physical(
        joinUriPath(m_contentRoot, b + externalPrefixLength));
    std::string removalError;
    bool removedLower = false;
    const auto removeLower = [&](const std::filesystem::path &path) {
      if (!std::filesystem::exists(path))
        return true;
      removedLower = true;
      if (m_storageRemove)
        return m_storageRemove(path, removalError);
      std::error_code error;
      std::filesystem::remove_all(path, error);
      if (error)
        removalError = error.message();
      return !error;
    };
    const bool cleaned = removeLower(lowerSource) &&
                         removeLower(lowerDestination);
    if (!cleaned ||
        (removedLower && m_commitContent && !m_commitContent())) {
      logging("external overlay cleanup failed before rename: %s",
              !cleaned ? removalError.c_str() : "content commit failed");
      return -1;
    }
  }
#if defined(_WIN32)
  const bool moved =
      MoveFileExA(source.c_str(), destination.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  const DWORD moveError = moved ? ERROR_SUCCESS : GetLastError();
  const int result = moved ? 0 : -1;
  if (result)
    std::fprintf(stderr,
                 "asset download: cannot publish %s -> %s: Windows error %lu\n",
                 source.c_str(), destination.c_str(),
                 static_cast<unsigned long>(moveError));
#else
  const int result = std::rename(source.c_str(), destination.c_str());
#if !defined(__WIIU__)
  if (result)
    std::fprintf(stderr, "asset download: cannot publish %s -> %s: %s\n",
                 source.c_str(), destination.c_str(), std::strerror(errno));
#endif
#endif
  StorageCommit &commit = stateDestination ? m_commitState : m_commitContent;
  if (!result && commit && !commit()) {
#if !defined(__WIIU__)
    std::fprintf(stderr, "asset download: storage commit failed after %s\n",
                 destination.c_str());
#endif
    return -1;
  }
#if !defined(__WIIU__)
  if (!result) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::fprintf(stderr,
                 "asset download publication: source=%s destination=%s "
                 "elapsed=%.3fs\n",
                 source.c_str(), destination.c_str(), seconds);
  }
#endif
  return result;
}
// The reconstructed clients run the 9.11 Android data/API contract even when
// their native host is not Android. These values are application identity,
// not a host-platform user-agent.
const char *RuntimePlatform::getBundleVersion() { return "9.11"; }
const char *RuntimePlatform::getBundleId() {
  return "klb.android.lovelive";
}
s64 RuntimePlatform::nanotime() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string RuntimePlatform::resolvePath(const char *input,
                                         bool *readOnly) const {
  if (readOnly)
    *readOnly = false;
  if (!input)
    return {};
  std::string path(input);
  const auto physical = [this](std::string value) {
    return m_pathMapper
               ? m_pathMapper(std::filesystem::path(value)).string()
               : value;
  };
  bool matched = false;
  auto map = [&](const char *prefix, const std::string &root,
                 bool ro) -> std::string {
    size_t n = std::strlen(prefix);
    if (path.compare(0, n, prefix) == 0) {
      matched = true;
      if (readOnly)
        *readOnly = ro;
      return physical(joinUriPath(root, path.substr(n)));
    }
    return {};
  };
  constexpr const char *externalPrefix = "file://external/";
  const std::size_t externalPrefixLength = std::strlen(externalPrefix);
  if (path.compare(0, externalPrefixLength, externalPrefix) == 0) {
    const std::string relative = path.substr(externalPrefixLength);
    const std::string state = physical(joinUriPath(m_stateRoot, relative));
    if (!relative.empty() && !state.empty() && std::filesystem::exists(state))
      return state;
    return physical(joinUriPath(m_contentRoot, relative));
  }
  auto mapped = map("file://state/", m_stateRoot, false);
  if (matched)
    return mapped;
  mapped = map("file://install/", m_installRoot, true);
  if (matched)
    return mapped;
  const char *assetPrefixes[]{"file://asset/", "asset://"};
  for (const char *prefix : assetPrefixes) {
    size_t n = std::strlen(prefix);
    if (path.compare(0, n, prefix) == 0) {
      std::string relative = path.substr(n);
      std::string external = physical(joinUriPath(m_contentRoot, relative));
      if (external.empty())
        return {};
      if (std::filesystem::exists(external))
        return external;
      std::string install = physical(joinUriPath(m_installRoot, relative));
      if (install.empty() || !std::filesystem::exists(install))
        return {};
      if (readOnly)
        *readOnly = true;
      return install;
    }
  }
  return physical(path);
}

IReadStream *RuntimePlatform::openReadStream(const char *n, bool decrypt,
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
IReadStream *RuntimePlatform::openWriteStream(const char *n, bool encrypt,
                                              u32) {
  if (!n)
    return nullptr;
  const char *key = !std::strncmp(n, "file://", 7) ? n + 7 : n;
  return reinterpret_cast<IReadStream *>(new DesktopWriteStream(
      resolvePath(n, nullptr), key, encrypt,
      usesStateStorage(n) ? m_commitState : m_commitContent,
      usesStateStorage(n) ? WriterFactory{} : m_contentWriterFactory));
}
void RuntimePlatform::beforeAssertFunction(const char *function, bool) {
  logging("assert callback: %s", function ? function : "");
  if (function && function[0])
    CKLBScriptEnv::getInstance().call_cbInt(function, 0);
}
void RuntimePlatform::addExtMsg(const char *key, const char *value,
                                bool sendImmediately) {
  if (!key || !key[0])
    return;
  m_state->set(stateKey("diagnostic", key), value ? value : "");
  if (sendImmediately)
    logging("diagnostic %s=%s", key, value ? value : "");
}
void RuntimePlatform::sendException(const char *m) {
  logging("exception: %s", m ? m : "");
}
void RuntimePlatform::leaveBreadcrumb(const char *message) {
  logging("script: %s", message ? message : "");
}
char *RuntimePlatform::createRequestIdHeader() {
  return m_scriptRegistry->createHeader();
}
void RuntimePlatform::copyToClipboard(const char *text) {
#if defined(__SWITCH__)
  logging("clipboard unavailable on Nintendo Switch: %s", text ? text : "");
#elif defined(__WIIU__)
  logging("clipboard unavailable on Nintendo Wii U: %s", text ? text : "");
#else
  if (!SDL_SetClipboardText(text ? text : ""))
    logging("clipboard write failed: %s", SDL_GetError());
#endif
}
double RuntimePlatform::getUsedMemorySize() {
#if defined(__SWITCH__)
  std::uint64_t used = 0;
  std::uint64_t total = 0;
  return switch_runtime::processMemory(used, total) ? static_cast<double>(used)
                                                    : 0.0;
#elif defined(__WIIU__)
  MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
  if (!heap)
    return 0.0;
  const auto total = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(heap->dataEnd) -
      reinterpret_cast<std::uintptr_t>(heap->dataStart));
  const auto free = static_cast<std::uint64_t>(MEMGetTotalFreeSizeForExpHeap(heap));
  return static_cast<double>(total > free ? total - free : 0);
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
             ? static_cast<double>(counters.WorkingSetSize)
             : 0.0;
#else
  std::ifstream status("/proc/self/statm");
  uint64_t pages = 0;
  uint64_t resident = 0;
  if (!(status >> pages >> resident))
    return 0.0;
  return static_cast<double>(resident) *
         static_cast<double>(sysconf(_SC_PAGESIZE));
#endif
}
double RuntimePlatform::getFreeMemorySize() {
#if defined(__SWITCH__)
  std::uint64_t total = 0;
  std::uint64_t used = 0;
  if (!switch_runtime::processMemory(used, total))
    return 0.0;
  return static_cast<double>(total > used ? total - used : 0);
#elif defined(__WIIU__)
  MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
  return heap ? static_cast<double>(MEMGetTotalFreeSizeForExpHeap(heap)) : 0.0;
#elif defined(_WIN32)
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  return GlobalMemoryStatusEx(&status)
             ? static_cast<double>(status.ullAvailPhys)
             : 0.0;
#else
  const long pages = sysconf(_SC_AVPHYS_PAGES);
  const long pageSize = sysconf(_SC_PAGESIZE);
  return pages < 0 || pageSize < 0
             ? 0.0
             : static_cast<double>(pages) * static_cast<double>(pageSize);
#endif
}
bool RuntimePlatform::getSMode() { return false; }
void RuntimePlatform::getDateTimeNow(char *b, int n) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  getLocalTime(t, tm);
  std::strftime(b, n, "%Y-%m-%d %H:%M:%S", &tm);
}
double RuntimePlatform::getUNIXTimeNow() {
  return static_cast<double>(std::time(nullptr));
}
void RuntimePlatform::requestExtensionEvent(const char *eventName,
                                            ExtensionEventArgs *arguments) {
  logging("platform extension event requested: %s (%zu arguments); no matching "
          "extension is registered",
          eventName ? eventName : "<unnamed>",
          arguments ? arguments->size() : 0U);
}
void RuntimePlatform::savePng2Album(const char *path) {
  if (!path || !path[0])
    return;
  const std::filesystem::path source(resolvePath(path, nullptr));
#if defined(__SWITCH__)
  std::string error;
  if (!switch_runtime::savePngToAlbum(source.string(), error))
    logging("screenshot album export failed: %s", error.c_str());
  return;
#else
  const std::filesystem::path destinationDirectory =
      std::filesystem::path(m_contentRoot) / "Pictures";
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
#endif
}
void RuntimePlatform::setIdleTimerActivity(bool active) {
#if defined(__SWITCH__)
  if (!switch_runtime::setAutoSleepDisabled(active))
    logging("failed to change Switch auto-sleep state");
#elif defined(__WIIU__)
  if (active) {
    IMDisableAPD();
    IMDisableDim();
  } else {
    IMEnableDim();
    IMEnableAPD();
  }
#else
  if (active)
    SDL_DisableScreenSaver();
  else
    SDL_EnableScreenSaver();
#endif
}
ITmpFile *RuntimePlatform::openTmpFile(const char *p) {
  auto *f = new DesktopTmpFile(resolvePath(p, nullptr),
                               usesStateStorage(p) ? m_commitState
                                                   : m_commitContent,
                               usesStateStorage(p) ? WriterFactory{} : m_contentWriterFactory);
  if (!f->ready()) {
    delete f;
    return nullptr;
  }
  return f;
}
bool RuntimePlatform::beginFileWriteBatch(const char* root) {
  return !m_contentWriteBatch || usesStateStorage(root) || m_contentWriteBatch(true, false);
}
bool RuntimePlatform::endFileWriteBatch(const char* root, bool success) {
  return !m_contentWriteBatch || usesStateStorage(root) || m_contentWriteBatch(false, success);
}
int RuntimePlatform::removeTmpFile(const char *p) {
  int result = std::remove(resolvePath(p, nullptr).c_str());
  StorageCommit &commit =
      usesStateStorage(p) ? m_commitState : m_commitContent;
  return !result && commit && !commit() ? -1 : result;
}
bool RuntimePlatform::removeFileOrFolder(const char *p) {
  if (!p)
    return false;
  std::lock_guard<std::mutex> mutationLock(m_storageMutationMutex);
  const auto removePhysical = [&](const std::filesystem::path &target) {
    if (m_storageRemove) {
      std::string error;
      const bool removed = m_storageRemove(target, error);
      if (!removed)
        logging("storage removal failed for %s: %s", target.c_str(),
                error.c_str());
      return removed;
    }
    std::error_code error;
    std::filesystem::remove_all(target, error);
    return !error;
  };

  constexpr const char *externalPrefix = "file://external/";
  const std::size_t externalPrefixLength = std::strlen(externalPrefix);
  if (std::strncmp(p, externalPrefix, externalPrefixLength) == 0) {
    const std::string relative(p + externalPrefixLength);
    if (relative.empty()) {
      logging("refusing to remove the mounted external-storage root");
      return false;
    }
    const auto physical = [this](const std::string &value) {
      return m_pathMapper
                 ? m_pathMapper(std::filesystem::path(value))
                 : std::filesystem::path(value);
    };
    const std::filesystem::path state =
        physical(joinUriPath(m_stateRoot, relative));
    const std::filesystem::path content =
        physical(joinUriPath(m_contentRoot, relative));
    if (state.empty() || content.empty())
      return false;

    const bool stateExists = std::filesystem::exists(state);
    const bool contentExists = content != state && std::filesystem::exists(content);
    if ((stateExists && !removePhysical(state)) ||
        (contentExists && !removePhysical(content)))
      return false;
    if (stateExists && m_commitState && !m_commitState())
      return false;
    if (contentExists && m_commitContent && !m_commitContent())
      return false;
    return true;
  }

  const std::filesystem::path target = resolvePath(p, nullptr);
  const bool removed = removePhysical(target);
  StorageCommit &commit =
      usesStateStorage(p) ? m_commitState : m_commitContent;
  return removed && (!commit || commit());
}
bool RuntimePlatform::listFiles(const char *p,
                                std::vector<std::string> &names) {
  names.clear();
  if (!p)
    return false;

  std::lock_guard<std::mutex> mutationLock(m_storageMutationMutex);
  std::set<std::string> merged;
  bool found = false;
  bool listingFailed = false;
  const auto collect = [&](const std::filesystem::path &directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
      return;
    found = true;
    if (m_storageList) {
      std::vector<std::string> physicalNames;
      std::string listError;
      if (!m_storageList(directory, physicalNames, listError)) {
        logging("storage listing failed for %s: %s", directory.c_str(),
                listError.c_str());
        listingFailed = true;
        return;
      }
      merged.insert(physicalNames.begin(), physicalNames.end());
      return;
    }
    for (std::filesystem::directory_iterator entry(directory, error), end;
         !error && entry != end; entry.increment(error))
      merged.insert(entry->path().filename().string());
  };

  constexpr const char *externalPrefix = "file://external/";
  const std::size_t externalPrefixLength = std::strlen(externalPrefix);
  if (std::strncmp(p, externalPrefix, externalPrefixLength) == 0) {
    const std::string relative(p + externalPrefixLength);
    const auto physical = [this](const std::string &value) {
      return m_pathMapper
                 ? m_pathMapper(std::filesystem::path(value))
                 : std::filesystem::path(value);
    };
    collect(physical(joinUriPath(m_stateRoot, relative)));
    collect(physical(joinUriPath(m_contentRoot, relative)));
  } else {
    collect(resolvePath(p, nullptr));
  }

  if (!found || listingFailed)
    return false;
  // The historical readdir-backed API exposed these entries, and existing
  // scripts commonly filter them explicitly.
  names.emplace_back(".");
  names.emplace_back("..");
  names.insert(names.end(), merged.begin(), merged.end());
  return true;
}
void RuntimePlatform::excludePathFromBackup(const char *) {
  // Desktop save data is not managed by a mobile cloud-backup service.
}
u32 RuntimePlatform::getFreeSpaceExternalKB() {
#if defined(__SWITCH__)
  if (switch_runtime::isInstalledApplication()) {
    std::uint64_t bytes = 0;
    std::string error;
    if (!switch_runtime::freeCacheStorageBytes(bytes, error)) {
      logging("Switch free-space query failed: %s", error.c_str());
      return 0;
    }
    return static_cast<u32>(
        std::min<std::uint64_t>(bytes / 1024, UINT32_MAX));
  }
  // An NRO explicitly uses an SD development root, not title CacheStorage.
  std::error_code e;
  const auto s = std::filesystem::space(m_contentRoot, e);
  return e ? 0
           : static_cast<u32>(
                 std::min<uintmax_t>(s.available / 1024, UINT32_MAX));
#elif defined(__WIIU__)
  std::uint64_t bytes = 0;
  return playground::wiiu_runtime::freeStorageBytes(m_contentRoot, bytes)
             ? static_cast<u32>(
                   std::min<std::uint64_t>(bytes / 1024, UINT32_MAX))
             : 0;
#else
  std::error_code e;
  auto s = std::filesystem::space(m_contentRoot, e);
  return e ? 0
           : static_cast<u32>(
                 std::min<uintmax_t>(s.available / 1024, UINT32_MAX));
#endif
}
u32 RuntimePlatform::getPhysicalMemKB() {
#if defined(__SWITCH__)
  std::uint64_t total = 0;
  std::uint64_t used = 0;
  return switch_runtime::processMemory(used, total)
             ? static_cast<u32>(std::min<u64>(total / 1024, UINT32_MAX))
             : 0;
#elif defined(__WIIU__)
  std::uint32_t address = 0;
  std::uint32_t size = 0;
  return OSGetMemBound(OS_MEM2, &address, &size)
             ? 0
             : static_cast<u32>(std::min<std::uint64_t>(size / 1024,
                                                        UINT32_MAX));
#else
  const int megabytes = SDL_GetSystemRAM();
  return megabytes <= 0
             ? 0
             : static_cast<u32>(std::min<uint64_t>(
                   static_cast<uint64_t>(megabytes) * 1024, UINT32_MAX));
#endif
}
char *RuntimePlatform::getDeviceIntegrityInfo(const char *request) {
  const DesktopHostInfo system = getDesktopHostInfo();

  std::map<std::string, std::string> properties;
  properties["ro.build.version.release"] = system.release;
#if defined(__SWITCH__)
  properties["ro.product.name"] = "Nintendo Switch";
  properties["ro.product.manufacturer"] = "Nintendo";
  properties["ro.product.brand"] = "Nintendo";
  properties["ro.build.tags"] = "horizon-release";
#else
  properties["ro.product.name"] = "PlaygroundOSS Desktop";
  properties["ro.product.manufacturer"] = "Community";
  properties["ro.product.brand"] = "PlaygroundOSS";
  properties["ro.build.tags"] = "desktop-release";
#endif
  properties["ro.product.device"] = system.hostname;
  properties["ro.product.model"] = system.machine;
  properties["ro.product.board"] = system.machine;
  properties["ro.build.fingerprint"] =
      system.system + "/" + system.release + "/" + system.machine;
  properties["Hardware"] = system.machine;
  properties["basePath"] = m_installRoot;
  properties["adbEnabled"] = "false";
  properties["device_id"] = m_deviceId;

  char databaseHash[64]{};
  const std::string integrityDatabase =
      !m_integrityDatabaseUri.empty()
          ? resolvePath(m_integrityDatabaseUri.c_str(), nullptr)
          : std::string();
#if defined(PLAYGROUND_WEB)
  const std::string webDatabaseHash = sha1FileForWeb(integrityDatabase);
  std::snprintf(databaseHash, sizeof(databaseHash), "%s",
                webDatabaseHash.c_str());
#else
  if (!integrityDatabase.empty())
    CKLBUtility::sha1File(integrityDatabase.c_str(), databaseHash, 40);
#endif
  properties["db_sha1"] = databaseHash[0] ? databaseHash : "NOT_FOUND";

  const std::string executable = getExecutablePath();
  char executableHash[64]{};
  if (!executable.empty())
    CKLBUtility::sha1File(executable.c_str(), executableHash, 40);
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
void RuntimePlatform::decompressBGM(bool decompress) {
  g_decompressBGM = decompress;
}
s64 RuntimePlatform::getElapsedTime() {
  // IPlatformRequest exposes this clock in seconds. Lua uses it for timeout
  // values such as the home-screen navigation voice interval.
  return nanotime() / 1000000000LL;
}
void *RuntimePlatform::getFontSystem() {
  static DesktopFontSystem fontSystem;
  return &fontSystem;
}
void RuntimePlatform::deleteFontSystem(void *font) {
  FontObject::destroyFont(static_cast<FontObject *>(font));
}
void *RuntimePlatform::getFont(int s, const char *n) {
  return FontObject::createFont(n, static_cast<u32>(s));
}
void RuntimePlatform::deleteFont(void *f) {
  FontObject::destroyFont(static_cast<FontObject *>(f));
}
const char *RuntimePlatform::getFullPath(const char *p, bool *ro) {
  std::string v = resolvePath(p, ro);
  // Match CKLBPathConv on Android: an asset which exists in neither the
  // writable content root nor the installed bundle has no native path.  In
  // particular, CKLBLuaDB passes this nullptr to SQLite, whose documented
  // temporary-database behavior is how the original client tolerates master
  // databases that are supplied by the initial package download.
  if (v.empty())
    return nullptr;
  char *out = new char[v.size() + 1];
  std::memcpy(out, v.c_str(), v.size() + 1);
  return out;
}
const char *RuntimePlatform::getPlatform() {
  static const std::string platform = [] {
    const DesktopHostInfo system = getDesktopHostInfo();
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    char timeZone[64] = "UTC";
    if (getLocalTime(now, localTime))
      std::strftime(timeZone, sizeof(timeZone), "%Z", &localTime);
    // The SIF network protocol only defines platform types for iOS and
    // Android. Keep the real desktop kernel in the version field while
    // presenting the compatible Android OS family to game scripts and HTTP
    // header construction.
    return std::string("Android;Desktop ") + system.system + " " +
           system.machine + " " + system.release + ";" + timeZone;
  }();
  return platform.c_str();
}
void *RuntimePlatform::getGLExtension(const char *n) {
  return m_glResolver ? m_glResolver(n) : nullptr;
}
const char *RuntimePlatform::getShaderExtension(int) { return ""; }
bool RuntimePlatform::setFrameRate(int n) {
#if defined(__SWITCH__)
  // The Switch host is synchronized to the 60 Hz display and has no dynamic
  // frame-pacing implementation. Match the original Android contract: do not
  // claim that an arbitrary requested rate was applied.
  (void)n;
  return false;
#else
  if (n <= 0)
    return false;
  m_frameRate = n;
  return true;
#endif
}
int RuntimePlatform::getMaxFrameRate() {
#if defined(__SWITCH__)
  return 60;
#else
  return 240;
#endif
}
IWidget *RuntimePlatform::createControl(IWidget::CONTROL type, int id,
                                        const char *caption, int x, int y,
                                        int width, int height, ...) {
  va_list arguments;
  va_start(arguments, height);
  IWidget *widget = m_widgetManager->create(type, id, caption, x, y, width,
                                            height, arguments);
  va_end(arguments);
  return widget;
}
void RuntimePlatform::destroyControl(IWidget *widget) {
  m_widgetManager->destroy(widget);
}
bool RuntimePlatform::callApplication(APP_TYPE type, ...) {
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
#if defined(__SWITCH__)
    // Horizon has no application clipboard API. Do not tell Lua that content
    // was shared when the platform only logged it.
    return false;
#else
    return true;
#endif
  }
  case APP_ATT: {
    const char *callback = va_arg(arguments, const char *);
    const bool request = va_arg(arguments, int) != 0;
    // Android reports the unsupported ATT state synchronously with status 0.
    // Desktop has no App Tracking Transparency prompt, but the script still
    // requires the callback to continue the login chain.
    beforeAssertFunction(callback, request);
    va_end(arguments);
    return true;
  }
  case APP_SETTINGS:
  default:
    break;
  }
  va_end(arguments);
  return !url.empty() && openExternalUrl(url.c_str());
}

bool RuntimePlatform::openExternalUrl(const char *url) {
#if defined(__EMSCRIPTEN__)
  if (!url || !url[0])
    return false;
  // The engine runs on an Emscripten pthread. SDL's Web implementation calls
  // window.open directly, but workers do not have a Window global. Proxy the
  // browser operation synchronously to the main browser thread instead.
  MAIN_THREAD_EM_ASM({ window.open(UTF8ToString($0), "_blank"); }, url);
  return true;
#elif defined(__SWITCH__)
  const bool shown = switch_runtime::showWebPage(url);
  if (!shown)
    logging("Switch web applet failed");
  return shown;
#elif defined(__WIIU__)
  if (!url || !url[0])
    return false;
  SysAppBrowserArgs arguments{};
  arguments.url = const_cast<char *>(url);
  arguments.urlSize = static_cast<std::uint32_t>(std::strlen(url) + 1);
  const int result = SYSSwitchToBrowserForViewer(&arguments);
  if (result != 0)
    logging("Wii U browser launch failed: %d", result);
  return result == 0;
#else
  return url && url[0] && SDL_OpenURL(url);
#endif
}
void RuntimePlatform::clearCookies() {
  const std::filesystem::path path = cookieStoragePath(m_stateRoot);
  const bool clearedInMemory = CurlObjectInternal::clearCookieStorage();
  std::error_code error;
  if (!clearedInMemory)
    std::filesystem::remove(path, error);
  else
    restrictStateFile(path);
  if (!error && m_commitState && !m_commitState())
    logging("cookie SaveData clear commit failed");
}
bool RuntimePlatform::readyDevID() { return !m_deviceId.empty(); }
int RuntimePlatform::getDevID(char *b, int n) {
  copyText(m_deviceId, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
void RuntimePlatform::exitGame() { m_quitRequested = true; }
bool RuntimePlatform::setSecureDataID(const char *s, const char *v) {
  return m_state->set(stateKey("secure", s, "user_id"), v ? v : "");
}
bool RuntimePlatform::setSecureDataPW(const char *s, const char *v) {
  return m_state->set(stateKey("secure", s, "passwd"), v ? v : "");
}
int RuntimePlatform::getSecureDataID(const char *s, char *b, int n) {
  const auto v = m_state->get(stateKey("secure", s, "user_id"));
  copyText(v, b, n);
  return static_cast<int>(v.size());
}
int RuntimePlatform::getSecureDataPW(const char *s, char *b, int n) {
  const auto v = m_state->get(stateKey("secure", s, "passwd"));
  copyText(v, b, n);
  return static_cast<int>(v.size());
}
bool RuntimePlatform::delSecureDataID(const char *s) {
  return m_state->erase(stateKey("secure", s, "user_id"));
}
bool RuntimePlatform::delSecureDataPW(const char *s) {
  return m_state->erase(stateKey("secure", s, "passwd"));
}
void RuntimePlatform::setUserDefaults(const char *k, bool v) {
  m_state->set(stateKey("default", k), v ? "TRUE" : "FALSE");
}
bool RuntimePlatform::getUserDefaults(const char *k) {
  return m_state->get(stateKey("default", k)) == "TRUE";
}
void RuntimePlatform::setUserDefaults(const char *k, const char *v) {
  m_state->set(stateKey("default", k), v ? v : "");
}
void RuntimePlatform::getUserDefaults(const char *k, char *b, int n) {
  copyText(m_state->get(stateKey("default", k)), b, n);
}
void *RuntimePlatform::createThread(s32 (*fn)(void *, void *), void *data) {
  auto *t = new DesktopThread;
  t->thread = std::thread([t, fn, data] {
    t->result = fn(t, data);
    t->done = true;
  });
  return t;
}
void RuntimePlatform::exitThread(void *h, s32 s) {
  static_cast<DesktopThread *>(h)->result = s;
}
bool RuntimePlatform::watchThread(void *h, s32 *s) {
  auto *t = static_cast<DesktopThread *>(h);
  if (!t->done)
    return true;
  if (s)
    *s = t->result;
  return false;
}
void RuntimePlatform::deleteThread(void *h) {
  auto *t = static_cast<DesktopThread *>(h);
  if (t->thread.joinable())
    t->thread.join();
  delete t;
}
void RuntimePlatform::breakThread(void *handle) {
  if (!handle)
    return;
  auto *thread = static_cast<DesktopThread *>(handle);
#if defined(_WIN32)
  if (!TerminateThread(thread->thread.native_handle(), 0))
    logging("desktop worker cancellation failed: %lu", GetLastError());
#else
  const int result = pthread_cancel(thread->thread.native_handle());
  if (result != 0)
    logging("desktop worker cancellation failed: %d", result);
#endif
}
int RuntimePlatform::genUserID(char *b, int n) {
  const std::string id = randomIdentifier();
  copyText(id, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
int RuntimePlatform::genUserPW(const char *s, char *b, int n) {
  unsigned char randomBytes[4];
  if (!cryptoRandom(randomBytes, sizeof(randomBytes)))
    return 0;
  u32 randomValue;
  std::memcpy(&randomValue, randomBytes, sizeof(randomValue));
  char input[1200];
  std::snprintf(input, sizeof(input), "%u.%u.%s", randomValue,
                static_cast<u32>(std::time(nullptr)), s ? s : "");
  unsigned char digest[64];
  if (!cryptoSha512(input, std::strlen(input), digest))
    return 0;
  std::string value;
  value.reserve(sizeof(digest) * 2);
  char hex[3];
  for (unsigned char byte : digest) {
    std::snprintf(hex, sizeof(hex), "%02x", byte);
    value += hex;
  }
  copyText(value, b, n);
  return b && n > 0 ? static_cast<int>(std::strlen(b)) : 0;
}
void RuntimePlatform::registerScriptSource(const char *source, int sourceSize,
                                           const char *sourceName) {
  m_scriptRegistry->registerSource(source, sourceSize, sourceName);
}
void RuntimePlatform::initStoreTransactionObserver() {
  logging("store observer initialized; purchases report unavailable");
}
void RuntimePlatform::releaseStoreTransactionObserver() {}
void RuntimePlatform::buyStoreItems(const char *item) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Purchases are unavailable on this platform";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_FAILED, nullptr,
      item && item[0] ? std::strlen(item) + 1 : 0,
      item && item[0] ? const_cast<char *>(item) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
void RuntimePlatform::getStoreProducts(const char *items, bool) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Store products are unavailable on this platform";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_GET_PRODUCTS_FAILED, nullptr,
      items && items[0] ? std::strlen(items) + 1 : 0,
      items && items[0] ? const_cast<char *>(items) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
void RuntimePlatform::finishStoreTransaction(const char *transaction) {
  if (!CPFInterface::getInstance().isClient())
    return;
  const char *message = "Store restoration is unavailable on this platform";
  CPFInterface::getInstance().client().controlEvent(
      IClientRequest::E_STORE_RESTORE_FAILED, nullptr,
      transaction && transaction[0] ? std::strlen(transaction) + 1 : 0,
      transaction && transaction[0] ? const_cast<char *>(transaction) : nullptr,
      std::strlen(message) + 1, const_cast<char *>(message));
}
bool RuntimePlatform::publicKeyVerify(unsigned char *message, int messageLength,
                                      unsigned char *signature,
                                      int signatureLength) {
  if (!message || messageLength < 0 || !signature || signatureLength < 0)
    return false;
  return cryptoPublicKeyVerify(message, static_cast<size_t>(messageLength),
                               signature, static_cast<size_t>(signatureLength));
}
int RuntimePlatform::publicKeyEncrypt(unsigned char *input, int inputLength,
                                      unsigned char *output, int outputLength) {
  if (!input || inputLength < 0 || !output || outputLength < 0)
    return -1;
  return cryptoPublicKeyEncrypt(input, static_cast<size_t>(inputLength), output,
                                static_cast<size_t>(outputLength));
}
bool RuntimePlatform::randomBytes(unsigned char *o, int n) {
  return o && n >= 0 && cryptoRandom(o, static_cast<size_t>(n));
}
int RuntimePlatform::encryptAES128CBC(unsigned char *output, int outputLength,
                                      const char *input, int inputLength,
                                      const char *key, int keyLength) {
  if (!output || outputLength < 0 || !input || inputLength < 0 || !key ||
      keyLength < 16)
    return -1;
  return cryptoEncryptAes128Cbc(output, static_cast<size_t>(outputLength),
                                reinterpret_cast<const unsigned char *>(input),
                                static_cast<size_t>(inputLength),
                                reinterpret_cast<const unsigned char *>(key));
}
int RuntimePlatform::decryptAES128CBC(unsigned char *output, int outputLength,
                                      const char *input, int inputLength,
                                      const char *key, int keyLength) {
  if (!output || outputLength < 0 || !input || inputLength < 16 || !key ||
      keyLength < 16)
    return -1;
  return cryptoDecryptAes128Cbc(output, static_cast<size_t>(outputLength),
                                reinterpret_cast<const unsigned char *>(input),
                                static_cast<size_t>(inputLength),
                                reinterpret_cast<const unsigned char *>(key));
}
bool RuntimePlatform::initNetwork() {
#if defined(__SWITCH__)
  std::string error;
  if (!switch_runtime::sharedSwitchNetwork().initialize(error)) {
    logging("Switch network initialization failed: %s", error.c_str());
    return false;
  }
#endif
  const bool initialized = CurlObjectInternal::initializeLibrary();
  if (initialized && !CurlObjectInternal::configureCookieStorage(
                         cookieStoragePath(m_stateRoot).string().c_str())) {
    logging("persistent cookie storage initialization failed");
    CurlObjectInternal::shutdownLibrary();
#if defined(__SWITCH__)
    std::string rollbackError;
    if (!switch_runtime::sharedSwitchNetwork().rollbackInitialization(
            rollbackError))
      logging("Switch network rollback failed: %s", rollbackError.c_str());
#endif
    return false;
  }
#if defined(__SWITCH__)
  if (!initialized) {
    std::string rollbackError;
    if (!switch_runtime::sharedSwitchNetwork().rollbackInitialization(
            rollbackError))
      logging("Switch network rollback failed: %s", rollbackError.c_str());
  }
#endif
  return initialized;
}
void RuntimePlatform::shutdownNetwork() {
#if defined(__SWITCH__)
  // A transfer owns both libcurl state and, for downloads, an open
  // CacheStorage writer. Request cancellation and wait for every transfer
  // lease before touching the process-wide cookie engine or curl globals.
  // Reversing this order can run curl_global_cleanup beneath a worker and
  // later unmount storage while its write callback is still active.
  switch_runtime::sharedSwitchNetwork().shutdownAndWait();
#endif
  bool changed = false;
  if (CurlObjectInternal::flushCookieStorage(&changed) && changed) {
    restrictStateFile(cookieStoragePath(m_stateRoot));
    if (m_commitState && !m_commitState())
      logging("cookie SaveData shutdown commit failed");
  }
  CurlObjectInternal::shutdownLibrary();
}
CurlObjectInternal *RuntimePlatform::createNetworkOperation() {
  return CurlObjectInternal::create();
}
void RuntimePlatform::resetNetworkOperation(CurlObjectInternal *o) {
  o->reset();
}
void RuntimePlatform::cleanupNetworkOperation(CurlObjectInternal *o) {
  o->cleanup();
  bool changed = false;
  if (CurlObjectInternal::flushCookieStorage(&changed) && changed) {
    restrictStateFile(cookieStoragePath(m_stateRoot));
    if (m_commitState && !m_commitState())
      logging("cookie SaveData request commit failed");
  }
}
int RuntimePlatform::performNetworkOperation(CurlObjectInternal *o) {
#if defined(__SWITCH__)
  std::string error;
  auto transfer = switch_runtime::sharedSwitchNetwork().beginTransfer(error);
  if (!transfer) {
    logging("Switch network transfer rejected: %s", error.c_str());
    return 2; // CURLE_FAILED_INIT without pulling curl types into this API.
  }
  o->setAbortPredicate(
      [](void *context) {
        return static_cast<switch_runtime::SwitchNetwork::Transfer *>(context)
            ->cancellationRequested();
      },
      &transfer);
  const int result = o->perform();
  const CurlTransferMetrics metrics = o->getTransferMetrics();
  transfer.complete(metrics.downloadedBytes > 0.0
                        ? static_cast<std::size_t>(metrics.downloadedBytes)
                        : 0);
  logging("network metrics: result=%d attempt=%llu queue=%.3fs dns=%.3fs "
          "connect=%.3fs "
          "first-byte=%.3fs total=%.3fs bytes=%.0f speed=%.0fB/s "
          "connections=%ld redirects=%ld",
          result, static_cast<unsigned long long>(metrics.attemptNumber),
          metrics.queueSeconds, metrics.dnsSeconds, metrics.connectSeconds,
          metrics.firstByteSeconds, metrics.totalSeconds,
          metrics.downloadedBytes, metrics.averageBytesPerSecond,
          metrics.connectionCount, metrics.redirectCount);
  return result;
#else
  return o->perform();
#endif
}
void RuntimePlatform::freeNetworkFormHeaders(CurlObjectInternal *o) {
  o->freeFormHeaders();
}
void RuntimePlatform::destroyNetworkOperation(CurlObjectInternal *o) {
  CurlObjectInternal::destroy(o);
}
void RuntimePlatform::appendNetworkHeader(CurlObjectInternal *o,
                                          const char *h) {
  o->appendHeader(h);
}
void RuntimePlatform::setNetworkPostFields(CurlObjectInternal *o) {
  o->setPostFields();
}
void RuntimePlatform::setNetworkPostData(CurlObjectInternal *o, long n,
                                         const void *p) {
  o->setPostData(n, p);
}
void RuntimePlatform::addNetworkFormData(CurlObjectInternal *o, const char *n,
                                         long s, const void *p) {
  o->addFormData(n, s, p);
}
void RuntimePlatform::setupNetworkConnection(CurlObjectInternal *o,
                                             const char *u, const char *p,
                                             void *c, void *a, void *h,
                                             void *w) {
  o->setupConnection(u, p, c, a, h, w);
#if defined(__SWITCH__)
  // Match SocketInitConfig's eight BSD sessions and fail a disconnected
  // transfer instead of leaving the engine's worker asleep indefinitely.
  o->configureTransportLimits(8, 20, 1024, 30);
#endif
}
long RuntimePlatform::getNetworkHttpCode(CurlObjectInternal *o) {
  return o->getHttpCode();
}
void RuntimePlatform::startAlertDialog(const char *t, const char *m) {
#if defined(__SWITCH__)
  if (!switch_runtime::showApplicationMessage(t ? t : "Alert", m ? m : ""))
    logging("failed to show Switch alert: %s: %s", t ? t : "Alert",
            m ? m : "");
#else
  logging("%s: %s", t ? t : "Alert", m ? m : "");
#endif
}
void RuntimePlatform::forbidSleep(bool forbidden) {
#if defined(__SWITCH__)
  if (!switch_runtime::setAutoSleepDisabled(forbidden))
    logging("failed to change Switch auto-sleep state");
#elif defined(__WIIU__)
  if (forbidden) {
    IMDisableAPD();
    IMDisableDim();
  } else {
    IMEnableDim();
    IMEnableAPD();
  }
#else
  if (forbidden)
    SDL_DisableScreenSaver();
  else
    SDL_EnableScreenSaver();
#endif
}
float RuntimePlatform::getDeviceScale() { return 1.0f; }
void RuntimePlatform::quitGame() { m_quitRequested = true; }
void *RuntimePlatform::allocMutex() { return new std::mutex; }
void RuntimePlatform::freeMutex(void *p) {
  delete static_cast<std::mutex *>(p);
}
void RuntimePlatform::mutexLock(void *p) {
  static_cast<std::mutex *>(p)->lock();
}
void RuntimePlatform::mutexUnlock(void *p) {
  static_cast<std::mutex *>(p)->unlock();
}
void *RuntimePlatform::allocEventLock() { return new DesktopEvent; }
void RuntimePlatform::freeEventLock(void *p) {
  delete static_cast<DesktopEvent *>(p);
}
void RuntimePlatform::eventSleep(void *p) {
  auto *e = static_cast<DesktopEvent *>(p);
  std::unique_lock<std::mutex> l(e->mutex);
  e->condition.wait(l, [e] { return e->signaled; });
  e->signaled = false;
}
void RuntimePlatform::eventWakeup(void *p) {
  auto *e = static_cast<DesktopEvent *>(p);
  {
    std::lock_guard<std::mutex> l(e->mutex);
    e->signaled = true;
  }
  e->condition.notify_one();
}
const char *RuntimePlatform::getLangCodeRAW() {
#if defined(__SWITCH__)
  return switch_runtime::systemLanguageCode();
#else
  const char *configured = std::getenv("PLAYGROUND_LANGUAGE");
  return configured && configured[0] ? configured : "ja";
#endif
}
const char *RuntimePlatform::getCountryCodeRAW() {
#if defined(__SWITCH__)
  return switch_runtime::systemCountryCode();
#else
  const char *configured = std::getenv("PLAYGROUND_COUNTRY");
  return configured && configured[0] ? configured : "JP";
#endif
}
const char *RuntimePlatform::getPreferredLangCodeRAW() {
  return getLangCodeRAW();
}
bool RuntimePlatform::getGyroPolar(float *a, float *e) {
#if defined(__SWITCH__)
  float azimuth = 0.0f;
  float elevation = 0.0f;
  const bool available =
      switch_runtime::readMotionAngles(azimuth, elevation);
  if (a)
    *a = azimuth;
  if (e)
    *e = elevation;
  return available;
#elif defined(__WIIU__)
  return playgroundWiiUHostGetMotionAngles(a, e);
#else
  if (a)
    *a = 0;
  if (e)
    *e = 0;
  return false;
#endif
}
void *RuntimePlatform::getFont(int s, const char *n, float *a) {
  auto *f = static_cast<FontObject *>(getFont(s, n));
  if (a)
    *a = f ? f->getAscent() : 0;
  return f;
}
void *RuntimePlatform::getFontSystem(int s, const char *n) {
  return getFont(s, n);
}
bool RuntimePlatform::getTextInfo(const char *t, void *f, STextInfo *i) {
  if (!f || !i)
    return false;
  static_cast<FontObject *>(f)->getTextInfo(t, i, 1, 1);
  return true;
}

void RuntimePlatform::handleTextInput(const char *text) {
  m_widgetManager->inputText(text);
}

bool RuntimePlatform::handleEditingKey(int key, bool pressed) {
  return m_widgetManager->inputKey(key, pressed);
}

bool RuntimePlatform::handleWidgetPointerDown(float x, float y) {
  return m_widgetManager->pointerDown(x, y);
}

void RuntimePlatform::pumpPlatformEvents() {
#if defined(__EMSCRIPTEN__)
  pumpWebPlatformServices();
#endif
  m_widgetManager->pumpEvents();
}

void RuntimePlatform::renderWidgets() { m_widgetManager->render(); }

} // namespace playground::runtime
