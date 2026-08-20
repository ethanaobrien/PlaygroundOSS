/* Nintendo Switch implementations for engine-owned platform services. */

#include "Playground/Switch/SwitchSystem.h"

#include "AdManager.h"
#include "CKLBLocationManager.h"
#include "CKLBMotionManager.h"
#include "CPFInterface.h"
#include "NotificationManager.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cerrno>
#include <sys/types.h>

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
    if (m_running)
      playground::switch_runtime::readMotionAngles(m_azimuth, m_elevation);
  }
  float m_azimuth{};
  float m_elevation{};
  bool m_running{};
};

class SwitchNotificationManager final : public INotificationManager {
public:
  explicit SwitchNotificationManager(CKLBNotificationManager *owner)
      : INotificationManager(owner) {}

  void setLocalNotificationWithAlarm(const char *, int, const char *, int,
                                     const char *) override {
    // Horizon exposes no application API for scheduling system notifications.
    // Report this honestly instead of writing fake alarms to shared SD data.
  }
  void cancelLocalNotification(const char *, int) override {}
  void requestPermission() override {
    notify(1, 0, "System notifications are unavailable on Nintendo Switch");
  }
  bool getEnableNotification() override { return false; }
  void getRemoteToken(char *buffer, int bufferLength) override {
    if (buffer && bufferLength > 0)
      buffer[0] = '\0';
  }
  void onActivityResume() override { notify(2, 0, ""); }
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
  std::error_code error;
  if (!std::filesystem::remove(filePath, error) && !error)
    std::filesystem::remove_all(filePath, error);
}

extern "C" void assertFunction(int line, const char *file,
                               const char *message, ...) {
  char formatted[2048];
  va_list arguments;
  va_start(arguments, message);
  std::vsnprintf(formatted, sizeof(formatted), message, arguments);
  va_end(arguments);

  char details[3072];
  std::snprintf(details, sizeof(details), "Assert l.%d in %s:\n%s", line,
                file ? file : "", formatted);
  std::fprintf(stderr, "%s\n", details);
  std::fflush(stderr);
  // An assertion message is diagnostic text, not a Lua callback name.  Feeding
  // it back through beforeAssertFunction makes a missing callback assert from
  // inside the first assertion and recursively exhaust the guest stack.  The
  // Android implementation sends assertions to its native crash reporter;
  // the Switch equivalent is the log plus the application error applet below.
  playground::switch_runtime::showApplicationMessage(formatted, details);
}

extern "C" void msgBox(char *message) {
  std::fprintf(stderr, "%s\n", message ? message : "");
  playground::switch_runtime::showApplicationMessage(
      message ? message : "PlaygroundOSS", message ? message : "");
}

// newlib/libnx intentionally omits Unix ownership and process umask APIs.
// Switch SaveData has no uid/gid permission model, while SQLite's Unix VFS
// keeps these optional calls in its syscall table even with WAL disabled.
extern "C" int fchown(int, uid_t, gid_t) {
  errno = ENOSYS;
  return -1;
}

extern "C" mode_t umask(mode_t) { return 0; }
