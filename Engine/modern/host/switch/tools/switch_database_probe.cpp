#include <switch.h>
#include "Playground/Switch/SwitchDatabase.h"
#include "Playground/Switch/SwitchStorage.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <exception>

namespace {
bool traceFileAvailable = false;
void waitForExit() {
  std::puts("Press + to exit.");
  consoleUpdate(nullptr);
  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(nullptr);
  }
}
}

void databaseProbeCheckpoint(const char* text) {
  std::printf("checkpoint: %s\n", text);
  std::fflush(stdout);
  consoleUpdate(nullptr);
  // Never leave a diagnostic writer open in the archive being tested. The SD
  // log survives process termination without a SaveData commit.
  if (traceFileAvailable) {
    FILE* file = std::fopen("sdmc:/switch/playground-db-probe.log", "ab");
    if (file) {
      std::fprintf(file, "%s\n", text);
      if (std::fclose(file) != 0) traceFileAvailable = false;
    } else traceFileAvailable = false;
  }
}
std::string databaseProbeNativeError() {
  return playground::switch_runtime::describeLastFileError();
}
// Horizon itself rejects conflicting native opens; the host probe additionally
// counts OS descriptors to catch accidental duplication on permissive systems.
bool databaseProbeSingleNativeHandle(const char*) { return true; }

int runDatabaseContractProbe(const std::filesystem::path&, const std::filesystem::path&,
    const std::filesystem::path&, const playground::switch_runtime::SwitchDatabase::Commit&, bool);


int main() {
  consoleInit(nullptr);
  std::error_code directoryError;
  std::filesystem::create_directories("sdmc:/switch", directoryError);
  traceFileAvailable = !directoryError;
  std::set_terminate([] {
    databaseProbeCheckpoint("Unhandled C++ termination");
    if (auto exception = std::current_exception()) {
      try { std::rethrow_exception(exception); }
      catch (const std::exception& e) { databaseProbeCheckpoint(e.what()); }
      catch (...) { databaseProbeCheckpoint("Non-standard exception"); }
    }
    waitForExit();
    std::_Exit(1);
  });
  databaseProbeCheckpoint("=== database probe v7 shared native handles ===");
  std::puts("No game assets are needed. No game save is accessed.");
  u64 id = 0;
  const bool installed = envIsNso();
  std::string error;
  playground::switch_runtime::SwitchStorage storage;
  bool mounted = false;
  if (installed) {
    if (R_SUCCEEDED(svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) &&
        id == 0x0100F59153492000ULL)
      mounted = storage.mountInstalledTitle(error);
    else error = "Refusing unexpected title ID";
  } else {
    std::puts("NRO/SD diagnostic only: does NOT validate SaveData!");
    mounted = storage.mountDevelopmentNro("sdmc:/switch/PlaygroundOSS-database-probe", error);
  }
  consoleUpdate(nullptr);
  int result = 1;
  if (mounted) {
    const auto& roots = storage.roots();
    const auto marker = roots.state / ".last-database-probe-v7";
    std::string run;
    std::ifstream previous(marker);
    std::getline(previous, run);
    previous.close();
    const bool verify = !run.empty();
    if (verify && (run.compare(0, 6, "probe-") != 0 ||
                   run.find_first_not_of("0123456789", 6) != std::string::npos)) {
      std::puts("Invalid probe marker"); std::fflush(nullptr); std::_Exit(1);
    }
    if (!verify) run = "probe-" + std::to_string(armGetSystemTick());
    std::puts(verify ? "Checking previous launch's committed database..." : "Running new database tests...");
    databaseProbeCheckpoint("Enter database tests");
    result = runDatabaseContractProbe(roots.content / run / "install",
        roots.content / run / "content", roots.state / run,
        [&](std::string& failure) {
          const bool ok = storage.commitState(failure);
          if (!ok) std::printf("COMMIT FAILED: %s\n", failure.c_str());
          return ok;
        }, verify);
    databaseProbeCheckpoint(result ? "Test runner returned FAIL" : "Test runner returned PASS");
    if (!result && !verify) {
      std::ofstream saved(marker);
      saved << run << '\n';
      saved.close();
      if (!saved || !storage.commitState(error)) result = 1;
      if (!result) std::puts("Exit and relaunch to check persistence.");
    }
    std::printf("\n%s\n", result ? "FAIL" : "PASS (single-run checks only)");
    // errno/fsdev's last result may belong to an earlier expected missing-file
    // probe. Only operation-local errors are meaningful here.
    if (!storage.unmount(&error)) {
      result = 1;
      std::printf("Unmount/commit failed: %s\n", error.c_str());
    }
  } else std::printf("Storage setup failed: %s\n", error.c_str());
  std::puts("\nPress + to exit. Hardware release acceptance remains pending.");
  waitForExit();
  databaseProbeCheckpoint("Normal process exit");
  consoleExit(nullptr);
  std::fflush(nullptr);
  std::_Exit(result);
}
