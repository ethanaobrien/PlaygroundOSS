#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>

#include "sqlite3.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

constexpr const char *Root = "/playground-opfs";
constexpr const char *Counter = "/playground-opfs/counter";
constexpr const char *Database = "/playground-opfs/probe.sqlite3";

[[noreturn]] void fail(const std::string &message) {
  std::fprintf(stderr, "web platform probe failed: %s\n", message.c_str());
  MAIN_THREAD_EM_ASM({
    document.body.dataset.probe = "fail";
    document.body.dataset.message = UTF8ToString($0);
  }, message.c_str());
  std::abort();
}

void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message + ": " + std::strerror(errno));
}

int readCounter() {
  std::ifstream input(Counter);
  int value = 0;
  if (input)
    input >> value;
  return value;
}

void writeCounter(int value) {
  const std::string temporary = std::string(Counter) + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    require(static_cast<bool>(output), "open counter temporary file");
    output << value << '\n';
    output.flush();
    require(static_cast<bool>(output), "flush counter temporary file");
  }
  std::error_code error;
  std::filesystem::remove(Counter, error);
  error.clear();
  std::filesystem::rename(temporary, Counter, error);
  require(!error, "publish counter file");
}

void testThreading() {
  std::atomic<int> value{0};
  std::thread worker([&] { value.store(73, std::memory_order_release); });
  worker.join();
  require(value.load(std::memory_order_acquire) == 73, "pthread execution");
}

void testSqlite() {
  sqlite3 *database = nullptr;
  require(sqlite3_open(Database, &database) == SQLITE_OK, "open SQLite database");
  char *error = nullptr;
  const char *statement =
      "PRAGMA journal_mode=DELETE;"
      "CREATE TABLE IF NOT EXISTS probe(value INTEGER NOT NULL);"
      "INSERT INTO probe(value) VALUES(1);";
  const int result = sqlite3_exec(database, statement, nullptr, nullptr, &error);
  if (result != SQLITE_OK) {
    const std::string message = error ? error : "unknown SQLite error";
    sqlite3_free(error);
    sqlite3_close(database);
    fail("SQLite transaction: " + message);
  }
  sqlite3_stmt *query = nullptr;
  require(sqlite3_prepare_v2(database, "SELECT count(*) FROM probe", -1,
                             &query, nullptr) == SQLITE_OK,
          "prepare SQLite query");
  require(sqlite3_step(query) == SQLITE_ROW, "execute SQLite query");
  require(sqlite3_column_int(query, 0) > 0, "persist SQLite row");
  sqlite3_finalize(query);
  require(sqlite3_close(database) == SQLITE_OK, "close SQLite database");
}

} // namespace

int main() {
  backend_t opfs = wasmfs_create_opfs_backend();
  require(opfs != nullptr, "create OPFS backend");
  require(wasmfs_create_directory(Root, 0777, opfs) == 0,
          "mount OPFS backend");
  testThreading();
  testSqlite();
  const int count = readCounter() + 1;
  writeCounter(count);
  MAIN_THREAD_EM_ASM({
    document.body.dataset.probe = "pass";
    document.body.dataset.count = String($0);
    document.title = "Playground web probe pass " + $0;
  }, count);
  std::printf("web platform probe passed, persistent run %d\n", count);
  return 0;
}
