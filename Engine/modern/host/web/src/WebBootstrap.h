#ifndef PLAYGROUND_HOST_WEB_BOOTSTRAP_H
#define PLAYGROUND_HOST_WEB_BOOTSTRAP_H

#include <string>

namespace playground::web {

struct RuntimePaths {
  std::string installRoot;
  std::string externalRoot;
};

bool prepareBrowserStorage(RuntimePaths &paths, std::string &error);
void showFatalError(const std::string &error);

} // namespace playground::web

#endif
