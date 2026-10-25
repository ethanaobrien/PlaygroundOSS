#include "Playground/Runtime/RuntimePlatform.h"
#include "Playground/Switch/SwitchArchiveWriters.h"
#include "Playground/Switch/BoundedDiagnosticLog.h"
#include "FileSystem.h"
#include "CUnZip.h"
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool noWriters(const std::filesystem::path& root, std::string& error) {
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    std::error_code ec;
    const auto path = std::filesystem::read_symlink(entry.path(), ec).string();
    if (ec || path.compare(0, root.string().size() + 1, root.string() + "/")) continue;
    const int flags = fcntl(std::stoi(entry.path().filename().string()), F_GETFL);
    if (flags >= 0 && (flags & O_ACCMODE) != O_RDONLY) {
      error = "WriteModeFileNotClosed: " + path;
      return false;
    }
  }
  return true;
}
std::string contents(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void stalledIO(const std::filesystem::path& content, bool blockClose) {
  using playground::switch_runtime::SwitchArchiveWriters;
  SwitchArchiveWriters writers(content.string(), [](std::string&) { return true; });
  const auto pipe = content / (blockClose ? "slow-close" : "slow-write");
  require(mkfifo(pipe.c_str(), 0600) == 0, "slow IO fixture");
  const int reader = open(pipe.c_str(), O_RDONLY | O_NONBLOCK);
  require(reader >= 0, "slow IO reader");
  auto slow = writers.open(pipe.string());
  require(bool(slow), "slow IO writer");
  if (blockClose) {
    const int filler = open(pipe.c_str(), O_WRONLY | O_NONBLOCK);
    require(filler >= 0, "pipe filler");
    char bytes[4096]{};
    while (::write(filler, bytes, sizeof(bytes)) > 0) {}
    ::close(filler);
    require(slow->write("x", 1) == 1, "buffer byte until slow close");
  }
  std::atomic<bool> entered{false}, finished{false}, independent{false}, ok{true};
  std::thread blocked([&] {
    entered = true;
    if (!blockClose) {
      const std::string payload(1024 * 1024, 'x');
      if (slow->write(payload.data(), payload.size()) != payload.size()) ok = false;
    }
    if (slow->close() != 0) ok = false;
    finished = true;
  });
  while (!entered) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const bool wasBlocked = !finished;
  std::thread other([&] {
    auto output = writers.open((content / "independent-output").string());
    if (!output || output->write("ok", 2) != 2 || output->close() != 0) ok = false;
    if (writers.profile().empty()) ok = false;
    independent = true;
  });
  // Baseline IO is deliberately serialized again, but the sampler must still
  // obtain a snapshot while a native write or close is stuck.
  const bool snapshot = !writers.profile().empty();
  // Always release the slow stream before assertions, including on regression.
  char buffer[65536];
  while (!finished) {
    while (::read(reader, buffer, sizeof(buffer)) > 0) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  blocked.join();
  other.join();
  ::close(reader);
  require(wasBlocked && snapshot && independent && ok, "diagnostics progress during blocked IO; workers recover");
}
void run(const std::filesystem::path& root, const char* zipFixture, const char* corruptFixture) {
  using playground::runtime::RuntimePlatform;
  using playground::switch_runtime::SwitchArchiveWriters;
  const auto content = root / "content";
  std::filesystem::create_directories(content);
  {
    playground::switch_runtime::BoundedDiagnosticLog log(root / "logs", 32);
    require(log.begin() && log.append(std::string(20, 'a')) &&
        log.append(std::string(20, 'b')), "bounded log rotates");
    require(contents(root / "logs/runtime-current.log") == std::string(20, 'b') &&
        contents(root / "logs/runtime-previous.log") == std::string(20, 'a'), "bounded log exact contents");
    require(!log.append(std::string(33, 'x')), "oversize log record rejected");
    require(log.begin() && log.append("new session") &&
        contents(root / "logs/runtime-previous.log") == std::string(20, 'b'), "relaunch preserves previous session");
    playground::switch_runtime::BoundedDiagnosticLog bad(root / "logs/runtime-current.log/child");
    require(!bad.begin(), "logging failure is reported");
    std::puts("bounded log rotation, relaunch retention, oversize and IO failure: PASS");
  }
  stalledIO(content, false);
  stalledIO(content, true);
  std::puts("serialized write/close retain responsive diagnostics and recover: PASS");
  unsigned rejected = 0;
  auto rawCommit = [&](std::string& error) {
    const bool ok = noWriters(content, error);
    if (!ok) ++rejected;
    return ok;
  };
  bool injected = false;
  unsigned attempts = 0;
  SwitchArchiveWriters writers(content.string(), [&](std::string& error) {
    ++attempts;
    if (injected) { error = "injected archive failure"; return false; }
    return rawCommit(error);
  });
  bool managed = false;
  RuntimePlatform platform((root / "install").string(), content.string(),
      (root / "state").string(), nullptr, {}, [&] {
        std::string error;
        return managed ? writers.requestCommit(error) : rawCommit(error);
      });
  auto first = std::unique_ptr<ITmpFile>(platform.openTmpFile("file://external/a.tmp"));
  auto second = std::unique_ptr<ITmpFile>(platform.openTmpFile("file://external/b.tmp"));
  require(first && second, "baseline opens");
  require(first->closeTmp() != 0 && rejected == 1, "baseline must reproduce overlapping-writer rejection");
  require(second->closeTmp() == 0, "last baseline writer commits");
  std::puts("uncoordinated download close reproduces archive rejection: PASS");
  managed = true;
  platform.setContentWriterFactory([&](const std::string& path) { return writers.open(path); });
  first.reset(platform.openTmpFile("file://external/a.tmp"));
  second.reset(platform.openTmpFile("file://external/b.tmp"));
  auto extracted = std::unique_ptr<IWriteStream>(reinterpret_cast<IWriteStream*>(
      platform.openWriteStream("file://external/extracted", false, 0)));
  require(first && second && extracted && extracted->getStatus() == IWriteStream::NORMAL, "managed opens");
  char start[] = "start", end[] = "end";
  require(first->writeTmp(start, 5) == 5 && second->writeTmp(start, 5) == 5, "download prefix");
  extracted->writeBlock(start, 5);
  require(first->closeTmp() == 0, "commit alongside download and extraction");
  require(platform.irename("file://external/a.tmp", "file://external/a.zip") == 0, "publish with active writers");
  require(second->writeTmp(end, 3) == 3, "resume download position");
  extracted->writeBlock(end, 3);
  require(extracted->getPosition() == 8, "resume extraction position");
  extracted.reset();
  require(second->closeTmp() == 0, "last download closes");
  require(contents(content / "a.zip") == "start" && contents(content / "b.tmp") == "startend" &&
          contents(content / "extracted") == "startend", "exact bytes after interleaved commits");
  require(rejected == 1, "no coordinated commit with native writers open");
  std::puts("overlapping downloads/extraction/publication retain exact bytes: PASS");
  auto concurrent = writers.open((content / "concurrent").string());
  require(bool(concurrent), "worker open");
  std::atomic<bool> ok{true};
  std::thread worker([&] {
    for (int i = 0; i < 1000; ++i) if (concurrent->write("x", 1) != 1) ok = false;
  });
  for (int i = 0; i < 64; ++i) {
    std::string error;
    if (!writers.commit(error)) ok = false;
  }
  worker.join();
  require(ok && concurrent->close() == 0 && contents(content / "concurrent") == std::string(1000, 'x'), "concurrent integrity");
  std::puts("worker writes versus archive commits: PASS");
  {
    std::array<std::unique_ptr<playground::runtime::StorageWriter>, 4> streams;
    std::array<std::thread, 4> workers;
    for (unsigned i = 0; i < streams.size(); ++i) {
      streams[i] = writers.open((content / ("parallel-" + std::to_string(i))).string());
      require(bool(streams[i]), "parallel open");
      workers[i] = std::thread([&, i] {
        const std::string chunk(4097, static_cast<char>('a' + i));
        for (unsigned n = 0; n < 128; ++n)
          if (streams[i]->write(chunk.data(), chunk.size()) != chunk.size()) ok = false;
        if (streams[i]->close() != 0) ok = false;
      });
    }
    for (unsigned i = 0; i < 64; ++i) {
      std::string failure;
      if (!writers.commit(failure)) ok = false;
    }
    for (auto& thread : workers) thread.join();
    for (unsigned i = 0; i < streams.size(); ++i)
      require(contents(content / ("parallel-" + std::to_string(i))) ==
                  std::string(4097 * 128, static_cast<char>('a' + i)), "parallel exact payload");
    require(ok, "parallel write/close/commit integrity");
    require(!writers.open((content / "absent-parent" / "file").string()), "failed open setup");
    std::string failure;
    require(writers.commit(failure), "failed open must release reservation");
    std::puts("parallel writers/closers versus forced commits; failed-open cleanup: PASS");
  }
  {
    auto buffered = writers.open((content / "buffer-boundaries").string());
    require(bool(buffered), "large buffered file open");
    std::string expected;
    std::string error;
    for (unsigned i = 0; i < 137; ++i) {
      std::string chunk(4093 + i, static_cast<char>('a' + i % 26));
      require(buffered->write(chunk.data(), chunk.size()) == chunk.size(), "buffered chunk write");
      expected += chunk;
      if (i % 19 == 0) require(writers.commit(error), "flush/reopen across buffer boundary");
      require(buffered->position() == static_cast<long>(expected.size()), "buffered logical position");
    }
    require(buffered->close() == 0 && contents(content / "buffer-boundaries") == expected,
            "buffered payload exact after repeated reopen");
    std::puts("multi-buffer payload and repeated commit/reopen: PASS");
  }
  auto recovery = writers.open((content / "recovery").string());
  require(recovery && recovery->write("a", 1) == 1, "failure setup");
  require(!writers.open((content / "recovery").string()), "duplicate open must not truncate");
  require(!writers.open((content / "./recovery").string()), "path aliases must not open duplicate writers");
  require(!writers.open((content / "../outside").string()), "writers must stay within their archive");
  std::string error;
  injected = true;
  require(!writers.commit(error) && error == "injected archive failure", "commit failure propagation");
  injected = false;
  require(recovery->write("b", 1) == 1 && writers.commit(error) && recovery->close() == 0 &&
      contents(content / "recovery") == "ab", "commit failure recovery");
  FILE* untracked = std::fopen((content / "untracked").c_str(), "wb");
  require(untracked && !writers.commit(error), "untracked writers are not ignored");
  std::fclose(untracked);
  require(writers.commit(error), "retry after untracked close");
  std::puts("duplicate-open protection, commit failure and external-writer retry: PASS");
  platform.setContentWriteBatch([&](bool begin, bool success) {
    std::string failure;
    return begin ? writers.beginBatch(failure) : writers.endBatch(success, failure);
  });
  {
    const unsigned before = attempts;
    require(platform.beginFileWriteBatch("file://external/"), "batch begins");
    require(!platform.beginFileWriteBatch("file://external/"), "nested batch rejected");
    for (unsigned i = 0; i < 200; ++i) {
      auto output = std::unique_ptr<ITmpFile>(platform.openTmpFile(
          ("file://external/batch-" + std::to_string(i)).c_str()));
      require(output && output->writeTmp(start, 5) == 5 && output->closeTmp() == 0, "batched file close");
    }
    require(attempts == before, "per-file commits deferred");
    std::thread independent([&] {
      std::string failure;
      require(writers.requestCommit(failure), "other thread commit not deferred");
    });
    independent.join();
    require(attempts == before + 1, "other thread has independent boundary");
    require(writers.commit(error) && attempts == before + 2, "lifecycle commit not deferred");
    require(platform.endFileWriteBatch("file://external/", true) && attempts == before + 3, "package final commit");
    require(platform.beginFileWriteBatch("file://external/") &&
        platform.endFileWriteBatch("file://external/", false) && attempts == before + 3, "abort clears batch without success commit");
    require(platform.beginFileWriteBatch("file://external/"), "failed commit setup");
    injected = true;
    require(!platform.endFileWriteBatch("file://external/", true), "batch commit failure propagated");
    injected = false;
    require(platform.beginFileWriteBatch("file://external/") &&
        platform.endFileWriteBatch("file://external/", true), "retry after failed batch");
    require(platform.beginFileWriteBatch("file://external/"), "unclosed output batch setup");
    auto unfinished = std::unique_ptr<ITmpFile>(platform.openTmpFile("file://external/unfinished"));
    require(unfinished && !platform.endFileWriteBatch("file://external/", true), "unclosed output prevents successful batch finish");
    require(unfinished->closeTmp() == 0, "unclosed output can unwind normally");
    std::puts("batch coalescing, thread isolation, lifecycle force, abort and retry: PASS");
  }
  if (zipFixture) {
    CPFInterface::getInstance().setPlatformRequest(&platform);
    struct Detach { ~Detach() { CPFInterface::getInstance().setPlatformRequest(nullptr); } } detach;
    const auto extract = [&](bool expectedError, const char* archive = nullptr) {
      if (!archive) archive = zipFixture;
      char* ownedPath = new char[std::strlen(archive) + 1];
      std::strcpy(ownedPath, archive);
      CKLBSubThreadUnzip unzip(ownedPath);
      require(unzip.unCompress("file://external/"), "actual ZIP worker start");
      while (!unzip.isFinishExtract()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      require(unzip.hasError() == expectedError, "actual ZIP completion status");
    };
    platform.setContentWriteBatch({});
    unsigned before = attempts;
    extract(false);
    require(attempts == before + 256, "unbatched actual extraction commits each file");
    platform.setContentWriteBatch([&](bool begin, bool success) {
      std::string failure;
      return begin ? writers.beginBatch(failure) : writers.endBatch(success, failure);
    });
    auto ongoing = writers.open((content / "ongoing-download").string());
    require(ongoing && ongoing->write("before", 6) == 6, "overlapping download open");
    before = attempts;
    extract(false);
    require(attempts == before + 1, "256 extracted files commit exactly once");
    require(ongoing->write("after", 5) == 5 && ongoing->close() == 0 &&
        contents(content / "ongoing-download") == "beforeafter", "download survives batch completion");
    for (int i = 0; i < 256; ++i)
      require(contents(content / ("fixture-" + std::to_string(i))) == std::string(8193, 'x'), "extracted payload exact");
    injected = true;
    extract(true);
    injected = false;
    extract(false);
    if (corruptFixture) {
      before = attempts;
      extract(true, corruptFixture);
      require(attempts == before, "corrupt ZIP must abort without successful batch commit");
      extract(false);
      require(attempts == before + 1, "good ZIP after corrupt ZIP commits once");
      std::puts("real corrupt ZIP abort and subsequent successful extraction: PASS");
    }
    std::puts("real ZIP worker: 256 -> 1 commits, exact payloads, final failure and retry: PASS");
  }
  {
    const auto bounded = root / "bounded";
    std::filesystem::create_directories(bounded);
    unsigned commits = 0;
    SwitchArchiveWriters policy(bounded.string(), [&](std::string& failure) {
      ++commits;
      return noWriters(bounded, failure);
    });
    policy.setCommitPolicy(1024 * 1024, 8);
    std::string failure;
    require(policy.commitIfDirty(failure) && commits == 0, "clean archive is not committed");
    for (unsigned i = 0; i < 7; ++i) {
      auto output = policy.open((bounded / ("small-" + std::to_string(i))).string());
      require(output && output->write("x", 1) == 1 && output->close() == 0, "bounded small file");
      require(policy.requestCommit(failure), "deferred publication request");
    }
    require(commits == 0, "requests below the operation bound are deferred");
    require(policy.requestCommit(failure) && commits == 1, "operation bound forces one commit");
    require(policy.commitIfDirty(failure) && commits == 1, "commit resets pending usage");
    auto large = policy.open((bounded / "large").string());
    require(bool(large), "large download open");
    const std::string chunk(64 * 1024, 'L');
    for (unsigned i = 0; i < 40; ++i)
      require(large->write(chunk.data(), chunk.size()) == chunk.size(), "large download write");
    require(commits == 3, "byte bound commits inside one open download");
    require(large->close() == 0 && contents(bounded / "large") == std::string(40 * chunk.size(), 'L'),
            "large download exact after journal-bound commits");
    require(policy.commitIfDirty(failure) && commits == 4, "dirty tail is committed on demand");
    require(policy.beginBatch(failure), "bounded batch begins");
    for (unsigned i = 0; i < 20; ++i)
      require(policy.requestCommit(failure), "bounded batch request");
    require(commits == 6, "batches cannot exceed the operation bound");
    require(policy.endBatch(true, failure) && commits == 7, "batch end still commits");
    std::puts("bounded commit policy: deferral, op/byte bounds, batch bound, dirty tail: PASS");
  }
  SwitchArchiveWriters missing(content.string(), [&](std::string&) {
    return std::remove((content / "missing").c_str()) == 0;
  });
  auto lost = missing.open((content / "missing").string());
  require(lost && lost->write("a", 1) == 1 && !missing.commit(error), "missing restore fails");
  require(!std::filesystem::exists(content / "missing") && lost->write("b", 1) == 0 && lost->close() != 0,
          "missing restore must not recreate or report success");
  std::puts("failed restore fails closed without recreating data: PASS");
}
}
int main(int argc, char** argv) {
  int result = 1;
  try {
    require(argc >= 2 && argc <= 4, "usage: archive-writer-probe NEW-scratch-directory [fixture.zip [corrupt.zip]]");
    const auto root = std::filesystem::absolute(argv[1]);
    require(std::filesystem::create_directory(root), "scratch directory must be new");
    run(root, argc >= 3 ? argv[2] : nullptr, argc == 4 ? argv[3] : nullptr);
    result = 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
  }
  std::fflush(nullptr);
  std::_Exit(result);
}
