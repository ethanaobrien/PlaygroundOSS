#ifndef PLAYGROUND_SWITCH_AUDIO_OUTPUT_H
#define PLAYGROUND_SWITCH_AUDIO_OUTPUT_H

#include <cstdint>

namespace playground::switch_runtime {

using AudioFillCallback = void (*)(void *context, std::int16_t *samples,
                                   std::uint32_t frameCount);

bool initializeAudioOutput(void *context, AudioFillCallback callback,
                           std::uint32_t &sampleRate,
                           std::uint32_t &framesPerBuffer);
void shutdownAudioOutput();
void pauseAudioOutput();
void resumeAudioOutput();

} // namespace playground::switch_runtime

#endif
