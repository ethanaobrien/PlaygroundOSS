#include "Playground/Switch/SwitchNetwork.h"

#include <switch.h>

#include <cstdio>
#include <utility>

namespace playground::switch_runtime {
namespace {

NifmRequest g_request{};

std::uint64_t nowNanoseconds() { return armTicksToNs(armGetSystemTick()); }

std::string resultError(const char *operation, Result result) {
  char text[160];
  std::snprintf(text, sizeof(text), "%s failed with result 0x%08x", operation,
                result);
  return text;
}

SocketInitConfig socketConfiguration() {
  SocketInitConfig configuration{};
  configuration.tcp_tx_buf_size = 0x20000;
  configuration.tcp_rx_buf_size = 0x100000;
  configuration.tcp_tx_buf_max_size = 0x80000;
  configuration.tcp_rx_buf_max_size = 0x200000;
  configuration.udp_tx_buf_size = 0x2400;
  configuration.udp_rx_buf_size = 0xA500;
  configuration.sb_efficiency = 4;
  configuration.num_bsd_sessions = 8;
  configuration.bsd_service_type = BsdServiceType_User;
  return configuration;
}

} // namespace

SwitchNetwork::Transfer::Transfer(SwitchNetwork *owner, std::uint64_t startedAt,
                                  std::uint64_t cancellationEpoch)
    : m_owner(owner), m_startedAt(startedAt),
      m_cancellationEpoch(cancellationEpoch) {}

SwitchNetwork::Transfer::Transfer(Transfer &&other) noexcept
    : m_owner(std::exchange(other.m_owner, nullptr)),
      m_startedAt(other.m_startedAt),
      m_cancellationEpoch(other.m_cancellationEpoch) {}

SwitchNetwork::Transfer &
SwitchNetwork::Transfer::operator=(Transfer &&other) noexcept {
  if (this == &other)
    return *this;
  if (m_owner)
    m_owner->finishTransfer(m_startedAt, 0);
  m_owner = std::exchange(other.m_owner, nullptr);
  m_startedAt = other.m_startedAt;
  m_cancellationEpoch = other.m_cancellationEpoch;
  return *this;
}

SwitchNetwork::Transfer::~Transfer() {
  if (m_owner)
    m_owner->finishTransfer(m_startedAt, 0);
}

void SwitchNetwork::Transfer::complete(std::size_t receivedBytes) {
  if (!m_owner)
    return;
  SwitchNetwork *owner = std::exchange(m_owner, nullptr);
  owner->finishTransfer(m_startedAt, receivedBytes);
}

bool SwitchNetwork::Transfer::cancellationRequested() const {
  return m_owner && m_owner->m_cancellationEpoch.load(
                        std::memory_order_acquire) != m_cancellationEpoch;
}

SwitchNetwork::~SwitchNetwork() { shutdown(); }

bool SwitchNetwork::initialize(std::string &error) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_socketReady) {
    m_suspendRequested.store(false, std::memory_order_release);
    return true;
  }

  Result result = nifmInitialize(NifmServiceType_User);
  if (R_FAILED(result)) {
    error = resultError("nifmInitialize", result);
    return false;
  }
  m_nifmReady = true;

  result = nifmCreateRequest(&g_request, true);
  if (R_FAILED(result)) {
    error = resultError("nifmCreateRequest", result);
    shutdownLocked();
    return false;
  }
  m_requestReady = true;
  result = nifmRequestSubmitAndWait(&g_request);
  if (R_FAILED(result)) {
    error = resultError("nifmRequestSubmitAndWait", result);
    shutdownLocked();
    return false;
  }

  const SocketInitConfig configuration = socketConfiguration();
  result = socketInitialize(&configuration);
  if (R_FAILED(result)) {
    error = resultError("socketInitialize", result);
    shutdownLocked();
    return false;
  }
  m_socketReady = true;
  m_suspendRequested.store(false, std::memory_order_release);
  ++m_metrics.initializationCount;
  return true;
}

SwitchNetwork::Transfer SwitchNetwork::beginTransfer(std::string &error) {
  if (!initialize(error))
    return {};
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_suspendRequested.load(std::memory_order_acquire)) {
    error = "A network transfer cannot begin while the title is suspended";
    return {};
  }
  ++m_metrics.activeTransfers;
  if (m_metrics.activeTransfers > m_metrics.peakActiveTransfers)
    m_metrics.peakActiveTransfers = m_metrics.activeTransfers;
  return Transfer(this, nowNanoseconds(),
                  m_cancellationEpoch.load(std::memory_order_acquire));
}

void SwitchNetwork::finishTransfer(std::uint64_t startedAt,
                                   std::size_t receivedBytes) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_metrics.activeTransfers)
    --m_metrics.activeTransfers;
  ++m_metrics.transferCount;
  m_metrics.bytesReceived += receivedBytes;
  const std::uint64_t endedAt = nowNanoseconds();
  if (endedAt >= startedAt)
    m_metrics.transferNanoseconds += endedAt - startedAt;
  if (m_suspendRequested.load(std::memory_order_acquire) &&
      !m_metrics.activeTransfers)
    shutdownLocked();
}

void SwitchNetwork::suspend() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cancellationEpoch.fetch_add(1, std::memory_order_acq_rel);
  m_suspendRequested.store(true, std::memory_order_release);
  if (!m_metrics.activeTransfers)
    shutdownLocked();
}

bool SwitchNetwork::resume(std::string &error) { return initialize(error); }

void SwitchNetwork::shutdown() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cancellationEpoch.fetch_add(1, std::memory_order_acq_rel);
  m_suspendRequested.store(true, std::memory_order_release);
  if (!m_metrics.activeTransfers)
    shutdownLocked();
}

void SwitchNetwork::shutdownLocked() {
  if (m_socketReady) {
    socketExit();
    m_socketReady = false;
  }
  if (m_requestReady) {
    nifmRequestClose(&g_request);
    g_request = {};
    m_requestReady = false;
  }
  if (m_nifmReady) {
    nifmExit();
    m_nifmReady = false;
  }
}

NetworkMetrics SwitchNetwork::metrics() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_metrics;
}

SwitchNetwork &sharedSwitchNetwork() {
  static SwitchNetwork network;
  return network;
}

} // namespace playground::switch_runtime
