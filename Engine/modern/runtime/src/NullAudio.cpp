/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0
*/

#include "KLBAudioSystem.h"

namespace {

class NullAudio final : public KLBAudioImplementation {
public:
    void beginAudioFrame(bool) override {}
    void endAudioFrame() override {}
    void terminate() override {}
    bool init() override { return true; }
    void shutdown() override {}
    void releaseAudio(void*) override {}
    void pauseAudio(void*, bool, float, u32) override {}
    void resumeAudio(void*, bool, float, u32) override {}
    void seekAudio(void*, s32) override {}
    s32 tellAudio(void*) override { return 0; }
    s32 getState(void*) override { return 0; }
    KLBAudioCommand* popAudioCommand() override { return nullptr; }
    void setFadeParam(void*, float, u32) override {}
    void setAudioMultiProcessType(s32) override {}
    void setMasterVolume(float, bool) override {}
    void setAudioVolume(void*, float, bool) override {}
    void setFormAudioVolume(void*, float, bool) override {}
    void setAudioLoop(void*, s32, s32) override {}
    void* loadAudio(const char*, bool, s32, s32) override { return nullptr; }
    bool preLoad(void*) override { return false; }
    bool playAudio(void*, s32, float, float) override { return false; }
    void stopAudio(void*, bool, float, u32) override {}
    void onActivityPause() override {}
    void onActivityResume() override {}
    s32 totalTimeAudio(void*) override { return 0; }
    void onHeadsetActive() override {}
};

} // namespace

KLBAudioImplementation* getNewAudioImplementation()
{
    static NullAudio audio;
    return &audio;
}
