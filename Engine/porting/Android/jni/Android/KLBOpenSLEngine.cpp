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
#include <pthread.h>
#include <unistd.h>
#include "CAndroidRequest.h"
#include "KLBOpenSLAudioPlayer.h"
#include "KLBOpenSLNewEngine.h"
#include "ivorbisfile.h"

extern KLBAudioImplementation* g_audioImplementation;

SLObjectItf g_platformEngineObject;
SLEngineItf g_platformEngine;
SLObjectItf g_platformOutputMixObject;
SLObjectItf g_platformPlayerObject;
SLAndroidSimpleBufferQueueItf g_platformBufferQueue;
SLPlayItf g_platformPlay;
SLVolumeItf g_platformVolume;
KLBOpenSLNewEngine* g_platformAudioEngine;
u32 g_platformBufferIndex;
s16 g_platformBuffers[4][4800];
static const size_t PLATFORM_BUFFER_BYTES = 4800;

u32 KLBOpenSLNewEngine::getMixBufferSamples() const {
	return m_mixBufferSamples;
}

KLBOpenSLOldEngine::KLBOpenSLOldEngine() :
engineObject(NULL),
master_volume_bgm(1),
master_volume_se(1),
is_bgm_off(false),
is_se_off(false),
is_sound_paused(false),
m_multiProcessType(IClientRequest::E_SOUND_MULTIPROCESS_MUSIC_CUT)
{
    // DEBUG_PRINT("AUDIO; construct KLBOpenSLEngine");
	SLresult result;
    // DEBUG_PRINT("AUDIO; slCreateEngine");
	result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // DEBUG_PRINT("AUDIO; realizing engineObject");
	result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(SL_RESULT_SUCCESS == result);

    // DEBUG_PRINT("AUDIO; creating output mix");
	result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // DEBUG_PRINT("AUDIO; realizing output mix");
	result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

	s_callbackMutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    // DEBUG_PRINT("AUDIO; allocating memories for handlers");
	soundHandles = (KLBOpenSLOldSoundHandle**)calloc(KLBOpenSLOldEngine::MAX_SOUND_HANDLE, sizeof(KLBOpenSLOldSoundHandle*));
	soundAssets = (KLBOpenSLOldSoundAsset**)calloc(KLBOpenSLOldEngine::MAX_SOUND_ASSETS, sizeof(KLBOpenSLOldSoundAsset*));

    // DEBUG_PRINT("AUDIO; creating KLBOpenSLOldSoundAssetLoader");
	assetLoader = new KLBOpenSLOldSoundAssetLoader();
	assetLoader->startObservation();

	cs_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	asset_refill_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

	observingThread = CPFInterface::getInstance().platform().createThread(ThreadSoundEngineParam, this);
	memset(assets_waiting_refill, 0, sizeof(assets_waiting_refill));
}

KLBOpenSLOldEngine::~KLBOpenSLOldEngine()
{
	if (outputMixObject != NULL)
	{
		(*outputMixObject)->Destroy(outputMixObject);
	}
	if (engineObject != NULL)
	{
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle* soundHandle = *(soundHandles + i);
		if (soundHandle != NULL)
		{
			delete soundHandle;
			soundHandles[i] = NULL;
		}
	}
	free(soundHandles);
	free(soundAssets);
	delete assetLoader;
    if(observingThread != NULL)
    {
        CPFInterface::getInstance().platform().deleteThread(observingThread);
        observingThread = NULL;
    }
	pthread_mutex_destroy(&cs_mutex);
	pthread_mutex_destroy(&asset_refill_mutex);
	pthread_mutex_destroy(&s_callbackMutex);
}

KLBOpenSLOldEngine* KLBOpenSLOldEngine::getInstance()
{
	if (!g_audioImplementation) {
		g_audioImplementation = new KLBOpenSLOldEngine();
	}
	return static_cast<KLBOpenSLOldEngine*>(g_audioImplementation);
}

void KLBOpenSLOldEngine::beginAudioFrame(bool) {
}

void KLBOpenSLOldEngine::endAudioFrame() {
}

void KLBOpenSLOldEngine::terminate() {
	delete g_audioImplementation;
	g_audioImplementation = NULL;
}

bool KLBOpenSLOldEngine::init() {
	return true;
}

void KLBOpenSLOldEngine::shutdown() {
	terminate();
}

void KLBOpenSLOldEngine::seekAudio(void* opaqueHandle, s32 msec) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	pthread_mutex_lock(&cs_mutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		KLBOpenSLOldSoundHandle* handle = soundHandles[i];
		if (handle != NULL && handle->isInitiated()
		 && handle->getSoundAsset() == asset) {
			handle->seekAudio(msec);
		}
	}
	pthread_mutex_unlock(&cs_mutex);
}

KLBAudioCommand* KLBOpenSLOldEngine::popAudioCommand() {
	return NULL;
}

void KLBOpenSLOldEngine::setAudioMultiProcessType(s32 processType) {
	m_multiProcessType = processType;
}

void KLBOpenSLOldEngine::setFormAudioVolume(void* handle, float volume, bool lockHeld) {
	setAudioVolume(handle, volume, lockHeld);
}

void KLBOpenSLOldEngine::setAudioLoop(void*, s32, s32) {
}

bool KLBOpenSLOldEngine::preLoad(void* handle) {
	if (!handle) {
		return false;
	}
	static_cast<KLBOpenSLOldSoundAsset*>(handle)->prepare(-1);
	return true;
}

bool KLBOpenSLOldEngine::playAudio(void* handle, s32 msec, float targetVolume, float startVolume) {
	if (handle) {
		static_cast<KLBOpenSLOldSoundAsset*>(handle)->play(
			KLBOpenSLOldSoundAsset::ONCE, msec, targetVolume, startVolume);
		return true;
	}
	return false;
}

s32 KLBOpenSLOldEngine::totalTimeAudio(void* handle) {
	return handle ? static_cast<KLBOpenSLOldSoundAsset*>(handle)->totalPlayTime() : 0;
}

void KLBOpenSLOldEngine::onHeadsetActive() {
}

bool KLBOpenSLOldEngine::containsSoundHandle(KLBOpenSLOldSoundHandle *soundHandle)
{
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		if (soundHandles[i] == soundHandle) {
			return true;
		}
	}
	return false;
}

void KLBOpenSLOldEngine::registerAssetForRefilling(KLBOpenSLOldSoundAsset* asset) {
	// DEBUG_PRINT("AUDIO; registering asset for vorbis samples refilling.");
	pthread_mutex_lock(&asset_refill_mutex);
	for (u32 i = 0; i < sizeof(assets_waiting_refill) / sizeof(assets_waiting_refill[0]); ++i) {
		if (assets_waiting_refill[i] == NULL) {
			assets_waiting_refill[i] = asset;
			pthread_mutex_unlock(&asset_refill_mutex);
			return;
		}
	}
	pthread_mutex_unlock(&asset_refill_mutex);
	klb_assertNull(false, "BGM assets are far from supported.");
}

void KLBOpenSLOldEngine::updateFadeRatio() {
	pthread_mutex_lock(&cs_mutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->isPlaying()) {
			(*handle)->updateFadeParam();
		}
	}
	pthread_mutex_unlock(&cs_mutex);
}

void KLBOpenSLOldEngine::performRefillOnCurrentAsset() {
	pthread_mutex_lock(&asset_refill_mutex);
	for (u32 i = 0; i < sizeof(assets_waiting_refill) / sizeof(assets_waiting_refill[0]); ++i) {
		if (assets_waiting_refill[i] != NULL) {
			pthread_mutex_lock(&s_callbackMutex);
			assets_waiting_refill[i]->readVorbisSamples(-1);
			pthread_mutex_unlock(&s_callbackMutex);
			assets_waiting_refill[i] = NULL;
		}
	}
	pthread_mutex_unlock(&asset_refill_mutex);

	std::list<DeferredObject>::iterator object = deferredObjects.begin();
	while (object != deferredObjects.end()) {
		object->framesRemaining--;
		if (object->framesRemaining <= 0) {
			(*object->object)->Destroy(object->object);
			object = deferredObjects.erase(object);
		} else {
			++object;
		}
	}
}

void KLBOpenSLOldEngine::deferObjectDestruction(SLObjectItf object) {
	deferredObjects.push_back(DeferredObject(object));
}

s32 KLBOpenSLOldEngine::ThreadSoundEngineParam(void * hThread, void * data)
{
	KLBOpenSLOldEngine* engine = (KLBOpenSLOldEngine*)data;
    while(1)
    {
		engine->performRefillOnCurrentAsset();
		engine->updateFadeRatio();
        usleep(16000);
    }
    return 0; // 終了
}

void *KLBOpenSLOldEngine::loadAudio(char const *path, bool is_se, s32, s32)
{
	if (strlen(path) == 8 && !strncmp(path, "asset://", 8)) {
		return NULL;
	}
	// TODO: load beginning frames, then associate with assetLoader (if any)
	// search for vacant asset first
	KLBOpenSLOldSoundAsset* currentAsset = NULL;
	for (int i = 0; i < MAX_SOUND_ASSETS; ++i)
	{
		if (soundAssets[i] == NULL)
		{
			soundAssets[i] = currentAsset = assetLoader->openFile(path, is_se);
			break;
		}
	}
	if (currentAsset == NULL) {
		return NULL;
	}
	if (is_se)
	{
		currentAsset->prepare(-1);
	}
	else
	{
		currentAsset->prepare(KLBOpenSLOldSoundAsset::DEFAULT_LOAD_SAMPLES);
		if (!currentAsset->isFullyBuffered())
		{
			assetLoader->registerAsset(currentAsset);
		}
	}
	return currentAsset;
}

s32 KLBOpenSLOldEngine::tellAudio(void* handle) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(handle);
	pthread_mutex_lock(&cs_mutex);
	s32 ret_val = 0;
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			// DEBUG_PRINT("found tellAudio()");
			// return result of first one
			ret_val = (*handle)->tellAudio();
			break;
		}
	}
	pthread_mutex_unlock(&cs_mutex);
	return ret_val;
}

s32 KLBOpenSLOldEngine::getState(void* handle) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(handle);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			return (*handle)->getState();
		}
	}
	return 0;
}

void KLBOpenSLOldEngine::stopAudio(void* opaqueHandle, bool isLockGained, float _tgtVol, u32 _millisec) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	if (!isLockGained) {
		pthread_mutex_lock(&cs_mutex);
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			(*handle)->stop(_millisec, _tgtVol);
		}
	}
	if (!isLockGained) {
		pthread_mutex_unlock(&cs_mutex);
	}
}

void KLBOpenSLOldEngine::pauseAudio(void* opaqueHandle, bool isLockGained, float _tgtVol, u32 _millisec) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	if (!isLockGained) {
		pthread_mutex_lock(&cs_mutex);
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			(*handle)->pause(_millisec, _tgtVol);
		}
	}
	if (!isLockGained) {
		pthread_mutex_unlock(&cs_mutex);
	}
}

void KLBOpenSLOldEngine::resumeAudio(void* opaqueHandle, bool isLockGained, float _tgtVol, u32 _millisec) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	if (!isLockGained) {
		pthread_mutex_lock(&cs_mutex);
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			(*handle)->resume(_millisec, _tgtVol);
		}
	}
	if (!isLockGained) {
		pthread_mutex_unlock(&cs_mutex);
	}
}

void KLBOpenSLOldEngine::setMasterVolume(float volume, bool is_se) {
	if (is_se) {
		master_volume_se = volume;
	}
	else {
		master_volume_bgm = volume;
	}
}

void KLBOpenSLOldEngine::setFadeParam(void* opaqueHandle, float _tgtVol, u32 _millisec) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	pthread_mutex_lock(&cs_mutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			(*handle)->setFadeParam(KLBOpenSLOldSoundHandle::FADE_TYPE_PLAYING, _tgtVol, _millisec, KLBOpenSLOldSoundHandle::INTER_TYPE_LINEAR, (*handle)->getVolume());
		}
	}
	pthread_mutex_unlock(&cs_mutex);
}

void KLBOpenSLOldEngine::setAudioVolume(void* opaqueHandle, float volume, bool isLockGained) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(opaqueHandle);
	if (!isLockGained) {
		pthread_mutex_lock(&cs_mutex);
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->getSoundAsset() == asset)
		{
			(*handle)->setVolume(volume);
		}
	}
	if (!isLockGained) {
		pthread_mutex_unlock(&cs_mutex);
	}
}

void KLBOpenSLOldEngine::discardCorrespondingSoundHandles(KLBOpenSLOldSoundAsset *asset, bool isLockGained) {
	if (!isLockGained) {
		pthread_mutex_lock(&cs_mutex);
	}
	pthread_mutex_lock(&s_callbackMutex);
	// DEBUG_PRINT("AUDIO; discarding sound handle...(searching %d)", (int)asset);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		// DEBUG_PRINT("AUDIO; - candidate: %d", (int)soundHandles[i]);
		if (soundHandles[i] != NULL && soundHandles[i]->isInitiated() && soundHandles[i]->getSoundAsset() == asset)
		{
			delete soundHandles[i];
			soundHandles[i] = NULL;
		}
	}
	pthread_mutex_unlock(&s_callbackMutex);
	if (!isLockGained) {
		pthread_mutex_unlock(&cs_mutex);
	}
}

void KLBOpenSLOldEngine::onActivityPause() {
	is_sound_paused = true;
	pthread_mutex_lock(&cs_mutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		KLBOpenSLOldSoundHandle* handle = soundHandles[i];
		if (handle != NULL) {
			if (handle->isInitiated() && handle->isPlaying() && (handle->getInterruptionType() == eINTERRUPTION_TYPE_NONE)) {
				// サウンドを停止し割り込みフラグをたてる
				handle->pause();
				handle->setInterruptionType(eINTERRUPTION_TYPE_RESIGN_ACTIVE);
			}
		}
	}
	pthread_mutex_unlock(&cs_mutex);
}

void KLBOpenSLOldEngine::onActivityResume() {
	// DEBUG_PRINT("AUDIO; performing resume on all playing sounds");
	is_sound_paused = false;
	pthread_mutex_lock(&cs_mutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		KLBOpenSLOldSoundHandle* handle = soundHandles[i];
		if (handle != NULL) {
			if (handle->isInitiated() && !handle->isPlaying() && (handle->getInterruptionType() == eINTERRUPTION_TYPE_RESIGN_ACTIVE)) {
				// 割り込みフラグがたっている物に限り再生を再開
				handle->resume();
				handle->setInterruptionType(eINTERRUPTION_TYPE_NONE);
			}
		}
	}
	pthread_mutex_unlock(&cs_mutex);
}

void KLBOpenSLOldEngine::chackAudioMasterVolume() {
	switch (m_multiProcessType) {
		case IClientRequest::E_SOUND_MULTIPROCESS_MUSIC_CUT:    // ミュージックを無効
		    is_se_off  = false;
		    is_bgm_off = false;
		    break;
		case IClientRequest::E_SOUND_MULTIPROCESS_SOUND_CUT:    // ゲームサウンドを無効
		    is_se_off  = true;
		    is_bgm_off = false;
		    break;
		case IClientRequest::E_SOUND_MULTIPROCESS_SOUND_BGM_CUT: // ゲームのBGMサウンドのみ無効
		    is_se_off  = false;
		    is_bgm_off = true;
			/*
		    if(!isMusicPlayerPlaying()) {
		    	is_bgm_off = false;
		    }
			*/
		    break;
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		KLBOpenSLOldSoundHandle** handle = soundHandles + i;
		if (*handle != NULL && (*handle)->isInitiated() && (*handle)->isPlaying())
		{
			(*handle)->refreshVolume();
		}
	}
}

void KLBOpenSLOldEngine::releaseAudio(void* handle) {
	KLBOpenSLOldSoundAsset* asset = static_cast<KLBOpenSLOldSoundAsset*>(handle);
	// DEBUG_PRINT("unloading asset: %s", asset->getSrcFullPath());
	pthread_mutex_lock(&cs_mutex);

	// wait for refilling if in process
	pthread_mutex_lock(&asset_refill_mutex);
	pthread_mutex_lock(&s_callbackMutex);
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i) {
		KLBOpenSLOldSoundHandle* soundHandle = soundHandles[i];
		if (soundHandle != NULL && soundHandle->isInitiated()
		 && soundHandle->getSoundAsset() == asset) {
			delete soundHandle;
			soundHandles[i] = NULL;
		}
	}
	pthread_mutex_unlock(&s_callbackMutex);

	// remove from refilling list immediately
	for (u32 i = 0; i < sizeof(assets_waiting_refill) / sizeof(assets_waiting_refill[0]); ++i) {
		if (asset == assets_waiting_refill[i]) {
			assets_waiting_refill[i] = NULL;
		}
	}
	pthread_mutex_unlock(&asset_refill_mutex);

	assetLoader->unregisterAsset(asset);
	for (int i = 0; i < MAX_SOUND_ASSETS; ++i)
	{
		if (soundAssets[i] == asset)
		{
			soundAssets[i] = NULL;
			break;
		}
	}
	delete asset;
	pthread_mutex_unlock(&cs_mutex);
}

KLBOpenSLOldSoundHandle* KLBOpenSLOldEngine::assignSoundHandle(KLBOpenSLOldSoundAsset *pAsset) {
	int dequeue_candidate = -1;
	s64 dequeue_candidate_start_at = 9223372036854775807LL;
	pthread_mutex_lock(&cs_mutex);
	KLBOpenSLOldSoundHandle* handle = NULL;
	if (!pAsset->isSE()) {
		// try to reuse sound-handle for BGM
		for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
		{
			handle = soundHandles[i];
			if (handle != NULL && handle->isInitiated() && handle->getSoundAsset() == pAsset) {
				return handle;
			}
		}
	}
	for (int i = 0; i < MAX_SOUND_HANDLE; ++i)
	{
		handle = soundHandles[i];
		if (handle == NULL)
		{
			// DEBUG_PRINT("assigning new sound handle: %d", i);
			handle = new KLBOpenSLOldSoundHandle();
			soundHandles[i] = handle;
			dequeue_candidate = -1;
			break;
		}
		else if (handle->isInitiated() && handle->getSoundAsset()->isFullyBuffered() && !handle->isPlaying())
		{
			// DEBUG_PRINT("reusing handle pointer: %d", i);
			dequeue_candidate = -1;
			break;
		}
		else
		{
			// TODO: prioritize. BGM > single SE > multi SE
			if (handle->getSoundAsset()->isFullyBuffered() && handle->getTimeStarted() < dequeue_candidate_start_at) {
				dequeue_candidate_start_at = handle->getTimeStarted();
				dequeue_candidate = i;
				// DEBUG_PRINT("deque candidate updated: %d", dequeue_candidate);
			}
		}
	}
	if (dequeue_candidate != -1) {
		// DEBUG_PRINT("deque&reassign: %d", dequeue_candidate);
		handle = soundHandles[dequeue_candidate];
		handle->stop();
	}

	handle->setSoundAsset(pAsset);
	pthread_mutex_unlock(&cs_mutex);
	return handle;
}

void platformBufferQueueCallback(
	SLAndroidSimpleBufferQueueItf queue, void*)
{
	if (!g_platformAudioEngine || queue != g_platformBufferQueue) {
		return;
	}

	g_platformAudioEngine->beginPlatformCallback();
	g_platformAudioEngine->m_workerAcknowledged = true;
	if (g_platformAudioEngine->m_workerEnabled) {
		g_platformAudioEngine->prepareOutputBuffer();

		(*queue)->Enqueue(
			queue,
			g_platformBuffers[(g_platformBufferIndex + 3) & 3],
			g_platformAudioEngine->m_mixBufferSamples * 4);

		g_platformAudioEngine->renderAudio(
			g_platformBuffers[g_platformBufferIndex & 3],
			g_platformAudioEngine->m_mixBufferSamples);
		++g_platformBufferIndex;
	} else {
		g_platformAudioEngine->m_workerWaiting = true;
	}
	g_platformAudioEngine->m_workerAcknowledged = false;
}

bool initializeOpenSLPlatform(KLBOpenSLNewEngine* engine) {
	if (g_platformAudioEngine) {
		return false;
	}
	g_platformAudioEngine = KLBOpenSLNewEngine::getInstance();
	engine->m_mixBufferSamples =
		CAndroidRequest::getInstance()->getOptimalAudioSamples();

	slCreateEngine(
		&g_platformEngineObject, 0, NULL, 0, NULL, NULL);
	(*g_platformEngineObject)->Realize(
		g_platformEngineObject, SL_BOOLEAN_FALSE);
	(*g_platformEngineObject)->GetInterface(
		g_platformEngineObject, SL_IID_ENGINE, &g_platformEngine);
	(*g_platformEngine)->CreateOutputMix(
		g_platformEngine, &g_platformOutputMixObject, 0, NULL, NULL);
	(*g_platformOutputMixObject)->Realize(
		g_platformOutputMixObject, SL_BOOLEAN_FALSE);

	SLDataLocator_AndroidSimpleBufferQueue bufferQueueLocator = {
		SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
	};
	SLDataFormat_PCM pcmFormat = {
		SL_DATAFORMAT_PCM,
		2,
		SL_SAMPLINGRATE_44_1,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
		SL_BYTEORDER_LITTLEENDIAN
	};
	SLDataSource audioSource = {
		&bufferQueueLocator, &pcmFormat
	};

	SLDataLocator_OutputMix outputMixLocator = {
		SL_DATALOCATOR_OUTPUTMIX, g_platformOutputMixObject
	};
	SLDataSink audioSink = {
		&outputMixLocator, NULL
	};

	const SLInterfaceID interfaces[] = {
		SL_IID_VOLUME, SL_IID_BUFFERQUEUE
	};
	const SLboolean required[] = {
		SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE
	};
	(*g_platformEngine)->CreateAudioPlayer(
		g_platformEngine,
		&g_platformPlayerObject,
		&audioSource,
		&audioSink,
		2,
		interfaces,
		required);
	(*g_platformPlayerObject)->Realize(
		g_platformPlayerObject, SL_BOOLEAN_FALSE);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject, SL_IID_PLAY, &g_platformPlay);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject,
		SL_IID_BUFFERQUEUE,
		&g_platformBufferQueue);
	(*g_platformBufferQueue)->RegisterCallback(
		g_platformBufferQueue, platformBufferQueueCallback, engine);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject, SL_IID_VOLUME, &g_platformVolume);

	engine->m_sampleRate = 44100;
	engine->m_outputInterface = NULL;
	for (u32 i = 0; i < 4; ++i) {
		memset(
			g_platformBuffers[i],
			0,
			PLATFORM_BUFFER_BYTES);
	}
	for (u32 i = 0; i < 3; ++i) {
		engine->renderAudio(
			g_platformBuffers[i], engine->m_mixBufferSamples);
	}
	g_platformBufferIndex = 3;

	engine->prepareOutputBuffer();
	(*g_platformBufferQueue)->Enqueue(
		g_platformBufferQueue,
		g_platformBuffers[0],
		engine->m_mixBufferSamples);
	engine->prepareOutputBuffer();
	(*g_platformBufferQueue)->Enqueue(
		g_platformBufferQueue,
		g_platformBuffers[1],
		engine->m_mixBufferSamples);
	(*g_platformPlay)->SetPlayState(
		g_platformPlay, SL_PLAYSTATE_PLAYING);
	return true;
}

//! プラットフォーム側の待機。ミリ秒で指定された時間だけスレッドを止める。
void sleepOpenSLPlatform(KLBOpenSLNewEngine*, u32 msec) {
	usleep(msec * 1000);
}

s32 KLBOpenSLNewEngine::workerThread(void*, void* data) {
	KLBOpenSLNewEngine* engine =
		static_cast<KLBOpenSLNewEngine*>(data);
	engine->m_workerEnabled = true;
	while (engine->m_workerEnabled) {
		engine->processAudio();
		usleep(15000);
	}
	engine->m_workerStopped = true;
	return 0;
}

void pauseOpenSLActivity(KLBOpenSLNewEngine*) {
	(*g_platformPlay)->SetPlayState(g_platformPlay, SL_PLAYSTATE_PAUSED);
}

extern s64  s_lastAudioNanoTime;
extern bool s_resetAudioNanoTime;

//! The worker clock never runs backwards across a suspend.
static inline s64 audioNanoTime() {
	s64 now = CPFInterface::getInstance().platform().nanotime();
	if (now >= s_lastAudioNanoTime
	 || ((s_lastAudioNanoTime ^ now) < 0)
	 || s_resetAudioNanoTime) {
		s_resetAudioNanoTime = false;
		s_lastAudioNanoTime  = now;
	}
	return s_lastAudioNanoTime & 0x7fffffffffffffffLL;
}

//! Activity resume restarts the platform player and rebases the worker
//! clock, so the first frame after resume does not see the paused span.
void resumeOpenSLActivity(KLBOpenSLNewEngine* engine) {
	engine->m_lastWorkerTime = audioNanoTime();
	(*g_platformPlay)->SetPlayState(g_platformPlay, SL_PLAYSTATE_PLAYING);
}

void shutdownOpenSLPlatform(KLBOpenSLNewEngine*) {
	g_platformAudioEngine = NULL;

	if (g_platformPlay) {
		(*g_platformPlay)->SetPlayState(g_platformPlay, SL_PLAYSTATE_STOPPED);
		g_platformPlay = NULL;
	}
	if (g_platformBufferQueue) {
		(*g_platformBufferQueue)->Clear(g_platformBufferQueue);
		g_platformBufferQueue = NULL;
	}
	g_platformVolume = NULL;

	(*g_platformPlayerObject)->Destroy(g_platformPlayerObject);
	g_platformPlayerObject = NULL;

	if (g_platformOutputMixObject) {
		(*g_platformOutputMixObject)->Destroy(g_platformOutputMixObject);
	}
	if (g_platformEngineObject) {
		(*g_platformEngineObject)->Destroy(g_platformEngineObject);
		g_platformEngineObject = NULL;
		g_platformEngine = NULL;
	}
}

void KLBOpenSLNewEngine::onHeadsetActive() {
	if (g_platformPlay) {
		(*g_platformPlay)->SetPlayState(
			g_platformPlay, SL_PLAYSTATE_STOPPED);
		g_platformPlay = NULL;
	}
	if (g_platformBufferQueue) {
		(*g_platformBufferQueue)->Clear(g_platformBufferQueue);
		g_platformBufferQueue = NULL;
	}
	g_platformVolume = NULL;

	(*g_platformPlayerObject)->Destroy(g_platformPlayerObject);
	g_platformPlayerObject = NULL;

	SLDataLocator_AndroidSimpleBufferQueue bufferQueueLocator = {
		SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
	};
	SLDataFormat_PCM pcmFormat = {
		SL_DATAFORMAT_PCM,
		2,
		SL_SAMPLINGRATE_44_1,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
		SL_BYTEORDER_LITTLEENDIAN
	};
	SLDataSource audioSource = {
		&bufferQueueLocator, &pcmFormat
	};
	SLDataLocator_OutputMix outputMixLocator = {
		SL_DATALOCATOR_OUTPUTMIX, g_platformOutputMixObject
	};
	SLDataSink audioSink = {
		&outputMixLocator, NULL
	};

	const SLInterfaceID interfaces[] = {
		SL_IID_VOLUME, SL_IID_BUFFERQUEUE
	};
	const SLboolean required[] = {
		SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE
	};
	(*g_platformEngine)->CreateAudioPlayer(
		g_platformEngine,
		&g_platformPlayerObject,
		&audioSource,
		&audioSink,
		2,
		interfaces,
		required);
	(*g_platformPlayerObject)->Realize(
		g_platformPlayerObject, SL_BOOLEAN_FALSE);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject, SL_IID_PLAY, &g_platformPlay);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject,
		SL_IID_BUFFERQUEUE,
		&g_platformBufferQueue);
	(*g_platformBufferQueue)->RegisterCallback(
		g_platformBufferQueue, platformBufferQueueCallback, this);
	(*g_platformPlayerObject)->GetInterface(
		g_platformPlayerObject, SL_IID_VOLUME, &g_platformVolume);

	m_sampleRate = 44100;
	m_outputInterface = NULL;
	for (u32 i = 0; i < 4; ++i) {
		memset(g_platformBuffers[i], 0, PLATFORM_BUFFER_BYTES);
	}
	for (u32 i = 0; i < 3; ++i) {
		renderAudio(g_platformBuffers[i], m_mixBufferSamples);
	}
	g_platformBufferIndex = 3;

	prepareOutputBuffer();
	(*g_platformBufferQueue)->Enqueue(
		g_platformBufferQueue,
		g_platformBuffers[0],
		m_mixBufferSamples);
	prepareOutputBuffer();
	(*g_platformBufferQueue)->Enqueue(
		g_platformBufferQueue,
		g_platformBuffers[1],
		m_mixBufferSamples);
	(*g_platformPlay)->SetPlayState(
		g_platformPlay, SL_PLAYSTATE_PLAYING);
}

void prepareOpenSLPlayback(bool) {
}

bool isOpenSLPlaybackBlocked() {
	return false;
}
