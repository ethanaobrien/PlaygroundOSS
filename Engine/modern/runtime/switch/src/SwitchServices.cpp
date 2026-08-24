/* Nintendo Switch implementations for engine-owned platform services. */

#include "Playground/Switch/SwitchSystem.h"
#include "Playground/Switch/SwitchNotifications.h"
#include "Playground/Runtime/AssertionDiagnostics.h"

#include "AdManager.h"
#include "CKLBLocationManager.h"
#include "CKLBMotionManager.h"
#include "CPFInterface.h"
#include "NotificationManager.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

class SwitchAdManager final : public IAdManager {
public:
  explicit SwitchAdManager(CKLBAdManager *owner) : IAdManager(owner) {}
  void preloadAd(bool, const char *) override {
    onAdResult(1, 0, "Rewarded ads are unavailable on Nintendo Switch");
  }
  void showAd() override {
    onAdResult(1, 0, "Rewarded ads are unavailable on Nintendo Switch");
  }
};

class SwitchLocationManager final : public ILocationManager {
public:
  explicit SwitchLocationManager(CKLBLocationManager *owner)
      : ILocationManager(owner) {}
  void requireLocation() override {
    notifyLocation(1, 0, 0.0, 0.0,
                   "Location is unavailable on Nintendo Switch");
  }
  bool stopLocation() override { return true; }
  int getPermissionStatus() override { return 0; }
  void requirePermission() override {
    notifyLocation(2, 0, 0.0, 0.0,
                   "Location is unavailable on Nintendo Switch");
  }
};

class SwitchMotionManager final : public IMotionManager {
public:
  ~SwitchMotionManager() override { stop(); }
  void start() override {
    m_running = playground::switch_runtime::startMotionSensors();
  }
  void stop() override {
    if (m_running)
      playground::switch_runtime::stopMotionSensors();
    m_running = false;
  }
  float getAzimuth() override {
    update();
    return m_azimuth;
  }
  float getElevation() override {
    update();
    return m_elevation;
  }

private:
  void update() {
    if (m_running && !playground::switch_runtime::readMotionAngles(
                         m_azimuth, m_elevation)) {
      m_azimuth = 0.0f;
      m_elevation = 0.0f;
    }
  }
  float m_azimuth{};
  float m_elevation{};
  bool m_running{};
};

class SwitchNotificationManager final : public INotificationManager {
public:
  explicit SwitchNotificationManager(CKLBNotificationManager *owner)
      : INotificationManager(owner) {
    std::string error;
    m_ready = playground::switch_runtime::initializeNotifications(error);
    if (!m_ready)
      logError(error);
    consumeTriggeredAlarms();
  }

  ~SwitchNotificationManager() override {
    if (m_ready)
      playground::switch_runtime::shutdownNotifications();
  }

  void setLocalNotificationWithAlarm(const char *, int tagIndex,
                                     const char *, int delaySeconds,
                                     const char *) override {
    if (!m_ready || tagIndex < 0 || tagIndex > 255)
      return;
    std::string error;
    if (!playground::switch_runtime::scheduleNotification(
            static_cast<std::uint8_t>(tagIndex), delaySeconds, error))
      logError(error);
  }
  void cancelLocalNotification(const char *, int tagIndex) override {
    if (!m_ready || tagIndex < 0 || tagIndex > 255)
      return;
    std::string error;
    if (!playground::switch_runtime::cancelNotification(
            static_cast<std::uint8_t>(tagIndex), error))
      logError(error);
  }
  void requestPermission() override {
    // Horizon exposes no runtime permission prompt for notif:a. Possessing
    // the application service is the capability result. Callback zero is the
    // notification-permission callback used by the Android contract.
    notify(0, m_ready ? 0 : -1,
           m_ready ? "permission gained" : "notifications unavailable");
  }
  bool getEnableNotification() override { return m_ready; }
  void getRemoteToken(char *buffer, int bufferLength) override {
    if (buffer && bufferLength > 0)
      buffer[0] = '\0';
  }
  void onActivityResume() override {
    consumeTriggeredAlarms();
    notify(2, 0, "");
  }

private:
  static void logError(const std::string &error) {
    if (!error.empty())
      CPFInterface::getInstance().platform().logging(
          "Switch notification failure: %s", error.c_str());
  }

  void consumeTriggeredAlarms() {
    if (!m_ready)
      return;
    std::vector<std::uint8_t> tags;
    std::string error;
    if (!playground::switch_runtime::consumeTriggeredNotifications(tags,
                                                                    error))
      logError(error);
  }

  bool m_ready{};
};

} // namespace

extern IAdManager *g_adManager;

IAdManager *IAdManager::getInstance(CKLBAdManager *owner) {
  if (!g_adManager)
    g_adManager = new SwitchAdManager(owner);
  return g_adManager;
}

ILocationManager *ILocationManager::create(CKLBLocationManager *owner) {
  if (!s_instance)
    s_instance = new SwitchLocationManager(owner);
  return s_instance;
}

IMotionManager *IMotionManager::getInstance() {
  if (!s_instance)
    s_instance = new SwitchMotionManager();
  return s_instance;
}

INotificationManager *
INotificationManager::create(CKLBNotificationManager *owner) {
  if (!s_instance)
    s_instance = new SwitchNotificationManager(owner);
  return s_instance;
}

void INotificationManager::notify(u32 callbackIndex, int parameter,
                                  const char *message) {
  m_owner->queueNotification(callbackIndex, parameter, message);
}

bool KLBCreateDirectories(const char *path) {
  if (!path || !path[0])
    return false;
  std::filesystem::path directory(path);
  const std::size_t length = std::strlen(path);
  if (length && path[length - 1] != '/' && path[length - 1] != '\\')
    directory = directory.parent_path();
  if (directory.empty())
    return true;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return !error;
}

const char *getFullNativePath(const char *path) {
  return CPFInterface::getInstance().platform().getFullPath(path);
}

void removeTmpFileNative(const char *filePath) {
  if (filePath && filePath[0])
    CPFInterface::getInstance().platform().removeFileOrFolder(filePath);
}

extern "C" void assertFunction(int line, const char *file,
                               const char *message, ...) {
  char formatted[2048];
  va_list arguments;
  va_start(arguments, message);
  std::vsnprintf(formatted, sizeof(formatted), message, arguments);
  va_end(arguments);
  const std::string readable =
      playground::runtime::readableAssertionMessage(formatted);

  char details[3072];
  std::snprintf(details, sizeof(details), "Assert l.%d in %s:\n%s", line,
                file ? file : "", readable.c_str());
  std::fprintf(stderr, "%s\n", details);
  std::fflush(stderr);
  // An assertion message is diagnostic text, not a Lua callback name.  Feeding
  // it back through beforeAssertFunction makes a missing callback assert from
  // inside the first assertion and recursively exhaust the guest stack.  The
  // Android implementation sends assertions to its native crash reporter;
  // the Switch equivalent is the log plus the application error applet below.
  playground::switch_runtime::showApplicationMessage(readable.c_str(), details);
}

extern "C" void msgBox(char *message) {
  std::fprintf(stderr, "%s\n", message ? message : "");
  playground::switch_runtime::showApplicationMessage(
      message ? message : "PlaygroundOSS", message ? message : "");
}
