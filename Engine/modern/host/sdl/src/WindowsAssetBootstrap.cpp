#if defined(_WIN32)

#include "WindowsAssetBootstrap.h"

#include "ioapi.h"
#include "unzip.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>
#include <vector>

namespace playground::windows {
namespace {

constexpr wchar_t AppDirectoryName[] = L"PlaygroundOSS-SIF";
constexpr wchar_t ArchiveName[] = L"AppAssets.zip";
constexpr wchar_t ManifestName[] = L"AppAssets.version";
constexpr wchar_t InstalledManifestName[] = L".appassets-version";
constexpr wchar_t MutexName[] = L"Local\\PlaygroundOSS-SIF-AssetBootstrap";

std::string wideToUtf8(const std::wstring &value) {
  if (value.empty())
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0)
    return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring utf8ToWide(const std::string &value) {
  if (value.empty())
    return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string windowsError(DWORD code) {
  wchar_t *message = nullptr;
  const DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
  std::wstring text = size && message
                          ? std::wstring(message, size)
                          : L"Windows error " + std::to_wstring(code);
  if (message)
    LocalFree(message);
  while (!text.empty() &&
         (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
    text.pop_back();
  return wideToUtf8(text);
}

void appendLog(const std::filesystem::path &dataRoot, const std::string &line) {
  std::error_code ignored;
  const std::filesystem::path logDirectory = dataRoot / L"logs";
  std::filesystem::create_directories(logDirectory, ignored);
  const std::filesystem::path path = logDirectory / L"bootstrap.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  SYSTEMTIME time{};
  GetLocalTime(&time);
  char prefix[64]{};
  std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u ",
                time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                time.wSecond);
  std::string record = prefix + line + "\r\n";
  DWORD written = 0;
  WriteFile(file, record.data(), static_cast<DWORD>(record.size()), &written,
            nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

class ScopedMutex {
public:
  explicit ScopedMutex(std::string &error) {
    m_handle = CreateMutexW(nullptr, FALSE, MutexName);
    if (!m_handle) {
      error = "Could not create the asset installation lock: " +
              windowsError(GetLastError());
      return;
    }
    const DWORD wait = WaitForSingleObject(m_handle, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
      error = "Could not acquire the asset installation lock: " +
              windowsError(GetLastError());
      CloseHandle(m_handle);
      m_handle = nullptr;
      return;
    }
    m_owned = true;
  }
  ~ScopedMutex() {
    if (m_owned)
      ReleaseMutex(m_handle);
    if (m_handle)
      CloseHandle(m_handle);
  }
  explicit operator bool() const { return m_owned; }

private:
  HANDLE m_handle{};
  bool m_owned{};
};

class ProgressDialog {
public:
  explicit ProgressDialog(bool enabled) : m_enabled(enabled) {
    if (!m_enabled)
      return;
    const HRESULT initialized =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_uninitialize = SUCCEEDED(initialized);
    if (FAILED(CoCreateInstance(CLSID_ProgressDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_dialog))))
      return;
    m_dialog->SetTitle(L"PlaygroundOSS SIF");
    m_dialog->SetCancelMsg(L"Finishing the current file before canceling...",
                           nullptr);
    m_dialog->SetLine(1, L"Installing application assets", FALSE, nullptr);
    m_dialog->StartProgressDialog(
        nullptr, nullptr, PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
  }
  ~ProgressDialog() {
    if (m_dialog) {
      m_dialog->StopProgressDialog();
      m_dialog->Release();
    }
    if (m_uninitialize)
      CoUninitialize();
  }
  void update(std::uint64_t complete, std::uint64_t total,
              const std::wstring &name) {
    if (!m_dialog)
      return;
    m_dialog->SetLine(2, name.c_str(), TRUE, nullptr);
    m_dialog->SetProgress64(complete, std::max<std::uint64_t>(total, 1));
  }
  bool cancelled() const { return m_dialog && m_dialog->HasUserCancelled(); }

private:
  bool m_enabled{};
  bool m_uninitialize{};
  IProgressDialog *m_dialog{};
};

voidpf ZCALLBACK openWideArchive(voidpf, const void *filename, int mode) {
  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ)
    return nullptr;
  return _wfopen(static_cast<const wchar_t *>(filename), L"rb");
}
uLong ZCALLBACK readArchive(voidpf, voidpf stream, void *buffer, uLong size) {
  return static_cast<uLong>(
      std::fread(buffer, 1, size, static_cast<FILE *>(stream)));
}
uLong ZCALLBACK writeArchive(voidpf, voidpf, const void *, uLong) { return 0; }
ZPOS64_T ZCALLBACK tellArchive(voidpf, voidpf stream) {
  const __int64 position = _ftelli64(static_cast<FILE *>(stream));
  return position < 0 ? std::numeric_limits<ZPOS64_T>::max()
                      : static_cast<ZPOS64_T>(position);
}
long ZCALLBACK seekArchive(voidpf, voidpf stream, ZPOS64_T offset, int origin) {
  int nativeOrigin = SEEK_SET;
  if (origin == ZLIB_FILEFUNC_SEEK_CUR)
    nativeOrigin = SEEK_CUR;
  else if (origin == ZLIB_FILEFUNC_SEEK_END)
    nativeOrigin = SEEK_END;
  return _fseeki64(static_cast<FILE *>(stream), static_cast<__int64>(offset),
                   nativeOrigin);
}
int ZCALLBACK closeArchive(voidpf, voidpf stream) {
  return std::fclose(static_cast<FILE *>(stream));
}
int ZCALLBACK errorArchive(voidpf, voidpf stream) {
  return std::ferror(static_cast<FILE *>(stream));
}

zlib_filefunc64_def wideArchiveFunctions() {
  zlib_filefunc64_def functions{};
  functions.zopen64_file = openWideArchive;
  functions.zread_file = readArchive;
  functions.zwrite_file = writeArchive;
  functions.ztell64_file = tellArchive;
  functions.zseek64_file = seekArchive;
  functions.zclose_file = closeArchive;
  functions.zerror_file = errorArchive;
  return functions;
}

bool readText(const std::filesystem::path &path, std::string &text) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  text.assign(std::istreambuf_iterator<char>(input),
              std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool writeText(const std::filesystem::path &path, const std::string &text,
               std::string &error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output ||
      !output.write(text.data(), static_cast<std::streamsize>(text.size())) ||
      !output.flush()) {
    error = "Could not write " + wideToUtf8(path.wstring());
    return false;
  }
  return true;
}

std::string trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  std::size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])))
    ++start;
  return value.substr(start);
}

bool parseManifest(const std::string &text,
                   std::map<std::string, std::string> &values,
                   std::string &error) {
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty())
      continue;
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      error = "AppAssets.version contains an invalid line";
      return false;
    }
    values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
  }
  const auto hash = values.find("sha256");
  const auto size = values.find("size");
  if (values["format"] != "1" || hash == values.end() ||
      hash->second.size() != 64 || size == values.end()) {
    error = "AppAssets.version is incomplete or unsupported";
    return false;
  }
  if (!std::all_of(hash->second.begin(), hash->second.end(),
                   [](unsigned char c) { return std::isxdigit(c) != 0; })) {
    error = "AppAssets.version contains an invalid SHA-256 value";
    return false;
  }
  return true;
}

bool hashFile(const std::filesystem::path &path, std::string &digest,
              std::string &error) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectSize = 0;
  DWORD hashSize = 0;
  DWORD resultSize = 0;
  std::vector<unsigned char> object;
  std::vector<unsigned char> output;
  bool valid = false;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&objectSize),
                        sizeof(objectSize), &resultSize, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                        reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize),
                        &resultSize, 0) < 0)
    goto cleanup;
  object.resize(objectSize);
  output.resize(hashSize);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0,
                       0) < 0)
    goto cleanup;
  {
    std::ifstream input(path, std::ios::binary);
    // The Windows executable has the conventional 1 MiB stack reservation;
    // keeping this buffer on the stack overflows before a first-run install
    // can even validate its archive.
    std::vector<unsigned char> buffer(1024 * 1024);
    if (!input) {
      error = "Could not open " + wideToUtf8(path.wstring());
      goto cleanup;
    }
    while (input) {
      input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
      const std::streamsize count = input.gcount();
      if (count > 0 &&
          BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) < 0)
        goto cleanup;
    }
    if (!input.eof())
      goto cleanup;
  }
  if (BCryptFinishHash(hash, output.data(), hashSize, 0) < 0)
    goto cleanup;
  {
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (unsigned char byte : output)
      text << std::setw(2) << static_cast<unsigned int>(byte);
    digest = text.str();
  }
  valid = true;

cleanup:
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!valid && error.empty())
    error = "Could not calculate the AppAssets SHA-256 digest";
  return valid;
}

bool safeArchiveName(const std::string &raw, std::filesystem::path &relative,
                     bool &directory, std::string &error) {
  if (raw.empty()) {
    error = "AppAssets.zip contains an empty path";
    return false;
  }
  std::string name = raw;
  std::replace(name.begin(), name.end(), '\\', '/');
  directory = name.back() == '/';
  if (name.front() == '/' || name.find(':') != std::string::npos) {
    error = "AppAssets.zip contains an absolute path: " + name;
    return false;
  }
  std::filesystem::path candidate;
  std::size_t start = 0;
  while (start < name.size()) {
    const std::size_t end = name.find('/', start);
    const std::string component = name.substr(start, end - start);
    if (component == "..") {
      error = "AppAssets.zip attempts to leave the install directory: " + name;
      return false;
    }
    if (!component.empty() && component != ".") {
      const std::wstring wide = utf8ToWide(component);
      if (wide.empty()) {
        error = "AppAssets.zip contains a non-UTF-8 path: " + name;
        return false;
      }
      candidate /= wide;
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  if (candidate.empty()) {
    error = "AppAssets.zip contains an unusable path: " + name;
    return false;
  }
  relative = candidate;
  return true;
}

struct ArchiveEntry {
  std::string rawName;
  std::filesystem::path relativePath;
  std::uint64_t size{};
  bool directory{};
};

bool inventoryArchive(unzFile archive, std::vector<ArchiveEntry> &entries,
                      std::uint64_t &totalBytes, std::string &error) {
  unz_global_info64 global{};
  if (unzGetGlobalInfo64(archive, &global) != UNZ_OK ||
      global.number_entry > 1000000) {
    error = "AppAssets.zip has an invalid directory";
    return false;
  }
  int result = global.number_entry ? unzGoToFirstFile(archive) : UNZ_OK;
  for (ZPOS64_T index = 0; index < global.number_entry; ++index) {
    unz_file_info64 info{};
    if (result != UNZ_OK ||
        unzGetCurrentFileInfo64(archive, &info, nullptr, 0, nullptr, 0, nullptr,
                                0) != UNZ_OK ||
        info.size_filename > 32767) {
      error = "AppAssets.zip contains an invalid entry";
      return false;
    }
    std::vector<char> name(info.size_filename + 1, '\0');
    if (unzGetCurrentFileInfo64(archive, &info, name.data(),
                                static_cast<uLong>(name.size()), nullptr, 0,
                                nullptr, 0) != UNZ_OK) {
      error = "Could not read an AppAssets.zip entry name";
      return false;
    }
    ArchiveEntry entry;
    entry.rawName.assign(name.data(), info.size_filename);
    if (!safeArchiveName(entry.rawName, entry.relativePath, entry.directory,
                         error))
      return false;
    const unsigned int unixType = (info.external_fa >> 16) & 0170000;
    if (unixType == 0120000) {
      error = "AppAssets.zip contains an unsupported symbolic link";
      return false;
    }
    entry.size = info.uncompressed_size;
    if (entry.size > std::numeric_limits<std::uint64_t>::max() - totalBytes) {
      error = "AppAssets.zip expands beyond the supported size";
      return false;
    }
    totalBytes += entry.size;
    entries.push_back(std::move(entry));
    result =
        index + 1 < global.number_entry ? unzGoToNextFile(archive) : UNZ_OK;
  }
  return true;
}

bool extractArchive(const std::filesystem::path &archivePath,
                    const std::filesystem::path &destination,
                    ProgressDialog &progress, std::uint64_t abortAfter,
                    std::string &error) {
  zlib_filefunc64_def functions = wideArchiveFunctions();
  unzFile archive = unzOpen2_64(archivePath.c_str(), &functions);
  if (!archive) {
    error = "AppAssets.zip could not be opened";
    return false;
  }
  std::vector<ArchiveEntry> entries;
  std::uint64_t totalBytes = 0;
  if (!inventoryArchive(archive, entries, totalBytes, error)) {
    unzClose(archive);
    return false;
  }
  if (!entries.empty() && unzGoToFirstFile(archive) != UNZ_OK) {
    unzClose(archive);
    error = "AppAssets.zip could not be rewound";
    return false;
  }
  std::uint64_t complete = 0;
  std::array<char, 256 * 1024> buffer{};
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const ArchiveEntry &entry = entries[index];
    const std::filesystem::path output = destination / entry.relativePath;
    progress.update(complete, totalBytes, entry.relativePath.wstring());
    if (progress.cancelled()) {
      error = "Asset installation was canceled";
      unzClose(archive);
      return false;
    }
    std::error_code fsError;
    if (entry.directory) {
      std::filesystem::create_directories(output, fsError);
    } else {
      std::filesystem::create_directories(output.parent_path(), fsError);
      if (!fsError && unzOpenCurrentFile(archive) != UNZ_OK) {
        error = "Could not decompress " + entry.rawName;
        unzClose(archive);
        return false;
      }
      std::ofstream file(output, std::ios::binary | std::ios::trunc);
      if (fsError || !file) {
        if (!fsError)
          unzCloseCurrentFile(archive);
        error = "Could not create " + wideToUtf8(output.wstring());
        unzClose(archive);
        return false;
      }
      int count = 0;
      while ((count = unzReadCurrentFile(
                  archive, buffer.data(),
                  static_cast<unsigned int>(buffer.size()))) > 0) {
        file.write(buffer.data(), count);
        if (!file) {
          error = "Could not write " + wideToUtf8(output.wstring());
          break;
        }
        complete += static_cast<std::uint64_t>(count);
        progress.update(complete, totalBytes, entry.relativePath.wstring());
        if (abortAfter && complete >= abortAfter)
          TerminateProcess(GetCurrentProcess(), 75);
        if (progress.cancelled()) {
          error = "Asset installation was canceled";
          break;
        }
      }
      file.close();
      const int closeResult = unzCloseCurrentFile(archive);
      if (count < 0 || closeResult != UNZ_OK) {
        if (error.empty())
          error = "AppAssets.zip failed CRC validation for " + entry.rawName;
      }
      if (!error.empty()) {
        unzClose(archive);
        return false;
      }
    }
    if (fsError) {
      error = "Could not create " + wideToUtf8(output.wstring()) + ": " +
              fsError.message();
      unzClose(archive);
      return false;
    }
    if (index + 1 < entries.size() && unzGoToNextFile(archive) != UNZ_OK) {
      error = "AppAssets.zip ended unexpectedly";
      unzClose(archive);
      return false;
    }
  }
  const int closeResult = unzClose(archive);
  if (closeResult != UNZ_OK) {
    error = "AppAssets.zip could not be closed cleanly";
    return false;
  }
  progress.update(totalBytes, totalBytes, L"Finishing installation");
  return true;
}

bool validInstall(const std::filesystem::path &directory,
                  const std::string &manifest) {
  std::string installed;
  return std::filesystem::is_regular_file(directory / L"start.lua") &&
         readText(directory / InstalledManifestName, installed) &&
         installed == manifest;
}

bool renameDirectory(const std::filesystem::path &source,
                     const std::filesystem::path &destination,
                     std::string &error) {
  std::error_code fsError;
  std::filesystem::rename(source, destination, fsError);
  if (!fsError)
    return true;
  error = "Could not activate installed assets: " + fsError.message();
  return false;
}

} // namespace

bool getDefaultBootstrapOptions(BootstrapOptions &options, std::string &error) {
  std::vector<wchar_t> executable(4096);
  while (true) {
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length) {
      error = "Could not locate the installed executable: " +
              windowsError(GetLastError());
      return false;
    }
    if (length < executable.size() - 1) {
      options.bundleDirectory =
          std::filesystem::path(std::wstring(executable.data(), length))
              .parent_path();
      break;
    }
    executable.resize(executable.size() * 2);
  }
  PWSTR localAppData = nullptr;
  const HRESULT result = SHGetKnownFolderPath(
      FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData);
  if (FAILED(result) || !localAppData) {
    error = "Could not locate the current user's Local AppData directory";
    if (localAppData)
      CoTaskMemFree(localAppData);
    return false;
  }
  options.dataRoot = std::filesystem::path(localAppData) / AppDirectoryName;
  CoTaskMemFree(localAppData);
  return true;
}

static bool prepareBundledAssetsInternal(const BootstrapOptions &options,
                                         RuntimePaths &paths,
                                         std::string &error) {
  const std::filesystem::path archive = options.bundleDirectory / ArchiveName;
  const std::filesystem::path manifestPath =
      options.bundleDirectory / ManifestName;
  const std::filesystem::path install = options.dataRoot / L"install";
  const std::filesystem::path external = options.dataRoot / L"external";
  const std::filesystem::path staging = options.dataRoot / L"install.new";
  const std::filesystem::path previous = options.dataRoot / L"install.old";
  paths.installRoot = wideToUtf8(install.wstring());
  paths.externalRoot = wideToUtf8(external.wstring());

  std::error_code fsError;
  std::filesystem::create_directories(options.dataRoot, fsError);
  std::filesystem::create_directories(external, fsError);
  if (fsError) {
    error =
        "Could not create the application data directory: " + fsError.message();
    return false;
  }
  ScopedMutex lock(error);
  if (!lock)
    return false;

  std::string manifest;
  std::map<std::string, std::string> values;
  if (!readText(manifestPath, manifest) ||
      !parseManifest(manifest, values, error)) {
    if (error.empty())
      error = "AppAssets.version is missing beside the executable";
    appendLog(options.dataRoot, error);
    return false;
  }
  if (!std::filesystem::exists(install) && validInstall(staging, manifest)) {
    appendLog(options.dataRoot, "Recovering a completed staged installation");
    std::filesystem::remove_all(previous, fsError);
    if (!renameDirectory(staging, install, error))
      return false;
  } else if (!std::filesystem::exists(install) &&
             std::filesystem::exists(previous)) {
    appendLog(
        options.dataRoot,
        "Restoring the previous installation after an interrupted update");
    std::filesystem::remove_all(staging, fsError);
    if (!renameDirectory(previous, install, error))
      return false;
  }

  if (validInstall(install, manifest)) {
    std::filesystem::remove_all(staging, fsError);
    std::filesystem::remove_all(previous, fsError);
    appendLog(options.dataRoot, "Bundled assets are current");
    return true;
  }

  // Hash the large immutable archive only when extraction is actually needed.
  // A current install was already CRC/SHA checked before its marker was
  // published, so ordinary launches remain fast and still work if Program
  // Files is temporarily unavailable after process startup.
  if (!std::filesystem::is_regular_file(archive)) {
    error = "AppAssets.zip is missing beside the executable";
    appendLog(options.dataRoot, error);
    return false;
  }
  std::uint64_t expectedSize = 0;
  std::size_t parsedSize = 0;
  try {
    expectedSize = std::stoull(values["size"], &parsedSize);
  } catch (...) {
    error = "AppAssets.version contains an invalid archive size";
    return false;
  }
  if (parsedSize != values["size"].size()) {
    error = "AppAssets.version contains an invalid archive size";
    return false;
  }
  fsError.clear();
  if (std::filesystem::file_size(archive, fsError) != expectedSize || fsError) {
    error = "AppAssets.zip does not match the recorded size";
    appendLog(options.dataRoot, error);
    return false;
  }
  std::string archiveHash;
  if (!hashFile(archive, archiveHash, error) ||
      _stricmp(archiveHash.c_str(), values["sha256"].c_str()) != 0) {
    if (error.empty())
      error = "AppAssets.zip failed SHA-256 validation";
    appendLog(options.dataRoot, error);
    return false;
  }

  appendLog(options.dataRoot, "Installing bundled AppAssets.zip");
  std::filesystem::remove_all(staging, fsError);
  fsError.clear();
  std::filesystem::create_directories(staging, fsError);
  if (fsError) {
    error =
        "Could not create the asset staging directory: " + fsError.message();
    return false;
  }
  ProgressDialog progress(options.showProgress);
  if (!extractArchive(archive, staging, progress,
                      options.abortProcessAfterBytes, error) ||
      !writeText(staging / InstalledManifestName, manifest, error) ||
      !validInstall(staging, manifest)) {
    if (error.empty())
      error = "The extracted application assets are incomplete";
    appendLog(options.dataRoot, error);
    std::filesystem::remove_all(staging, fsError);
    return false;
  }

  std::filesystem::remove_all(previous, fsError);
  if (std::filesystem::exists(install)) {
    if (!renameDirectory(install, previous, error)) {
      std::filesystem::remove_all(staging, fsError);
      return false;
    }
  }
  if (!renameDirectory(staging, install, error)) {
    if (std::filesystem::exists(previous)) {
      std::string restoreError;
      renameDirectory(previous, install, restoreError);
    }
    appendLog(options.dataRoot, error);
    return false;
  }
  std::filesystem::remove_all(previous, fsError);
  appendLog(options.dataRoot, "Bundled assets installed successfully");
  return true;
}

bool prepareBundledAssets(const BootstrapOptions &options, RuntimePaths &paths,
                          std::string &error) {
  try {
    return prepareBundledAssetsInternal(options, paths, error);
  } catch (const std::exception &exception) {
    error = "Windows could not access the asset installation: " +
            std::string(exception.what());
    appendLog(options.dataRoot, error);
    return false;
  }
}

void showBootstrapError(const std::string &error) {
  const std::wstring message = utf8ToWide(
      "PlaygroundOSS SIF could not prepare its application assets.\n\n" +
      error +
      "\n\nSee %LOCALAPPDATA%\\PlaygroundOSS-SIF\\logs\\bootstrap.log for "
      "details.");
  MessageBoxW(nullptr, message.c_str(), L"PlaygroundOSS SIF",
              MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

void attachParentConsole() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS))
    return;
  FILE *stream = nullptr;
  freopen_s(&stream, "CONOUT$", "w", stdout);
  freopen_s(&stream, "CONOUT$", "w", stderr);
  freopen_s(&stream, "CONIN$", "r", stdin);
  SetConsoleOutputCP(CP_UTF8);
}

} // namespace playground::windows

#endif
