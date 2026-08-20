#include "Playground/Bootstrap/AssetBootstrap.h"

#include "unzip.h"

#include <algorithm>
#include <array>
#include <cctype>
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
                     const AssetBundleMetadata &metadata) {
  std::string marker;
  if (!readFile(root / MarkerName, marker) || marker != metadata.canonicalText)
    return false;
  for (const auto &required : metadata.requiredFiles) {
    if (!std::filesystem::is_regular_file(root / required))
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
  std::array<char, 256 * 1024> buffer{};
  std::uint64_t complete = 0;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const Entry &entry = entries[index];
    const std::filesystem::path output = destination / entry.relative;
    std::error_code fsError;
    if (entry.directory) {
      std::filesystem::create_directories(output, fsError);
    } else {
      std::filesystem::create_directories(output.parent_path(), fsError);
      if (!fsError && unzOpenCurrentFile(archive) != UNZ_OK) {
        error = "Could not open compressed entry: " + entry.name;
        return false;
      }
      std::ofstream file(output, std::ios::binary | std::ios::trunc);
      if (fsError || !file) {
        if (!fsError)
          unzCloseCurrentFile(archive);
        error = "Could not create extracted file: " + output.string();
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

bool publishGeneration(const AssetBootstrapOptions &options,
                       const std::filesystem::path &staging,
                       const std::filesystem::path &generation,
                       std::string &error) {
  if (options.publishGeneration)
    return options.publishGeneration(staging, generation, error);
  std::error_code fsError;
  if (std::filesystem::exists(generation))
    std::filesystem::remove_all(generation, fsError);
  fsError.clear();
  std::filesystem::rename(staging, generation, fsError);
  if (!fsError)
    return true;
  error = "Could not publish asset generation: " + fsError.message();
  return false;
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
  const std::filesystem::path generation =
      options.cacheRoot /
      (std::string(GenerationPrefix) + options.metadata.sha256);
  const std::filesystem::path staging =
      options.cacheRoot /
      (std::string(StagingPrefix) + options.metadata.sha256);
  result.installRoot = generation;
  result.installed = false;
  result.removedStaleGenerations = 0;
  std::error_code fsError;
  std::filesystem::create_directories(options.cacheRoot, fsError);
  if (fsError) {
    error = "Could not create CacheStorage asset root: " + fsError.message();
    return false;
  }
  if (!validGeneration(generation, options.metadata)) {
    if (!std::filesystem::is_regular_file(options.archive)) {
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
    unzFile archive = unzOpen64(options.archive.string().c_str());
    if (!archive) {
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
    std::filesystem::remove_all(staging, fsError);
    fsError.clear();
    if (valid) {
      std::filesystem::create_directories(staging, fsError);
      if (fsError) {
        error =
            "Could not create asset staging generation: " + fsError.message();
        valid = false;
      }
    }
    if (valid)
      valid = extract(archive, entries, expanded, options, staging, error);
    const int archiveClosed = unzClose(archive);
    if (valid && archiveClosed != UNZ_OK) {
      error = "AppAssets.zip could not be closed cleanly";
      valid = false;
    }
    if (valid)
      valid = writeFile(staging / MarkerName, options.metadata.canonicalText,
                        error);
    if (valid && !validGeneration(staging, options.metadata)) {
      error = "Extracted AppAssets generation is incomplete";
      valid = false;
    }
    if (valid)
      valid = commit(options, error);
    if (valid)
      valid = checkpoint(options, "staging-committed", error);
    if (valid)
      valid = publishGeneration(options, staging, generation, error);
    if (valid)
      valid = checkpoint(options, "generation-renamed", error);
    if (valid)
      valid = commit(options, error);
    if (valid)
      valid = checkpoint(options, "generation-committed", error);
    if (!valid) {
      std::filesystem::remove_all(staging, fsError);
      return false;
    }
    result.installed = true;
  }

  for (const auto &entry :
       std::filesystem::directory_iterator(options.cacheRoot, fsError)) {
    if (fsError)
      break;
    const std::string name = entry.path().filename().string();
    if (entry.path() == generation || (name.rfind(GenerationPrefix, 0) != 0 &&
                                       name.rfind(StagingPrefix, 0) != 0))
      continue;
    std::error_code removeError;
    std::filesystem::remove_all(entry.path(), removeError);
    if (!removeError)
      ++result.removedStaleGenerations;
  }
  if (result.removedStaleGenerations && !commit(options, error))
    return false;
  return validGeneration(generation, options.metadata);
}

} // namespace playground::bootstrap
