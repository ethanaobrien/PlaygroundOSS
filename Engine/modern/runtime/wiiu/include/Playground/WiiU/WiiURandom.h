#ifndef PLAYGROUND_WIIU_RANDOM_H
#define PLAYGROUND_WIIU_RANDOM_H

#include <cstddef>
#include <filesystem>
#include <string>

namespace playground::wiiu_runtime {

bool initializeRandom(const std::filesystem::path &seedPath,
                      const void *startupEntropy,
                      std::size_t startupEntropySize,
                      std::string &error);
void shutdownRandom();

} // namespace playground::wiiu_runtime

#endif
