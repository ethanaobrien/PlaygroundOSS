#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace playground::switch_runtime {
class SwitchDiagnostics {
public:
  ~SwitchDiagnostics() { stop(); }
  void start(std::function<std::string()> snapshot);
  void stop();
  void phase(unsigned value);
  void frame() { ++frames; }
  // 0 disabled, 1 starting, 2 logging, -1 failed, 3 stopped.
  int status() const { return state.load(); }
private:
  std::atomic<int> state{0};
  std::atomic<unsigned> stage{0};
  std::atomic<std::uint64_t> changedAt{0}, frames{0};
  std::mutex mutex;
  std::condition_variable wake;
  bool stopping{};
  std::thread worker;
};
}
