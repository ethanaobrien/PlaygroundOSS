#include "Playground/Switch/SwitchAudioOutput.h"

#include "KLBOpenSLNewEngine.h"

#include "CPFInterface.h"

#include <chrono>
#include <cstring>
#include <thread>

KLBAudioImplementation *g_audioImplementation = nullptr;

namespace {
KLBOpenSLNewEngine *platformEngine;
}

void switchAudioFill(void *context, s16 *samples, u16 frames) {
  auto *engine = static_cast<KLBOpenSLNewEngine *>(context);
  if (!engine || engine != platformEngine)
    return;
  engine->beginPlatformCallback();
  engine->m_workerAcknowledged = true;
  if (engine->m_workerEnabled) {
    engine->prepareOutputBuffer();
    engine->renderAudio(samples, frames);
  } else {
    std::memset(samples, 0, static_cast<std::size_t>(frames) * 4);
    engine->m_workerWaiting = true;
  }
  engine->m_workerAcknowledged = false;
}

bool initializeOpenSLPlatform(KLBOpenSLNewEngine *engine) {
  if (platformEngine || !engine)
    return false;
  std::uint32_t sampleRate = 0;
  std::uint32_t frames = 0;
  platformEngine = engine;
  if (!playground::switch_runtime::initializeAudioOutput(
          engine,
          [](void *context, std::int16_t *samples, std::uint32_t frameCount) {
            switchAudioFill(context, reinterpret_cast<s16 *>(samples),
                            static_cast<u16>(frameCount));
          },
          sampleRate, frames)) {
    platformEngine = nullptr;
    return false;
  }
  engine->m_sampleRate = sampleRate;
  engine->m_mixBufferSamples = frames;
  engine->m_outputInterface = engine;
  return true;
}

u32 KLBOpenSLNewEngine::getMixBufferSamples() const {
  return m_mixBufferSamples;
}

void shutdownOpenSLPlatform(KLBOpenSLNewEngine *) {
  playground::switch_runtime::shutdownAudioOutput();
  platformEngine = nullptr;
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
  playground::switch_runtime::pauseAudioOutput();
}

extern s64 s_lastAudioNanoTime;
extern bool s_resetAudioNanoTime;

void resumeOpenSLActivity(KLBOpenSLNewEngine *engine) {
  const s64 now = CPFInterface::getInstance().platform().nanotime();
  if (now >= s_lastAudioNanoTime || ((s_lastAudioNanoTime ^ now) < 0) ||
      s_resetAudioNanoTime) {
    s_resetAudioNanoTime = false;
    s_lastAudioNanoTime = now;
  }
  engine->m_lastWorkerTime = s_lastAudioNanoTime & 0x7fffffffffffffffLL;
  playground::switch_runtime::resumeAudioOutput();
}

void KLBOpenSLNewEngine::onHeadsetActive() {}
