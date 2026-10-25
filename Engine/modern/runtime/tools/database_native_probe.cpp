#include "Playground/Runtime/RuntimePlatform.h"
#include "CPFInterface.h"
#include "CKLBDatabase.h"
#include "encryptFile.h"
#include "sqlite3.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// Exercise the unchanged default database path with NO platform adapter.
static int run(const std::filesystem::path& root) {
  playground::runtime::RuntimePlatform platform(root.string(), root.string(),
                                               root.string(), nullptr, {}, {});
  CPFInterface::getInstance().setPlatformRequest(&platform);
  struct Detach { ~Detach() { CPFInterface::getInstance().setPlatformRequest(nullptr); } } detach;
  const auto sentinel = (root / "sentinel").string();
  { std::ofstream out(sentinel); out << "preserve existing bytes"; }
  if (!platform.icreateEmptyFile(sentinel.c_str())) return 1;
  { std::ifstream in(sentinel); std::string text; std::getline(in, text);
    if (text != "preserve existing bytes") return 1; }
  std::puts("default host ensure-file preserves existing contents: PASS");
  initNMAsset(0);
  if (!initializeEncryptedSQLiteVFS()) return 1;
  const auto path = (root / "native.db").string();
  sqlite3* first = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &first, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (!rc) rc = sqlite3_exec(first,
      "PRAGMA journal_mode=OFF; CREATE TABLE t(v); INSERT INTO t VALUES(42);", nullptr, nullptr, nullptr);
  for (int flags : {SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, SQLITE_OPEN_READONLY}) {
    sqlite3* second = nullptr;
    if (!rc) rc = sqlite3_open_v2(path.c_str(), &second, flags, nullptr);
    sqlite3_stmt* query = nullptr;
    if (!rc) rc = sqlite3_prepare_v2(second, "SELECT v FROM t", -1, &query, nullptr);
    if (!rc && (sqlite3_step(query) != SQLITE_ROW || sqlite3_column_int(query, 0) != 42)) rc = 1;
    sqlite3_finalize(query);
    sqlite3_close(second);
  }
  sqlite3_close(first);
  if (rc) { std::printf("same-file default-host failure: %d\n", rc); return 1; }
  std::puts("default host encrypted RW/RW and RW/RO reads: PASS");
  sqlite3* missing = nullptr;
  const auto absent = (root / "missing.db").string();
  rc = sqlite3_open_v2(absent.c_str(), &missing, SQLITE_OPEN_READONLY, nullptr);
  sqlite3_close(missing);
  if (rc != SQLITE_CANTOPEN || std::filesystem::exists(absent)) return 1;
  std::puts("default host missing read-only database returns CANTOPEN: PASS");
  return 0;
}
int main(int argc, char** argv) {
  if (argc != 2) std::_Exit(64);
  const auto root = std::filesystem::absolute(argv[1]);
  std::error_code ec;
  if (!std::filesystem::create_directory(root, ec)) std::_Exit(1);
  const int result = run(root);
  std::fflush(nullptr);
  std::_Exit(result); // Probe does not initialize game process singletons.
}
