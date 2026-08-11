#include "Playground/Runtime/DesktopPlatform.h"

#include "CPFInterface.h"
#include "KLBAudioSystem.h"
#include "encryptFile.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char *message) {
  std::fprintf(stderr, "audio-probe: %s\n", message);
  std::fflush(nullptr);
  std::_Exit(1);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::fprintf(
        stderr,
        "usage: %s install-root external-root asset-without-extension\n",
        argv[0]);
    return 64;
  }
  if (!SDL_Init(SDL_INIT_AUDIO))
    fail(SDL_GetError());

  playground::runtime::DesktopPlatform platform(argv[1], argv[2]);
  CPFInterface::getInstance().setPlatformRequest(&platform);
  initNMAsset(0);
  if (!platform.init())
    fail("platform audio initialization failed");

  KLBAudioImplementation *audio = getNewAudioImplementation();
  void *asset = audio->loadAudio(argv[3], true, 0, 0);
  if (!asset)
    fail("official audio asset did not load");
  if (audio->totalTimeAudio(asset) <= 0)
    fail("audio duration is invalid");
  if (!audio->preLoad(asset))
    fail("audio preload failed");
  if (!audio->playAudio(asset, 0, 1.0f, 1.0f))
    fail("audio playback failed");
  SDL_Delay(100);
  audio->stopAudio(asset, false, 0.0f, 0);
  audio->releaseAudio(asset);
  platform.shutdownAudioSystem();
  SDL_Quit();

  std::puts("audio-probe passed");
  std::fflush(nullptr);
  std::_Exit(0);
}
