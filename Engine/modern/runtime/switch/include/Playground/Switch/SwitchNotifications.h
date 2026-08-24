#ifndef PLAYGROUND_SWITCH_NOTIFICATIONS_H
#define PLAYGROUND_SWITCH_NOTIFICATIONS_H

#include <cstdint>
#include <string>
#include <vector>

namespace playground::switch_runtime {

// Owns Horizon's notif:a session. Alarm registration is account-associated
// and uses a private application parameter so triggered one-shot alarms can
// be recognized and removed when the title resumes.
bool initializeNotifications(std::string &error);
void shutdownNotifications();
bool notificationsAvailable();
bool scheduleNotification(std::uint8_t tagIndex, int delaySeconds,
                          std::string &error);
bool cancelNotification(std::uint8_t tagIndex, std::string &error);
bool consumeTriggeredNotifications(std::vector<std::uint8_t> &tagIndices,
                                   std::string &error);

} // namespace playground::switch_runtime

#endif
