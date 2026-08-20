#ifndef PLAYGROUND_SWITCH_ALBUM_H
#define PLAYGROUND_SWITCH_ALBUM_H

#include <string>

namespace playground::switch_runtime {

bool savePngToAlbum(const std::string &path, std::string &error);

} // namespace playground::switch_runtime

#endif
