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

#include "KLBOpenSLNewEngine.h"
#include "AudioCodec.h"
#include "CAndroidPathConv.h"
#include "CAndroidRequest.h"
#include "CKLBUtility.h"
#include "CPFInterface.h"

#include <stdlib.h>
#include <string.h>

extern KLBAudioImplementation* g_audioImplementation;
bool g_decompressBGM;

void CAndroidRequest::decompressBGM(bool decompress) {
	g_decompressBGM = decompress;
}

extern void pauseOpenSLActivity(KLBOpenSLNewEngine* engine);
extern void resumeOpenSLActivity(KLBOpenSLNewEngine* engine);
extern bool initializeOpenSLPlatform(KLBOpenSLNewEngine* engine);
extern void shutdownOpenSLPlatform(KLBOpenSLNewEngine* engine);

KLBOpenSLNewSoundAsset* s_soundAssets = NULL;
u32 KLBOpenSLVoice::s_positionQueryIndex = 0;
s64 s_lastAudioNanoTime = 0;
bool s_resetAudioNanoTime = true;
static s16 s_silentSample;

static inline s64 audioNanoTime() {
	s64 now = CPFInterface::getInstance().platform().nanotime();
	if (now >= s_lastAudioNanoTime
	 || ((s_lastAudioNanoTime ^ now) < 0)
	 || s_resetAudioNanoTime) {
		s_resetAudioNanoTime = false;
		s_lastAudioNanoTime = now;
	}
	return s_lastAudioNanoTime & 0x7fffffffffffffffLL;
}

AudioCodecSource::AudioCodecSource()
: CDecryptBaseClass(0x0e)
, sourceFile(NULL) {
}

KLBOpenSLNewEngine::KLBOpenSLNewEngine()
: m_pendingAsset(NULL)
, m_pendingVoice(NULL)
, m_voices()
, m_activeVoiceCount(0)
, m_bgmMasterVolume(1.0f)
, m_seMasterVolume(1.0f)
, m_bgmMultiProcessVolume(1.0f)
, m_seMultiProcessVolume(1.0f)
, m_workerThread(NULL)
, m_resetWorkerClock(true)
, m_workerStopped(false)
, m_workerEnabled(true)
, m_commandReadIndex(0)
, m_commandWriteIndex(0)
, m_workerWaiting(false)
, m_workerAcknowledged(false) {
	memset(m_codecs, 0, sizeof(m_codecs));
	setAudioMultiProcessType(0);
}

KLBOpenSLNewEngine* KLBOpenSLNewEngine::getInstance() {
	if (!g_audioImplementation) {
		g_audioImplementation = new KLBOpenSLNewEngine();
	}
	return static_cast<KLBOpenSLNewEngine*>(g_audioImplementation);
}

bool KLBOpenSLNewEngine::registerCodec(u32 index, IAudioCodec* codec) {
	m_codecs[index] = codec;
	return codec ? codec->initialize() : false;
}

void KLBOpenSLNewEngine::beginPlatformCallback() {
}

void KLBOpenSLNewEngine::prepareOutputBuffer() {
}

void KLBOpenSLNewEngine::resetWorkerClock() {
	if (!m_pendingVoice) {
		return;
	}

	m_pendingVoice->m_clock->lastUpdateTime = audioNanoTime();
	KLBOpenSLNewEngine::getInstance()->m_lastWorkerTime = audioNanoTime();
}

void KLBOpenSLNewEngine::resyncPendingAudio() {
	if (m_pendingAsset) {
		seekAudio(
			m_pendingAsset,
			m_pendingVoice->m_clock->lastBufferSamples);
	}
}

void KLBOpenSLNewEngine::setPositionQuery(u32 queryIndex) {
	KLBOpenSLVoice::setPositionQuery(queryIndex);
	resetWorkerClock();
}

KLBOpenSLNewSoundAsset::KLBOpenSLNewSoundAsset()
: m_referenceCount(0)
, m_releaseCountdown(5)
, m_asyncReferences(0)
, m_updateGeneration(0)
, m_appliedGeneration(0xff)
, m_loaderTask(NULL)
, m_loaderFinished(true)
, m_next(NULL)
, m_source() {
	m_playback.loadingFailed = false;
	m_source.pcmSamples = NULL;
	clearDecodeState();
}

KLBOpenSLNewSoundAsset::~KLBOpenSLNewSoundAsset() {
	if (m_loaderTask) {
		while (!m_loaderFinished) {
		}
		CPFInterface::getInstance().platform().deleteThread(m_loaderTask);
		m_loaderTask = NULL;
	}

	if (m_playback.decoder) {
		m_playback.decoder->close(&m_source);
		delete m_playback.decoder;
	}
	m_playback.decoder = NULL;

	if (m_source.sourceFile) {
		fclose(m_source.sourceFile);
		m_source.sourceFile = NULL;
	}

	free(m_source.pcmSamples);
	delete [] m_playback.archivePath;
	delete [] m_playback.resolvedPath;
	delete [] m_playback.sourcePath;

	if (m_playback.mutex) {
		CPFInterface::getInstance().platform().freeMutex(m_playback.mutex);
	}
	delete [] m_playback.pcmBufferBegin;
}

KLBOpenSLNewSoundAsset* KLBOpenSLNewSoundAsset::load(
	const char* path,
	bool isSE,
	s32 mode,
	s32 option
) {
	KLBOpenSLNewSoundAsset* asset = new KLBOpenSLNewSoundAsset();

	bool useLoader = false;
	bool skipLoader = g_decompressBGM ? false : !isSE;
	u32 bufferSamples = 0;
	if (!skipLoader) {
		bufferSamples =
			KLBOpenSLNewEngine::getInstance()->getMixBufferSamples() * 10;
		useLoader = true;
	}

	if (!asset->initialize(path, isSE, bufferSamples)) {
		delete asset;
		return NULL;
	}

	asset->retain();
	if (!asset->m_playback.loadingFailed) {
		asset->m_playback.sampleRate = asset->m_source.sampleRate;
		asset->m_playback.sampleCount =
			asset->m_source.pcmByteCount / asset->m_source.channelCount;
		asset->m_playback.pcmReadCursor = asset->m_source.pcmSamples;
		asset->m_codecType = static_cast<u8>(mode);
		asset->m_loadMode = static_cast<u8>(option);

		if (useLoader) {
			s32 workUnit;
			if (isSE) {
				workUnit = asset->m_source.pcmSampleCount;
			} else {
				workUnit = (asset->m_source.channelCount * 3)
					* asset->m_source.sampleRate;
			}

			asset->m_playback.decoder->decode(
				workUnit, 0, asset->m_playback.pcmReadCursor);
			memset(
				asset->m_playback.pcmReadCursor
					+ static_cast<s32>(asset->m_source.pcmSampleCount),
				0,
				asset->m_source.channelCount * bufferSamples);

			if (isSE) {
				asset->m_playback.decoder->close(&asset->m_source);
			} else {
				asset->m_loaderWorkUnit = workUnit;
				asset->m_loaderFinished = false;
				asset->m_loaderTask =
					CPFInterface::getInstance().platform().createThread(
						loaderThread, asset);
			}
		}
	}

	asset->m_next = s_soundAssets;
	s_soundAssets = asset;
	return asset;
}

bool KLBOpenSLNewSoundAsset::isBGMDecompressionDisabled() {
	return !g_decompressBGM;
}

bool KLBOpenSLNewSoundAsset::initialize(
	const char* path,
	bool isSE,
	s32 bufferSamples
) {
	m_playback.isSE = isSE;
	// Background music that stays packed is decoded on demand into the
	// double buffer instead of being expanded up front.
	m_playback.fullyBuffered = isSE ? false : isBGMDecompressionDisabled();

	s32 bufferingMode;
	if (m_playback.fullyBuffered) {
		bufferingMode = 2;
	} else {
		bufferingMode = !m_playback.isSE;
	}
	m_playback.resolvedPath = CKLBUtility::copyString(path);

	// The codec table is shared by every asset, so the first asset to reach
	// this point publishes it.
	if (!KLBOpenSLNewEngine::getInstance()->m_codecs[0]) {
		for (u32 i = 0; i < IAudioCodec::getCodecCount(); ++i) {
			KLBOpenSLNewEngine::getInstance()->registerCodec(
				i, IAudioCodec::createCodec(i));
			if (!KLBOpenSLNewEngine::getInstance()->m_codecs[i]) {
				return false;
			}
		}
	}

	// The container is identified by trying every registered codec's
	// extension against the resolved asset location.
	IAudioCodec* codec = NULL;
	bool opened = false;
	for (u32 i = 0; i < IAudioCodec::getCodecCount(); ++i) {
		IAudioCodec* candidate = KLBOpenSLNewEngine::getInstance()->m_codecs[i];
		m_playback.archivePath = CKLBPathConv::getInstance().fullpath(
			path, candidate->getExtension());
		if (m_playback.archivePath) {
			m_source.sourceFile = fopen(m_playback.archivePath, "rb");
			if (m_source.sourceFile) {
				codec = candidate;
				opened = true;
				break;
			}
			delete [] m_playback.archivePath;
			m_playback.archivePath = NULL;
		}
	}

	bool ready = false;
	if (opened) {
		fseek(m_source.sourceFile, 0, SEEK_END);
		fgetpos(m_source.sourceFile, (fpos_t*)&m_source.sourceSize);
		fseek(m_source.sourceFile, 0, SEEK_SET);

		u8 header[4];
		header[0] = 0;
		header[1] = 0;
		header[2] = 0;
		header[3] = 0;
		fread(header, 1, 4, m_source.sourceFile);

		if (CPFInterface::getInstance().platform().useEncryption()) {
			u32 headerSize = 0;
			m_source.decryptSetup(
				(const u8*)m_playback.archivePath, header, &headerSize);
			if (headerSize >= 5) {
				u8 extendedHeader[128];
				fread(extendedHeader, 1, headerSize - 4, m_source.sourceFile);
				m_source.finishSetup(extendedHeader, m_playback.archivePath);
			}
			fseek(m_source.sourceFile, headerSize, SEEK_SET);
		} else {
			fseek(m_source.sourceFile, 0, SEEK_SET);
		}

		IAudioCodecInstance* decoder = codec->createInstance();
		if (decoder && decoder->open(&m_source, bufferingMode)) {
			m_playback.decoder = decoder;
			s32 channelCount = m_source.channelCount;
			bool allocated;
			if (m_playback.fullyBuffered) {
				// Streaming playback works on two halves of one buffer, each
				// holding a second of interleaved audio.
				u32 halfSamples = channelCount * m_source.sampleRate;
				m_playback.pcmBufferBegin = new s16[halfSamples * 2];
				m_playback.pcmBufferHalf =
					m_playback.pcmBufferBegin + halfSamples;
				m_source.pcmSamples = NULL;
				allocated = true;
			} else {
				u32 sampleCount = channelCount * bufferSamples
					+ m_source.pcmSampleCount;
				m_source.pcmSamples =
					(s16*)malloc(sampleCount * sizeof(s16));
				allocated = m_source.pcmSamples != NULL;
			}
			m_playback.mutex =
				CPFInterface::getInstance().platform().allocMutex();
			ready = allocated && m_playback.mutex != NULL;
		}
	}
	return opened && ready;
}

void KLBOpenSLNewSoundAsset::decode(
	u32 sampleOffset,
	s32 sampleCount,
	s16* output
) {
	IPlatformRequest* platform = &CPFInterface::getInstance().platform();
	platform->mutexLock(m_playback.mutex);
	m_playback.decoder->decode(sampleCount, sampleOffset, output);
	platform->mutexUnlock(m_playback.mutex);
}

void KLBOpenSLNewSoundAsset::decodeLoaderChunk() {
	u32 sampleOffset = m_loaderWorkUnit / m_source.channelCount;
	u32 remainingSamples =
		m_source.pcmByteCount - m_loaderWorkUnit;
	s16* output = m_loaderWorkUnit + m_source.pcmSamples;

	IPlatformRequest* platform = &CPFInterface::getInstance().platform();
	platform->mutexLock(m_playback.mutex);
	m_playback.decoder->decode(remainingSamples, sampleOffset, output);
	platform->mutexUnlock(m_playback.mutex);
	m_loaderFinished = true;
}

void KLBOpenSLNewSoundAsset::resolvePcmPointer(
	u32 sampleOffset,
	u32,
	s16** output
) {
	*output = m_playback.pcmReadCursor
		+ (sampleOffset << m_source.interleavedStereo);
}

void KLBOpenSLNewSoundAsset::retain() {
	++m_referenceCount;
}

void KLBOpenSLNewSoundAsset::releaseReference() {
	if (m_referenceCount) {
		--m_referenceCount;
	}
}

void KLBOpenSLNewSoundAsset::retainAsyncReference() {
	++m_asyncReferences;
}

void KLBOpenSLNewSoundAsset::releaseAsyncReference() {
	if (m_asyncReferences) {
		--m_asyncReferences;
	}
}

void KLBOpenSLNewEngine::beginAudioFrame(bool processing) {
	KLBOpenSLNewSoundAsset* asset = s_soundAssets;
	KLBOpenSLNewSoundAsset* previous = NULL;

	while (asset) {
		KLBOpenSLNewSoundAsset* next = asset->m_next;

		if (processing) {
			--asset->m_releaseCountdown;
			goto release_asset;
		} else if (!asset->m_asyncReferences && !asset->m_referenceCount) {
			if (asset->m_updateGeneration != asset->m_appliedGeneration) {
				asset->m_appliedGeneration = asset->m_updateGeneration;
				asset->m_releaseCountdown = 5;
			} else if (!--asset->m_releaseCountdown) {
release_asset:
				if (previous) {
					previous->m_next = next;
				} else {
					s_soundAssets = next;
				}
				delete asset;
				asset = NULL;
			}
		}

		if (asset) {
			previous = asset;
		}
		asset = next;
	}
}

void KLBOpenSLNewEngine::endAudioFrame() {
	for (u32 i = 0; i < 32; ++i) {
		if (m_voices[i].m_lifecycleState == 1) {
			m_voices[i].m_lifecycleState = 2;
		}
	}

	s32 consecutiveIdleVoices = 0;
	while (consecutiveIdleVoices < 32) {
		if (m_voices[consecutiveIdleVoices].m_lifecycleState) {
			consecutiveIdleVoices = 0;
		} else {
			++consecutiveIdleVoices;
		}
	}
	beginAudioFrame(true);
}

KLBOpenSLNewEngine::~KLBOpenSLNewEngine() {
	KLBOpenSLNewSoundAsset* asset = s_soundAssets;
	while (asset) {
		KLBOpenSLNewSoundAsset* next = asset->m_next;
		--asset->m_releaseCountdown;
		s_soundAssets = next;
		delete asset;
		asset = next;
	}
}

void KLBOpenSLNewEngine::terminate() {
	delete g_audioImplementation;
	g_audioImplementation = NULL;
}

bool KLBOpenSLNewEngine::init() {
	if (!initializeOpenSLPlatform(this)) {
		return false;
	}

	m_voiceMutex =
		CPFInterface::getInstance().platform().allocMutex();
	m_workerThread =
		CPFInterface::getInstance().platform().createThread(workerThread, this);
	if (!m_workerThread) {
		shutdownOpenSLPlatform(this);
		return false;
	}
	return true;
}

void KLBOpenSLNewEngine::shutdown() {
	if (m_workerThread) {
		m_workerEnabled = false;
		while (!m_workerStopped) {
		}
		while (!m_workerWaiting) {
		}
		while (m_workerAcknowledged) {
		}
		CPFInterface::getInstance().platform().deleteThread(m_workerThread);
		m_workerThread = NULL;

		if (m_voiceMutex) {
			CPFInterface::getInstance().platform().freeMutex(m_voiceMutex);
			m_voiceMutex = NULL;
		}
	}

	for (u32 i = 0; i < 4; ++i) {
		IAudioCodec* codec = m_codecs[i];
		if (codec) {
			codec->shutdown();
			delete codec;
		}
		m_codecs[i] = NULL;
	}

	shutdownOpenSLPlatform(this);
	terminate();
}

bool KLBOpenSLNewEngine::preLoad(void*) {
	return true;
}

bool KLBOpenSLNewEngine::playAudio(
	void* handle,
	s32 msec,
	float targetVolume,
	float startVolume
) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	KLBOpenSLVoice* voice = NULL;
	prepareOpenSLPlayback(false);
	if (asset->m_playback.isSE) {
		bool playbackAllowed = !isOpenSLPlaybackBlocked();
		if (!playbackAllowed) {
			return playbackAllowed;
		}
	} else {
		m_pendingAsset = asset;
	}
	bool acquired = acquireVoice(asset, &voice, false);
	if (!acquired || !voice) {
		return acquired;
	}

	if (msec) {
		voice->m_fadeCurrentVolume = startVolume;
		voice->m_assetVolume = 1.0f;
		voice->m_outputVolume = static_cast<s16>(
			voice->m_channelVolume
				* voice->m_masterVolume
				* voice->m_fadeCurrentVolume);
		voice->m_volumeDirty = false;
	} else {
		voice->m_assetVolume = targetVolume;
		voice->m_outputVolume = static_cast<s16>(
			voice->m_channelVolume
				* voice->m_assetVolume
				* voice->m_masterVolume
				* voice->m_fadeCurrentVolume);
		voice->m_volumeDirty = false;
	}
	voice->m_previousOutputVolume = voice->m_outputVolume;

	if (startVolume < 0.0f) {
		startVolume = 0.0f;
	} else if (startVolume > 1.0f) {
		startVolume = 1.0f;
	}
	if (msec > 0) {
		voice->m_fadeElapsedSamples = 0;
		voice->m_fadeTargetVolume = targetVolume;
		voice->m_fadeCommand = 1;
		voice->m_fadeStep = 1;
		voice->m_fadeStartVolume = startVolume;
		voice->m_fadeCurrentVolume = startVolume;
		u32 durationSamples = 0;
		if (voice->m_asset) {
			durationSamples =
				voice->m_asset->m_playback.sampleRate * msec / 1000;
		}
		voice->m_fadeDurationSamples = durationSamples;
	} else {
		voice->m_fadeCommand = 0;
		voice->m_fadeTargetVolume = 0.0f;
		voice->m_fadeElapsedSamples = 0;
		voice->m_fadeCurrentVolume = 1.0f;
		voice->m_fadeDurationSamples = 0;
	}
	voice->m_fadeFromCurrentVolume = false;
	voice->m_fadeStartTime = 0;
	return acquired;
}

s32 KLBOpenSLNewEngine::totalTimeAudio(void* handle) {
	KLBOpenSLNewSoundAsset* asset = static_cast<KLBOpenSLNewSoundAsset*>(handle);
	return asset ? asset->m_source.durationMs : 0;
}

void* KLBOpenSLNewEngine::loadAudio(const char* path, bool isSE, s32 mode, s32 option) {
	if ((strlen(path) == 8) && (strncmp(path, "asset://", 8) == 0)) {
		return NULL;
	}
	return KLBOpenSLNewSoundAsset::load(path, isSE, mode, option);
}

bool KLBOpenSLNewEngine::acquireVoice(
	KLBOpenSLNewSoundAsset* asset,
	KLBOpenSLVoice** result,
	bool
) {
	if (asset->m_playback.loadingFailed) {
		return false;
	}

	IPlatformRequest* platform = &CPFInterface::getInstance().platform();
	bool needsNewVoice = true;
	KLBOpenSLVoice* voice = NULL;
	if (asset->m_codecType == 1) {
		platform->mutexLock(m_voiceMutex);
		for (s32 i = 0; i < 32; ++i) {
			KLBOpenSLVoice& candidate = m_voices[i];
			if (candidate.m_lifecycleState == 1
			 && candidate.m_asset == asset) {
				voice = &candidate;
				needsNewVoice = false;
				break;
			}
		}
		platform->mutexUnlock(m_voiceMutex);
		if (!needsNewVoice) {
			*result = voice;
			return true;
		}
	} else if (asset->m_codecType == 2) {
		platform->mutexLock(m_voiceMutex);
		for (s32 i = 0; i < 32; ++i) {
			KLBOpenSLVoice& candidate = m_voices[i];
			if (candidate.m_lifecycleState == 1
			 && candidate.m_asset == asset) {
				candidate.m_state = 0x11;
				candidate.m_lifecycleState = 2;
				break;
			}
		}
		platform->mutexUnlock(m_voiceMutex);
	}

	s32 voiceIndex = 0;
	platform->mutexLock(m_voiceMutex);
	for (; voiceIndex < 32; ++voiceIndex) {
		if (m_voices[voiceIndex].m_lifecycleState == 0) {
			voice = &m_voices[voiceIndex];
			break;
		}
	}
	platform->mutexUnlock(m_voiceMutex);

	*result = voice;
	if (!voice) {
		return false;
	}
	if (voiceIndex >= m_activeVoiceCount) {
		m_activeVoiceCount = static_cast<u16>(voiceIndex + 1);
	}

	voice->initialize(asset);
	switch (asset->m_loadMode) {
	case 0:
		voice->m_loopEnabled = !asset->m_playback.isSE;
		break;
	case 1:
		voice->m_loopEnabled = true;
		break;
	case 2:
		voice->m_loopEnabled = false;
		break;
	}

	float assetVolume = voice->m_assetVolume;
	float channelVolume = voice->m_channelVolume;
	float fadeCurrentVolume = voice->m_fadeCurrentVolume;
	if (asset->m_playback.isSE) {
		float volume = m_seMultiProcessVolume * m_seMasterVolume;
		float clampedVolume = volume < 0.0f ? 0.0f : volume;
		clampedVolume = clampedVolume > 1.0f ? 1.0f : clampedVolume;
		voice->m_masterVolume = clampedVolume * 256.0f;
		voice->m_outputVolume = static_cast<s16>(
			assetVolume
				* channelVolume
				* voice->m_masterVolume
				* fadeCurrentVolume);
		voice->m_volumeDirty = false;
	} else {
		float volume = m_bgmMultiProcessVolume * m_bgmMasterVolume;
		float clampedVolume = volume < 0.0f ? 0.0f : volume;
		clampedVolume = clampedVolume > 1.0f ? 1.0f : clampedVolume;
		voice->m_masterVolume = clampedVolume * 256.0f;
		voice->m_outputVolume = static_cast<s16>(
			assetVolume
				* channelVolume
				* voice->m_masterVolume
				* fadeCurrentVolume);
		voice->m_volumeDirty = false;
		m_pendingVoice = voice;
	}
	return true;
}

void KLBOpenSLVoice::reset() {
	m_asset = NULL;
	m_pendingCommands = 0;
	m_queuedBufferCount = 0;
	m_callbackEnabled = true;
	m_reusable = true;
	m_loopEnabled = false;
	m_loopStartSample = 0;
	m_loopEndSample = 0;
	m_playbackSampleLimit = 0;
	m_fullyBuffered = false;
	m_clock = &m_primaryClock;
	m_primaryClock.elapsedTime = 0;
	m_clock->lastUpdateTime = 0;
	m_primaryBufferState.loopBoundary = false;
	m_primaryBufferState.currentPosition = 0;
	m_primaryBufferState.endReached = false;
	m_primaryBufferState.positionChanged = false;
	m_secondaryBufferState.loopBoundary = false;
	m_secondaryBufferState.currentPosition = 0;
	m_secondaryBufferState.endReached = false;
	m_secondaryBufferState.positionChanged = false;
	m_pendingRefillSamples = 0;
	m_refillOffset = 0;
	m_lifecycleState = 0;
	m_playbackMode = 0;
	m_state = 0;
	m_previousOutputVolume = 256;
	m_outputVolume = 256;
	m_volumeDirty = false;
	m_assetVolume = 1.0f;
	m_masterVolume = 1.0f;
	m_channelVolume = 1.0f;
	m_clock->lastBufferSamples = 0;
	m_clock->decodedSampleCount = 0;
	m_clock->presentedSampleCount = 0;
	m_loopStart = -1;
	m_loopEnd = -1;
	m_fadeElapsedSamples = 0;
	m_fadeStartVolume = 1.0f;
	m_fadeTargetVolume = 1.0f;
	m_fadeCurrentVolume = 1.0f;
	m_fadeStartTime = 0;
	m_fadeFromCurrentVolume = false;
	m_fadeDurationSamples = 0;
	m_fadeStep = 0;
	m_fadeCommand = 0;
	m_positionQueries[0] = &KLBOpenSLVoice::queryDecodedPosition;
	m_positionQueries[1] = &KLBOpenSLVoice::queryPlatformPosition;
}

KLBOpenSLVoice::KLBOpenSLVoice() {
	reset();
	m_stagingBuffer = NULL;
	m_primaryBufferState.bufferBegin = NULL;
	m_primaryBufferState.bufferEnd = NULL;
}

KLBOpenSLVoice::~KLBOpenSLVoice() {
	delete [] m_stagingBuffer;
	m_stagingBuffer = NULL;
}

void KLBOpenSLVoice::initialize(KLBOpenSLNewSoundAsset* asset) {
	reset();

	m_asset = asset;
	m_interleavedStereo = asset->m_source.interleavedStereo;
	m_bufferCapacity = static_cast<u32>(
		static_cast<double>(
			static_cast<u64>(asset->m_playback.sampleRate) << 24)
			/ static_cast<double>(
				KLBOpenSLNewEngine::getInstance()->m_sampleRate));
	m_playbackSampleLimit = asset->m_playback.sampleCount;
	m_fullyBuffered = asset->m_playback.fullyBuffered;
	const bool isSE = asset->m_playback.isSE;

	float masterVolume;
	KLBOpenSLNewEngine* engine = KLBOpenSLNewEngine::getInstance();
	if (isSE) {
		masterVolume =
			engine->m_seMasterVolume * engine->m_seMultiProcessVolume;
	} else {
		masterVolume =
			engine->m_bgmMasterVolume * engine->m_bgmMultiProcessVolume;
	}
	setMasterVolume(masterVolume);

	if (m_fullyBuffered) {
		m_loopEnabled = true;
		const u32 refillOffset =
			asset->m_playback.sampleRate * asset->m_source.channelCount;
		m_refillOffset = refillOffset;
		m_refillSize = asset->m_playback.sampleRate / 2;
		m_primaryBufferState.bufferBegin = asset->m_playback.pcmBufferBegin;
		m_primaryBufferState.bufferEnd = asset->m_playback.pcmBufferHalf;
		m_primaryBufferState.bufferOffset = 0;
		m_primaryBufferState.currentPosition = 0;
		m_secondaryBufferState.bufferOffset = 0;
		m_secondaryBufferState.currentPosition = 0;
		m_bufferState = &m_primaryBufferState;
		m_pendingRefillSamples = 0;
		m_currentPcmBuffer = m_primaryBufferState.bufferBegin;

		KLBOpenSLNewSoundAsset* voiceAsset = m_asset;
		s16* volatile& sharedBufferBegin = m_primaryBufferState.bufferBegin;
		s16* decodeTarget = sharedBufferBegin;
		IPlatformRequest* platform = &CPFInterface::getInstance().platform();
		platform->mutexLock(voiceAsset->m_playback.mutex);
		voiceAsset->m_playback.decoder->decode(refillOffset, 0, decodeTarget);
		platform->mutexUnlock(voiceAsset->m_playback.mutex);
		++m_asset->m_asyncReferences;
	} else {
		KLBOpenSLNewSoundAsset* voiceAsset = m_asset;
		m_currentPcmBuffer = voiceAsset->m_playback.pcmReadCursor;
		m_bufferState = &m_primaryBufferState;
		m_primaryBufferState.bufferBegin = NULL;
		m_primaryBufferState.bufferEnd = NULL;
		m_primaryBufferState.bufferOffset = 0;
		m_primaryBufferState.currentPosition = 0;
		m_secondaryBufferState.bufferOffset = 0;
		m_secondaryBufferState.currentPosition = 0;
		++voiceAsset->m_asyncReferences;
	}

	m_state = 0x10;
	m_callbackEnabled = false;
	m_queuedBufferCount = m_bufferCapacity;
	m_clock->elapsedTime = 0;
	m_playbackStarted = true;
	m_pendingCommands |= 1;
	m_outputVolume = static_cast<s16>(
		m_assetVolume * m_channelVolume
			* m_masterVolume * m_fadeCurrentVolume);
	m_volumeDirty = false;
	m_lifecycleState = 1;
}

s32 KLBOpenSLVoice::getState() const {
	return m_state;
}

s32 KLBOpenSLVoice::playbackPosition() {
	return (this->*m_positionQueries[s_positionQueryIndex])();
}

void KLBOpenSLVoice::setPositionQuery(u32 queryIndex) {
	s_positionQueryIndex = queryIndex;
}

s32 KLBOpenSLVoice::queryDecodedPosition() {
	PlaybackClock* clock = m_clock;
	s32 result;
	if (m_callbackEnabled) {
		result = clock->lastBufferSamples;
	} else {
		volatile s64 baseTime = clock->elapsedTime;
		volatile s64 previousTime =
			KLBOpenSLNewEngine::getInstance()->m_lastWorkerTime;
		volatile s64 now = audioNanoTime();
		s64 previousValue = previousTime;
		s64 nowValue = now;
		s64 elapsedValue = nowValue < previousValue
			? nowValue - previousValue + 0x7fffffffffffffffLL
			: nowValue - previousValue;
		volatile s64 elapsed =
			elapsedValue & 0x7fffffffffffffffLL;

		volatile s64 decodedSamples = clock->decodedSampleCount;
		volatile s64 decodedTime =
			decodedSamples / m_asset->m_playback.sampleRate;
		volatile s64 scaledTime = decodedTime * 1000;
		volatile s64 position = scaledTime >> 14;
		if (clock->presentedSampleCount <= position) {
			clock->presentedSampleCount = position;

			position = (baseTime + elapsed) / 1000000;
			if (position < clock->lastBufferSamples) {
				position = clock->lastBufferSamples;
			} else {
				if ((position - clock->lastBufferSamples) > 4096
				 && m_playbackMode != 1) {
					m_playbackMode = 1;
					char timing[256];
					sprintf(timing, "%lld, %u, %lld",
						position, clock->lastBufferSamples, elapsed);
					CPFInterface::getInstance().platform().addExtMsg(
						"TimerInfo", timing, false);
					CPFInterface::getInstance().platform().sendException(
						"tellAudioByDAC");
				}
				clock->lastBufferSamples = static_cast<u32>(position);
			}
		}
		result = clock->lastBufferSamples;
	}
	return result;
}

s32 KLBOpenSLVoice::queryPlatformPosition() {
	PlaybackClock* clock = m_clock;
	s64 now = audioNanoTime();
	if (!clock->lastUpdateTime) {
		clock->lastUpdateTime = now;
	}

	s64 lastUpdate = clock->lastUpdateTime;
	s64 elapsed = now - lastUpdate;
	if (now < lastUpdate) {
		elapsed += 0x7fffffffffffffffLL;
	}
	elapsed &= 0x7fffffffffffffffLL;
	clock->elapsedTime += elapsed;
	clock->decodedSampleCount = clock->lastBufferSamples;
	s64 decodedTime = clock->decodedSampleCount * 1000000 + elapsed;
	clock->decodedSampleCount = decodedTime;

	KLBOpenSLNewEngine::getInstance()->m_lastWorkerTime = now;
	clock->lastUpdateTime = now;
	u32 position = static_cast<u32>(clock->decodedSampleCount / 1000000);
	clock->lastBufferSamples = position;
	return position;
}

void KLBOpenSLVoice::pause(s32 duration, float targetVolume) {
	if (duration > 0) {
		m_fadeElapsedSamples = 0;
		m_fadeTargetVolume = targetVolume;
		m_fadeCommand = 3;
		m_fadeStep = 1;
		m_fadeStartVolume = 0.0f;
		u32 durationSamples = 0;
		if (m_asset) {
			durationSamples =
				m_asset->m_playback.sampleRate * duration / 1000;
		}
		m_fadeDurationSamples = durationSamples;
		m_fadeFromCurrentVolume = false;
		m_fadeStartTime = 0;
	} else if (m_state != 0x12) {
		m_state = 0x12;
		m_callbackEnabled = true;
		m_queuedBufferCount = 0;
		m_pendingCommands |= 0x10;
		m_outputVolume = static_cast<s16>(
			m_assetVolume
				* m_channelVolume
				* m_masterVolume
				* m_fadeCurrentVolume);
		m_volumeDirty = false;
	}
	syncPlaybackClock();
}

void KLBOpenSLVoice::resume(u32 msec, float targetVolume) {
	s32 duration = static_cast<s32>(msec);
	if (duration > 0) {
		m_fadeElapsedSamples = 0;
		m_fadeTargetVolume = targetVolume;
		m_fadeCommand = 4;
		m_fadeStep = 1;
		m_fadeStartVolume = 0.0f;
		m_fadeCurrentVolume = 0.0f;
		u32 durationSamples = 0;
		if (m_asset) {
			durationSamples =
				m_asset->m_playback.sampleRate * duration / 1000;
		}
		m_fadeDurationSamples = durationSamples;
		m_fadeFromCurrentVolume = false;
		m_fadeStartTime = 0;
	}

	KLBOpenSLNewEngine::getInstance()->resetWorkerClock();
	if (m_state != 0x10) {
		m_state = 0x10;
		m_callbackEnabled = false;
		m_queuedBufferCount = m_bufferCapacity;
		m_pendingCommands |= 0x20;
		m_outputVolume = static_cast<s16>(
			m_assetVolume
				* m_channelVolume
				* m_masterVolume
				* m_fadeCurrentVolume);
		m_volumeDirty = false;
	}
}

void KLBOpenSLVoice::stop(s32 duration, float targetVolume) {
	if (duration > 0) {
		float startVolume = m_fadeFromCurrentVolume
			? m_fadeCurrentVolume
			: m_assetVolume;
		m_assetVolume = 1.0f;
		float clampedVolume = startVolume;
		if (clampedVolume < 0.0f) {
			clampedVolume = 0.0f;
		} else if (clampedVolume > 1.0f) {
			clampedVolume = 1.0f;
		}

		m_fadeElapsedSamples = 0;
		m_fadeTargetVolume = targetVolume;
		m_fadeCommand = 2;
		m_fadeStep = 1;
		m_fadeStartVolume = clampedVolume;
		u32 durationSamples = 0;
		if (m_asset) {
			durationSamples =
				m_asset->m_playback.sampleRate * duration / 1000;
		}
		m_fadeDurationSamples = durationSamples;
		m_fadeFromCurrentVolume = false;
		m_fadeStartTime = 0;
	} else {
		m_state = 0x11;
		m_lifecycleState = 2;
	}
}

void KLBOpenSLVoice::syncPlaybackClock() {
	PlaybackClock* clock = m_clock;
	s64 position = clock->decodedSampleCount;
	position /= m_asset->m_playback.sampleRate;
	position *= 1000;
	position >>= 14;
	clock->elapsedTime =
		static_cast<s64>(static_cast<float>(position) * 1000000.0f);
}

void KLBOpenSLVoice::setMasterVolume(float volume) {
	float clampedVolume = volume < 0.0f ? 0.0f : volume;
	clampedVolume = clampedVolume > 1.0f ? 1.0f : clampedVolume;
	m_masterVolume = clampedVolume * 256.0f;
	m_outputVolume = static_cast<s16>(
		m_assetVolume * m_channelVolume * m_masterVolume * m_fadeCurrentVolume);
	m_volumeDirty = false;
}

void KLBOpenSLVoice::setAssetVolume(float volume, bool notifyPlayer) {
	m_assetVolume = volume;
	m_outputVolume = static_cast<s16>(
		m_assetVolume * m_channelVolume * m_masterVolume * m_fadeCurrentVolume);
	m_volumeDirty = notifyPlayer;
}

void KLBOpenSLVoice::setChannelVolume(float volume) {
	if (m_channelVolume != volume) {
		m_channelVolume = volume;
		m_outputVolume = static_cast<s16>(
			m_channelVolume * m_assetVolume * m_masterVolume * m_fadeCurrentVolume);
		m_volumeDirty = true;
	}
}

void KLBOpenSLVoice::setLoop(s32 start, s32 end) {
	if (start != 1 || end != -1) {
		m_loopStartSample =
			static_cast<u32>(
				static_cast<u64>(start)
					* static_cast<u64>(m_asset->m_playback.sampleRate)
					/ 1000);
		u32 loopEndSample;
		u64 playbackSampleLimit;
		if (end == -1) {
			playbackSampleLimit = m_asset->m_playback.sampleCount;
			loopEndSample = 0;
		} else {
			playbackSampleLimit =
				static_cast<u64>(m_asset->m_playback.sampleRate)
					* static_cast<u64>(end) / 1000;
			loopEndSample = static_cast<u32>(playbackSampleLimit);
		}
		m_playbackSampleLimit = playbackSampleLimit;
		m_loopEndSample = loopEndSample;
		m_loopEnabled = true;

		if (m_asset->m_playback.fullyBuffered) {
			if (!m_stagingBuffer) {
				m_stagingBuffer =
					new u8[static_cast<size_t>(m_refillOffset) * 2];
			}
			m_reusable = false;
		}
	} else {
		u32 sampleCount = m_asset->m_playback.sampleCount;
		m_playbackSampleLimit = sampleCount;
		m_loopEndSample = sampleCount;
		m_loopStartSample = 0;
		m_loopEnabled = !m_asset->m_playback.isSE;
	}
}

bool KLBOpenSLVoice::configureFade(
	s16 command,
	s32 duration,
	float targetVolume,
	float startVolume,
	u16 step
) {
	float clampedStartVolume = startVolume;
	if (clampedStartVolume < 0.0f) {
		clampedStartVolume = 0.0f;
	} else if (clampedStartVolume > 1.0f) {
		clampedStartVolume = 1.0f;
	}

	if (static_cast<u16>(command - 1) <= 4 && duration > 0) {
		m_fadeElapsedSamples = 0;
		m_fadeTargetVolume = targetVolume;
		m_fadeCommand = command;
		m_fadeStep = step;
		switch (command) {
		case 2:
		case 3:
		case 5:
			m_fadeStartVolume = clampedStartVolume;
			break;
		case 1:
		case 4:
			m_fadeStartVolume = clampedStartVolume;
			m_fadeCurrentVolume = clampedStartVolume;
			break;
		}

		u32 durationSamples = 0;
		if (m_asset) {
			durationSamples =
				m_asset->m_playback.sampleRate * duration / 1000;
		}
		m_fadeDurationSamples = durationSamples;
	} else {
		m_fadeCommand = 0;
		m_fadeTargetVolume = 0.0f;
		m_fadeElapsedSamples = 0;
		m_fadeCurrentVolume = 1.0f;
		m_fadeDurationSamples = 0;
	}
	m_fadeFromCurrentVolume = false;
	m_fadeStartTime = 0;
	return true;
}

bool KLBOpenSLVoice::updateFade() {
	s64 now = audioNanoTime();
	if (!m_fadeFromCurrentVolume && !m_fadeCommand) {
		return false;
	}
	if (!m_fadeFromCurrentVolume) {
		m_fadeFromCurrentVolume = true;
		m_fadeStartTime = now;
	}

	s64 elapsedTime = now - m_fadeStartTime;
	m_fadeElapsedSamples += static_cast<s32>(
		elapsedTime * m_asset->m_playback.sampleRate / 1000000000);
	if (m_fadeElapsedSamples >= m_fadeDurationSamples) {
		m_fadeElapsedSamples = m_fadeDurationSamples;
	} else if (m_fadeElapsedSamples < 0) {
		m_fadeElapsedSamples = 0;
	}

	if (m_fadeElapsedSamples >= 0 && m_fadeDurationSamples > 0) {
		m_fadeCurrentVolume = m_fadeStartVolume
			+ (m_fadeTargetVolume - m_fadeStartVolume)
				* (static_cast<float>(m_fadeElapsedSamples)
					/ static_cast<float>(m_fadeDurationSamples));
		if (m_fadeCurrentVolume < 0.0f) {
			m_fadeCurrentVolume = 0.0f;
		} else if (m_fadeCurrentVolume > 1.0f) {
			m_fadeCurrentVolume = 1.0f;
		}
	}

	m_outputVolume = static_cast<s16>(
		m_assetVolume * m_channelVolume
			* m_masterVolume * m_fadeCurrentVolume);
	m_volumeDirty = true;
	if (m_fadeElapsedSamples < m_fadeDurationSamples) {
		m_fadeStartTime = now;
		return true;
	}

	m_fadeElapsedSamples = m_fadeDurationSamples;
	m_fadeFromCurrentVolume = false;
	m_fadeCurrentVolume = 1.0f;
	m_assetVolume = m_fadeTargetVolume;
	m_outputVolume = static_cast<s16>(
		m_assetVolume * m_channelVolume * m_masterVolume);
	m_volumeDirty = false;

	if (m_fadeCommand == 2) {
		m_state = 0x11;
		m_lifecycleState = 2;
	} else if (m_fadeCommand == 3) {
		if (m_state != 0x12) {
			m_state = 0x12;
			m_callbackEnabled = true;
			m_queuedBufferCount = 0;
			m_pendingCommands |= 0x10;
			m_outputVolume = static_cast<s16>(
				m_assetVolume * m_channelVolume * m_masterVolume);
			m_volumeDirty = false;
		}
	}
	m_fadeCommand = 0;
	m_fadeStartTime = now;
	return false;
}

u64 KLBOpenSLVoice::advancePlayback(
	u32 samples, PlaybackBufferState* state
) {
	if (m_lifecycleState != 1) {
		return 0;
	}

	if (state->positionChanged) {
		state->positionChanged = false;
		state->currentPosition = state->requestedPosition;
		if (m_loopEnd >= 0) {
			m_loopEnd = -1;
			PlaybackClock* clock = &m_primaryClock;
			if (clock == m_clock) {
				clock = &m_secondaryClock;
			}
			m_clock = clock;
		}
	}

	u64* position = &state->currentPosition;
	u64 playbackPosition = *position;
	u64 samplePosition = playbackPosition >> 24;
	u64 requestedEnd =
		state->bufferOffset + samplePosition + samples;
	bool wrapped = false;
	if (requestedEnd >= m_playbackSampleLimit) {
		if (!m_loopEnabled) {
			m_lifecycleState = 2;
			m_currentPcmBuffer = &s_silentSample;
			m_queuedBufferCount = 0;
			m_bufferCapacity = 0;
			return 0;
		}
		state->endReached = m_fullyBuffered;
		state->loopBoundary = m_fullyBuffered;
		wrapped = true;
	}

	if (m_fullyBuffered) {
		if (samplePosition >= m_refillSize) {
			state->loopBoundary = true;
		}
		u32 lateLoopThreshold = m_refillSize * 30 / 16;
		if (samplePosition > lateLoopThreshold) {
			*position = static_cast<u64>(lateLoopThreshold) << 24;
		}
		m_currentPcmBuffer = state->bufferBegin;
		return playbackPosition;
	}
	if (wrapped) {
		*position = static_cast<u64>(
			static_cast<s32>(m_loopStartSample)) << 24;
	}
	return playbackPosition;
}

void KLBOpenSLVoice::releaseStagingBuffer() {
	delete [] m_stagingBuffer;
	m_stagingBuffer = NULL;
}

void KLBOpenSLNewSoundAsset::stopLoader() {
	if (m_loaderTask) {
		while (!m_loaderFinished) {
		}
		CPFInterface::getInstance().platform().deleteThread(m_loaderTask);
		m_loaderTask = NULL;
	}
}

void KLBOpenSLNewSoundAsset::startLoader(s32 workUnit) {
	m_loaderWorkUnit = workUnit;
	m_loaderFinished = false;
	m_loaderTask = CPFInterface::getInstance().platform().createThread(
		loaderThread, this);
}

s32 KLBOpenSLNewSoundAsset::loaderThread(void*, void* data) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(data);
	u32 sampleOffset = asset->m_loaderWorkUnit / asset->m_source.channelCount;
	u32 remainingSamples =
		asset->m_source.pcmByteCount - asset->m_loaderWorkUnit;
	s16* output = asset->m_loaderWorkUnit + asset->m_source.pcmSamples;

	IPlatformRequest* platform = &CPFInterface::getInstance().platform();
	platform->mutexLock(asset->m_playback.mutex);
	asset->m_playback.decoder->decode(remainingSamples, sampleOffset, output);
	platform->mutexUnlock(asset->m_playback.mutex);
	asset->m_loaderFinished = true;
	return 0;
}

void KLBOpenSLNewEngine::renderAudio(s16* buffer, u16 samples) {
	const s64 now = audioNanoTime();
	if (m_resetWorkerClock) {
		m_resetWorkerClock = false;
		m_lastWorkerTime = now;
	}
	const s64 previousTime = static_cast<s64>(m_lastWorkerTime);
	const s64 elapsed =
		(now < previousTime
			? now - previousTime + 0x7fffffffffffffffLL
			: now - previousTime)
		& 0x7fffffffffffffffLL;
	m_lastWorkerTime = now;

	KLBOpenSLVoice* activeVoices[32];
	s32 volumes[32];
	s32 volumeSteps[32];
	u32 activeVoiceCount = 0;

	for (s32 i = 0; i < m_activeVoiceCount; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}
		const s32 voiceSamples = samples;

		voice.m_asset->m_updateGeneration =
			static_cast<u8>((voice.m_asset->m_updateGeneration + 1) & 0x7f);
		KLBOpenSLVoice::PlaybackBufferState* state = voice.m_bufferState;
		u64 startTime = voice.advancePlayback(voiceSamples, state);
		KLBOpenSLVoice::PlaybackClock* clock = voice.m_clock;
		clock->startTime = startTime;
		if (voice.m_lifecycleState != 1 || voice.m_callbackEnabled) {
			continue;
		}

		const s32 outputVolume = voice.m_outputVolume;
		if (voice.m_volumeDirty) {
			volumeSteps[activeVoiceCount] =
				((outputVolume - voice.m_previousOutputVolume) << 22)
				/ voiceSamples;
			volumes[activeVoiceCount] =
				voice.m_previousOutputVolume << 22;
		} else {
			volumes[activeVoiceCount] = outputVolume << 22;
			volumeSteps[activeVoiceCount] = 0;
		}
		voice.m_previousOutputVolume = static_cast<u16>(outputVolume);
		activeVoices[activeVoiceCount] = &voice;

		const u64 playbackAdvance =
			static_cast<u64>(voice.m_queuedBufferCount) * voiceSamples;
		state->currentPosition += playbackAdvance;
		clock->decodedSampleCount += playbackAdvance >> 10;
		++activeVoiceCount;
		clock->elapsedTime += voice.m_queuedBufferCount ? elapsed : 0;
	}

	const u32 voiceCount = activeVoiceCount;
	for (u16 sample = 0; sample < samples; ++sample) {
		s32 right = 0;
		s32 left = 0;

		for (u32 i = 0; i < voiceCount; ++i) {
			KLBOpenSLVoice* voice = activeVoices[i];
			KLBOpenSLVoice::PlaybackClock* clock = voice->m_clock;
			const u32 stereoShift = voice->m_interleavedStereo;
			u32 sampleIndex =
				static_cast<u32>(clock->startTime >> 24) << stereoShift;
			s16* pcm = voice->m_currentPcmBuffer;

			const s64 leftBegin = pcm[sampleIndex];
			const u64 leftBeginTime = clock->startTime;
			const s64 leftEnd =
				pcm[sampleIndex + stereoShift + 1];
			const u64 leftEndTime = clock->startTime;
			const s32 gain = volumes[i];
			sampleIndex += stereoShift;
			const s64 rightBegin =
				pcm[sampleIndex];
			const u64 rightBeginTime = clock->startTime;
			const s64 rightEnd =
				pcm[sampleIndex + stereoShift + 1];
			const u64 rightEndTime = clock->startTime;
			volumes[i] += volumeSteps[i];
			const s32 scaledGain = gain >> 22;
			clock->startTime += voice->m_queuedBufferCount;
			const s64 leftInterpolated =
				((((0x3ff - (leftBeginTime >> 14)) & 0x3ff)
					* leftBegin)
				+ (((leftEndTime >> 14) & 0x3ff) * leftEnd))
				>> 10;
			left += leftInterpolated * scaledGain;
			const s64 rightInterpolated =
				((((0x3ff - (rightBeginTime >> 14)) & 0x3ff)
					* rightBegin)
				+ (((rightEndTime >> 14) & 0x3ff) * rightEnd))
				>> 10;
			right += rightInterpolated * scaledGain;
		}

		left >>= 8;
		right >>= 8;
		if (left < -32768) {
			left = -32768;
		}
		if (right < -32768) {
			right = -32768;
		}
		if (left > 32767) {
			left = 32767;
		}
		if (right > 32767) {
			right = 32767;
		}
		*buffer++ = static_cast<s16>(left);
		*buffer++ = static_cast<s16>(right);
	}
}

KLBAudioCommand* KLBOpenSLNewEngine::popAudioCommand() {
	u16 readIndex = m_commandReadIndex;
	KLBAudioCommand* command = NULL;
	if (readIndex != m_commandWriteIndex) {
		command = &m_commands[readIndex];
		readIndex = static_cast<u16>(readIndex + 1);
		if (readIndex > 127) {
			readIndex = 0;
		}
	}
	m_commandReadIndex = readIndex;
	return command;
}

void KLBOpenSLNewEngine::pushAudioCommand(
	void* asset,
	bool soundEffect,
	s32 type,
	void* voice,
	s32 value,
	s64 playbackPosition
) {
	u32 writeIndex = m_commandWriteIndex;
	KLBAudioCommand& command = m_commands[writeIndex];
	command.asset = asset;
	command.soundEffect = soundEffect;
	command.type = type;
	command.voice = voice;
	command.value = value;
	if (type == 1) {
		command.payload.playbackPosition = playbackPosition;
	} else {
		command.payload.state = value;
	}

	u32 nextWriteIndex = writeIndex + 1;
	if (nextWriteIndex > 127) {
		nextWriteIndex = 0;
	}
	m_commandWriteIndex = static_cast<u16>(nextWriteIndex);
}

void KLBOpenSLNewEngine::processAudio() {
	IPlatformRequest* platform = &CPFInterface::getInstance().platform();
	platform->mutexLock(m_voiceMutex);

	s32 activeVoiceCount = 0;
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 2) {
			voice.m_lifecycleState = 0;
			voice.m_pendingCommands |= 2;
		}

		if (voice.m_pendingCommands) {
			if (voice.m_pendingCommands & 1) {
				KLBOpenSLNewSoundAsset* asset = voice.m_asset;
				pushAudioCommand(asset, asset->m_playback.isSE, 1, &voice,
					asset->m_source.durationMs,
					reinterpret_cast<s64>(asset->m_playback.archivePath));
			}
			if (voice.m_pendingCommands & 2) {
				KLBOpenSLNewSoundAsset* asset = voice.m_asset;
				pushAudioCommand(asset, asset->m_playback.isSE, 2, &voice, 0, 0);
			}
			if (voice.m_pendingCommands & 0x10) {
				KLBOpenSLNewSoundAsset* asset = voice.m_asset;
				pushAudioCommand(
					asset, asset->m_playback.isSE, 0x10, &voice, 0, 0);
			}
			if (voice.m_pendingCommands & 0x20) {
				KLBOpenSLNewSoundAsset* asset = voice.m_asset;
				pushAudioCommand(
					asset, asset->m_playback.isSE, 0x20, &voice, 0, 0);
			}
			if (voice.m_bufferState->endReached) {
				KLBOpenSLNewSoundAsset* asset = voice.m_asset;
				pushAudioCommand(asset, asset->m_playback.isSE, 4, &voice, 0, 0);
			}
			if (voice.m_pendingCommands & 2) {
				voice.m_asset->releaseAsyncReference();
			}
			voice.m_pendingCommands = 0;
		}

		if (voice.m_lifecycleState) {
			activeVoiceCount = i + 1;
		}
	}
	m_activeVoiceCount = static_cast<u16>(activeVoiceCount);
	platform->mutexUnlock(m_voiceMutex);

	// The voice whose staging buffer still has to be primed is refilled once,
	// after every voice has been advanced.
	KLBOpenSLVoice* pendingRefill = NULL;
	for (s32 i = 0; i < activeVoiceCount; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}

		if (voice.m_stagingBuffer && !voice.m_reusable) {
			pendingRefill = &voice;
		}

		KLBOpenSLNewSoundAsset* asset = voice.m_asset;
		KLBOpenSLVoice::PlaybackClock* playbackClock = voice.m_clock;
		const bool soundEffect = asset->m_playback.isSE;
		s64 decodedTime = playbackClock->decodedSampleCount
			/ asset->m_playback.sampleRate;
		u64 playbackPosition = static_cast<u64>(decodedTime * 1000) >> 14;
		pushAudioCommand(asset, soundEffect, 8, &voice,
			static_cast<s32>(playbackPosition),
			static_cast<s64>(playbackPosition));

		if (!voice.updateFade()) {
			voice.m_outputVolume = static_cast<s16>(
				voice.m_assetVolume * voice.m_channelVolume
					* voice.m_masterVolume * voice.m_fadeCurrentVolume);
			voice.m_volumeDirty = false;
		}

		// Streaming playback keeps two buffer halves in flight: one is played
		// while the other is refilled from the decoder or the staging copy.
		const s32 seekMsec = voice.m_loopStart;
		KLBOpenSLVoice::PlaybackClock* clock = &voice.m_primaryClock;
		if (clock == voice.m_clock) {
			clock = &voice.m_secondaryClock;
		}
		if (!voice.m_asset->m_playback.fullyBuffered) {
			continue;
		}

		KLBOpenSLVoice::PlaybackBufferState* current = voice.m_bufferState;
		if (!current->loopBoundary && seekMsec < 0) {
			continue;
		}

		KLBOpenSLVoice::PlaybackBufferState* target =
			&voice.m_primaryBufferState;
		if (target == current) {
			target = &voice.m_secondaryBufferState;
		}
		const u32 refillOffset = voice.m_refillOffset;
		const bool seekRequested = seekMsec >= 0;
		s16* bufferBegin = current->bufferBegin;
		s16* bufferEnd = current->bufferEnd;

		if (seekRequested || current->endReached) {
			if (seekMsec < 0) {
				const s64 sampleOffset =
					static_cast<s32>(voice.m_loopStartSample);
				target->bufferOffset = sampleOffset;

				if (voice.m_stagingBuffer && voice.m_reusable) {
					memcpy(bufferEnd, voice.m_stagingBuffer,
						refillOffset * sizeof(s16));
				} else {
					KLBOpenSLNewSoundAsset* loopAsset = voice.m_asset;
					IPlatformRequest* loopPlatform =
						&CPFInterface::getInstance().platform();
					loopPlatform->mutexLock(loopAsset->m_playback.mutex);
					loopAsset->m_playback.decoder->decode(refillOffset,
						static_cast<u32>(sampleOffset), bufferEnd);
					loopPlatform->mutexUnlock(loopAsset->m_playback.mutex);
				}
				target->requestedPosition = 0;
			} else {
				const s64 requestedMsec = seekMsec;
				voice.m_loopStart = -1;
				clock->elapsedTime = requestedMsec * 1000000;
				clock->lastBufferSamples = seekMsec;
				clock->decodedSampleCount =
					(voice.m_asset->m_playback.sampleRate * requestedMsec
						/ 1000) << 14;
				clock->presentedSampleCount = 0;
				const s64 sampleOffset = clock->decodedSampleCount >> 14;
				target->bufferOffset = sampleOffset;

				if (voice.m_stagingBuffer && voice.m_reusable) {
					memcpy(bufferEnd, voice.m_stagingBuffer,
						voice.m_refillOffset * 2);
				} else {
					KLBOpenSLNewSoundAsset* seekAsset = voice.m_asset;
					u32 sampleCount = voice.m_refillOffset;
					IPlatformRequest* seekPlatform =
						&CPFInterface::getInstance().platform();
					seekPlatform->mutexLock(seekAsset->m_playback.mutex);
					seekAsset->m_playback.decoder->decode(sampleCount,
						static_cast<u32>(sampleOffset), bufferEnd);
					seekPlatform->mutexUnlock(seekAsset->m_playback.mutex);
				}
				target->requestedPosition = 0;
				voice.m_loopEnd = seekMsec;
			}
		} else {
			const u32 halfSamples = refillOffset >> 1;
			memcpy(bufferEnd, bufferBegin + halfSamples,
				halfSamples * sizeof(s16));

			const u32 advance = voice.m_refillOffset
				>> (voice.m_interleavedStereo + 1);
			const u64 bufferOffset = current->bufferOffset + advance;
			target->bufferOffset = bufferOffset;

			KLBOpenSLNewSoundAsset* decodeAsset = voice.m_asset;
			s16* output = bufferEnd + halfSamples;
			IPlatformRequest* decodePlatform =
				&CPFInterface::getInstance().platform();
			decodePlatform->mutexLock(decodeAsset->m_playback.mutex);
			decodeAsset->m_playback.decoder->decode(halfSamples,
				static_cast<u32>(bufferOffset + advance), output);
			decodePlatform->mutexUnlock(decodeAsset->m_playback.mutex);

			const s64 requestedPosition =
				static_cast<s64>(current->currentPosition)
					- (static_cast<s64>(voice.m_refillSize) << 24);
			if (requestedPosition < 0) {
				continue;
			}
			target->requestedPosition = requestedPosition;
		}

		target->bufferBegin = bufferEnd;
		target->bufferEnd = bufferBegin;
		target->loopBoundary = false;
		target->endReached = false;
		target->positionChanged = true;
		voice.m_bufferState = target;
	}

	if (pendingRefill) {
		u8* stagingBuffer = pendingRefill->m_stagingBuffer;
		if (stagingBuffer && !pendingRefill->m_reusable) {
			KLBOpenSLNewSoundAsset* asset = pendingRefill->m_asset;
			u32 sampleCount = pendingRefill->m_refillOffset;
			u32 sampleOffset = pendingRefill->m_loopStartSample;
			IPlatformRequest* refillPlatform =
				&CPFInterface::getInstance().platform();
			refillPlatform->mutexLock(asset->m_playback.mutex);
			asset->m_playback.decoder->decode(sampleCount, sampleOffset,
				(s16*)stagingBuffer);
			refillPlatform->mutexUnlock(asset->m_playback.mutex);
			pendingRefill->m_reusable = true;
		}
	}
}

void KLBOpenSLNewEngine::setMasterVolume(float volume, bool seMode) {
	if (seMode) {
		m_seMasterVolume = volume;
	} else {
		m_bgmMasterVolume = volume;
	}

	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1) {
			if (seMode) {
				if (voice.m_asset->m_playback.isSE) {
					voice.setMasterVolume(
						m_seMultiProcessVolume * volume);
				}
			} else {
				if (!voice.m_asset->m_playback.isSE) {
					voice.setMasterVolume(
						m_bgmMultiProcessVolume * volume);
				}
			}
		}
	}
}

void KLBOpenSLNewEngine::setAudioMultiProcessType(s32 processType) {
	float seVolume;
	float bgmVolume;
	switch (processType) {
	case 20:
		seVolume = 1.0f;
		bgmVolume = 1.0f;
		break;
	case 21:
		seVolume = 0.0f;
		bgmVolume = 1.0f;
		break;
	case 22:
		seVolume = 1.0f;
		bgmVolume = 0.0f;
		break;
	default:
		seVolume = 1.0f;
		bgmVolume = 1.0f;
		break;
	}

	m_seMultiProcessVolume = seVolume;
	m_bgmMultiProcessVolume = bgmVolume;
	setMasterVolume(m_seMasterVolume, true);
	setMasterVolume(m_bgmMasterVolume, false);
}

void KLBOpenSLNewEngine::releaseAudio(void* handle) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			voice.m_state = 0x11;
			voice.m_lifecycleState = 2;
		}
	}
	asset->releaseReference();
}

s32 KLBOpenSLNewEngine::getState(void* handle) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (s32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			return voice.getState();
		}
	}
	return 0;
}

s32 KLBOpenSLNewEngine::tellAudio(void* handle) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (s32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			return voice.playbackPosition();
		}
	}
	return 0;
}

void KLBOpenSLNewEngine::setAudioVolume(
	void* handle, float volume, bool
) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (s32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			voice.setAssetVolume(volume, false);
			return;
		}
	}
}

void KLBOpenSLNewEngine::setFormAudioVolume(
	void* handle, float volume, bool
) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (s32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			voice.setChannelVolume(volume);
			return;
		}
	}
}

void KLBOpenSLNewEngine::setAudioLoop(
	void* handle, s32 start, s32 end
) {
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}
		KLBOpenSLNewSoundAsset* asset = voice.m_asset;
		if (asset != handle) {
			continue;
		}

		if (end != -1 || start != 1) {
			voice.m_loopStartSample =
				static_cast<u32>(
					static_cast<u64>(asset->m_playback.sampleRate)
						* static_cast<u64>(start) / 1000);
			u64 playbackSampleLimit;
			u32 loopEndSample = 0;
			if (end == -1) {
				playbackSampleLimit =
					asset->m_playback.sampleCount;
			} else {
				playbackSampleLimit =
					static_cast<u64>(asset->m_playback.sampleRate)
						* static_cast<u64>(end) / 1000;
				loopEndSample =
					static_cast<u32>(playbackSampleLimit);
			}
			voice.m_playbackSampleLimit = playbackSampleLimit;
			voice.m_loopEndSample = loopEndSample;
			voice.m_loopEnabled = true;

			if (asset->m_playback.fullyBuffered) {
				if (!voice.m_stagingBuffer) {
					voice.m_stagingBuffer =
						new u8[
							static_cast<size_t>(
								voice.m_refillOffset) * 2];
				}
				voice.m_reusable = false;
			}
		} else {
			u32 sampleCount = asset->m_playback.sampleCount;
			voice.m_playbackSampleLimit = sampleCount;
			voice.m_loopEndSample = sampleCount;
			voice.m_loopStartSample = 0;
			voice.m_loopEnabled = !asset->m_playback.isSE;
		}
	}
}

void KLBOpenSLNewEngine::resumeAudio(
	void* handle, bool, float targetVolume, u32 msec
) {
	KLBOpenSLNewSoundAsset* asset =
		static_cast<KLBOpenSLNewSoundAsset*>(handle);
	for (s32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_asset == asset) {
			voice.resume(msec, targetVolume);
		}
	}
}

void KLBOpenSLNewEngine::seekAudio(void* handle, s32 msec) {
	s64 requestedTime = static_cast<s64>(msec);
	s64 elapsedTime = requestedTime * 1000000;

	for (u32 i = 0; i < 32; ++i) {
		if (m_voices[i].m_lifecycleState == 1
		 && m_voices[i].m_asset == handle) {
			KLBOpenSLVoice& voice = m_voices[i];
			voice.m_loopStart = msec;
			volatile KLBOpenSLVoice& sharedVoice = voice;
			if (!sharedVoice.m_asset->m_playback.fullyBuffered) {
				KLBOpenSLVoice::PlaybackClock* clock =
					&voice.m_primaryClock;
				if (voice.m_clock == clock) {
					clock = &voice.m_secondaryClock;
				}
				clock->elapsedTime = elapsedTime;
				clock->lastBufferSamples = msec;
				clock->decodedSampleCount =
					(sharedVoice.m_asset->m_playback.sampleRate
						* requestedTime / 1000) << 14;
				clock->presentedSampleCount = 0;

				KLBOpenSLVoice::PlaybackBufferState* state =
					&voice.m_primaryBufferState;
				if (voice.m_bufferState == state) {
					state = &voice.m_secondaryBufferState;
				}
				state->bufferOffset = 0;
				state->requestedPosition =
					clock->decodedSampleCount << 10;
				voice.m_loopEnd = msec;
				state->positionChanged = true;
				voice.m_bufferState = state;
			}
		}
	}
}

void KLBOpenSLNewEngine::stopAudio(
	void* handle, bool, float targetVolume, u32 msec
) {
	s32 duration = static_cast<s32>(msec);
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}
		KLBOpenSLNewSoundAsset* voiceAsset = voice.m_asset;
		if (voiceAsset != handle) {
			continue;
		}

		if (duration <= 0) {
			voice.m_state = 0x11;
			voice.m_lifecycleState = 2;
			continue;
		}

		float startVolume = voice.m_fadeFromCurrentVolume
			? voice.m_fadeCurrentVolume
			: voice.m_assetVolume;
		voice.m_assetVolume = 1.0f;
		float clampedVolume = startVolume;
		if (clampedVolume < 0.0f) {
			clampedVolume = 0.0f;
		} else if (clampedVolume > 1.0f) {
			clampedVolume = 1.0f;
		}

		voice.m_fadeElapsedSamples = 0;
		voice.m_fadeTargetVolume = targetVolume;
		voice.m_fadeCommand = 2;
		voice.m_fadeStep = 1;
		voice.m_fadeStartVolume = clampedVolume;
		u32 durationSamples = 0;
		if (voiceAsset) {
			durationSamples =
				voiceAsset->m_playback.sampleRate * duration / 1000;
		}
		voice.m_fadeDurationSamples = durationSamples;
		voice.m_fadeFromCurrentVolume = false;
		voice.m_fadeStartTime = 0;
	}
}

void KLBOpenSLNewEngine::setFadeParam(
	void* handle, float targetVolume, u32 msec
) {
	s32 duration = static_cast<s32>(msec);
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}
		KLBOpenSLNewSoundAsset* voiceAsset = voice.m_asset;
		if (voiceAsset != handle) {
			continue;
		}

		if (duration > 0) {
			voice.m_fadeElapsedSamples = 0;
			voice.m_fadeTargetVolume = targetVolume;
			voice.m_fadeCommand = 5;
			voice.m_fadeStep = 1;
			voice.m_fadeStartVolume = 1.0f;
			u32 durationSamples = 0;
			if (voiceAsset) {
				durationSamples =
					voiceAsset->m_playback.sampleRate * duration / 1000;
			}
			voice.m_fadeDurationSamples = durationSamples;
		} else {
			voice.m_fadeCommand = 0;
			voice.m_fadeTargetVolume = 0.0f;
			voice.m_fadeElapsedSamples = 0;
			voice.m_fadeCurrentVolume = 1.0f;
			voice.m_fadeDurationSamples = 0;
		}
		voice.m_fadeFromCurrentVolume = false;
		voice.m_fadeStartTime = 0;
	}
}

void KLBOpenSLNewEngine::pauseAudio(
	void* handle, bool, float targetVolume, u32 msec
) {
	s32 duration = static_cast<s32>(msec);
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState != 1) {
			continue;
		}
		KLBOpenSLNewSoundAsset* voiceAsset = voice.m_asset;
		if (voiceAsset != handle) {
			continue;
		}

		if (duration > 0) {
			voice.m_fadeElapsedSamples = 0;
			voice.m_fadeTargetVolume = targetVolume;
			voice.m_fadeCommand = 3;
			voice.m_fadeStep = 1;
			voice.m_fadeStartVolume = 0.0f;
			u32 durationSamples = 0;
			if (voiceAsset) {
				durationSamples =
					voiceAsset->m_playback.sampleRate * duration / 1000;
			}
			voice.m_fadeDurationSamples = durationSamples;
			voice.m_fadeFromCurrentVolume = false;
			voice.m_fadeStartTime = 0;
		} else if (voice.m_state != 0x12) {
			voice.m_state = 0x12;
			voice.m_callbackEnabled = true;
			voice.m_queuedBufferCount = 0;
			voice.m_pendingCommands |= 0x10;
			voice.m_outputVolume = static_cast<s16>(
				voice.m_assetVolume
				* voice.m_channelVolume
				* voice.m_masterVolume
				* voice.m_fadeCurrentVolume);
			voice.m_volumeDirty = false;
		}

		KLBOpenSLVoice::PlaybackClock* clock = voice.m_clock;
		s64 position = clock->decodedSampleCount;
		position /= voiceAsset->m_playback.sampleRate;
		position *= 1000;
		position >>= 14;
		clock->elapsedTime =
			static_cast<s64>(static_cast<float>(position) * 1000000.0f);
	}
}

void KLBOpenSLNewEngine::onActivityPause() {
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_state == 0x10) {
			voice.m_callbackEnabled = true;
			voice.m_queuedBufferCount = 0;
			voice.m_pendingCommands |= 0x10;
			voice.m_outputVolume = static_cast<s16>(
				voice.m_assetVolume
				* voice.m_channelVolume
				* voice.m_masterVolume
				* voice.m_fadeCurrentVolume);
			voice.m_volumeDirty = false;
		}
	}
	pauseOpenSLActivity(this);
}

void KLBOpenSLNewEngine::onActivityResume() {
	for (u32 i = 0; i < 32; ++i) {
		KLBOpenSLVoice& voice = m_voices[i];
		if (voice.m_lifecycleState == 1 && voice.m_state == 0x10) {
			voice.m_callbackEnabled = false;
			voice.m_queuedBufferCount = voice.m_bufferCapacity;
			voice.m_pendingCommands |= 0x20;
			voice.m_outputVolume = static_cast<s16>(
				voice.m_assetVolume
				* voice.m_channelVolume
				* voice.m_masterVolume
				* voice.m_fadeCurrentVolume);
			voice.m_volumeDirty = false;
		}
	}
	resumeOpenSLActivity(this);
}

//! Android provides the audio backend hook that Core declares in
//! KLBAudioSystem.h. Without this definition the engine does not link.
KLBAudioImplementation*
getNewAudioImplementation()
{
	return KLBOpenSLNewEngine::getInstance();
}
