/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0
*/

#include "KLBOpenSLNewEngine.h"

#include "CPFInterface.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

KLBAudioImplementation *g_audioImplementation = nullptr;

namespace {

KLBOpenSLNewEngine *platformEngine;
SDL_AudioStream *platformStream;

} // namespace

void desktopAudioStreamCallback(void *userdata, SDL_AudioStream *stream,
                                int additionalAmount, int) {
  auto *engine = static_cast<KLBOpenSLNewEngine *>(userdata);
  if (!engine || engine != platformEngine || additionalAmount <= 0)
    return;

  thread_local std::array<s16, 8192> samples;
  engine->beginPlatformCallback();
  engine->m_workerAcknowledged = true;
  while (additionalAmount > 0) {
    const int frames = std::min(additionalAmount / 4, 4096);
    if (frames <= 0)
      break;
    if (engine->m_workerEnabled) {
      engine->prepareOutputBuffer();
      engine->renderAudio(samples.data(), static_cast<u16>(frames));
    } else {
      std::memset(samples.data(), 0, frames * 4);
      engine->m_workerWaiting = true;
    }
    if (!SDL_PutAudioStreamData(stream, samples.data(), frames * 4)) {
      std::fprintf(stderr, "SDL audio stream write failed: %s\n",
                   SDL_GetError());
      break;
    }
    additionalAmount -= frames * 4;
  }
  engine->m_workerAcknowledged = false;
}

bool initializeOpenSLPlatform(KLBOpenSLNewEngine *engine) {
  if (platformEngine || !engine)
    return false;

  SDL_AudioSpec format{};
  format.format = SDL_AUDIO_S16;
  format.channels = 2;
  format.freq = 44100;
  platformStream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format,
                                desktopAudioStreamCallback, engine);
  if (!platformStream) {
    std::fprintf(stderr, "Unable to open SDL audio output: %s\n",
                 SDL_GetError());
    return false;
  }

  platformEngine = engine;
  engine->m_sampleRate = 44100;
  engine->m_mixBufferSamples = 512;
  engine->m_outputInterface = platformStream;
  if (!SDL_ResumeAudioStreamDevice(platformStream)) {
    std::fprintf(stderr, "Unable to start SDL audio output: %s\n",
                 SDL_GetError());
    SDL_DestroyAudioStream(platformStream);
    platformStream = nullptr;
    platformEngine = nullptr;
    return false;
  }
  return true;
}

u32 KLBOpenSLNewEngine::getMixBufferSamples() const {
  return m_mixBufferSamples;
}

void shutdownOpenSLPlatform(KLBOpenSLNewEngine *) {
  SDL_AudioStream *stream = platformStream;
  platformStream = nullptr;
  platformEngine = nullptr;
  if (stream) {
    SDL_PauseAudioStreamDevice(stream);
    SDL_DestroyAudioStream(stream);
  }
}

void prepareOpenSLPlayback(bool) {}
bool isOpenSLPlaybackBlocked() { return false; }

s32 KLBOpenSLNewEngine::workerThread(void *, void *data) {
  auto *engine = static_cast<KLBOpenSLNewEngine *>(data);
  engine->m_workerEnabled = true;
  while (engine->m_workerEnabled) {
    engine->processAudio();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  engine->m_workerStopped = true;
  return 0;
}

void pauseOpenSLActivity(KLBOpenSLNewEngine *) {
  if (platformStream)
    SDL_PauseAudioStreamDevice(platformStream);
}

extern s64 s_lastAudioNanoTime;
extern bool s_resetAudioNanoTime;

void resumeOpenSLActivity(KLBOpenSLNewEngine *engine) {
  s64 now = CPFInterface::getInstance().platform().nanotime();
  if (now >= s_lastAudioNanoTime || ((s_lastAudioNanoTime ^ now) < 0) ||
      s_resetAudioNanoTime) {
    s_resetAudioNanoTime = false;
    s_lastAudioNanoTime = now;
  }
  engine->m_lastWorkerTime = s_lastAudioNanoTime & 0x7fffffffffffffffLL;
  if (platformStream)
    SDL_ResumeAudioStreamDevice(platformStream);
}

void KLBOpenSLNewEngine::onHeadsetActive() {}
