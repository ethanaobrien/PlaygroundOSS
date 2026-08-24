#ifndef PLAYGROUND_SWITCH_NETWORK_H
#define PLAYGROUND_SWITCH_NETWORK_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>

namespace playground::switch_runtime {

struct NetworkMetrics {
  std::uint64_t initializationCount{};
  std::uint64_t transferCount{};
  std::uint64_t bytesReceived{};
  std::uint64_t transferNanoseconds{};
  std::uint32_t activeTransfers{};
  std::uint32_t peakActiveTransfers{};
};

// Owns the Switch network services. A transfer lease prevents HOME/sleep from
// tearing down BSD while libcurl still owns sockets; suspension is completed
// immediately after the final lease is released.
class SwitchNetwork {
public:
  class Transfer {
  public:
    Transfer() = default;
    Transfer(Transfer &&other) noexcept;
    Transfer &operator=(Transfer &&other) noexcept;
    ~Transfer();
    Transfer(const Transfer &) = delete;
    Transfer &operator=(const Transfer &) = delete;

    explicit operator bool() const { return m_owner != nullptr; }
    void complete(std::size_t receivedBytes);
    bool cancellationRequested() const;

  private:
    friend class SwitchNetwork;
    explicit Transfer(SwitchNetwork *owner, std::uint64_t startedAt,
                      std::uint64_t cancellationEpoch);
    SwitchNetwork *m_owner{};
    std::uint64_t m_startedAt{};
    std::uint64_t m_cancellationEpoch{};
  };

  SwitchNetwork() = default;
  ~SwitchNetwork();
  SwitchNetwork(const SwitchNetwork &) = delete;
  SwitchNetwork &operator=(const SwitchNetwork &) = delete;

  bool initialize(std::string &error);
  Transfer beginTransfer(std::string &error);
  // Requests cancellation and waits briefly for every libcurl lease to leave
  // the mounted filesystems before the applet lifecycle commits storage.
  bool suspend(std::string &error);
  bool resume(std::string &error);
  // Roll back service initialization before any transfer lease exists without
  // changing foreground/suspended lifecycle state, so a transient higher-
  // layer initialization failure can be retried.
  bool rollbackInitialization(std::string &error);
  // Final teardown has no safe timeout: the platform object, curl globals,
  // and mounted filesystems are all still reachable by an active lease.
  // Cancel and drain every lease before their owners are destroyed.
  void shutdownAndWait();
  NetworkMetrics metrics() const;

private:
  void finishTransfer(std::uint64_t startedAt, std::size_t receivedBytes);
  void shutdownLocked();
  mutable std::mutex m_mutex;
  std::condition_variable m_idle;
  NetworkMetrics m_metrics;
  bool m_nifmReady{};
  bool m_requestReady{};
  bool m_socketReady{};
  std::atomic<bool> m_suspendRequested{};
  std::atomic<std::uint64_t> m_cancellationEpoch{};
};

SwitchNetwork &sharedSwitchNetwork();

} // namespace playground::switch_runtime

#endif
