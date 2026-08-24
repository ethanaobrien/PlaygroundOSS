#include "RuntimeStateStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool require(bool condition, const char *message) {
  if (!condition)
    std::cerr << "runtime-state-store-probe: " << message << '\n';
  return condition;
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("playground-state-store-" + std::to_string(nonce));
  const std::filesystem::path state = root / "state";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (!require(!error, "could not create test directory"))
    return 1;

  bool commitAllowed = false;
  {
    playground::runtime::RuntimeStateStore store(
        state, [&commitAllowed] { return commitAllowed; });
    if (!require(!store.set("first", "rejected"),
                 "first-generation commit failure was accepted") ||
        !require(store.get("first").empty(),
                 "failed first generation changed memory") ||
        !require(!std::filesystem::exists(state),
                 "failed first generation remained published"))
      return 2;
  }

  commitAllowed = true;
  {
    playground::runtime::RuntimeStateStore store(
        state, [&commitAllowed] { return commitAllowed; });
    if (!require(store.set("identity", "old"), "initial durable write failed"))
      return 3;
  }
  {
    playground::runtime::RuntimeStateStore store(state);
    if (!require(store.get("identity") == "old",
                 "initial value did not survive reload"))
      return 4;
  }

  commitAllowed = false;
  {
    playground::runtime::RuntimeStateStore store(
        state, [&commitAllowed] { return commitAllowed; });
    if (!require(!store.set("identity", "new"),
                 "replacement commit failure was accepted") ||
        !require(store.get("identity") == "old",
                 "replacement failure changed memory"))
      return 5;
  }
  {
    playground::runtime::RuntimeStateStore store(state);
    if (!require(store.get("identity") == "old",
                 "replacement failure changed the mounted file"))
      return 6;
  }

  // Model power loss between moving the active generation aside and
  // publishing its replacement. The constructor must recover the previous
  // complete generation rather than silently starting with empty state.
  const std::filesystem::path previous = state.string() + ".previous";
  std::filesystem::rename(state, previous, error);
  if (!require(!error, "could not create interrupted-publication fixture"))
    return 7;
  commitAllowed = false;
  {
    playground::runtime::RuntimeStateStore store(
        state, [&commitAllowed] { return commitAllowed; });
    if (!require(!store.valid(),
                 "failed recovery publication was reported durable"))
      return 8;
  }
  {
    playground::runtime::RuntimeStateStore store(state);
    if (!require(store.get("identity") == "old",
                 "previous generation was not recovered") ||
        !require(std::filesystem::is_regular_file(state),
                 "recovered generation was not republished"))
      return 9;
  }

  // A malformed active file must likewise fall back to a valid previous one.
  std::filesystem::copy_file(state, previous,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  if (!require(!error, "could not create corruption fixture"))
    return 10;
  std::ofstream(state, std::ios::binary | std::ios::trunc) << "broken";
  {
    playground::runtime::RuntimeStateStore store(state);
    if (!require(store.get("identity") == "old",
                 "corrupt active generation did not recover"))
      return 11;
  }

  // If both generations are malformed, fail closed. Treating corruption as
  // first launch would silently replace account identity and secure values.
  std::filesystem::remove(previous, error);
  std::ofstream(state, std::ios::binary | std::ios::trunc) << "broken-again";
  {
    playground::runtime::RuntimeStateStore store(state);
    if (!require(!store.valid(), "unrecoverable corruption was accepted") ||
        !require(!store.set("identity", "replacement"),
                 "corrupt state was overwritten") ||
        !require(store.get("identity").empty(),
                 "corrupt state exposed invented values"))
      return 12;
  }

  // A path query failure is not equivalent to a missing first-launch store.
  // A self-referential symlink produces ELOOP without relying on permission
  // behavior (the probe may run as root). Windows CI cannot assume symlink
  // creation privilege, so this POSIX error-path assertion is scoped there.
#if !defined(_WIN32)
  const std::filesystem::path loop = root / "loop";
  std::filesystem::create_symlink("loop", loop, error);
  if (!require(!error, "could not create path-query-error fixture"))
    return 13;
  {
    playground::runtime::RuntimeStateStore store(loop / "state");
    if (!require(!store.valid(), "state path query failure was accepted") ||
        !require(!store.set("identity", "replacement"),
                 "state path query failure allowed a replacement write"))
      return 13;
  }
#endif

  std::filesystem::remove_all(root, error);
  std::cout << "runtime-state-store-probe passed\n";
  return 0;
}
