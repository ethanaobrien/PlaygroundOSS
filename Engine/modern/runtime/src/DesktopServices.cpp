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
#include "NotificationManager.h"
#include "CPFInterface.h"
#include "assert_klb.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

class DesktopAdManager final : public IAdManager {
public:
    explicit DesktopAdManager(CKLBAdManager* owner) : IAdManager(owner) {}
    void preloadAd(bool, const char*) override {}
    void showAd() override {}
};

class DesktopLocationManager final : public ILocationManager {
public:
    explicit DesktopLocationManager(CKLBLocationManager* owner)
    : ILocationManager(owner) {}

    void requireLocation() override {}
    bool stopLocation() override { return true; }
    int getPermissionStatus() override { return 0; }
    void requirePermission() override {}
};

class DesktopMotionManager final : public IMotionManager {
public:
    void start() override {}
    void stop() override {}
    float getAzimuth() override { return 0.0f; }
    float getElevation() override { return 0.0f; }
};

class DesktopNotificationManager final : public INotificationManager {
public:
    explicit DesktopNotificationManager(CKLBNotificationManager* owner)
    : INotificationManager(owner) {}

    void setLocalNotificationWithAlarm(
        const char*, int, const char*, int, const char*) override {}
    void cancelLocalNotification(const char*, int) override {}
    void requestPermission() override {}
    bool getEnableNotification() override { return false; }
    void getRemoteToken(char* buffer, int bufferLength) override
    {
        if(buffer && bufferLength > 0) {
            buffer[0] = '\0';
        }
    }
    void onActivityResume() override {}
};

} // namespace

extern IAdManager* g_adManager;

IAdManager* IAdManager::getInstance(CKLBAdManager* owner)
{
    if(!g_adManager) {
        g_adManager = new DesktopAdManager(owner);
    }
    return g_adManager;
}

ILocationManager* ILocationManager::create(CKLBLocationManager* owner)
{
    if(!s_instance) {
        s_instance = new DesktopLocationManager(owner);
    }
    return s_instance;
}

IMotionManager* IMotionManager::getInstance()
{
    if(!s_instance) {
        s_instance = new DesktopMotionManager();
    }
    return s_instance;
}

INotificationManager* INotificationManager::create(
    CKLBNotificationManager* owner)
{
    if(!s_instance) {
        s_instance = new DesktopNotificationManager(owner);
    }
    return s_instance;
}

void INotificationManager::notify(
    u32 callbackIndex, int parameter, const char* message)
{
    m_owner->queueNotification(callbackIndex, parameter, message);
}

bool KLBCreateDirectories(const char* path)
{
    if(!path || !path[0]) {
        return false;
    }

    std::filesystem::path directory(path);
    const size_t length = std::strlen(path);
    if(length && path[length - 1] != '/' && path[length - 1] != '\\') {
        directory = directory.parent_path();
    }
    if(directory.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return !error;
}

const char* getFullNativePath(const char* path)
{
    return CPFInterface::getInstance().platform().getFullPath(path);
}

void removeTmpFileNative(const char* filePath)
{
    std::error_code error;
    if(!std::filesystem::remove(filePath, error) && !error) {
        std::filesystem::remove_all(filePath, error);
    }
}

extern "C" void assertFunction(
    int line, const char* file, const char* message, ...)
{
    char formatted[2048];
    va_list arguments;
    va_start(arguments, message);
    std::vsnprintf(formatted, sizeof(formatted), message, arguments);
    va_end(arguments);

    std::fprintf(stderr, "Assert l.%d in %s:\n%s\n",
        line, file ? file : "", formatted);
    std::fflush(stderr);
}

extern "C" void msgBox(char* message)
{
    std::fprintf(stderr, "%s\n", message ? message : "");
}
