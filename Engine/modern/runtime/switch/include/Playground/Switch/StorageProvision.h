#ifndef PLAYGROUND_SWITCH_STORAGE_PROVISION_H
#define PLAYGROUND_SWITCH_STORAGE_PROVISION_H

#include <cstdint>

namespace playground::switch_runtime {

struct StorageProvisionOutcome {
  std::uint32_t initialResult{};
  std::uint32_t creationResult{};
  std::uint32_t retryResult{};
  bool creationAttempted{};
  bool mounted{};
};

// Open first and provision only when the platform positively identifies an
// absent store. The post-create open is unconditional: a concurrent creator
// may legitimately make create fail while still making the storage usable.
template <typename Mount, typename Create, typename IsMissing>
StorageProvisionOutcome provisionStorage(Mount mount, Create create,
                                          IsMissing isMissing) {
  StorageProvisionOutcome outcome;
  outcome.initialResult = static_cast<std::uint32_t>(mount());
  if (outcome.initialResult == 0) {
    outcome.mounted = true;
    return outcome;
  }
  if (!isMissing(outcome.initialResult))
    return outcome;

  outcome.creationAttempted = true;
  outcome.creationResult = static_cast<std::uint32_t>(create());
  outcome.retryResult = static_cast<std::uint32_t>(mount());
  outcome.mounted = outcome.retryResult == 0;
  return outcome;
}

} // namespace playground::switch_runtime

#endif
