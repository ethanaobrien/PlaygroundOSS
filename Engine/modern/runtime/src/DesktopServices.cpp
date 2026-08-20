/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0
*/

#include "AdManager.h"
#include "CKLBLocationManager.h"
#include "CKLBMotionManager.h"
#include "CPFInterface.h"
#include "NotificationManager.h"
#include "assert_klb.h"

#include <SDL3/SDL_messagebox.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

namespace {

class DesktopAdManager final : public IAdManager {
public:
  explicit DesktopAdManager(CKLBAdManager *owner) : IAdManager(owner) {}
  void preloadAd(bool, const char *) override {
    onAdResult(1, 0, "Rewarded ads are unavailable on desktop");
  }
  void showAd() override {
    onAdResult(1, 0, "Rewarded ads are unavailable on desktop");
  }
};

#if defined(__EMSCRIPTEN__)
class WebLocationManager final : public ILocationManager {
public:
  explicit WebLocationManager(CKLBLocationManager *owner)
      : ILocationManager(owner) {
    MAIN_THREAD_EM_ASM({
      if (navigator.permissions && navigator.permissions.query) {
        navigator.permissions.query({name: 'geolocation'}).then(result => {
          Atomics.store(HEAP32, $0 >> 2,
            result.state === 'granted' ? 1 : 0);
        }).catch(() => {});
      }
    }, &m_permission);
  }

  ~WebLocationManager() override { stopLocation(); }

  void requireLocation() override {
    MAIN_THREAD_EM_ASM({
      if (!globalThis.playgroundLocationWatches)
        globalThis.playgroundLocationWatches = new Map();
      const watches = globalThis.playgroundLocationWatches;
      if (watches.has($0)) navigator.geolocation.clearWatch(watches.get($0));
      const watch = navigator.geolocation.watchPosition(position => {
        HEAPF64[$1 >> 3] = position.coords.latitude;
        HEAPF64[$2 >> 3] = position.coords.longitude;
        Atomics.store(HEAP32, $3 >> 2, 0);
        Atomics.store(HEAP32, $4 >> 2, 1);
        Atomics.add(HEAP32, $5 >> 2, 1);
      }, error => {
        Atomics.store(HEAP32, $3 >> 2, error.code || 2);
        Atomics.store(HEAP32, $4 >> 2, error.code === 1 ? 0 :
          Atomics.load(HEAP32, $4 >> 2));
        Atomics.add(HEAP32, $5 >> 2, 1);
      }, {enableHighAccuracy: true, maximumAge: 10000, timeout: 30000});
      watches.set($0, watch);
    }, this, &m_latitude, &m_longitude, &m_status, &m_permission,
       &m_locationSequence);
  }

  bool stopLocation() override {
    MAIN_THREAD_EM_ASM({
      const watches = globalThis.playgroundLocationWatches;
      if (watches && watches.has($0)) {
        navigator.geolocation.clearWatch(watches.get($0));
        watches.delete($0);
      }
    }, this);
    return true;
  }

  int getPermissionStatus() override { return m_permission.load(); }

  void requirePermission() override {
    MAIN_THREAD_EM_ASM({
      navigator.geolocation.getCurrentPosition(() => {
        Atomics.store(HEAP32, $0 >> 2, 1);
        Atomics.store(HEAP32, $1 >> 2, 1);
        Atomics.add(HEAP32, $2 >> 2, 1);
      }, error => {
        Atomics.store(HEAP32, $0 >> 2, 0);
        Atomics.store(HEAP32, $1 >> 2, error.code || 1);
        Atomics.add(HEAP32, $2 >> 2, 1);
      }, {enableHighAccuracy: false, maximumAge: Infinity, timeout: 30000});
    }, &m_permission, &m_permissionResult, &m_permissionSequence);
  }

  void pump() {
    const int locationSequence = m_locationSequence.load();
    if (locationSequence != m_seenLocationSequence) {
      m_seenLocationSequence = locationSequence;
      const int status = m_status.load();
      if (status == 0) {
        notifyLocation(0, 0, m_latitude, m_longitude,
                       "browser geolocation update");
      } else {
        const char *message = status == 1 ? "browser location permission denied"
                              : status == 3 ? "browser location request timed out"
                                            : "browser location unavailable";
        notifyLocation(1, status, 0.0, 0.0, message);
      }
    }
    const int permissionSequence = m_permissionSequence.load();
    if (permissionSequence != m_seenPermissionSequence) {
      m_seenPermissionSequence = permissionSequence;
      notifyLocation(2, m_permission.load(), 0.0, 0.0,
                     m_permissionResult.load() == 1
                         ? "browser location permission granted"
                         : "browser location permission denied");
    }
  }

private:
  alignas(8) double m_latitude{};
  alignas(8) double m_longitude{};
  std::atomic<int> m_status{0};
  std::atomic<int> m_permission{0};
  std::atomic<int> m_locationSequence{0};
  std::atomic<int> m_permissionResult{0};
  std::atomic<int> m_permissionSequence{0};
  int m_seenLocationSequence{};
  int m_seenPermissionSequence{};
};

class WebMotionManager final : public IMotionManager {
public:
  ~WebMotionManager() override { stop(); }
  void start() override {
    if (m_running)
      return;
    MAIN_THREAD_EM_ASM({
      if (globalThis.DeviceOrientationEvent &&
          typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission().catch(error =>
          console.warn('motion permission was not granted', error));
      }
    });
    m_running = emscripten_set_deviceorientation_callback(
                    this, true, &WebMotionManager::onOrientation) ==
                EMSCRIPTEN_RESULT_SUCCESS;
  }
  void stop() override {
    if (m_running)
      emscripten_set_deviceorientation_callback(nullptr, false, nullptr);
    m_running = false;
  }
  float getAzimuth() override {
    return m_running ? m_azimuth.load() : 0.0f;
  }
  float getElevation() override {
    return m_running ? m_elevation.load() : 0.0f;
  }

private:
  static bool onOrientation(int, const EmscriptenDeviceOrientationEvent *event,
                            void *context) {
    auto &self = *static_cast<WebMotionManager *>(context);
    self.m_azimuth.store(static_cast<float>(event->alpha));
    self.m_elevation.store(static_cast<float>(event->beta));
    return false;
  }
  std::atomic<float> m_azimuth{0.0f};
  std::atomic<float> m_elevation{0.0f};
  bool m_running{};
};
#else
class DesktopLocationManager final : public ILocationManager {
public:
  explicit DesktopLocationManager(CKLBLocationManager *owner)
      : ILocationManager(owner) {}

  void requireLocation() override {
    const char *configured = std::getenv("PLAYGROUND_LOCATION");
    double latitude;
    double longitude;
    if (configured &&
        std::sscanf(configured, "%lf,%lf", &latitude, &longitude) == 2) {
      notifyLocation(0, 0, latitude, longitude, "desktop configured location");
    } else {
      notifyLocation(1, 0, 0.0, 0.0,
                     "Set PLAYGROUND_LOCATION=latitude,longitude to provide a "
                     "desktop location");
    }
  }
  bool stopLocation() override { return true; }
  int getPermissionStatus() override {
    return std::getenv("PLAYGROUND_LOCATION") ? 1 : 0;
  }
  void requirePermission() override {
    notifyLocation(2, getPermissionStatus(), 0.0, 0.0,
                   getPermissionStatus() ? "desktop location configured"
                                         : "desktop location not configured");
  }
};

class DesktopMotionManager final : public IMotionManager {
public:
  void start() override {
    m_running = true;
    const char *configured = std::getenv("PLAYGROUND_MOTION");
    if (configured) {
      std::sscanf(configured, "%f,%f", &m_azimuth, &m_elevation);
    }
  }
  void stop() override { m_running = false; }
  float getAzimuth() override { return m_running ? m_azimuth : 0.0f; }
  float getElevation() override { return m_running ? m_elevation : 0.0f; }

private:
  float m_azimuth{};
  float m_elevation{};
  bool m_running{};
};
#endif

#if defined(__EMSCRIPTEN__)
class WebNotificationManager final : public INotificationManager {
public:
  explicit WebNotificationManager(CKLBNotificationManager *owner)
      : INotificationManager(owner) {}

  ~WebNotificationManager() override {
    MAIN_THREAD_EM_ASM({
      const timers = globalThis.playgroundNotificationTimers;
      if (timers) {
        for (const entry of timers) {
          const key = entry[0];
          const timer = entry[1];
          if (key.startsWith($0 + '/')) {
            clearTimeout(timer);
            timers.delete(key);
          }
        }
      }
    }, reinterpret_cast<std::uintptr_t>(this));
  }

  void setLocalNotificationWithAlarm(const char *tag, int tagIndex,
                                     const char *message, int delaySeconds,
                                     const char *) override {
    const std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(this)) +
                            "/" + (tag ? tag : "") + "/" +
                            std::to_string(tagIndex);
    const std::string title = tag ? tag : "PlaygroundOSS";
    const std::string body = message ? message : "";
    MAIN_THREAD_EM_ASM({
      if (!globalThis.playgroundNotificationTimers)
        globalThis.playgroundNotificationTimers = new Map();
      const timers = globalThis.playgroundNotificationTimers;
      const key = UTF8ToString($0);
      if (timers.has(key)) clearTimeout(timers.get(key));
      const title = UTF8ToString($1);
      const body = UTF8ToString($2);
      const timer = setTimeout(() => {
        timers.delete(key);
        if (Notification.permission === 'granted')
          new Notification(title, {body});
        else
          console.info(title + ': ' + body);
      }, Math.max(0, $3) * 1000);
      timers.set(key, timer);
    }, key.c_str(), title.c_str(), body.c_str(), delaySeconds);
  }

  void cancelLocalNotification(const char *tag, int tagIndex) override {
    const std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(this)) +
                            "/" + (tag ? tag : "") + "/" +
                            std::to_string(tagIndex);
    MAIN_THREAD_EM_ASM({
      const timers = globalThis.playgroundNotificationTimers;
      const key = UTF8ToString($0);
      if (timers && timers.has(key)) {
        clearTimeout(timers.get(key));
        timers.delete(key);
      }
    }, key.c_str());
  }

  void requestPermission() override {
    MAIN_THREAD_EM_ASM({
      Notification.requestPermission().then(permission => {
        Atomics.store(HEAP32, $0 >> 2, permission === 'granted' ? 1 : 0);
        Atomics.add(HEAP32, $1 >> 2, 1);
      }).catch(() => {
        Atomics.store(HEAP32, $0 >> 2, 0);
        Atomics.add(HEAP32, $1 >> 2, 1);
      });
    }, &m_permissionResult, &m_permissionSequence);
  }

  bool getEnableNotification() override {
    return MAIN_THREAD_EM_ASM_INT({
      return Notification.permission === 'granted';
    }) != 0;
  }

  void getRemoteToken(char *buffer, int bufferLength) override {
    // Push delivery needs an application-owned push service and VAPID key.
    // Local browser notifications remain fully available without fabricating
    // a remote token.
    if (buffer && bufferLength > 0)
      buffer[0] = '\0';
  }

  void onActivityResume() override { notify(2, 0, ""); }

  void pump() {
    const int sequence = m_permissionSequence.load();
    if (sequence == m_seenPermissionSequence)
      return;
    m_seenPermissionSequence = sequence;
    const bool enabled = m_permissionResult.load() != 0;
    notify(1, enabled ? 1 : 0,
           enabled ? "browser notifications enabled"
                   : "browser notifications denied");
  }

private:
  std::atomic<int> m_permissionResult{0};
  std::atomic<int> m_permissionSequence{0};
  int m_seenPermissionSequence{};
};
#endif

struct DesktopNotification {
  std::mutex mutex;
  std::condition_variable condition;
  std::atomic<bool> cancelled{false};
};

void showDesktopNotification(const std::string &title,
                             const std::string &message) {
#if defined(__EMSCRIPTEN__)
  EM_ASM({
    const title = UTF8ToString($0);
    const body = UTF8ToString($1);
    if (Notification.permission === 'granted') {
      new Notification(title, {body});
    } else {
      console.info(title + ': ' + body);
    }
  }, title.c_str(), message.c_str());
#elif defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(),
                           message.c_str(), nullptr);
#else
  pid_t process = 0;
  char *arguments[] = {const_cast<char *>("notify-send"),
                       const_cast<char *>(title.c_str()),
                       const_cast<char *>(message.c_str()), nullptr};
  if (posix_spawnp(&process, "notify-send", nullptr, nullptr, arguments,
                   environ) == 0) {
    int status;
    waitpid(process, &status, 0);
  }
#endif
}

class DesktopNotificationManager final : public INotificationManager {
public:
  explicit DesktopNotificationManager(CKLBNotificationManager *owner)
      : INotificationManager(owner) {}

  ~DesktopNotificationManager() override {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &notification : m_notifications) {
      notification.second->cancelled = true;
      notification.second->condition.notify_all();
    }
  }

  void setLocalNotificationWithAlarm(const char *tag, int tagIndex,
                                     const char *message, int delaySeconds,
                                     const char *) override {
    cancelLocalNotification(tag, tagIndex);
    const std::string key =
        std::string(tag ? tag : "") + "/" + std::to_string(tagIndex);
    auto state = std::make_shared<DesktopNotification>();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_notifications[key] = state;
    }
    const std::string title = tag ? tag : "PlaygroundOSS";
    const std::string body = message ? message : "";
    std::thread([state, title, body, delaySeconds] {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->condition.wait_for(lock,
                                std::chrono::seconds(std::max(delaySeconds, 0)),
                                [state] { return state->cancelled.load(); });
      if (!state->cancelled)
        showDesktopNotification(title, body);
    }).detach();
  }
  void cancelLocalNotification(const char *tag, int tagIndex) override {
    const std::string key =
        std::string(tag ? tag : "") + "/" + std::to_string(tagIndex);
    std::shared_ptr<DesktopNotification> state;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto found = m_notifications.find(key);
      if (found == m_notifications.end())
        return;
      state = found->second;
      m_notifications.erase(found);
    }
    state->cancelled = true;
    state->condition.notify_all();
  }
  void requestPermission() override {
#if defined(__EMSCRIPTEN__)
    EM_ASM({
      if (Notification.permission === 'default') {
        Notification.requestPermission();
      }
    });
#endif
    notify(1, 1, "desktop notifications enabled");
  }
  bool getEnableNotification() override {
#if defined(__EMSCRIPTEN__)
    return EM_ASM_INT({ return Notification.permission === 'granted'; }) != 0;
#elif defined(_WIN32)
    return true;
#else
    return std::filesystem::exists("/usr/bin/notify-send");
#endif
  }
  void getRemoteToken(char *buffer, int bufferLength) override {
    if (!buffer || bufferLength <= 0)
      return;
    char deviceId[128]{};
    CPFInterface::getInstance().platform().getDevID(deviceId, sizeof(deviceId));
    std::snprintf(buffer, static_cast<size_t>(bufferLength), "desktop:%s",
                  deviceId);
  }
  void onActivityResume() override { notify(2, 0, ""); }

private:
  std::mutex m_mutex;
  std::map<std::string, std::shared_ptr<DesktopNotification>> m_notifications;
};

} // namespace

extern IAdManager *g_adManager;

IAdManager *IAdManager::getInstance(CKLBAdManager *owner) {
  if (!g_adManager) {
    g_adManager = new DesktopAdManager(owner);
  }
  return g_adManager;
}

ILocationManager *ILocationManager::create(CKLBLocationManager *owner) {
  if (!s_instance) {
#if defined(__EMSCRIPTEN__)
    s_instance = new WebLocationManager(owner);
#else
    s_instance = new DesktopLocationManager(owner);
#endif
  }
  return s_instance;
}

IMotionManager *IMotionManager::getInstance() {
  if (!s_instance) {
#if defined(__EMSCRIPTEN__)
    s_instance = new WebMotionManager();
#else
    s_instance = new DesktopMotionManager();
#endif
  }
  return s_instance;
}

#if defined(__EMSCRIPTEN__)
namespace playground::runtime {
void pumpWebPlatformServices() {
  if (ILocationManager::getInstance())
    static_cast<WebLocationManager *>(ILocationManager::getInstance())->pump();
  if (INotificationManager::getInstance())
    static_cast<WebNotificationManager *>(INotificationManager::getInstance())
        ->pump();
}
} // namespace playground::runtime
#endif

INotificationManager *
INotificationManager::create(CKLBNotificationManager *owner) {
  if (!s_instance) {
#if defined(__EMSCRIPTEN__)
    s_instance = new WebNotificationManager(owner);
#else
    s_instance = new DesktopNotificationManager(owner);
#endif
  }
  return s_instance;
}

void INotificationManager::notify(u32 callbackIndex, int parameter,
                                  const char *message) {
  m_owner->queueNotification(callbackIndex, parameter, message);
}

bool KLBCreateDirectories(const char *path) {
  if (!path || !path[0]) {
    return false;
  }

  std::filesystem::path directory(path);
  const size_t length = std::strlen(path);
  if (length && path[length - 1] != '/' && path[length - 1] != '\\') {
    directory = directory.parent_path();
  }
  if (directory.empty()) {
    return true;
  }
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return !error;
}

const char *getFullNativePath(const char *path) {
  return CPFInterface::getInstance().platform().getFullPath(path);
}

void removeTmpFileNative(const char *filePath) {
  std::error_code error;
  if (!std::filesystem::remove(filePath, error) && !error) {
    std::filesystem::remove_all(filePath, error);
  }
}

extern "C" void assertFunction(int line, const char *file, const char *message,
                               ...) {
  char formatted[2048];
  va_list arguments;
  va_start(arguments, message);
  std::vsnprintf(formatted, sizeof(formatted), message, arguments);
  va_end(arguments);

  std::fprintf(stderr, "Assert l.%d in %s:\n%s\n", line, file ? file : "",
               formatted);
  std::fflush(stderr);
}

extern "C" void msgBox(char *message) {
  std::fprintf(stderr, "%s\n", message ? message : "");
}
