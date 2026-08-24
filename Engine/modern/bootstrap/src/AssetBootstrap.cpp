#include "Playground/Bootstrap/AssetBootstrap.h"

#include "unzip.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace playground::bootstrap {
namespace {

constexpr const char *MarkerName = ".appassets-version";
constexpr const char *GenerationPrefix = "install-";
constexpr const char *StagingPrefix = ".staging-";
constexpr const char *CompactStagingName = ".staging";

struct InternalLayout {
  std::string generation;
  std::string staging;
  bool compact{};
};

bool internalLayout(const AssetBootstrapOptions &options,
                    InternalLayout &layout, std::string &error) {
  layout.generation = std::string(GenerationPrefix) + options.metadata.sha256;
  layout.staging = std::string(StagingPrefix) + options.metadata.sha256;
  if (!options.maximumInternalNameLength ||
      (layout.generation.size() <= options.maximumInternalNameLength &&
       layout.staging.size() <= options.maximumInternalNameLength))
    return true;

  // Preserve all 256 bits in the immutable generation name. Staging is a
  // single per-bundle workspace: it is never valid without its final marker,
  // and prepareAssetBundle is deliberately single-writer.
  layout.generation = options.metadata.sha256;
  layout.staging = CompactStagingName;
  layout.compact = true;
  if (layout.generation.size() <= options.maximumInternalNameLength &&
      layout.staging.size() <= options.maximumInternalNameLength)
    return true;
  error = "Asset bootstrap internal-name limit cannot represent SHA-256";
  return false;
}

bool hexadecimalDigestName(const std::string &name) {
  return name.size() == 64 &&
         std::all_of(name.begin(), name.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
         });
}

std::filesystem::path storagePath(const AssetBootstrapOptions &options,
                                  const std::filesystem::path &path) {
  return options.mapStoragePath ? options.mapStoragePath(path) : path;
}

bool isRegularFile(const AssetBootstrapOptions &options,
                   const std::filesystem::path &path) {
  return options.isRegularFile ? options.isRegularFile(path)
                               : std::filesystem::is_regular_file(path);
}

std::string trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])))
    ++first;
  return value.substr(first);
}

bool decimal(const std::string &value, std::uint64_t &result) {
  if (value.empty() ||
      !std::all_of(value.begin(), value.end(),
                   [](unsigned char c) { return std::isdigit(c) != 0; }))
    return false;
  try {
    std::size_t parsed = 0;
    result = std::stoull(value, &parsed, 10);
    return parsed == value.size();
  } catch (...) {
    return false;
  }
}

bool validUtf8(const std::string &value) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(value.data());
  std::size_t i = 0;
  while (i < value.size()) {
    unsigned char lead = bytes[i++];
    if (lead < 0x80) {
      if (lead == 0)
        return false;
      continue;
    }
    unsigned int remaining = 0;
    std::uint32_t code = 0;
    if ((lead & 0xe0) == 0xc0) {
      remaining = 1;
      code = lead & 0x1f;
    } else if ((lead & 0xf0) == 0xe0) {
      remaining = 2;
      code = lead & 0x0f;
    } else if ((lead & 0xf8) == 0xf0) {
      remaining = 3;
      code = lead & 0x07;
    } else {
      return false;
    }
    if (i + remaining > value.size())
      return false;
    for (unsigned int n = 0; n < remaining; ++n) {
      unsigned char continuation = bytes[i++];
      if ((continuation & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (continuation & 0x3f);
    }
    if ((remaining == 1 && code < 0x80) || (remaining == 2 && code < 0x800) ||
        (remaining == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}

bool archivePath(const std::string &raw, std::filesystem::path &relative,
                 bool &directory, std::string &error) {
  if (raw.empty() || !validUtf8(raw)) {
    error = "AppAssets.zip contains an empty or invalid UTF-8 path";
    return false;
  }
  std::string name = raw;
  std::replace(name.begin(), name.end(), '\\', '/');
  directory = name.back() == '/';
  if (name.front() == '/' || name.find(':') != std::string::npos) {
    error = "AppAssets.zip contains an absolute path: " + name;
    return false;
  }
  std::filesystem::path path;
  std::size_t start = 0;
  while (start < name.size()) {
    const std::size_t end = name.find('/', start);
    const std::string component = name.substr(start, end - start);
    if (component == "." || component == ".." ||
        (component.empty() && end != name.size() - 1)) {
      error = "AppAssets.zip contains an unsafe path: " + name;
      return false;
    }
    if (!component.empty())
      path /= std::filesystem::path(component);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  if (path.empty()) {
    error = "AppAssets.zip contains an unusable path: " + name;
    return false;
  }
  relative = path;
  return true;
}

bool normalizedArchiveKey(const std::filesystem::path &path, std::string &key,
                          std::string &error) {
  key.clear();
  for (const auto &part : path) {
    std::string component = part.string();
    if (component.empty() || component.back() == ' ' ||
        component.back() == '.') {
      error = "AppAssets.zip contains a path with an ambiguous trailing "
              "character: " +
              path.generic_string();
      return false;
    }
    if (!key.empty())
      key.push_back('/');
    for (unsigned char byte : component) {
      if (byte < 0x20 || byte == 0x7f) {
        error = "AppAssets.zip contains a control character in a path";
        return false;
      }
      key.push_back(byte < 0x80 ? static_cast<char>(std::tolower(byte))
                                : static_cast<char>(byte));
    }
  }
  return true;
}

struct Entry {
  std::string name;
  std::filesystem::path relative;
  std::uint64_t size{};
  bool directory{};
};

bool inventory(unzFile archive, const AssetBootstrapOptions &options,
               std::vector<Entry> &entries, std::uint64_t &expanded,
               std::string &error) {
  unz_global_info64 global{};
  if (unzGetGlobalInfo64(archive, &global) != UNZ_OK ||
      global.number_entry > options.maximumEntries) {
    error = "AppAssets.zip contains too many or an invalid number of entries";
    return false;
  }
  std::set<std::string> normalized;
  int cursor = global.number_entry ? unzGoToFirstFile(archive) : UNZ_OK;
  for (ZPOS64_T index = 0; index < global.number_entry; ++index) {
    unz_file_info64 info{};
    if (cursor != UNZ_OK ||
        unzGetCurrentFileInfo64(archive, &info, nullptr, 0, nullptr, 0, nullptr,
                                0) != UNZ_OK ||
        info.size_filename == 0 || info.size_filename > 32767) {
      error = "AppAssets.zip contains an invalid directory entry";
      return false;
    }
    std::vector<char> name(info.size_filename + 1, '\0');
    if (unzGetCurrentFileInfo64(archive, &info, name.data(),
                                static_cast<uLong>(name.size()), nullptr, 0,
                                nullptr, 0) != UNZ_OK) {
      error = "AppAssets.zip entry name could not be read";
      return false;
    }
    Entry entry;
    entry.name.assign(name.data(), info.size_filename);
    if (!archivePath(entry.name, entry.relative, entry.directory, error))
      return false;
    const unsigned int unixType = (info.external_fa >> 16) & 0170000;
    if (unixType == 0120000) {
      error = "AppAssets.zip contains a symbolic link: " + entry.name;
      return false;
    }
    std::string key;
    if (!normalizedArchiveKey(entry.relative, key, error))
      return false;
    if (!normalized.insert(key).second) {
      error = "AppAssets.zip contains a duplicate or case-colliding path: " +
              entry.name;
      return false;
    }
    entry.size = info.uncompressed_size;
    if (entry.size > options.maximumExpandedBytes - expanded) {
      error = "AppAssets.zip exceeds the configured expanded-size limit";
      return false;
    }
    expanded += entry.size;
    entries.push_back(std::move(entry));
    if (options.progress &&
        (index % 64 == 63 || index + 1 == global.number_entry) &&
        !options.progress(index + 1, global.number_entry,
                          "Inspecting AppAssets.zip")) {
      error = "AppAssets.zip inspection was canceled";
      return false;
    }
    cursor =
        index + 1 < global.number_entry ? unzGoToNextFile(archive) : UNZ_OK;
  }
  return true;
}

bool readFile(const std::filesystem::path &path, std::string &value) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  value.assign(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool writeFile(const std::filesystem::path &path, const std::string &value,
               std::string &error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output ||
      !output.write(value.data(), static_cast<std::streamsize>(value.size())) ||
      !output.flush()) {
    error = "Could not write asset-generation marker: " + path.string();
    return false;
  }
  output.close();
  if (!output) {
    error = "Could not close asset-generation marker: " + path.string();
    return false;
  }
  return true;
}

bool validGeneration(const std::filesystem::path &root,
                     const AssetBundleMetadata &metadata,
                     const AssetBootstrapOptions &options) {
  std::string marker;
  // Keep the composed and mapped paths alive as named objects. GCC's PPC
  // libstdc++ filesystem implementation can corrupt the component-list
  // ownership when a composed path is returned through std::function and
  // destroyed as a nested full-expression temporary.
  const std::filesystem::path markerLogical = root / MarkerName;
  const std::filesystem::path markerPhysical =
      storagePath(options, markerLogical);
  if (!readFile(markerPhysical, marker) || marker != metadata.canonicalText)
    return false;
  for (const auto &required : metadata.requiredFiles) {
    const std::filesystem::path requiredLogical = root / required;
    const std::filesystem::path requiredPhysical =
        storagePath(options, requiredLogical);
    if (!isRegularFile(options, requiredPhysical))
      return false;
  }
  return true;
}

bool extract(unzFile archive, const std::vector<Entry> &entries,
             std::uint64_t total, const AssetBootstrapOptions &options,
             const std::filesystem::path &destination, std::string &error) {
  if (!entries.empty() && unzGoToFirstFile(archive) != UNZ_OK) {
    error = "AppAssets.zip could not be rewound";
    return false;
  }
  // Extraction runs on the platform main thread so lifecycle callbacks can
  // be pumped between chunks. Keep this large throughput buffer off that
  // thread's relatively small console stack.
  std::vector<char> buffer(256 * 1024);
  std::uint64_t complete = 0;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const Entry &entry = entries[index];
    const std::filesystem::path output =
        storagePath(options, destination / entry.relative);
    if (output.empty()) {
      error = "Target storage cannot represent AppAssets path: " +
              entry.name;
      return false;
    }
    std::error_code fsError;
    if (entry.directory) {
      std::filesystem::create_directories(output, fsError);
    } else {
      std::filesystem::create_directories(output.parent_path(), fsError);
      if (!fsError && unzOpenCurrentFile(archive) != UNZ_OK) {
        error = "Could not open compressed entry: " + entry.name;
        return false;
      }
      errno = 0;
      std::ofstream file(output, std::ios::binary | std::ios::trunc);
      const int openError = errno;
      if (fsError || !file) {
        if (!fsError)
          unzCloseCurrentFile(archive);
        if (fsError) {
          error = "Could not create extracted file (filesystem error: " +
                  fsError.message() + "): " + output.string();
        } else {
          error = "Could not create extracted file (errno=" +
                  std::to_string(openError) + ": " +
                  std::strerror(openError);
          if (options.describeFileError) {
            const std::string nativeError = options.describeFileError();
            if (!nativeError.empty())
              error += ", " + nativeError;
          }
          error += "): " + output.string();
        }
        return false;
      }
      std::uint64_t entryBytes = 0;
      int count = 0;
      while ((count = unzReadCurrentFile(
                  archive, buffer.data(),
                  static_cast<unsigned int>(buffer.size()))) > 0) {
        file.write(buffer.data(), count);
        if (!file) {
          error = "Could not write extracted file: " + output.string();
          break;
        }
        entryBytes += static_cast<std::uint64_t>(count);
        complete += static_cast<std::uint64_t>(count);
        if (options.progress &&
            !options.progress(complete, total, entry.name)) {
          error = "AppAssets.zip extraction was canceled";
          break;
        }
      }
      file.close();
      const int closed = unzCloseCurrentFile(archive);
      if (count < 0 || closed != UNZ_OK || entryBytes != entry.size) {
        if (error.empty())
          error = "AppAssets.zip failed size or CRC validation: " + entry.name;
      }
      if (!error.empty())
        return false;
    }
    if (fsError) {
      error = "Could not create extracted path: " + output.string();
      return false;
    }
    if (options.commitStorage && options.extractionCommitInterval != 0 &&
        (index + 1) % options.extractionCommitInterval == 0) {
      std::string commitError;
      if (!options.commitStorage(commitError)) {
        error = "Could not commit asset extraction checkpoint";
        if (!commitError.empty())
          error += ": " + commitError;
        return false;
      }
    }
    if (index + 1 < entries.size() && unzGoToNextFile(archive) != UNZ_OK) {
      error = "AppAssets.zip ended before its directory was exhausted";
      return false;
    }
  }
  return true;
}

bool commit(const AssetBootstrapOptions &options, std::string &error) {
  if (!options.commitStorage)
    return true;
  return options.commitStorage(error);
}

bool checkpoint(const AssetBootstrapOptions &options, const char *name,
                std::string &error) {
  if (!options.publicationCheckpoint || options.publicationCheckpoint(name))
    return true;
  error = std::string("Asset publication stopped at checkpoint: ") + name;
  return false;
}

bool removeTree(const AssetBootstrapOptions &options,
                const std::filesystem::path &path, std::string &error) {
  if (options.removeTree)
    return options.removeTree(path, error);
  std::error_code fsError;
  std::filesystem::remove_all(path, fsError);
  if (!fsError)
    return true;
  error = "Could not remove asset tree: " + fsError.message();
  return false;
}

bool publishGeneration(const AssetBootstrapOptions &options,
                       const std::filesystem::path &staging,
                       const std::filesystem::path &generation,
                       std::string &error) {
  if (options.publishGeneration)
    return options.publishGeneration(staging, generation, error);
  std::error_code fsError;
  if (std::filesystem::exists(generation) &&
      !removeTree(options, generation, error))
    return false;
  fsError.clear();
  std::filesystem::rename(staging, generation, fsError);
  if (!fsError)
    return true;
  error = "Could not publish asset generation: " + fsError.message();
  return false;
}

bool completedGenerationExists(const AssetBootstrapOptions &options,
                               const std::filesystem::path &cacheRoot) {
  (void)options;
  std::error_code fsError;
  for (const auto &entry :
       std::filesystem::directory_iterator(cacheRoot, fsError)) {
    if (fsError)
      return true; // Be conservative: an unreadable root must not be reset.
    const std::string name = entry.path().filename().string();
    if (name.rfind(GenerationPrefix, 0) != 0 &&
        !hexadecimalDigestName(name))
      continue;
    // Be conservative at the destructive recovery boundary. Generation
    // names are published only after their marker is written, but a legacy
    // over-limit basename may itself make the marker impossible to query.
    // Any generation-shaped entry therefore blocks whole-CacheStorage reset;
    // ordinary validation still checks its marker before using it.
    return true;
  }
  return fsError ? true : false;
}

bool removeIncompleteStaging(const AssetBootstrapOptions &options,
                             const std::filesystem::path &cacheRoot,
                             const std::filesystem::path &currentStaging,
                             std::string &error) {
  std::error_code fsError;
  std::vector<std::filesystem::path> stale;
  for (const auto &entry :
       std::filesystem::directory_iterator(cacheRoot, fsError)) {
    if (fsError)
      break;
    const std::string name = entry.path().filename().string();
    if (entry.path() == currentStaging || name == CompactStagingName ||
        name.rfind(StagingPrefix, 0) == 0)
      stale.push_back(entry.path());
  }
  if (fsError) {
    error = "Could not inspect incomplete asset staging: " +
            fsError.message();
    return false;
  }
  for (const auto &path : stale) {
    std::string removalError;
    if (removeTree(options, path, removalError))
      continue;
    if (!options.resetReplaceableStorage ||
        completedGenerationExists(options, cacheRoot)) {
      error = removalError;
      return false;
    }
    if (!options.resetReplaceableStorage(error))
      return false;
    fsError.clear();
    std::filesystem::create_directories(cacheRoot, fsError);
    if (fsError) {
      error = "Could not recreate asset root after replaceable-storage "
              "recovery: " + fsError.message();
      return false;
    }
    return true;
  }
  return true;
}

} // namespace

bool parseAssetBundleMetadata(const std::string &text,
                              AssetBundleMetadata &metadata,
                              std::string &error) {
  std::istringstream lines(text);
  std::map<std::string, std::string> singleton;
  std::vector<std::filesystem::path> required;
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty())
      continue;
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      error = "AppAssets metadata contains an invalid line";
      return false;
    }
    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));
    if (key == "required") {
      std::filesystem::path path;
      bool directory = false;
      if (!archivePath(value, path, directory, error) || directory) {
        if (error.empty())
          error = "AppAssets metadata contains an invalid required file";
        return false;
      }
      required.push_back(std::move(path));
    } else if (!singleton.emplace(key, value).second) {
      error = "AppAssets metadata repeats field: " + key;
      return false;
    }
  }
  if (singleton["format"] != "2" || singleton["sha256"].size() != 64 ||
      !std::all_of(singleton["sha256"].begin(), singleton["sha256"].end(),
                   [](unsigned char c) { return std::isxdigit(c) != 0; }) ||
      !decimal(singleton["size"], metadata.archiveSize) ||
      !decimal(singleton["expanded_size"], metadata.expandedSize) ||
      !decimal(singleton["entry_count"], metadata.entryCount) ||
      required.empty()) {
    error = "AppAssets metadata is incomplete or unsupported";
    return false;
  }
  metadata.canonicalText = text;
  metadata.sha256 = singleton["sha256"];
  std::transform(
      metadata.sha256.begin(), metadata.sha256.end(), metadata.sha256.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  metadata.requiredFiles = std::move(required);
  return true;
}

bool prepareAssetBundle(const AssetBootstrapOptions &options,
                        AssetBootstrapResult &result, std::string &error) {
  if (!options.hashFile || options.metadata.sha256.size() != 64 ||
      options.metadata.canonicalText.empty() ||
      options.metadata.requiredFiles.empty()) {
    error = "Asset bootstrap options are incomplete";
    return false;
  }
  InternalLayout layout;
  if (!internalLayout(options, layout, error))
    return false;
  const std::filesystem::path cacheRoot = options.cacheRoot;
  const std::filesystem::path generation = cacheRoot / layout.generation;
  const std::filesystem::path staging = cacheRoot / layout.staging;
  const std::filesystem::path physicalCacheRoot =
      storagePath(options, cacheRoot);
  const std::filesystem::path physicalGeneration =
      storagePath(options, generation);
  const std::filesystem::path physicalStaging =
      storagePath(options, staging);
  if (physicalCacheRoot.empty() || physicalGeneration.empty() ||
      physicalStaging.empty()) {
    error = "Target storage cannot represent the asset-generation layout";
    return false;
  }
  // Consumers resolve logical asset names through the same mapper. Never
  // expose a physical codec representation as an application namespace.
  result.installRoot = generation;
  result.installed = false;
  result.removedStaleGenerations = 0;
  std::error_code fsError;
  std::filesystem::create_directories(physicalCacheRoot, fsError);
  if (fsError) {
    error = "Could not create CacheStorage asset root: " + fsError.message();
    return false;
  }
  const bool generationValid =
      validGeneration(generation, options.metadata, options);
  if (!generationValid) {
    if (!isRegularFile(options, options.archive)) {
      error = "Bundled AppAssets.zip is missing";
      return false;
    }
    const std::uint64_t archiveSize =
        std::filesystem::file_size(options.archive, fsError);
    if (fsError || archiveSize != options.metadata.archiveSize) {
      error = "Bundled AppAssets.zip has the wrong size";
      return false;
    }
    std::string digest;
    if (!options.hashFile(options.archive, digest, error))
      return false;
    std::transform(
        digest.begin(), digest.end(), digest.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (digest != options.metadata.sha256) {
      error = "Bundled AppAssets.zip failed SHA-256 validation";
      return false;
    }
    unzFile archive = options.openArchive
                          ? static_cast<unzFile>(
                                options.openArchive(options.archive, error))
                          : unzOpen64(options.archive.string().c_str());
    if (!archive) {
      if (error.empty())
        error = "Bundled AppAssets.zip could not be opened";
      return false;
    }
    std::vector<Entry> entries;
    std::uint64_t expanded = 0;
    bool valid = inventory(archive, options, entries, expanded, error);
    if (valid && (expanded != options.metadata.expandedSize ||
                  entries.size() != options.metadata.entryCount)) {
      error = "AppAssets.zip directory does not match its build metadata";
      valid = false;
    }
    if (valid && !options.publishWithCompletionMarker)
      valid = removeIncompleteStaging(options, physicalCacheRoot,
                                      physicalStaging, error);
    const std::filesystem::path extractionRoot =
        options.publishWithCompletionMarker ? generation : staging;
    const std::filesystem::path physicalExtractionRoot =
        options.publishWithCompletionMarker ? physicalGeneration
                                            : physicalStaging;
    // In resumable in-place mode, invalidate an older complete generation
    // before touching any member. A power loss can then never leave a stale
    // marker blessing a partially overwritten tree.
    if (options.publishWithCompletionMarker) {
      const std::filesystem::path completionMarker =
          storagePath(options, generation / MarkerName);
      if (!removeTree(options, completionMarker, error))
        return false;
    }
    if (!removeTree(options, physicalExtractionRoot, error))
      return false;
    fsError.clear();
    if (valid) {
      std::filesystem::create_directories(physicalExtractionRoot, fsError);
      if (fsError) {
        error =
            "Could not create asset staging generation: " + fsError.message();
        valid = false;
      }
    }
    if (valid)
      valid = extract(archive, entries, expanded, options, extractionRoot,
                      error);
    const int archiveClosed = unzClose(archive);
    if (valid && archiveClosed != UNZ_OK) {
      error = "AppAssets.zip could not be closed cleanly";
      valid = false;
    }
    // In marker-publication mode the marker is deliberately the final write.
    // Before it exists, no interrupted tree is a valid generation.
    if (valid && !options.publishWithCompletionMarker) {
      const std::filesystem::path marker =
          storagePath(options, staging / MarkerName);
      valid = !marker.empty() &&
              writeFile(marker, options.metadata.canonicalText, error);
    }
    if (valid && !options.publishWithCompletionMarker &&
        !validGeneration(staging, options.metadata, options)) {
      error = "Extracted AppAssets generation is incomplete";
      valid = false;
    }
    if (valid)
      valid = commit(options, error);
    if (valid)
      valid = checkpoint(options, "staging-committed", error);
    if (valid && options.publishWithCompletionMarker) {
      const std::filesystem::path marker =
          storagePath(options, generation / MarkerName);
      valid = !marker.empty() &&
              writeFile(marker, options.metadata.canonicalText, error);
    }
    if (valid && !options.publishWithCompletionMarker)
      valid = publishGeneration(options, physicalStaging, physicalGeneration,
                                error);
    if (valid)
      valid = checkpoint(options,
                         options.publishWithCompletionMarker
                             ? "generation-marked"
                             : "generation-renamed",
                         error);
    if (valid)
      valid = commit(options, error);
    if (valid)
      valid = checkpoint(options, "generation-committed", error);
    if (!valid) {
      std::string ignored;
      removeTree(options, physicalExtractionRoot, ignored);
      return false;
    }
    result.installed = true;
  }


  if (options.cleanupStaleGenerations) {
    if (!options.cleanupStaleGenerations(
            physicalCacheRoot, physicalGeneration,
            result.removedStaleGenerations, error))
      return false;
  } else {
    for (const auto &entry :
         std::filesystem::directory_iterator(physicalCacheRoot, fsError)) {
      if (fsError)
        break;
      const std::string name = entry.path().filename().string();
      if (entry.path() == physicalGeneration ||
          (name.rfind(GenerationPrefix, 0) != 0 &&
           name.rfind(StagingPrefix, 0) != 0 &&
           name != CompactStagingName &&
           !(layout.compact && hexadecimalDigestName(name))))
        continue;
      if (!removeTree(options, entry.path(), error))
        return false;
      ++result.removedStaleGenerations;
    }
  }
  if (result.removedStaleGenerations && !commit(options, error))
    return false;
  return validGeneration(generation, options.metadata, options);
}

} // namespace playground::bootstrap
