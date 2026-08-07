/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "CPFInterface.h"
#include <string.h>

// The shipped audio translation unit retains this fatal boundary even though
// every call site for the abandoned feature was removed before release.
void assertUnsupportedAudioFeature() {
	klb_assertAlways("UNUSED FEATURE, COMMENTED OUT");
}

void IPlatformRequest::beginAudioFrame() {
	m_audio->beginAudioFrame(false);
}

void IPlatformRequest::endAudioFrame() {
	m_audio->endAudioFrame();
}

void* IPlatformRequest::loadAudio(const char* url, bool isSE, s32 mode, s32 option) {
	const char* path = strncmp(url, "asset://", 8) ? url + 7 : url;
	return m_audio->loadAudio(path, isSE, mode, option);
}

void IPlatformRequest::setAudioLoop(void* handle, s32 start, s32 end) {
	if (handle) m_audio->setAudioLoop(handle, start, end);
}

void IPlatformRequest::playAudio(void* handle, s32 msec, float targetVolume, float startVolume) {
	if (handle) m_audio->playAudio(handle, msec, targetVolume, startVolume);
}

void IPlatformRequest::stopAudio(void* handle, s32 msec, float targetVolume) {
	if (handle) m_audio->stopAudio(handle, false, targetVolume, msec);
}

void IPlatformRequest::setAudioVolume(void* handle, float volume, bool formVolume) {
	if (handle) {
		if (formVolume) {
			m_audio->setFormAudioVolume(handle, volume, false);
		} else {
			m_audio->setAudioVolume(handle, volume, false);
		}
	}
}

void IPlatformRequest::setMasterVolume(float volume, bool seMode) {
	m_audio->setMasterVolume(volume, seMode);
}

void IPlatformRequest::releaseAudio(void* handle) {
	if (handle) m_audio->releaseAudio(handle);
}

void IPlatformRequest::pauseAudio(void* handle, s32 msec, float targetVolume) {
	if (handle) m_audio->pauseAudio(handle, false, targetVolume, msec);
}

void IPlatformRequest::resumeAudio(void* handle, s32 msec, float targetVolume) {
	if (handle) m_audio->resumeAudio(handle, false, targetVolume, msec);
}

s32 IPlatformRequest::tellAudio(void* handle) {
	return handle ? m_audio->tellAudio(handle) : 0;
}

s32 IPlatformRequest::totalTimeAudio(void* handle) {
	return handle ? m_audio->totalTimeAudio(handle) : 0;
}

s32 IPlatformRequest::getState(void* handle) {
	return handle ? m_audio->getState(handle) : 0;
}

void IPlatformRequest::setFadeParam(void* handle, float targetVolume, u32 msec) {
	if (handle) m_audio->setFadeParam(handle, targetVolume, msec);
}

void IPlatformRequest::setAudioMultiProcessType(s32 processType) {
	if (processType >= 20 && processType <= 22) m_audio->setAudioMultiProcessType(processType);
}

KLBAudioCommand* IPlatformRequest::popAudioCommand() {
	return m_audio->popAudioCommand();
}

void IPlatformRequest::setPauseOnInterruption(bool) {
}

void IPlatformRequest::seekAudio(void* handle, s32 millisec) {
	if (handle) m_audio->seekAudio(handle, millisec);
}

void IPlatformRequest::setAudioPan(void*, float) {
}

void IPlatformRequest::keepAudioSessions(void*, s32) {
}

bool IPlatformRequest::setBufSize(void*, int) {
	return true;
}

bool IPlatformRequest::preLoad(void*) {
	return true;
}
