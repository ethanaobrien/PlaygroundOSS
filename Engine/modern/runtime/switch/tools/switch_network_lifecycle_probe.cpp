#include "Playground/Switch/SwitchNetwork.h"
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <thread>

void check(bool value, const char* what) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main() {
  using playground::switch_runtime::SwitchNetwork;
  std::string error;
  {
    SwitchNetwork network;
    for (int i = 0; i < 8; ++i) {
      check(network.initialize(error), "initialize after soft reboot");
      auto transfer = network.beginTransfer(error);
      check(bool(transfer), "transfer after soft reboot");
      transfer.complete(12);
      network.shutdownAndWait();
      check(testSockets == 0 && testNifm == 0 && testRequests == 0, "all services released");
    }
    std::puts("repeated init/transfer/shutdown/reinit: PASS");
    check(network.initialize(error), "drain setup");
    auto transfer = network.beginTransfer(error);
    std::thread teardown([&] { network.shutdownAndWait(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!transfer.cancellationRequested() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    check(transfer.cancellationRequested(), "shutdown requests cancellation");
    check(!network.initialize(error) && !network.beginTransfer(error), "no new transfers during drain");
    check(testSockets == 1, "active lease keeps sockets alive");
    transfer.complete(0);
    teardown.join();
    check(network.initialize(error), "initialize after draining shutdown");
    std::puts("active transfer cancellation/drain and restart: PASS");
    check(network.suspend(error), "HOME suspend");
    network.shutdownAndWait();
    check(!network.initialize(error) && !network.beginTransfer(error), "shutdown must preserve real suspension");
    check(network.resume(error), "HOME resume");
    std::puts("real suspension preserved until resume: PASS");
    network.shutdownAndWait();
    testFailSocket = true;
    check(!network.initialize(error), "service failure returned");
    check(testSockets == 0 && testNifm == 0 && testRequests == 0, "failed initialization cleanup");
    testFailSocket = false;
    check(network.initialize(error), "retry after service failure");
  }
  check(testSockets == 0 && testNifm == 0 && testRequests == 0, "destructor releases services");
  std::puts("service failure/retry and destruction: PASS");
}
