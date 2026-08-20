#include "Playground/Switch/SwitchAlbum.h"

#include <png.h>
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace playground::switch_runtime {
namespace {

constexpr unsigned AlbumWidth = 1280;
constexpr unsigned AlbumHeight = 720;

std::string resultError(const char *operation, Result result) {
  char text[128];
  std::snprintf(text, sizeof(text), "%s failed with result 0x%08x", operation,
                result);
  return text;
}

} // namespace

bool savePngToAlbum(const std::string &path, std::string &error) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path.c_str())) {
    error = "cannot decode screenshot PNG: " + std::string(image.message);
    return false;
  }
  if (!image.width || !image.height ||
      image.width > std::numeric_limits<unsigned>::max() / image.height) {
    png_image_free(&image);
    error = "screenshot PNG dimensions are invalid";
    return false;
  }
  image.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> source(PNG_IMAGE_SIZE(image));
  if (!png_image_finish_read(&image, nullptr, source.data(), 0, nullptr)) {
    error =
        "cannot finish decoding screenshot PNG: " + std::string(image.message);
    png_image_free(&image);
    return false;
  }

  std::vector<unsigned char> output(AlbumWidth * AlbumHeight * 4, 0);
  for (std::size_t index = 3; index < output.size(); index += 4)
    output[index] = 0xff;
  const double scale =
      std::min(static_cast<double>(AlbumWidth) / image.width,
               static_cast<double>(AlbumHeight) / image.height);
  const unsigned width =
      std::max(1U, static_cast<unsigned>(image.width * scale));
  const unsigned height =
      std::max(1U, static_cast<unsigned>(image.height * scale));
  const unsigned left = (AlbumWidth - width) / 2;
  const unsigned top = (AlbumHeight - height) / 2;
  for (unsigned y = 0; y < height; ++y) {
    const unsigned sourceY =
        std::min(image.height - 1,
                 static_cast<unsigned>(static_cast<double>(y) / scale));
    for (unsigned x = 0; x < width; ++x) {
      const unsigned sourceX =
          std::min(image.width - 1,
                   static_cast<unsigned>(static_cast<double>(x) / scale));
      const std::size_t sourceOffset =
          (static_cast<std::size_t>(sourceY) * image.width + sourceX) * 4;
      const std::size_t outputOffset =
          (static_cast<std::size_t>(top + y) * AlbumWidth + left + x) * 4;
      std::memcpy(output.data() + outputOffset, source.data() + sourceOffset,
                  4);
    }
  }

  Result result = capssuInitialize();
  if (R_FAILED(result)) {
    error = resultError("capssuInitialize", result);
    return false;
  }
  result = capssuSaveScreenShot(output.data(), output.size(),
                                AlbumReportOption_Enable,
                                AlbumImageOrientation_Unknown0, nullptr);
  capssuExit();
  if (R_FAILED(result)) {
    error = resultError("capssuSaveScreenShot", result);
    return false;
  }
  return true;
}

} // namespace playground::switch_runtime
