#include "Playground/Switch/SwitchSystem.h"

#include <switch.h>

#include <algorithm>
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

const char *systemLanguageCode() {
  u64 languageCode = 0;
  SetLanguage language = SetLanguage_JA;
  if (R_FAILED(setGetSystemLanguage(&languageCode)) ||
      R_FAILED(setMakeLanguage(languageCode, &language)))
    return "ja";
  switch (language) {
  case SetLanguage_ENUS:
  case SetLanguage_ENGB:
    return "en";
  case SetLanguage_FR:
  case SetLanguage_FRCA:
    return "fr";
  case SetLanguage_DE:
    return "de";
  case SetLanguage_IT:
    return "it";
  case SetLanguage_ES:
  case SetLanguage_ES419:
    return "es";
  case SetLanguage_ZHCN:
  case SetLanguage_ZHTW:
  case SetLanguage_ZHHANS:
  case SetLanguage_ZHHANT:
    return "zh";
  case SetLanguage_KO:
    return "ko";
  case SetLanguage_NL:
    return "nl";
  case SetLanguage_PT:
  case SetLanguage_PTBR:
    return "pt";
  case SetLanguage_RU:
    return "ru";
  default:
    return "ja";
  }
}

const char *systemCountryCode() {
  SetRegion region = SetRegion_JPN;
  if (R_FAILED(setGetRegionCode(&region)))
    return "JP";
  switch (region) {
  case SetRegion_USA:
    return "US";
  case SetRegion_EUR:
    return "EU";
  case SetRegion_AUS:
    return "AU";
  case SetRegion_HTK:
    return "HK";
  case SetRegion_CHN:
    return "CN";
  default:
    return "JP";
  }
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
                    {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual},
                    {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyLeft},
                    {HidNpadIdType_No1, HidNpadStyleTag_NpadJoyRight}};
  for (const Candidate &candidate : candidates) {
    if (!(hidGetNpadStyleSet(candidate.id) & candidate.style))
      continue;
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
  if (hidGetSixAxisSensorStates(g_motionHandle, &state, 1) != 1 ||
      !(state.attributes & HidSixAxisSensorAttribute_IsConnected)) {
    // Handles identify a particular Npad style. They become stale when a
    // controller disconnects or changes style, so reacquire from the current
    // HID style set instead of returning the last controller's angles forever.
    stopMotionSensors();
    if (!startMotionSensors() ||
        hidGetSixAxisSensorStates(g_motionHandle, &state, 1) != 1 ||
        !(state.attributes & HidSixAxisSensorAttribute_IsConnected))
      return false;
  }
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
  // The foreground keyboard's protocol has room for at most 500 UTF-16 code
  // units. TX_MAXLEN is an engine-side byte limit, so using it as the applet's
  // character ceiling is conservative; the widget applies its exact byte
  // limit after the callback. Reserve four UTF-8 bytes per accepted code
  // point so Japanese text is not silently truncated in swkbdShow's output
  // conversion.
  constexpr std::uint32_t MaximumKeyboardCharacters = 500;
  const std::uint32_t bounded = std::min<std::uint32_t>(
      maximumLength ? maximumLength : MaximumKeyboardCharacters,
      MaximumKeyboardCharacters);
  swkbdConfigSetStringLenMax(&config, bounded);
  std::string buffer(static_cast<std::size_t>(bounded) * 4 + 1, '\0');
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
