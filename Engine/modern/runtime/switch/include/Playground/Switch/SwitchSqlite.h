#ifndef PLAYGROUND_SWITCH_SQLITE_H
#define PLAYGROUND_SWITCH_SQLITE_H

#include <string>

namespace playground {
namespace switch_runtime {
inline const char *switchPathVfsName() {
  return "playground-switch-paths";
}

bool initializeSwitchSqlitePaths(std::string &error);
void shutdownSwitchSqlitePaths();
} // namespace switch_runtime
} // namespace playground

#endif
