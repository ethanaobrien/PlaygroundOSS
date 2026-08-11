#include "Playground/Runtime/DesktopPlatform.h"

#include "CKLBTextureMovie.h"
#include "CPFInterface.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s movie-path\n", argv[0]);
    return 64;
  }
  playground::runtime::DesktopPlatform platform(".",
                                                "/tmp/playground-movie-probe");
  CPFInterface::getInstance().setPlatformRequest(&platform);
  IMovieInterface *movie = platform.createMoviePlayer(argv[1], -1, -1);
  if (!movie || !movie->isInfoReady()) {
    std::fprintf(stderr, "movie-probe: movie initialization failed\n");
    std::_Exit(1);
  }
  s32 size[2]{};
  movie->getSize(size);
  if (size[0] <= 0 || size[1] <= 0) {
    std::fprintf(stderr, "movie-probe: invalid dimensions\n");
    std::_Exit(1);
  }
  movie->setPlay();
  for (int attempt = 0; attempt < 100 && !movie->isFrameReady(); ++attempt) {
    movie->update();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!movie->isFrameReady()) {
    std::fprintf(stderr, "movie-probe: no decoded frame\n");
    std::_Exit(1);
  }
  platform.destroyMoviePlayer(movie);
  std::printf("movie-probe passed (%dx%d)\n", size[0], size[1]);
  std::fflush(nullptr);
  std::_Exit(0);
}
