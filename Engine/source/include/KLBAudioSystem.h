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

#ifndef KLBAudioSystem_h
#define KLBAudioSystem_h

#include "BaseType.h"

struct KLBAudioCommand
{
	union Payload {
		s64 playbackPosition;
		s32 state;
		const char* soundPath;
	};

	void*	asset;
	void*	voice;
	s32	type;
	s32	value;
	bool	soundEffect;
	Payload	payload;
};

class KLBAudioImplementation
{
public:
	virtual ~KLBAudioImplementation() {}
	virtual void beginAudioFrame(bool processing) = 0;
	virtual void endAudioFrame() = 0;
	virtual void terminate() = 0;
	virtual bool init() = 0;
	virtual void shutdown() = 0;
	virtual void releaseAudio(void* handle) = 0;
	virtual void pauseAudio(void* handle, bool lockHeld, float targetVolume, u32 msec) = 0;
	virtual void resumeAudio(void* handle, bool lockHeld, float targetVolume, u32 msec) = 0;
	virtual void seekAudio(void* handle, s32 msec) = 0;
	virtual s32 tellAudio(void* handle) = 0;
	virtual s32 getState(void* handle) = 0;
	virtual KLBAudioCommand* popAudioCommand() = 0;
	virtual void setFadeParam(void* handle, float targetVolume, u32 msec) = 0;
	virtual void setAudioMultiProcessType(s32 processType) = 0;
	virtual void setMasterVolume(float volume, bool seMode) = 0;
	virtual void setAudioVolume(void* handle, float volume, bool lockHeld) = 0;
	virtual void setFormAudioVolume(void* handle, float volume, bool lockHeld) = 0;
	virtual void setAudioLoop(void* handle, s32 start, s32 end) = 0;
	virtual void* loadAudio(const char* path, bool isSE, s32 mode, s32 option) = 0;
	virtual bool preLoad(void* handle) = 0;
	virtual bool playAudio(void* handle, s32 msec, float targetVolume, float startVolume) = 0;
	virtual void stopAudio(void* handle, bool lockHeld, float targetVolume, u32 msec) = 0;
	virtual void onActivityPause() = 0;
	virtual void onActivityResume() = 0;
	virtual s32 totalTimeAudio(void* handle) = 0;
	virtual void onHeadsetActive() = 0;
};

KLBAudioImplementation* getNewAudioImplementation();

#endif
