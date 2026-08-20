#include "Playground/Runtime/RuntimePlatform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "runtime-path-policy-probe: %s\n", message);
    std::fflush(nullptr);
    std::_Exit(1);
  }
  return condition;
}

std::string fullPath(playground::runtime::RuntimePlatform &platform,
                     const char *uri, bool &readOnly) {
  const char *value = platform.getFullPath(uri, &readOnly);
  std::string result = value ? value : "";
  delete[] value;
  return result;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s scratch-root\n", argv[0]);
    return 64;
  }
  const std::filesystem::path scratch = argv[1];
  const std::filesystem::path install = scratch / "install";
  const std::filesystem::path content = scratch / "content";
  const std::filesystem::path state = scratch / "state";
  std::error_code error;
  std::filesystem::remove_all(scratch, error);
  std::filesystem::create_directories(install / "asset", error);
  std::filesystem::create_directories(content / "asset", error);
  std::filesystem::create_directories(state, error);
  if (!check(!error, "cannot create scratch roots"))
    return 1;

  int stateCommits = 0;
  int contentCommits = 0;
  playground::runtime::RuntimePlatform platform(
      install.string(), content.string(), state.string(), nullptr,
      [&] {
        ++stateCommits;
        return true;
      },
      [&] {
        ++contentCommits;
        return true;
      });

  bool readOnly = false;
  if (!check(fullPath(platform, "file://state/profile.db", readOnly) ==
                     (state / "profile.db").string() &&
                 !readOnly,
             "state URI did not select the private state root") ||
      !check(fullPath(platform, "file://external/cache.bin", readOnly) ==
                     (content / "cache.bin").string() &&
                 !readOnly,
             "external URI did not select the content root") ||
      !check(fullPath(platform, "file://install/start.lua", readOnly) ==
                     (install / "start.lua").string() &&
                 readOnly,
             "install URI did not select the read-only install root"))
    return 1;

  std::ofstream(install / "asset/value.texb") << "install";
  if (!check(fullPath(platform, "asset://asset/value.texb", readOnly) ==
                     (install / "asset/value.texb").string() &&
                 readOnly,
             "asset fallback did not select install content"))
    return 1;
  std::ofstream(content / "asset/value.texb") << "downloaded";
  if (!check(fullPath(platform, "asset://asset/value.texb", readOnly) ==
                     (content / "asset/value.texb").string() &&
                 !readOnly,
             "downloaded asset did not override install content"))
    return 1;

  if (!check(fullPath(platform, "asset://asset/not-present.db_", readOnly)
                         .empty(),
             "missing asset unexpectedly resolved to an install path"))
    return 1;

  for (const char *invalid :
       {"file://state/../content/leak", "file://external/a/../../leak",
        "file://install/C:/escape", "asset://a\\escape"}) {
    if (!check(fullPath(platform, invalid, readOnly).empty(),
               "unsafe URI was accepted"))
      return 1;
  }

  std::ofstream(state / "old") << "state";
  const int stateBeforeRename = stateCommits;
  const int contentBeforeRename = contentCommits;
  if (!check(platform.irename("file://state/old", "file://state/new") == 0 &&
                 stateCommits == stateBeforeRename + 1 &&
                 contentCommits == contentBeforeRename,
             "state publication did not commit SaveData exactly once"))
    return 1;
  std::ofstream(content / "old") << "content";
  if (!check(platform.irename("file://external/old", "file://external/new") ==
                     0 &&
                 stateCommits == stateBeforeRename + 1 &&
                 contentCommits == contentBeforeRename + 1,
             "content publication did not commit CacheStorage exactly once"))
    return 1;
  if (!check(platform.irename("file://state/new", "file://external/cross") != 0,
             "cross-device state publication was accepted"))
    return 1;

  std::filesystem::remove_all(scratch, error);
  std::puts("runtime-path-policy-probe passed");
  std::fflush(nullptr);
  std::_Exit(0);
}
