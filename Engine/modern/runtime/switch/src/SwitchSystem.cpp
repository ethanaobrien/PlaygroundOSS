#include "Playground/Switch/SwitchSystem.h"

#include <switch.h>

#include <cstring>
#include <utility>

namespace playground::switch_runtime {
namespace {

HidSixAxisSensorHandle g_motionHandle{};
bool g_motionActive = false;

} // namespace

SystemVersion systemVersion() {
  const u32 version = hosversionGet();
  return {static_cast<std::uint8_t>(HOSVER_MAJOR(version)),
          static_cast<std::uint8_t>(HOSVER_MINOR(version)),
          static_cast<std::uint8_t>(HOSVER_MICRO(version))};
}

bool processMemory(std::uint64_t &used, std::uint64_t &total) {
  u64 nativeUsed = 0;
  u64 nativeTotal = 0;
  if (R_FAILED(svcGetInfo(&nativeUsed, InfoType_UsedMemorySize,
                          CUR_PROCESS_HANDLE, 0)) ||
      R_FAILED(svcGetInfo(&nativeTotal, InfoType_TotalMemorySize,
                          CUR_PROCESS_HANDLE, 0)))
    return false;
  used = nativeUsed;
  total = nativeTotal;
  return true;
}

bool setAutoSleepDisabled(bool disabled) {
  return R_SUCCEEDED(appletSetAutoSleepDisabled(disabled));
}

bool showWebPage(const char *url) {
  if (!url || !url[0])
    return false;
  WebCommonConfig config{};
  WebCommonReply reply{};
  return R_SUCCEEDED(webPageCreate(&config, url)) &&
         R_SUCCEEDED(webConfigShow(&config, &reply));
}

bool showApplicationMessage(const char *message, const char *details) {
  ErrorApplicationConfig config{};
  if (R_FAILED(errorApplicationCreate(&config, message ? message : "",
                                      details ? details : "")))
    return false;
  errorApplicationSetNumber(&config, 1);
  return R_SUCCEEDED(errorApplicationShow(&config));
}

bool startMotionSensors() {
  if (g_motionActive)
    return true;
  const struct Candidate {
    HidNpadIdType id;
    HidNpadStyleTag style;
  } candidates[] = {{HidNpadIdType_Handheld,
                     HidNpadStyleTag_NpadHandheld},
                    {HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey},
                    {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual}};
  for (const Candidate &candidate : candidates) {
    HidSixAxisSensorHandle handle{};
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(
            &handle, 1, candidate.id, candidate.style)) &&
        R_SUCCEEDED(hidStartSixAxisSensor(handle))) {
      g_motionHandle = handle;
      g_motionActive = true;
      return true;
    }
  }
  return false;
}

void stopMotionSensors() {
  if (g_motionActive)
    hidStopSixAxisSensor(g_motionHandle);
  g_motionActive = false;
}

bool readMotionAngles(float &azimuth, float &elevation) {
  if (!g_motionActive)
    return false;
  HidSixAxisSensorState state{};
  if (hidGetSixAxisSensorStates(g_motionHandle, &state, 1) != 1)
    return false;
  azimuth = state.angle.z;
  elevation = state.angle.x;
  return true;
}

bool showSoftwareKeyboard(bool password, const char *initialText,
                          std::uint32_t maximumLength, std::string &result) {
  SwkbdConfig config{};
  if (R_FAILED(swkbdCreate(&config, 0)))
    return false;
  if (password)
    swkbdConfigMakePresetPassword(&config);
  else
    swkbdConfigMakePresetDefault(&config);
  if (initialText)
    swkbdConfigSetInitialText(&config, initialText);
  const std::uint32_t bounded = maximumLength ? maximumLength : 4096;
  swkbdConfigSetStringLenMax(&config, bounded);
  std::string buffer(static_cast<std::size_t>(bounded) + 1, '\0');
  const Result shown = swkbdShow(&config, buffer.data(), buffer.size());
  swkbdClose(&config);
  if (R_FAILED(shown))
    return false;
  buffer.resize(std::strlen(buffer.c_str()));
  result = std::move(buffer);
  return true;
}

bool initializeRomFs() { return R_SUCCEEDED(romfsInit()); }
void shutdownRomFs() { romfsExit(); }
bool isInstalledApplication() { return envIsNso(); }

} // namespace playground::switch_runtime
