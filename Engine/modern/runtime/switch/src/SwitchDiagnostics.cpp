#include "Playground/Switch/SwitchDiagnostics.h"
#include "Playground/Switch/BoundedDiagnosticLog.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#if defined(PLAYGROUND_SWITCH_FILE_DIAGNOSTICS)
#include <switch.h>
#include <malloc.h>
#endif

namespace playground::switch_runtime {
namespace {
std::uint64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
void SwitchDiagnostics::phase(unsigned value) {
  changedAt = nowNs();
  stage = value;
}
void SwitchDiagnostics::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
  }
  wake.notify_all();
  if (worker.joinable()) worker.join();
}
void SwitchDiagnostics::start(std::function<std::string()> snapshot) {
#if defined(PLAYGROUND_SWITCH_FILE_DIAGNOSTICS)
  if (worker.joinable()) return;
  stopping = false;
  state = 1;
  phase(0);
  worker = std::thread([this, snapshot = std::move(snapshot)] {
    const bool ownMount = fsdevGetDeviceFileSystem("sdmc") == nullptr;
    const Result mountResult = ownMount ? fsdevMountSdmc() : 0;
    if (R_FAILED(mountResult)) { state = -1; return; }
    BoundedDiagnosticLog log("sdmc:/switch/PlaygroundOSS/logs");
    // Engine diagnostics (network failures with curl codes, per-transfer
    // metrics, storage errors) go to stderr, which an installed title
    // discards. Capture it fully buffered so logging threads never wait on SD
    // per line; the sampler flushes it every interval. The tail since the
    // last flush can be lost on a crash.
    static char engineBuffer[32 * 1024];
    const char* enginePath = "sdmc:/switch/PlaygroundOSS/logs/engine-current.log";
    {
      std::error_code ec;
      std::filesystem::create_directories("sdmc:/switch/PlaygroundOSS/logs", ec);
      std::filesystem::remove("sdmc:/switch/PlaygroundOSS/logs/engine-previous.log", ec);
      std::filesystem::rename(enginePath, "sdmc:/switch/PlaygroundOSS/logs/engine-previous.log", ec);
    }
    const bool captured = std::freopen(enginePath, "w", stderr) != nullptr;
    if (captured) std::setvbuf(stderr, engineBuffer, _IOFBF, sizeof(engineBuffer));
    bool ok = log.begin() && log.append(
        "PlaygroundOSS diagnostic: serialized IO, package commits, default audio priorities\n"
        "Samples every 5s; counters overlap, not wire speed. Threads/sessions belong to the process resource limit (not total handles).\n"
        "stage: 0 start,1 pump,2 engine,3 widgets,4 swap,5 idle,6 paused,7 shutdown\n");
    u64 resource = 0;
    const Result resourceResult = svcGetInfo(&resource, InfoType_ResourceLimit, CUR_PROCESS_HANDLE, 0);
    const auto began = nowNs();
    double previousSampleMs = 0;
    while (ok) {
      const auto sampleStart = nowNs();
      u64 used = 0;
      const Result memoryResult = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
      s64 threads = -1, sessions = -1;
      Result threadResult = resourceResult, sessionResult = resourceResult;
      if (R_SUCCEEDED(resourceResult)) {
        threadResult = svcGetResourceLimitCurrentValue(&threads, static_cast<Handle>(resource), LimitableResource_Threads);
        sessionResult = svcGetResourceLimitCurrentValue(&sessions, static_cast<Handle>(resource), LimitableResource_Sessions);
      }
      const auto heap = mallinfo();
      const auto currentStage = stage.load();
      const auto changed = changedAt.load();
      const auto now = nowNs();
      char line[768];
      std::snprintf(line, sizeof(line),
          "\nt=%.3f frames=%llu stage=%u stage_age=%.3f previous_sample_ms=%.1f\n"
          "process_used=%llu rc=%08x malloc_used=%d malloc_free=%d free_blocks=%d "
          "threads=%lld rc=%08x sessions=%lld rc=%08x\n",
          (now-began)/1e9, static_cast<unsigned long long>(frames.load()), currentStage,
          now >= changed ? (now-changed)/1e9 : 0, previousSampleMs,
          static_cast<unsigned long long>(used), memoryResult, heap.uordblks, heap.fordblks, heap.ordblks,
          static_cast<long long>(threads), threadResult, static_cast<long long>(sessions), sessionResult);
      ok = log.append(std::string(line) + snapshot());
      if (captured) {
        std::fflush(stderr);
        // Bound the capture; the previous file keeps the earlier session.
        if (std::ftell(stderr) > 8L * 1024 * 1024 &&
            std::freopen(enginePath, "w", stderr))
          std::setvbuf(stderr, engineBuffer, _IOFBF, sizeof(engineBuffer));
      }
      previousSampleMs = (nowNs() - sampleStart) / 1e6;
      state = ok ? 2 : -1;
      std::unique_lock<std::mutex> lock(mutex);
      if (wake.wait_for(lock, std::chrono::seconds(5), [this] { return stopping; })) break;
    }
    if (ok) ok = log.append("diagnostic sampler stopped normally\n");
    if (captured) std::fflush(stderr);
    if (R_SUCCEEDED(resourceResult)) svcCloseHandle(static_cast<Handle>(resource));
    // stderr stays bound to SD for the remainder of shutdown logging.
    if (ownMount && !captured) fsdevUnmountDevice("sdmc");
    state = ok ? 3 : -1;
  });
#else
  (void)snapshot;
#endif
}
}
