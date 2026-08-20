#include "Playground/Switch/SwitchAudioOutput.h"

#include <switch.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace playground::switch_runtime {
namespace {

constexpr std::size_t BufferCount = 3;
constexpr std::uint32_t FramesPerBuffer = 1024;
constexpr std::size_t BytesPerBuffer =
    FramesPerBuffer * 2 * sizeof(std::int16_t);

struct OutputState {
  std::array<AudioOutBuffer, BufferCount> buffers{};
  std::array<void *, BufferCount> allocations{};
  AudioFillCallback callback{};
  void *context{};
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<bool> paused{false};
  bool initialized{};
};

OutputState g_output;

void fill(AudioOutBuffer &buffer) {
  auto *samples = static_cast<std::int16_t *>(buffer.buffer);
  if (g_output.callback && !g_output.paused)
    g_output.callback(g_output.context, samples, FramesPerBuffer);
  else
    std::memset(samples, 0, BytesPerBuffer);
  armDCacheFlush(buffer.buffer, buffer.buffer_size);
}

void outputThread() {
  while (g_output.running) {
    AudioOutBuffer *released = nullptr;
    u32 releasedCount = 0;
    const Result result =
        audoutWaitPlayFinish(&released, &releasedCount, 100000000ULL);
    if (!g_output.running)
      break;
    if (R_FAILED(result) || !released || !releasedCount)
      continue;
    fill(*released);
    const Result appendResult = audoutAppendAudioOutBuffer(released);
    if (R_FAILED(appendResult)) {
      std::fprintf(stderr, "audoutAppendAudioOutBuffer failed with 0x%08x\n",
                   appendResult);
      g_output.running = false;
    }
  }
}

void releaseAllocations() {
  for (void *allocation : g_output.allocations)
    std::free(allocation);
  g_output.allocations.fill(nullptr);
}

} // namespace

bool initializeAudioOutput(void *context, AudioFillCallback callback,
                           std::uint32_t &sampleRate,
                           std::uint32_t &framesPerBuffer) {
  if (g_output.initialized || !context || !callback)
    return false;
  if (R_FAILED(audoutInitialize()))
    return false;

  g_output.context = context;
  g_output.callback = callback;
  for (std::size_t index = 0; index < BufferCount; ++index) {
    void *allocation = std::aligned_alloc(0x1000, BytesPerBuffer);
    if (!allocation) {
      audoutExit();
      releaseAllocations();
      return false;
    }
    g_output.allocations[index] = allocation;
    AudioOutBuffer &buffer = g_output.buffers[index];
    buffer.buffer = allocation;
    buffer.buffer_size = BytesPerBuffer;
    buffer.data_size = BytesPerBuffer;
    fill(buffer);
    if (R_FAILED(audoutAppendAudioOutBuffer(&buffer))) {
      audoutExit();
      releaseAllocations();
      return false;
    }
  }
  if (R_FAILED(audoutStartAudioOut())) {
    releaseAllocations();
    audoutExit();
    return false;
  }
  appletSetMediaPlaybackState(true);
  g_output.running = true;
  g_output.initialized = true;
  g_output.thread = std::thread(outputThread);
  sampleRate = audoutGetSampleRate();
  framesPerBuffer = FramesPerBuffer;
  return true;
}

void shutdownAudioOutput() {
  if (!g_output.initialized)
    return;
  g_output.running = false;
  audoutStopAudioOut();
  if (g_output.thread.joinable())
    g_output.thread.join();
  if (hosversionAtLeast(4, 0, 0)) {
    bool flushed = false;
    audoutFlushAudioOutBuffers(&flushed);
  }
  appletSetMediaPlaybackState(false);
  audoutExit();
  releaseAllocations();
  g_output.buffers.fill(AudioOutBuffer{});
  g_output.callback = nullptr;
  g_output.context = nullptr;
  g_output.running = false;
  g_output.paused = false;
  g_output.initialized = false;
}

void pauseAudioOutput() {
  if (!g_output.initialized || g_output.paused.exchange(true))
    return;
  audoutStopAudioOut();
  appletSetMediaPlaybackState(false);
}

void resumeAudioOutput() {
  if (!g_output.initialized || !g_output.paused.exchange(false))
    return;
  const Result result = audoutStartAudioOut();
  if (R_FAILED(result)) {
    std::fprintf(stderr, "audoutStartAudioOut failed with 0x%08x\n", result);
    g_output.paused = true;
    return;
  }
  appletSetMediaPlaybackState(true);
}

} // namespace playground::switch_runtime
