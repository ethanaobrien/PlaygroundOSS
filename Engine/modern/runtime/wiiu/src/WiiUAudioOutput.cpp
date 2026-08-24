#include "Playground/WiiU/WiiUAudioOutput.h"

#include <coreinit/cache.h>
#include <coreinit/atomic.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <thread>

namespace playground::wiiu_runtime {
namespace {

constexpr std::uint32_t SampleRate = 44100;
constexpr std::uint32_t FramesPerBuffer = 1024;
constexpr std::uint32_t ChannelCount = 2;
constexpr std::uint32_t BufferCount = 3;
constexpr std::size_t ChannelBytes = FramesPerBuffer * sizeof(std::int16_t);
constexpr std::size_t BufferBytes = ChannelBytes * ChannelCount;
constexpr std::size_t AllocationBytes = BufferBytes * BufferCount;
constexpr std::uint32_t AxAffinity = OS_THREAD_ATTRIB_AFFINITY_CPU1;

struct OutputState {
  std::array<AXVoice *, ChannelCount> voices{};
  std::array<unsigned char *, BufferCount> buffers{};
  std::int16_t *interleaved{};
  void *allocation{};
  AudioFillCallback callback{};
  void *context{};
  std::thread thread;
  volatile std::uint32_t running{};
  volatile std::uint32_t paused{};
  volatile std::uint32_t playing{};
  volatile std::uint32_t rendering{1};
  bool ownsAx{};
  bool initialized{};
};

OutputState g_output;

std::uint32_t atomicLoad(volatile std::uint32_t *value) {
  OSMemoryBarrier();
  const std::uint32_t result = *value;
  OSMemoryBarrier();
  return result;
}

void atomicStore(volatile std::uint32_t *value, std::uint32_t replacement) {
  OSSwapAtomic(value, replacement);
}

std::uint32_t offsetFor(const AXVoiceOffsets &offsets, const void *address) {
  return static_cast<std::uint32_t>(
      (static_cast<const unsigned char *>(address) -
       static_cast<const unsigned char *>(offsets.data)) /
      sizeof(std::int16_t));
}

int nextBuffer(int buffer) { return (buffer + 1) % BufferCount; }

void configureDeviceMix(AXVoice *voice, std::uint32_t channel,
                        AXDeviceType device) {
  std::array<AXVoiceDeviceMixData, 6> mix{};
  mix[channel].bus[0].volume = 0x8000;
  AXSetVoiceDeviceMix(voice, device, 0, mix.data());
}

void fillBuffer(int id) {
  if (id < 0 || id >= static_cast<int>(BufferCount))
    return;
  if (g_output.callback && !atomicLoad(&g_output.paused))
    g_output.callback(g_output.context, g_output.interleaved,
                      FramesPerBuffer);
  else
    std::memset(g_output.interleaved, 0, BufferBytes);

  auto *left = reinterpret_cast<std::int16_t *>(g_output.buffers[id]);
  auto *right = left + FramesPerBuffer;
  for (std::uint32_t frame = 0; frame < FramesPerBuffer; ++frame) {
    left[frame] = g_output.interleaved[frame * 2];
    right[frame] = g_output.interleaved[frame * 2 + 1];
  }
  DCStoreRange(g_output.buffers[id], BufferBytes);
}

void audioFrameCallback() {
  if (!g_output.initialized || !g_output.voices[0])
    return;

  AXVoiceOffsets offsets[ChannelCount]{};
  for (std::uint32_t channel = 0; channel < ChannelCount; ++channel)
    AXGetVoiceOffsets(g_output.voices[channel], &offsets[channel]);

  int playing = -1;
  for (int id = 0; id < static_cast<int>(BufferCount); ++id) {
    const std::uint32_t start = offsetFor(offsets[0], g_output.buffers[id]);
    const std::uint32_t end = start + FramesPerBuffer - 1;
    if (offsets[0].currentOffset >= start &&
        offsets[0].currentOffset <= end) {
      playing = id;
      break;
    }
  }
  if (playing < 0)
    return;
  atomicStore(&g_output.playing, static_cast<std::uint32_t>(playing));

  const int ready = nextBuffer(playing);
  const bool readyIsBeingWritten =
      atomicLoad(&g_output.rendering) == static_cast<std::uint32_t>(ready);
  for (std::uint32_t channel = 0; channel < ChannelCount; ++channel) {
    const auto *currentStart =
        g_output.buffers[playing] + channel * ChannelBytes;
    const auto *readyStart = g_output.buffers[ready] + channel * ChannelBytes;
    AXSetVoiceEndOffset(g_output.voices[channel],
                        offsetFor(offsets[channel],
                                  currentStart + ChannelBytes - 2));
    AXSetVoiceLoopOffset(g_output.voices[channel],
                         offsetFor(offsets[channel],
                                   readyIsBeingWritten ? currentStart
                                                       : readyStart));
  }
}

void outputThread() {
  while (atomicLoad(&g_output.running)) {
    const int rendering = static_cast<int>(atomicLoad(&g_output.rendering));
    if (rendering != static_cast<int>(atomicLoad(&g_output.playing))) {
      fillBuffer(rendering);
      atomicStore(&g_output.rendering,
                  static_cast<std::uint32_t>(nextBuffer(rendering)));
    } else {
      OSSleepTicks(OSMillisecondsToTicks(2));
    }
  }
}

void stopAndReleaseVoices() {
  for (AXVoice *&voice : g_output.voices) {
    if (!voice)
      continue;
    AXVoiceBegin(voice);
    AXSetVoiceState(voice, AX_VOICE_STATE_STOPPED);
    AXVoiceEnd(voice);
    AXFreeVoice(voice);
    voice = nullptr;
  }
}

void releaseBuffers() {
  std::free(g_output.interleaved);
  std::free(g_output.allocation);
  g_output.interleaved = nullptr;
  g_output.allocation = nullptr;
  g_output.buffers.fill(nullptr);
}

} // namespace

bool initializeAudioOutput(void *context, AudioFillCallback callback,
                           std::uint32_t &sampleRate,
                           std::uint32_t &framesPerBuffer) {
  if (g_output.initialized || !context || !callback)
    return false;

  const std::uint32_t oldAffinity = OSGetThreadAffinity(OSGetCurrentThread());
  OSSetThreadAffinity(OSGetCurrentThread(), AxAffinity);
  if (!AXIsInit()) {
    AXInitParams parameters{};
    parameters.renderer = AX_INIT_RENDERER_48KHZ;
    parameters.pipeline = AX_INIT_PIPELINE_SINGLE;
    AXInitWithParams(&parameters);
    g_output.ownsAx = true;
  }

  std::array<void *, 32> inaccessibleAllocations{};
  std::size_t inaccessibleCount = 0;
  for (std::size_t attempt = 0; attempt < inaccessibleAllocations.size();
       ++attempt) {
    void *candidate = memalign(0x40, AllocationBytes);
    if (!candidate)
      break;
    const std::uint32_t physicalStart =
        OSEffectiveToPhysical(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(candidate))) &
        0x1fffffff;
    if (((physicalStart + AllocationBytes) & 0xe0000000) == 0) {
      g_output.allocation = candidate;
      break;
    }
    inaccessibleAllocations[inaccessibleCount++] = candidate;
  }
  for (std::size_t index = 0; index < inaccessibleCount; ++index)
    std::free(inaccessibleAllocations[index]);
  g_output.interleaved =
      static_cast<std::int16_t *>(memalign(0x40, BufferBytes));
  if (!g_output.allocation || !g_output.interleaved) {
    releaseBuffers();
    if (g_output.ownsAx)
      AXQuit();
    g_output.ownsAx = false;
    OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);
    return false;
  }
  std::memset(g_output.allocation, 0, AllocationBytes);
  std::memset(g_output.interleaved, 0, BufferBytes);
  for (std::uint32_t id = 0; id < BufferCount; ++id)
    g_output.buffers[id] =
        static_cast<unsigned char *>(g_output.allocation) + id * BufferBytes;
  DCStoreRange(g_output.allocation, AllocationBytes);

  g_output.context = context;
  g_output.callback = callback;
  for (std::uint32_t id = 0; id < BufferCount; ++id)
    fillBuffer(id);

  bool voicesReady = true;
  for (std::uint32_t channel = 0; channel < ChannelCount; ++channel) {
    AXVoice *voice = AXAcquireVoice(31, nullptr, nullptr);
    g_output.voices[channel] = voice;
    if (!voice) {
      voicesReady = false;
      break;
    }
    AXVoiceBegin(voice);
    AXSetVoiceType(voice, AX_VOICE_TYPE_UNKNOWN);
    AXVoiceVeData volume{};
    volume.volume = 0x8000;
    AXSetVoiceVe(voice, &volume);
    configureDeviceMix(voice, channel, AX_DEVICE_TYPE_TV);
    configureDeviceMix(voice, channel, AX_DEVICE_TYPE_DRC);
    AXSetVoiceSrcRatio(voice, static_cast<float>(SampleRate) /
                                 static_cast<float>(AXGetInputSamplesPerSec()));
    AXSetVoiceSrcType(voice, AX_VOICE_SRC_TYPE_LINEAR);

    AXVoiceOffsets offsets{};
    offsets.dataType = AX_VOICE_FORMAT_LPCM16;
    offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
    offsets.loopOffset = 0;
    offsets.endOffset = FramesPerBuffer - 1;
    offsets.currentOffset = 0;
    offsets.data = g_output.buffers[0] + channel * ChannelBytes;
    AXSetVoiceOffsets(voice, &offsets);
    AXSetVoiceState(voice, AX_VOICE_STATE_PLAYING);
    AXVoiceEnd(voice);
  }

  if (!voicesReady || AXRegisterAppFrameCallback(audioFrameCallback) !=
                          AX_RESULT_SUCCESS) {
    stopAndReleaseVoices();
    releaseBuffers();
    if (g_output.ownsAx)
      AXQuit();
    g_output.ownsAx = false;
    g_output.callback = nullptr;
    g_output.context = nullptr;
    OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);
    return false;
  }

  atomicStore(&g_output.playing, 0);
  atomicStore(&g_output.rendering, 1);
  atomicStore(&g_output.paused, 0);
  atomicStore(&g_output.running, 1);
  g_output.initialized = true;
  g_output.thread = std::thread(outputThread);
  sampleRate = SampleRate;
  framesPerBuffer = FramesPerBuffer;
  OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);
  return true;
}

void shutdownAudioOutput() {
  if (!g_output.initialized)
    return;
  atomicStore(&g_output.running, 0);
  if (g_output.thread.joinable())
    g_output.thread.join();

  const std::uint32_t oldAffinity = OSGetThreadAffinity(OSGetCurrentThread());
  OSSetThreadAffinity(OSGetCurrentThread(), AxAffinity);
  AXDeregisterAppFrameCallback(audioFrameCallback);
  stopAndReleaseVoices();
  if (g_output.ownsAx)
    AXQuit();
  OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);

  releaseBuffers();
  g_output.callback = nullptr;
  g_output.context = nullptr;
  atomicStore(&g_output.playing, 0);
  atomicStore(&g_output.rendering, 1);
  atomicStore(&g_output.paused, 0);
  g_output.ownsAx = false;
  g_output.initialized = false;
}

void pauseAudioOutput() {
  if (!g_output.initialized || OSSwapAtomic(&g_output.paused, 1))
    return;
  const std::uint32_t oldAffinity = OSGetThreadAffinity(OSGetCurrentThread());
  OSSetThreadAffinity(OSGetCurrentThread(), AxAffinity);
  for (AXVoice *voice : g_output.voices) {
    AXVoiceBegin(voice);
    AXSetVoiceState(voice, AX_VOICE_STATE_STOPPED);
    AXVoiceEnd(voice);
  }
  OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);
}

void resumeAudioOutput() {
  if (!g_output.initialized || !OSSwapAtomic(&g_output.paused, 0))
    return;
  const std::uint32_t oldAffinity = OSGetThreadAffinity(OSGetCurrentThread());
  OSSetThreadAffinity(OSGetCurrentThread(), AxAffinity);
  for (AXVoice *voice : g_output.voices) {
    AXVoiceBegin(voice);
    AXSetVoiceState(voice, AX_VOICE_STATE_PLAYING);
    AXVoiceEnd(voice);
  }
  OSSetThreadAffinity(OSGetCurrentThread(), oldAffinity);
}

} // namespace playground::wiiu_runtime
