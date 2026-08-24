#include "Playground/Switch/StorageProvision.h"
#include "Playground/Switch/SwitchStorage.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t Missing = 0x7d402;

playground::switch_runtime::StorageProvisionOutcome run(
    std::vector<std::uint32_t> mounts, std::uint32_t createResult,
    std::string &events) {
  std::size_t mountIndex = 0;
  return playground::switch_runtime::provisionStorage(
      [&] {
        events += 'M';
        assert(mountIndex < mounts.size());
        return mounts[mountIndex++];
      },
      [&] {
        events += 'C';
        return createResult;
      },
      [](std::uint32_t result) { return result == Missing; });
}

} // namespace

int main() {
  std::string events;
  auto outcome = run({0}, 99, events);
  assert(outcome.mounted && !outcome.creationAttempted && events == "M");

  events.clear();
  outcome = run({0x1234}, 0, events);
  assert(!outcome.mounted && !outcome.creationAttempted && events == "M");

  events.clear();
  outcome = run({Missing, 0}, 0, events);
  assert(outcome.mounted && outcome.creationAttempted && events == "MCM");

  events.clear();
  outcome = run({Missing, 0}, 0x5678, events);
  assert(outcome.mounted && outcome.creationAttempted &&
         outcome.creationResult == 0x5678 && events == "MCM");

  events.clear();
  outcome = run({Missing, Missing}, 0, events);
  assert(!outcome.mounted && outcome.creationAttempted &&
         outcome.retryResult == Missing && events == "MCM");
  return 0;
}
