#ifndef PLAYGROUND_WIIU_ARCHIVE_H
#define PLAYGROUND_WIIU_ARCHIVE_H

#include <filesystem>
#include <string>

namespace playground::wiiu_runtime {

// Returns a minizip-compatible unzFile backed by CafeOS descriptor I/O.
void *openArchive(const std::filesystem::path &path, std::string &error);

} // namespace playground::wiiu_runtime

#endif
