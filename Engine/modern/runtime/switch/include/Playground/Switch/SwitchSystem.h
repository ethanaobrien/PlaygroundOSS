#ifndef PLAYGROUND_SWITCH_SYSTEM_H
#define PLAYGROUND_SWITCH_SYSTEM_H

#include <cstdint>
#include <string>

namespace playground::switch_runtime {

struct SystemVersion {
  std::uint8_t major;
  std::uint8_t minor;
  std::uint8_t micro;
};

SystemVersion systemVersion();
bool processMemory(std::uint64_t &used, std::uint64_t &total);
bool setAutoSleepDisabled(bool disabled);
bool showWebPage(const char *url);
bool showApplicationMessage(const char *message, const char *details);
bool startMotionSensors();
void stopMotionSensors();
bool readMotionAngles(float &azimuth, float &elevation);
bool showSoftwareKeyboard(bool password, const char *initialText,
                          std::uint32_t maximumLength, std::string &result);
bool initializeRomFs();
void shutdownRomFs();
bool isInstalledApplication();

} // namespace playground::switch_runtime

#endif
