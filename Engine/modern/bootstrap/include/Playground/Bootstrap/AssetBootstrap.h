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
using MapStoragePath = std::function<std::filesystem::path(
    const std::filesystem::path &)>;
using CleanupStaleGenerations = std::function<bool(
    const std::filesystem::path &, const std::filesystem::path &,
    std::size_t &, std::string &)>;
using PublishGeneration =
    std::function<bool(const std::filesystem::path &,
                       const std::filesystem::path &, std::string &)>;
using RemoveTree = std::function<bool(const std::filesystem::path &,
                                      std::string &)>;
using ResetReplaceableStorage = std::function<bool(std::string &)>;
using OpenArchive = std::function<void *(const std::filesystem::path &,
                                         std::string &)>;
using IsRegularFile =
    std::function<bool(const std::filesystem::path &)>;
using DescribeFileError = std::function<std::string()>;

struct AssetBootstrapOptions {
  std::filesystem::path archive;
  std::filesystem::path cacheRoot;
  AssetBundleMetadata metadata;
  HashFile hashFile;
  CommitStorage commitStorage;
  Progress progress;
  // Optional physical-path encoder for filesystems whose component or total
  // path limits cannot represent canonical asset names. The mapping must be
  // deterministic and injective; logical archive names remain unchanged.
  // Bootstrap results also remain logical paths: the mapper is applied once,
  // and only at a physical filesystem-operation boundary.
  MapStoragePath mapStoragePath;
  // Optional directory-enumeration implementation for platforms whose C++
  // filesystem iterator is unavailable or unsuitable.
  CleanupStaleGenerations cleanupStaleGenerations;
  // Optional filesystem-specific publication operation. The default is an
  // atomic directory rename. Compatibility hosts whose filesystem service
  // lacks RenameDirectory may copy the already validated content-addressed
  // staging tree, validate it again, and remove staging instead.
  PublishGeneration publishGeneration;
  // Optional recursive removal for targets whose C++ filesystem
  // implementation cannot safely remove a large directory tree.
  RemoveTree removeTree;
  // Last-resort recovery for a bootstrap-owned incomplete tree that the
  // target filesystem can no longer remove. This may reset only replaceable
  // storage; durable application/user state must never be attached here.
  // It is called only after the bundled archive has been authenticated and
  // only when no completed asset generation exists to preserve.
  ResetReplaceableStorage resetReplaceableStorage;
  // Optional seekable archive reader. The returned handle must be compatible
  // with unzClose(); this lets constrained hosts avoid minizip's stdio layer.
  OpenArchive openArchive;
  // Optional status implementation for targets whose libstdc++ filesystem
  // status bridge is unavailable or ABI-incompatible with the platform SDK.
  IsRegularFile isRegularFile;
  // Optional native detail for failures collapsed by the C/C++ filesystem
  // bridge into a generic errno (for example, Horizon devoptab EIO).
  DescribeFileError describeFileError;
  // Filesystems such as WasmFS OPFS cannot rename directories. Extract into
  // the content-addressed generation and publish it by writing the verified
  // completion marker last. An interrupted generation has no marker and is
  // removed before the next extraction.
  bool publishWithCompletionMarker{};
  // Optional test/telemetry boundary. Production leaves this empty; the host
  // probe terminates its process here to model power loss between commits.
  PublicationCheckpoint publicationCheckpoint;
  // Some transactional filesystems have a bounded journal mapping table.
  // When nonzero, commit the incomplete staging generation after this many
  // archive entries. The generation remains invalid until its marker is
  // written, so these checkpoints preserve interruption safety.
  std::uint64_t extractionCommitInterval{};
  // Optional basename limit for bootstrap-owned generation/staging names.
  // Archive member names are canonical application data and are never
  // rewritten by this option. Zero retains the legacy descriptive names.
  std::size_t maximumInternalNameLength{};
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
