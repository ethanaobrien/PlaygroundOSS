#pragma once
// Host-test SDK boundary only. Tests compile the production SwitchNetwork.cpp;
// this file models service success/lifetime, not networking or Horizon itself.
#include <atomic>
#include <chrono>
#include <cstdint>
using Result = unsigned;
#define R_FAILED(value) ((value) != 0)
struct NifmRequest {};
enum { NifmServiceType_User, BsdServiceType_User };
struct SocketInitConfig {
  unsigned tcp_tx_buf_size, tcp_rx_buf_size, tcp_tx_buf_max_size, tcp_rx_buf_max_size;
  unsigned udp_tx_buf_size, udp_rx_buf_size, sb_efficiency, num_bsd_sessions, bsd_service_type;
};
inline std::atomic<int> testSockets{}, testNifm{}, testRequests{};
inline std::atomic<bool> testFailSocket{};
inline std::uint64_t armGetSystemTick() {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}
inline std::uint64_t armTicksToNs(std::uint64_t value) { return value; }
inline Result nifmInitialize(int) { ++testNifm; return 0; }
inline void nifmExit() { --testNifm; }
inline Result nifmCreateRequest(NifmRequest*, bool) { ++testRequests; return 0; }
inline void nifmRequestClose(NifmRequest*) { --testRequests; }
inline Result nifmRequestSubmitAndWait(NifmRequest*) { return 0; }
inline Result nifmLaHandleNetworkRequestResult(NifmRequest*) { return 0; }
inline Result socketInitialize(const SocketInitConfig*) {
  if (testFailSocket) return 1;
  ++testSockets;
  return 0;
}
inline void socketExit() { --testSockets; }
