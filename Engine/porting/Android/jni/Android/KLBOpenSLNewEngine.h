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

#ifndef KLBOpenSLNewEngine_h
#define KLBOpenSLNewEngine_h

#include "AudioCodec.h"
#include "KLBAudioSystem.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <SLES/OpenSLES_Android.h>

class KLBOpenSLNewEngine;
void platformBufferQueueCallback(
	SLAndroidSimpleBufferQueueItf queue, void* context);
bool initializeOpenSLPlatform(KLBOpenSLNewEngine* engine);
void prepareOpenSLPlayback(bool soundEffect);
bool isOpenSLPlaybackBlocked();

class KLBOpenSLNewSoundAsset
{
public:
	static KLBOpenSLNewSoundAsset* load(const char* path, bool isSE, s32 mode, s32 option);
	// True while background music keeps its packed source data instead of
	// being expanded up front by the streaming loader.
	static bool isBGMDecompressionDisabled();
	void retain();
	void releaseReference();
	void retainAsyncReference();
	void releaseAsyncReference();

private:
	friend class KLBOpenSLNewEngine;
	friend class KLBOpenSLVoice;

	struct PlaybackState {
		u32				sampleRate;
		u32				sampleCount;
		s16*				pcmReadCursor;
		s16*				pcmBufferBegin;
		s16*				pcmBufferHalf;
		IAudioCodecInstance*	decoder;
		void*				mutex;
		const char*			sourcePath;
		const char*			resolvedPath;
		const char*			archivePath;
		bool				decodeComplete;
		bool				isSE;
		bool				fullyBuffered;
		bool				loadingFailed;
	};

	KLBOpenSLNewSoundAsset();
	~KLBOpenSLNewSoundAsset();
	void decode(u32 sampleOffset, s32 sampleCount, s16* output);
	void decodeLoaderChunk();
	void resolvePcmPointer(u32 sampleOffset, u32 channel, s16** output);
	bool initialize(const char* path, bool isSE, s32 bufferSamples);
	void startLoader(s32 workUnit);
	void stopLoader();
	static s32 loaderThread(void* thread, void* data);
	void clearDecodeState() {
		// Loading policy follows this range and deliberately survives a
		// decoder-state reset.
		memset(&m_source.playbackSampleRate, 0,
			sizeof(m_source.playbackSampleRate)
			+ offsetof(PlaybackState, isSE));
	}

	u16					m_referenceCount;
	u8					m_releaseCountdown;
	volatile u8			m_asyncReferences;
	volatile u8			m_updateGeneration;
	u8					m_appliedGeneration;
	u8					m_codecType;
	u8					m_loadMode;
	void*				m_loaderTask;
	volatile bool		m_loaderFinished;
	bool				m_loaderStarted;
	bool				m_loaderCancelRequested;
	bool				m_loaderOwnsReference;
	volatile s32			m_loaderWorkUnit;
	KLBOpenSLNewSoundAsset*	m_next;
	AudioCodecSource		m_source;
	PlaybackState		m_playback;
};

class KLBOpenSLVoice
{
public:
	typedef s32 (KLBOpenSLVoice::*PositionQuery)();

	KLBOpenSLVoice();
	~KLBOpenSLVoice();
	s32 getState() const;
	void setMasterVolume(float volume);
	void setAssetVolume(float volume, bool notifyPlayer);
	void setChannelVolume(float volume);
	void releaseStagingBuffer();
	void initialize(KLBOpenSLNewSoundAsset* asset);
	void pause(s32 duration, float targetVolume);
	void resume(u32 msec, float targetVolume);
	void stop(s32 duration, float targetVolume);
	void setLoop(s32 start, s32 end);
	void syncPlaybackClock();
	bool updateFade();
	s32 playbackPosition();
	static void setPositionQuery(u32 queryIndex);

private:
	friend class KLBOpenSLNewEngine;

	void reset();
	bool configureFade(
		s16 command,
		s32 duration,
		float targetVolume,
		float startVolume,
		u16 step);
	s32 queryDecodedPosition();
	s32 queryPlatformPosition();
	static u32 s_positionQueryIndex;

	struct PlaybackClock {
		volatile u64			startTime;
		volatile u64			elapsedTime;
		volatile u64			lastUpdateTime;
		volatile s64			decodedSampleCount;
		s64				presentedSampleCount;
		u64				nextBufferTime;
		volatile u32			lastBufferSamples;
		u32				underrunCount;
	};

	struct PlaybackBufferState {
		s16*				bufferBegin;
		s16*				bufferEnd;
		u64				bufferOffset;
		u64				currentPosition;
		u64				requestedPosition;
		bool				loopBoundary;
		bool				endReached;
		bool				positionChanged;
	};

	u64 advancePlayback(u32 samples, PlaybackBufferState* state);

	PositionQuery			m_positionQueries[2];
	s32				m_fadeElapsedSamples;
	float				m_fadeStartVolume;
	float				m_fadeTargetVolume;
	float				m_fadeCurrentVolume;
	s32				m_fadeDurationSamples;
	u16				m_fadeStep;
	u16				m_fadeCommand;
	bool				m_fadeFromCurrentVolume;
	u64				m_fadeStartTime;
	volatile s32			m_lifecycleState;
	s32				m_playbackMode;
	s32				m_loopStart;
	s32				m_loopEnd;
	s16				m_state;
	u16				m_pendingCommands;
	KLBOpenSLNewSoundAsset*	m_asset;
	u32				m_queuedBufferCount;
	u32				m_bufferCapacity;
	u64				m_playbackSampleLimit;
	PlaybackClock			m_primaryClock;
	PlaybackClock			m_secondaryClock;
	PlaybackClock* volatile		m_clock;
	s16				m_outputVolume;
	u16				m_previousOutputVolume;
	bool				m_volumeDirty;
	float				m_masterVolume;
	float				m_assetVolume;
	float				m_channelVolume;
	u32				m_sourceChannel;
	PlaybackBufferState		m_primaryBufferState;
	PlaybackBufferState		m_secondaryBufferState;
	PlaybackBufferState* volatile	m_bufferState;
	s16*				m_currentPcmBuffer;
	u8*				m_stagingBuffer;
	bool				m_reusable;
	u32				m_refillOffset;
	u32				m_refillSize;
	u32				m_pendingRefillSamples;
	bool				m_interleavedStereo;
	bool				m_loopEnabled;
	bool				m_callbackEnabled;
	bool				m_fullyBuffered;
	bool				m_playbackStarted;
	u32				m_loopStartSample;
	u32				m_loopEndSample;
};

/**
 * OpenSL implementation using independent voice records and deferred audio
 * commands. The record layout is intentionally left to the implementation;
 * only target-proven interface behavior belongs in this declaration.
 */
class KLBOpenSLNewEngine : public KLBAudioImplementation
{
public:
	static KLBOpenSLNewEngine* getInstance();
	bool registerCodec(u32 index, IAudioCodec* codec);
	u32 getMixBufferSamples() const;
	virtual ~KLBOpenSLNewEngine();

	virtual void beginAudioFrame(bool processing);
	virtual void endAudioFrame();
	virtual void terminate();
	virtual bool init();
	virtual void shutdown();
	virtual void releaseAudio(void* handle);
	virtual void pauseAudio(void* handle, bool lockHeld, float targetVolume, u32 msec);
	virtual void resumeAudio(void* handle, bool lockHeld, float targetVolume, u32 msec);
	virtual void seekAudio(void* handle, s32 msec);
	virtual s32 tellAudio(void* handle);
	virtual s32 getState(void* handle);
	virtual KLBAudioCommand* popAudioCommand();
	virtual void setFadeParam(void* handle, float targetVolume, u32 msec);
	virtual void setAudioMultiProcessType(s32 processType);
	virtual void setMasterVolume(float volume, bool seMode);
	virtual void setAudioVolume(void* handle, float volume, bool lockHeld);
	virtual void setFormAudioVolume(void* handle, float volume, bool lockHeld);
	virtual void setAudioLoop(void* handle, s32 start, s32 end);
	virtual void* loadAudio(const char* path, bool isSE, s32 mode, s32 option);
	virtual bool preLoad(void* handle);
	virtual bool playAudio(void* handle, s32 msec, float targetVolume, float startVolume);
	virtual void stopAudio(void* handle, bool lockHeld, float targetVolume, u32 msec);
	virtual void onActivityPause();
	virtual void onActivityResume();
	virtual s32 totalTimeAudio(void* handle);
	virtual void onHeadsetActive();

private:
	friend class KLBOpenSLNewSoundAsset;
	friend class KLBOpenSLVoice;
	friend void platformBufferQueueCallback(
		SLAndroidSimpleBufferQueueItf queue, void* context);
	friend void resumeOpenSLActivity(KLBOpenSLNewEngine* engine);
	friend bool initializeOpenSLPlatform(KLBOpenSLNewEngine* engine);

	KLBOpenSLNewEngine();
	static s32 workerThread(void* thread, void* data);
	void resetWorkerClock();
	void resyncPendingAudio();
	void beginPlatformCallback();
	void prepareOutputBuffer();
	void renderAudio(s16* buffer, u16 samples);
	void processAudio();
	void setPositionQuery(u32 queryIndex);
	bool acquireVoice(
		KLBOpenSLNewSoundAsset* asset,
		KLBOpenSLVoice** voice,
		bool lockHeld);
	void pushAudioCommand(void* asset, bool soundEffect, s32 type,
		void* voice, s32 value, s64 playbackPosition);

	KLBOpenSLNewSoundAsset*	m_pendingAsset;
	KLBOpenSLVoice*			m_pendingVoice;
	IAudioCodec*			m_codecs[4];
	KLBOpenSLVoice			m_voices[32];
	void*				m_voiceMutex;
	u16				m_activeVoiceCount;
	u32				m_sampleRate;
	u32				m_mixBufferSamples;
	float				m_bgmMasterVolume;
	float				m_seMasterVolume;
	float				m_bgmMultiProcessVolume;
	float				m_seMultiProcessVolume;
	void*				m_outputInterface;
	void*				m_workerThread;
	bool				m_resetWorkerClock;
	volatile u64			m_lastWorkerTime;
	volatile bool			m_workerStopped;
	volatile bool			m_workerEnabled;
	KLBAudioCommand			m_commands[128];
	u16				m_commandReadIndex;
	u16				m_commandWriteIndex;
	volatile bool			m_workerWaiting;
	volatile bool			m_workerAcknowledged;
};

#endif
