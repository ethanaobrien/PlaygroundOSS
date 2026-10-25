#include <cerrno>
#include <sys/types.h>

// libnx has no Unix uid/gid permission model. Some portable libraries retain
// optional references to these functions even though Horizon cannot implement
// their process/filesystem semantics. Keep the compatibility boundary local
// to the Switch runtime instead of leaking console conditions into common
// engine or third-party source.
extern "C" int fchown(int, uid_t, gid_t) {
  errno = ENOSYS;
  return -1;
}

extern "C" mode_t umask(mode_t) { return 0; }
