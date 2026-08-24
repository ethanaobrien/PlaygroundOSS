#ifndef PLAYGROUND_SWITCH_CRYPTO_H
#define PLAYGROUND_SWITCH_CRYPTO_H

#include <string>

namespace playground::switch_runtime {

// The installed application owns the Horizon CSRNG service for its complete
// runtime. Keeping this explicit makes crypto failures visible at startup and
// prevents random generation from dispatching through an unopened session.
bool initializeCryptoService(std::string &error);
void shutdownCryptoService();

} // namespace playground::switch_runtime

#endif
