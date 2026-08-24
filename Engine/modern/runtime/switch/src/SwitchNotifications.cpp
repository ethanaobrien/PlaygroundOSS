#include "Playground/Switch/SwitchNotifications.h"

#include <switch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace playground::switch_runtime {
namespace {

constexpr std::array<std::uint8_t, 7> PayloadMagic{
    'P', 'G', 'N', 'O', 'T', 'I', 'F'};
constexpr std::uint8_t LegacyPayloadVersion = 1;
constexpr std::uint8_t PayloadVersion = 2;
constexpr std::size_t LegacyPayloadSize = PayloadMagic.size() + 2;
constexpr std::size_t PayloadSize = PayloadMagic.size() + 2 + sizeof(std::uint64_t);
// Horizon exposes a weekly alarm schedule. Any target at least one full week
// away would fire on an earlier matching weekday and cannot represent the
// engine's one-shot delay contract.
constexpr int MaximumOneShotDelaySeconds = 7 * 24 * 60 * 60 - 60;
bool g_ready{};

struct NotificationPayload {
  std::uint8_t tagIndex{};
  std::uint64_t dueTime{};
  bool legacy{};
};

std::string resultError(const char *operation, Result result) {
  char text[160];
  std::snprintf(text, sizeof(text), "%s failed with result 0x%08x",
                operation, result);
  return text;
}

std::array<std::uint8_t, PayloadSize>
makePayload(std::uint8_t tagIndex, std::uint64_t dueTime) {
  std::array<std::uint8_t, PayloadSize> payload{};
  std::copy(PayloadMagic.begin(), PayloadMagic.end(), payload.begin());
  payload[PayloadMagic.size()] = PayloadVersion;
  payload[PayloadMagic.size() + 1] = tagIndex;
  for (std::size_t index = 0; index < sizeof(dueTime); ++index)
    payload[PayloadMagic.size() + 2 + index] =
        static_cast<std::uint8_t>(dueTime >> (index * 8));
  return payload;
}

bool parsePayload(const void *data, std::size_t size,
                  NotificationPayload &payload) {
  if (!data || (size != LegacyPayloadSize && size != PayloadSize))
    return false;
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  if (!std::equal(PayloadMagic.begin(), PayloadMagic.end(), bytes))
    return false;
  const std::uint8_t version = bytes[PayloadMagic.size()];
  if (version == LegacyPayloadVersion && size == LegacyPayloadSize) {
    payload.tagIndex = bytes[PayloadMagic.size() + 1];
    payload.legacy = true;
    return true;
  }
  if (version != PayloadVersion || size != PayloadSize)
    return false;
  payload.tagIndex = bytes[PayloadMagic.size() + 1];
  for (std::size_t index = 0; index < sizeof(payload.dueTime); ++index)
    payload.dueTime |= static_cast<std::uint64_t>(
                           bytes[PayloadMagic.size() + 2 + index])
                       << (index * 8);
  return true;
}

bool sameUser(const AccountUid &left, const AccountUid &right) {
  return left.uid[0] == right.uid[0] && left.uid[1] == right.uid[1];
}

bool selectedUser(AccountUid &user, std::string &error) {
  const Result result = accountGetPreselectedUser(&user);
  if (R_FAILED(result)) {
    error = resultError("accountGetPreselectedUser for notification", result);
    return false;
  }
  if (!accountUidIsValid(&user)) {
    error = "Notification alarm requires the launch-selected account";
    return false;
  }
  return true;
}

bool findTag(std::uint8_t tagIndex, const AccountUid &user,
             std::vector<u16> &alarmIds, std::string &error) {
  std::array<NotifAlarmSetting, NOTIF_MAX_ALARMS> settings{};
  s32 count = 0;
  Result result =
      notifListAlarmSettings(settings.data(), settings.size(), &count);
  if (R_FAILED(result)) {
    error = resultError("notifListAlarmSettings", result);
    return false;
  }
  const s32 bounded = std::min<s32>(count, settings.size());
  for (s32 index = 0; index < bounded; ++index) {
    // Alarm registrations are per account even though notif:a lists every
    // registration owned by this application. Never let one selected user
    // cancel another user's reminder merely because the engine tag matches.
    if (!sameUser(settings[index].uid, user))
      continue;
    std::array<std::uint8_t, 0x400> parameter{};
    u32 actual = 0;
    result = notifLoadApplicationParameter(
        settings[index].alarm_setting_id, parameter.data(), parameter.size(),
        &actual);
    if (R_FAILED(result)) {
      error = resultError("notifLoadApplicationParameter", result);
      return false;
    }
    NotificationPayload payload{};
    if (!parsePayload(parameter.data(), actual, payload) ||
        payload.tagIndex != tagIndex)
      continue;
    alarmIds.push_back(settings[index].alarm_setting_id);
  }
  return true;
}

bool deleteTag(std::uint8_t tagIndex, const AccountUid &user,
               std::string &error) {
  std::vector<u16> alarmIds;
  if (!findTag(tagIndex, user, alarmIds, error))
    return false;
  for (u16 alarmId : alarmIds) {
    const Result result = notifDeleteAlarmSetting(alarmId);
    if (R_FAILED(result)) {
      error = resultError("notifDeleteAlarmSetting", result);
      return false;
    }
  }
  return true;
}

bool storeTag(std::uint8_t tagIndex, const AccountUid &user,
              NotifAlarmSetting &setting, const void *payload,
              std::size_t payloadSize, std::string &error) {
  std::vector<u16> alarmIds;
  if (!findTag(tagIndex, user, alarmIds, error))
    return false;
  Result result = 0;
  if (alarmIds.empty()) {
    u16 alarmId = 0;
    result = notifRegisterAlarmSetting(&alarmId, &setting, payload,
                                       payloadSize);
    if (R_FAILED(result)) {
      error = resultError("notifRegisterAlarmSetting", result);
      return false;
    }
    return true;
  }

  // Updating the existing account/tag registration avoids a delete/register
  // gap and also works when this application already owns all eight alarm
  // slots. Only retire accidental duplicates after the replacement is live.
  setting.alarm_setting_id = alarmIds.front();
  result = notifUpdateAlarmSetting(&setting, payload, payloadSize);
  if (R_FAILED(result)) {
    error = resultError("notifUpdateAlarmSetting", result);
    return false;
  }
  for (std::size_t index = 1; index < alarmIds.size(); ++index) {
    result = notifDeleteAlarmSetting(alarmIds[index]);
    if (R_FAILED(result)) {
      error = resultError("notifDeleteAlarmSetting duplicate", result);
      return false;
    }
  }
  return true;
}

bool deleteExpired(const AccountUid &user, std::string &error) {
  std::array<NotifAlarmSetting, NOTIF_MAX_ALARMS> settings{};
  s32 count = 0;
  Result result =
      notifListAlarmSettings(settings.data(), settings.size(), &count);
  if (R_FAILED(result)) {
    error = resultError("notifListAlarmSettings", result);
    return false;
  }
  const std::time_t now = std::time(nullptr);
  const s32 bounded = std::min<s32>(count, settings.size());
  for (s32 index = 0; index < bounded; ++index) {
    if (!sameUser(settings[index].uid, user))
      continue;
    std::array<std::uint8_t, 0x400> parameter{};
    u32 actual = 0;
    result = notifLoadApplicationParameter(
        settings[index].alarm_setting_id, parameter.data(), parameter.size(),
        &actual);
    if (R_FAILED(result)) {
      error = resultError("notifLoadApplicationParameter", result);
      return false;
    }
    NotificationPayload payload{};
    if (!parsePayload(parameter.data(), actual, payload))
      continue;
    // Version-1 registrations had no due timestamp and would otherwise recur
    // every week forever. They are safe to retire during the format upgrade.
    if (!payload.legacy && payload.dueTime > static_cast<std::uint64_t>(now))
      continue;
    result = notifDeleteAlarmSetting(settings[index].alarm_setting_id);
    if (R_FAILED(result)) {
      error = resultError("notifDeleteAlarmSetting", result);
      return false;
    }
  }
  return true;
}

} // namespace

bool initializeNotifications(std::string &error) {
  if (g_ready)
    return true;
  if (hosversionBefore(9, 0, 0)) {
    error = "Horizon application alarms require system version 9.0.0 or later";
    return false;
  }
  const Result result = notifInitialize(NotifServiceType_Application);
  if (R_FAILED(result)) {
    error = resultError("notifInitialize", result);
    return false;
  }
  g_ready = true;
  return true;
}

void shutdownNotifications() {
  if (!g_ready)
    return;
  notifExit();
  g_ready = false;
}

bool notificationsAvailable() { return g_ready; }

bool scheduleNotification(std::uint8_t tagIndex, int delaySeconds,
                          std::string &error) {
  if (!g_ready) {
    error = "Horizon notification service is unavailable";
    return false;
  }
  if (delaySeconds > MaximumOneShotDelaySeconds) {
    error = "Horizon weekly alarms cannot represent a one-shot delay of " +
            std::to_string(delaySeconds) + " seconds";
    return false;
  }

  AccountUid user{};
  if (!selectedUser(user, error))
    return false;
  u64 applicationId = 0;
  Result result = svcGetInfo(&applicationId, InfoType_ProgramId,
                             CUR_PROCESS_HANDLE, 0);
  if (R_FAILED(result)) {
    error = resultError("notification application ID lookup", result);
    return false;
  }

  const auto now = std::chrono::system_clock::now();
  const auto delay = std::chrono::seconds(std::max(delaySeconds, 0));
  auto target = std::chrono::time_point_cast<std::chrono::minutes>(
      now + delay + std::chrono::seconds(59));
  if (target <= now)
    target += std::chrono::minutes(1);
  const std::time_t targetTime = std::chrono::system_clock::to_time_t(target);
  std::tm local{};
  if (!localtime_r(&targetTime, &local)) {
    error = "Notification target could not be converted to local time";
    return false;
  }

  NotifAlarmSetting setting{};
  notifAlarmSettingCreate(&setting);
  notifAlarmSettingSetUid(&setting, user);
  setting.application_id = applicationId;
  result = notifAlarmSettingEnable(
      &setting, static_cast<u32>(local.tm_wday), local.tm_hour, local.tm_min);
  if (R_FAILED(result)) {
    error = resultError("notifAlarmSettingEnable", result);
    return false;
  }
  const auto payload = makePayload(
      tagIndex, static_cast<std::uint64_t>(targetTime));
  return storeTag(tagIndex, user, setting, payload.data(), payload.size(),
                  error);
}

bool cancelNotification(std::uint8_t tagIndex, std::string &error) {
  if (!g_ready) {
    error = "Horizon notification service is unavailable";
    return false;
  }
  AccountUid user{};
  return selectedUser(user, error) && deleteTag(tagIndex, user, error);
}

bool consumeTriggeredNotifications(std::vector<std::uint8_t> &tagIndices,
                                   std::string &error) {
  if (!g_ready)
    return true;
  AccountUid user{};
  if (!selectedUser(user, error))
    return false;

  Event available{};
  Result result = notifGetNotificationSystemEvent(&available);
  if (R_FAILED(result)) {
    error = resultError("notifGetNotificationSystemEvent", result);
    return false;
  }
  const auto closeAvailable = [&available]() { eventClose(&available); };
  for (;;) {
    result = eventWait(&available, 0);
    if (result == MAKERESULT(Module_Kernel, KernelError_TimedOut))
      break;
    if (R_FAILED(result)) {
      error = resultError("notification-channel eventWait", result);
      closeAvailable();
      return false;
    }

    std::array<std::uint8_t, 0x400> parameter{};
    u64 actual = 0;
    result = notifTryPopNotifiedApplicationParameter(
        parameter.data(), parameter.size(), &actual);
    if (R_FAILED(result)) {
      error = resultError("notifTryPopNotifiedApplicationParameter", result);
      closeAvailable();
      return false;
    }
    NotificationPayload payload{};
    if (!parsePayload(parameter.data(), static_cast<std::size_t>(actual),
                      payload))
      continue;
    tagIndices.push_back(payload.tagIndex);
    if (!deleteTag(payload.tagIndex, user, error)) {
      closeAvailable();
      return false;
    }
  }
  closeAvailable();
  // Notification storage only contains a payload when an alarm launched the
  // title from sleep. A manual resume after the due minute must also retire
  // the weekly schedule so the one-shot never repeats seven days later.
  return deleteExpired(user, error);
}

} // namespace playground::switch_runtime
