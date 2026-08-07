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
//
// To change the template use AppCode | Preferences | File Templates.
//


#include "KLBOpenSLAudioPlayer.h"
#include "CAndroidRequest.h"

pthread_mutex_t s_callbackMutex = PTHREAD_MUTEX_INITIALIZER;

KLBOpenSLOldSoundHandle::KLBOpenSLOldSoundHandle() :
is_initiated(false),
is_loop_requested(false),
cur_mbel(-1)
{
}

KLBOpenSLOldSoundHandle::~KLBOpenSLOldSoundHandle()
{
	m_bActive = false;
	asset = NULL;
	destroyOpenSLObjects();
}

void KLBOpenSLOldSoundHandle::initInternalResources() {
	consumed_pos = 0;
	head_bufsize = 0;
	is_playing = false;
	is_initiated = false;
	m_bActive = true;
	position_offset = 0;
	last_position = 0;
	state = 0;
	volume = 1.0f;
	m_interruptionType = KLBOpenSLOldEngine::eINTERRUPTION_TYPE_NONE;
	fade_param.m_fadeCnt = 0;
	fade_param.m_startVol = 1.0f;
	fade_param.m_endVol = 1.0f;
	fade_param.m_fadeRatio = 1.0f;
	fade_param.m_fadeMiliSec = 0;
	fade_param.m_nowFadeInterType = INTER_TYPE_NONE;
	fade_param.m_nowFadeType = FADE_TYPE_NONE;
	fade_param.m_bfade = false;
	fade_param.m_prevmseq = 0;
	last_error = 0;
}

void KLBOpenSLOldSoundHandle::destroyOpenSLObjects() {
	if (bqPlayerObject != NULL)
	{
		if (bqPlayerPlay != NULL) {
			stop();
		}
		KLBOpenSLOldEngine::getInstance()->deferObjectDestruction(bqPlayerObject);
		bqPlayerObject = NULL;
		bqPlayerPlay = NULL;
		bqPlayerBufferQueue = NULL;
		bqPlayerVol = NULL;
	}
}

void KLBOpenSLOldSoundHandle::play(int _msec, float _tgtVol, float _startVol)
{
	SLresult result;
	time_elapsed = 0;
	consumed_pos = 0;
	result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
	fade_param.m_fadeRatio = (_startVol < 0.0f) ? (0.0f) : ((_startVol > 1.0f) ? 1.0f : _startVol);
	cur_mbel = 0;
	if( _msec > 0 ) {
		setFadeParam(FADE_TYPE_PLAY, _tgtVol, _msec, INTER_TYPE_LINEAR, _startVol);
	}
	refreshVolume();
	fillPcmBuffer(bqPlayerBufferQueue, true);
    state = IClientRequest::E_SOUND_STATE_PLAY;
	if (KLBOpenSLOldEngine::getInstance()->isSoundPaused()) {
		this->pause();
		this->setInterruptionType(KLBOpenSLOldEngine::eINTERRUPTION_TYPE_RESIGN_ACTIVE);
	}
}

void KLBOpenSLOldSoundHandle::stop(int _msec, float _tgtVol)
{
	SLresult result;
	if( _msec <= 0 ) {
		if (state == IClientRequest::E_SOUND_STATE_PLAY) {
			result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
		}
		if (bqPlayerBufferQueue != NULL) {
			result = (*bqPlayerBufferQueue)->Clear(bqPlayerBufferQueue);
		}
		time_elapsed = 0;
		consumed_pos = 0;
		state = IClientRequest::E_SOUND_STATE_STOP;
	} else {
   		setFadeParam(FADE_TYPE_STOP, _tgtVol, _msec, INTER_TYPE_LINEAR, 0.0f);
	}
}

void KLBOpenSLOldSoundHandle::pause(int _msec, float _tgtVol)
{
    if (state == IClientRequest::E_SOUND_STATE_PLAY) {
    	if( _msec <= 0 ) {
			SLresult result;
			result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PAUSED);
			time_elapsed += CAndroidRequest::getInstance()->nanotime() - time_started;
			state = IClientRequest::E_SOUND_STATE_PAUSE;
    	} else {
    		setFadeParam(FADE_TYPE_PAUSE, _tgtVol, _msec, INTER_TYPE_LINEAR, 0.0f);
    	}
	}
}

void KLBOpenSLOldSoundHandle::resume(int _msec, float _tgtVol)
{
    if (state == IClientRequest::E_SOUND_STATE_PAUSE) {
		SLresult result;
		result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
		state = IClientRequest::E_SOUND_STATE_PLAY;
		if( _msec > 0 ) {
			setFadeParam(FADE_TYPE_RESUME, _tgtVol, _msec, INTER_TYPE_LINEAR, 0.0f);
		}
		refreshVolume();
		updateTimeStarted();
	}
}

s32 KLBOpenSLOldSoundHandle::tellAudio()
{
	s32 elapsed_sysclock_ms = 0;
	if (isPlaying()) {
		elapsed_sysclock_ms = (s32)((CAndroidRequest::getInstance()->nanotime() - time_started + time_elapsed) / 1000000);
	}
	else {
		elapsed_sysclock_ms = (s32)((time_elapsed) / 1000000);
	}
	SLmillisecond msec;
	// DEBUG_PRINT("before GetPosition");
	(*bqPlayerPlay)->GetPosition(bqPlayerPlay, &msec);
	last_position = msec;
	// DEBUG_PRINT("after GetPosition");
	s32 ts_error = msec - elapsed_sysclock_ms;
	s32 ts_adj = (ts_error - last_error) >> 2;
	// DEBUG_PRINT("tellAudio ts_error=%d. diff delta=%d, adj=%d", ts_error, ts_error - last_error, ts_adj);
	last_error += ts_adj;
	return elapsed_sysclock_ms + position_offset + last_error;
}

void KLBOpenSLOldSoundHandle::seekAudio(s32 msec)
{
	if (!asset) {
		return;
	}

	const float originalVolume = volume;
	volume = 0.0f;
	refreshVolume();

	if (bqPlayerBufferQueue) {
		(*bqPlayerBufferQueue)->Clear(bqPlayerBufferQueue);
		is_loop_requested = true;
	}

	const s64 seekMillis = msec;
	const s64 sampleOffset = (seekMillis * pcm_sampling_rate) / 1000;
	pthread_mutex_lock(&s_callbackMutex);
	asset->readVorbisSamples(-1, sampleOffset);
	pthread_mutex_unlock(&s_callbackMutex);

	position_offset = msec - last_position;
	asset->fetchNextPcmBuffer(this);

	int formerSize = head_bufsize;
	int latterSize = 0;
	if (is_loop_requested && !asset->isSE()) {
		latterSize = formerSize;
		formerSize >>= 1;
		latterSize -= formerSize;
	}
	(*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, current_head, formerSize);
	if (latterSize) {
		(*bqPlayerBufferQueue)->Enqueue(
			bqPlayerBufferQueue,
			reinterpret_cast<u8*>(current_head) + formerSize,
			latterSize);
	}

	if (!asset->isFullyBuffered()) {
		KLBOpenSLOldEngine::getInstance()->registerAssetForRefilling(asset);
	}
	is_loop_requested = false;
	volume = originalVolume;
	refreshVolume();
}

void KLBOpenSLOldSoundHandle::closeAudio()
{

}

void KLBOpenSLOldSoundHandle::setVolume(float volume)
{
	KLBOpenSLOldSoundHandle::volume = volume;
	refreshVolume();
}

void KLBOpenSLOldSoundHandle::refreshVolume() {
	if (bqPlayerVol != NULL && asset != NULL) {
		float actual_volume = KLBOpenSLOldEngine::getInstance()->GetMasterVolume(this->asset->isSE()) * fade_param.m_fadeRatio * volume;
		SLmillibel max_mbel = 0;
		(*bqPlayerVol)->GetMaxVolumeLevel(bqPlayerVol, &max_mbel);
		SLmillibel mbel = gain_to_attenuation(actual_volume, max_mbel);
		if (mbel != cur_mbel) {
			(*bqPlayerVol)->SetVolumeLevel(bqPlayerVol, mbel);
			cur_mbel = mbel;
		}
	}
}

void KLBOpenSLOldSoundHandle::updateFadeParam() {
	s64 nowSeq = 0;

	if ((fade_param.m_bfade == false) && (fade_param.m_nowFadeType != FADE_TYPE_NONE))
	{
		fade_param.m_bfade = true;
		fade_param.m_prevmseq = tellAudio();
		// DEBUG_PRINT("[sound] fade start!");
	}
	if (fade_param.m_bfade)
	{
		nowSeq = tellAudio();

		// フェードカウントインクリメント
		fade_param.m_fadeCnt += (nowSeq - fade_param.m_prevmseq);
		if (fade_param.m_fadeCnt >= fade_param.m_fadeMiliSec) {
			fade_param.m_fadeCnt = fade_param.m_fadeMiliSec;
		} else if (fade_param.m_fadeCnt < 0) {
			fade_param.m_fadeCnt = 0;
		}

		// 係数計算
		if (fade_param.m_fadeCnt >= 0 && fade_param.m_fadeMiliSec > 0)
		{
			fade_param.m_fadeRatio = fade_param.m_startVol + ((fade_param.m_endVol - fade_param.m_startVol) * ((float)fade_param.m_fadeCnt / (float)fade_param.m_fadeMiliSec));
			if (fade_param.m_fadeRatio < 0.0f) {
				fade_param.m_fadeRatio = 0.0f;
			} else if (fade_param.m_fadeRatio > 1.0f) {
				fade_param.m_fadeRatio = 1.0f;
			}
		}

		// サウンドに大して設定
		refreshVolume();

		// フェード終了の確認
		if (fade_param.m_fadeCnt >= fade_param.m_fadeMiliSec)
		{
			fade_param.m_fadeCnt = fade_param.m_fadeMiliSec;
			fade_param.m_bfade = false;
			fade_param.m_fadeRatio = 1.0f;
			volume = fade_param.m_endVol;
			refreshVolume();

			if (fade_param.m_nowFadeType == FADE_TYPE_STOP)
			{
				if (state == IClientRequest::E_SOUND_STATE_PLAY) {
					(*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
				}
				if (bqPlayerBufferQueue != NULL) {
					(*bqPlayerBufferQueue)->Clear(bqPlayerBufferQueue);
				}
				time_elapsed = 0;
				consumed_pos = 0;
				state = IClientRequest::E_SOUND_STATE_STOP;
			}
			else if (fade_param.m_nowFadeType == FADE_TYPE_PAUSE)
			{
				if (state == IClientRequest::E_SOUND_STATE_PLAY) {
					(*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PAUSED);
					time_elapsed += CAndroidRequest::getInstance()->nanotime() - time_started;
					state = IClientRequest::E_SOUND_STATE_PAUSE;
				}
			}
			fade_param.m_nowFadeType = FADE_TYPE_NONE;
		}

		fade_param.m_prevmseq = tellAudio();
	}
}

bool KLBOpenSLOldSoundHandle::setFadeParam(s16 _fadeType, float _tgtVol, u32 _msec, s16 _interType, float _startVolume)
{
	if (_startVolume < 0.0f) {
		_startVolume = 0.0f;
	}
	else if (1.0f < _startVolume) {
		_startVolume = 1.0f;
	}
	if (_msec <= 0 || _fadeType >= FADE_TYPE_NUM || _fadeType <= FADE_TYPE_NONE) {
		// フェードしないため初期化
		fade_param.m_nowFadeType = INTER_TYPE_NONE;
		fade_param.m_nowFadeType = FADE_TYPE_NONE;
		fade_param.m_endVol = 0.0f;
		fade_param.m_fadeCnt = 0;
		fade_param.m_fadeRatio = 1.0f;
		fade_param.m_fadeMiliSec = 0;
		fade_param.m_bfade = false;
		fade_param.m_prevmseq = 0;
		return true;
	}

	fade_param.m_fadeCnt = 0;
	fade_param.m_endVol = _tgtVol;
	fade_param.m_nowFadeType = _fadeType;
	fade_param.m_nowFadeInterType = _interType;
	switch (_fadeType) {
		case FADE_TYPE_PLAY:
		case FADE_TYPE_RESUME:
			fade_param.m_startVol = _startVolume;
			fade_param.m_fadeRatio = _startVolume;
			break;

		case FADE_TYPE_STOP:
		case FADE_TYPE_PAUSE:
		case FADE_TYPE_PLAYING:
			fade_param.m_startVol = _startVolume;
			break;
	}
	fade_param.m_fadeMiliSec = _msec;
	fade_param.m_bfade = false;
	fade_param.m_prevmseq = 0;
	return true;
}

void KLBOpenSLOldSoundHandle::setPan(float pan)
{

}

void KLBOpenSLOldSoundHandle::setSoundAsset(KLBOpenSLOldSoundAsset *asset) {
	// destroy current OpenSL related objects if nb-channel or sampling-rate is not equal to prior sample
	if (is_initiated) {
		if (asset->getChannels() == this->pcm_channels && asset->getPcmSamplingRate() == this->pcm_sampling_rate) {
			// shortcut. simply change asset reference and initialize sound handle internal data
			updateAsset(asset);
			initInternalResources();
			is_initiated = true;
			return;
		}
		else {
			destroyOpenSLObjects();
		}
	}

	initInternalResources();
	updateAsset(asset);
	SLEngineItf engine = KLBOpenSLOldEngine::getInstance()->engineEngine;
	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
	SLDataFormat_PCM format_pcm = {
			SL_DATAFORMAT_PCM, this->getPcmChannels(), this->getSlSamplingRate(),
			this->getSlSampleFormat(), this->getSlSampleFormat(),
			this->getSlChannelMask(), SL_BYTEORDER_LITTLEENDIAN};
	SLDataSource audioSrc = {&loc_bufq, &format_pcm};

	// configure audio sink
	SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, KLBOpenSLOldEngine::getInstance()->outputMixObject};
	SLDataSink audioSnk = {&loc_outmix, NULL};

	// create audio player
	const SLInterfaceID ids[2] = {SL_IID_VOLUME, SL_IID_BUFFERQUEUE};
	const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
	(*engine)->CreateAudioPlayer(engine, &bqPlayerObject, &audioSrc, &audioSnk, 2, ids, req);

	// realize the player
	(*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);

	// get the play interface
	(*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);

	// get the buffer queue interface
	(*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
			&bqPlayerBufferQueue);

	// register callback on the buffer queue
	(*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, KLBOpenSLOldSoundHandle::bqPlayerCallback, this);

	// get the volume interface
	(*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVol);

	is_initiated = true;
}

void KLBOpenSLOldSoundHandle::bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
	KLBOpenSLOldSoundHandle* soundHandle = (KLBOpenSLOldSoundHandle*)context;
	pthread_mutex_lock(&s_callbackMutex);
	if (KLBOpenSLOldEngine::getInstance()->containsSoundHandle(soundHandle)) {
		soundHandle->fillPcmBuffer(bq, false);
	}
	pthread_mutex_unlock(&s_callbackMutex);
}

void KLBOpenSLOldSoundHandle::fillPcmBuffer(SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue, bool is_starting) {
	if (!m_bActive) {
		return;
	}
	if (!asset) {
		return;
	}
	asset->fetchNextPcmBuffer(this);
	if (head_bufsize == 0)
	{
		// set the player's state to playing
		if (!asset->isSE())
		{
			if (!is_loop_requested) {
				is_loop_requested = true;
				return;
			}
			else {
				asset->resetBuffer();
				asset->readVorbisSamples(-1);
				consumed_pos = 0;
				asset->fetchNextPcmBuffer(this);
			}
		}
		else if (repeatMode == KLBOpenSLOldSoundAsset::ONCE || true)
		{
			this->stop();
			return;
		}
		else
		{
			consumed_pos = 0;
			return;
		}
	}
	// DEBUG_PRINT("AUDIO; enqueue PCM buffer (addr=%d, size=%d)", (int)current_head, head_bufsize);
	// enqueue another buffer
	int bufsize_former = head_bufsize, bufsize_latter = 0;
	if ((is_starting || is_loop_requested) && !asset->isSE()) {
		// separate bufqueue
		bufsize_former >>= 1;
		bufsize_latter = head_bufsize - bufsize_former;
	}
	(*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, current_head, bufsize_former);
	if (bufsize_latter != 0) {
		(*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, (short*)((u8*)current_head + bufsize_former), bufsize_latter);
	}
    if (is_starting) {
    	updateTimeStarted();
    }

	if (!asset->isFullyBuffered())
	{
		// DEBUG_PRINT("AUDIO; not a full buffer");
		KLBOpenSLOldEngine::getInstance()->registerAssetForRefilling(asset);
	}
	if (is_loop_requested) {
		is_loop_requested = false;
	}
	return;
}
