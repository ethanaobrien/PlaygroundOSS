#include "Playground/Host/WiiUHost.h"
#include "Playground/Runtime/AssertionDiagnostics.h"

#include "AdManager.h"
#include "CKLBLocationManager.h"
#include "CKLBMotionManager.h"
#include "CPFInterface.h"
#include "NotificationManager.h"

#include <coreinit/debug.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/types.h>

namespace {

class WiiUAdManager final : public IAdManager {
public:
  explicit WiiUAdManager(CKLBAdManager *owner) : IAdManager(owner) {}
  void preloadAd(bool, const char *) override {
    onAdResult(1, 0, "Rewarded ads are unavailable on Nintendo Wii U");
  }
  void showAd() override {
    onAdResult(1, 0, "Rewarded ads are unavailable on Nintendo Wii U");
  }
};

class WiiULocationManager final : public ILocationManager {
public:
  explicit WiiULocationManager(CKLBLocationManager *owner)
      : ILocationManager(owner) {}
  void requireLocation() override {
    notifyLocation(1, 0, 0.0, 0.0,
                   "Location is unavailable on Nintendo Wii U");
  }
  bool stopLocation() override { return true; }
  int getPermissionStatus() override { return 0; }
  void requirePermission() override {
    notifyLocation(2, 0, 0.0, 0.0,
                   "Location is unavailable on Nintendo Wii U");
  }
};

class WiiUMotionManager final : public IMotionManager {
public:
  void start() override { m_running = true; }
  void stop() override { m_running = false; }
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
      playgroundWiiUHostGetMotionAngles(&m_azimuth, &m_elevation);
  }
  float m_azimuth{};
  float m_elevation{};
  bool m_running{};
};

class WiiUNotificationManager final : public INotificationManager {
public:
  explicit WiiUNotificationManager(CKLBNotificationManager *owner)
      : INotificationManager(owner) {}
  void setLocalNotificationWithAlarm(const char *, int, const char *, int,
                                     const char *) override {}
  void cancelLocalNotification(const char *, int) override {}
  void requestPermission() override {
    notify(1, 0, "System notifications are unavailable on Nintendo Wii U");
  }
  bool getEnableNotification() override { return false; }
  void getRemoteToken(char *buffer, int length) override {
    if (buffer && length > 0)
      buffer[0] = '\0';
  }
  void onActivityResume() override { notify(2, 0, ""); }
};

} // namespace

extern IAdManager *g_adManager;

IAdManager *IAdManager::getInstance(CKLBAdManager *owner) {
  if (!g_adManager)
    g_adManager = new WiiUAdManager(owner);
  return g_adManager;
}

ILocationManager *ILocationManager::create(CKLBLocationManager *owner) {
  if (!s_instance)
    s_instance = new WiiULocationManager(owner);
  return s_instance;
}

IMotionManager *IMotionManager::getInstance() {
  if (!s_instance)
    s_instance = new WiiUMotionManager();
  return s_instance;
}

INotificationManager *
INotificationManager::create(CKLBNotificationManager *owner) {
  if (!s_instance)
    s_instance = new WiiUNotificationManager(owner);
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
  if (path[length - 1] != '/' && path[length - 1] != '\\')
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

void removeTmpFileNative(const char *path) {
  std::error_code error;
  if (!std::filesystem::remove(path, error) && !error)
    std::filesystem::remove_all(path, error);
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
  OSFatal(details);
}

extern "C" void msgBox(char *) {}

extern "C" int fchown(int, uid_t, gid_t) {
  errno = ENOSYS;
  return -1;
}

extern "C" mode_t umask(mode_t) { return 0; }
