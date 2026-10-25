# Switch network soft-restart regression — 2026-09-19

Package `out/package/switch-soft-restart/0100f59153491000.nsp`, SHA-256
`549639b2464cb57e04b4eee7ca60ef69ac45e6f5adb4b07174dfb2afc8fcf54b`.
The matching ELF is retained alongside the NSP. Hardware acceptance is pending.

After the initial download the game calls `reboot()`, which executes
`finishGame(false)` followed by `initGame()` in the same process. The former
releases HTTP services and calls `SwitchNetwork::shutdownAndWait()`. Previously
this set `m_suspendRequested=true` permanently. The subsequent `initNetwork()`
was rejected as suspended, and `initGame()` skipped startup scripts. No HOME
resume event occurs on this path to clear the flag.

Shutdown now has a separate mutex-protected draining flag. It cancels and waits
for active leases, excludes new leases during teardown, and leaves the actual
HOME/sleep suspension state unchanged. Initialization can run again after a
foreground soft restart. A title genuinely suspended still requires `resume()`.
The lease acquisition path also rechecks service availability under the mutex
after initialization, covering an intervening shutdown.

Host regression compiles the production network implementation with test-only
SDK service stubs (not an emulation of Horizon or the network):

```sh
g++ -std=c++17 -pthread -Wall -Wextra \
  -I Engine/modern/runtime/switch/tools/network_test_sdk \
  -I Engine/modern/runtime/switch/include \
  Engine/modern/runtime/switch/src/SwitchNetwork.cpp \
  Engine/modern/runtime/switch/tools/switch_network_lifecycle_probe.cpp \
  -o /tmp/pg-network-lifecycle-probe
/tmp/pg-network-lifecycle-probe
```

Repeated initialization/transfer/shutdown, active lease drain, refusal of new
leases during teardown, real suspension preservation, service failure/retry,
and destructor cleanup pass. No common engine changes are involved.
The same test compiled against the pre-fix `HEAD` implementation fails with
`FAIL: initialize after soft reboot`, confirming regression sensitivity.

The source proves an automatic-restart failure, but physical verification of
the user's freeze remains pending. The remaining occasional download popup
and reported approximately 200 KB/s throughput are separate unresolved reports.
