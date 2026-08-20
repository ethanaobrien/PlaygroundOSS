#ifndef PLAYGROUND_BOOTSTRAP_ASSET_BOOTSTRAP_H
#define PLAYGROUND_BOOTSTRAP_ASSET_BOOTSTRAP_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace playground::bootstrap {

struct AssetBundleMetadata {
  std::string canonicalText;
  std::string sha256;
  std::uint64_t archiveSize{};
  std::uint64_t expandedSize{};
  std::uint64_t entryCount{};
  std::vector<std::filesystem::path> requiredFiles;
};

using HashFile = std::function<bool(const std::filesystem::path &,
                                    std::string &, std::string &)>;
using CommitStorage = std::function<bool(std::string &)>;
using Progress =
    std::function<bool(std::uint64_t, std::uint64_t, const std::string &)>;
using PublicationCheckpoint = std::function<bool(const std::string &)>;
using PublishGeneration =
    std::function<bool(const std::filesystem::path &,
                       const std::filesystem::path &, std::string &)>;

struct AssetBootstrapOptions {
  std::filesystem::path archive;
  std::filesystem::path cacheRoot;
  AssetBundleMetadata metadata;
  HashFile hashFile;
  CommitStorage commitStorage;
  Progress progress;
  // Optional filesystem-specific publication operation. The default is an
  // atomic directory rename. Compatibility hosts whose filesystem service
  // lacks RenameDirectory may copy the already validated content-addressed
  // staging tree, validate it again, and remove staging instead.
  PublishGeneration publishGeneration;
  // Optional test/telemetry boundary. Production leaves this empty; the host
  // probe terminates its process here to model power loss between commits.
  PublicationCheckpoint publicationCheckpoint;
  std::uint64_t maximumEntries{1000000};
  std::uint64_t maximumExpandedBytes{UINT64_MAX};
};

struct AssetBootstrapResult {
  std::filesystem::path installRoot;
  bool installed{};
  std::size_t removedStaleGenerations{};
};

bool parseAssetBundleMetadata(const std::string &, AssetBundleMetadata &,
                              std::string &error);
bool prepareAssetBundle(const AssetBootstrapOptions &, AssetBootstrapResult &,
                        std::string &error);

} // namespace playground::bootstrap

#endif
