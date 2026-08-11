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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <string>
#include <thread>

#include <sys/wait.h>

extern char **environ;

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

struct DesktopNotification {
  std::mutex mutex;
  std::condition_variable condition;
  std::atomic<bool> cancelled{false};
};

void showDesktopNotification(const std::string &title,
                             const std::string &message) {
  pid_t process = 0;
  char *arguments[] = {const_cast<char *>("notify-send"),
                       const_cast<char *>(title.c_str()),
                       const_cast<char *>(message.c_str()), nullptr};
  if (posix_spawnp(&process, "notify-send", nullptr, nullptr, arguments,
                   environ) == 0) {
    int status;
    waitpid(process, &status, 0);
  }
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
    notify(1, 1, "desktop notifications enabled");
  }
  bool getEnableNotification() override {
    return std::filesystem::exists("/usr/bin/notify-send");
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
    s_instance = new DesktopLocationManager(owner);
  }
  return s_instance;
}

IMotionManager *IMotionManager::getInstance() {
  if (!s_instance) {
    s_instance = new DesktopMotionManager();
  }
  return s_instance;
}

INotificationManager *
INotificationManager::create(CKLBNotificationManager *owner) {
  if (!s_instance) {
    s_instance = new DesktopNotificationManager(owner);
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
