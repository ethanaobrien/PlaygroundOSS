#include "Playground/Bootstrap/AssetBootstrap.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  if (argc < 5 || argc > 6) {
    std::cerr << "usage: " << argv[0]
              << " archive metadata cache-root actual-sha256 [cancel-after]\n";
    return 64;
  }
  std::ifstream manifestFile(argv[2], std::ios::binary);
  std::string manifest{std::istreambuf_iterator<char>(manifestFile),
                       std::istreambuf_iterator<char>()};
  playground::bootstrap::AssetBundleMetadata metadata;
  std::string error;
  if ((!manifestFile && !manifestFile.eof()) ||
      !playground::bootstrap::parseAssetBundleMetadata(manifest, metadata,
                                                       error)) {
    std::cerr << error << '\n';
    return 2;
  }
  const std::string actualHash = argv[4];
  const std::string fault = argc == 6 ? argv[5] : "";
  const bool crashCheckpoint = fault.rfind("crash:", 0) == 0;
  const bool copyPublish = fault == "copy-publish";
  const std::string crashAt = crashCheckpoint ? fault.substr(6) : "";
  const std::uint64_t cancelAfter = !fault.empty() && !crashCheckpoint
                                        ? std::strtoull(argv[5], nullptr, 10)
                                        : 0;
  playground::bootstrap::AssetBootstrapOptions options;
  options.archive = argv[1];
  options.cacheRoot = argv[3];
  options.metadata = metadata;
  options.hashFile = [actualHash](const std::filesystem::path &,
                                  std::string &digest, std::string &) {
    digest = actualHash;
    return true;
  };
  options.commitStorage = [](std::string &) { return true; };
  options.progress = [cancelAfter](std::uint64_t complete, std::uint64_t,
                                   const std::string &) {
    return !cancelAfter || complete < cancelAfter;
  };
  if (copyPublish) {
    options.publishGeneration = [](const std::filesystem::path &staging,
                                   const std::filesystem::path &generation,
                                   std::string &error) {
      std::error_code fsError;
      std::filesystem::remove_all(generation, fsError);
      fsError.clear();
      std::filesystem::copy(staging, generation,
                            std::filesystem::copy_options::recursive, fsError);
      if (!fsError)
        std::filesystem::remove_all(staging, fsError);
      if (!fsError)
        return true;
      error = fsError.message();
      return false;
    };
  }
  options.publicationCheckpoint = [crashAt](const std::string &checkpoint) {
    if (!crashAt.empty() && checkpoint == crashAt) {
      std::cerr << "simulated power loss at " << checkpoint << '\n';
      std::cerr.flush();
      std::_Exit(70);
    }
    return true;
  };
  playground::bootstrap::AssetBootstrapResult result;
  if (!playground::bootstrap::prepareAssetBundle(options, result, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::cout << result.installRoot.string() << '\n'
            << "installed=" << result.installed << '\n'
            << "removed=" << result.removedStaleGenerations << '\n';
  return 0;
}
