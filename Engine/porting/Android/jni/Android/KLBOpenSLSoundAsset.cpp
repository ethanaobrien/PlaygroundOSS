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
#include "CAndroidPathConv.h"
#include "KLBOpenSLAudioPlayer.h"
#include "ivorbisfile.h"
#include "KLBPlatformMetrics.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
size_t KLBOpenSLOldSoundAsset::read_func(void *ptr, size_t size, size_t nmemb, void *datasource) {
	KLBOpenSLOldSoundAsset* me = (KLBOpenSLOldSoundAsset*)datasource;

	// http://xiph.org/vorbis/doc/vorbisfile/callbacks.html
	// > short reads mean nothing special (short reads are not treated as error conditions)
	// So, we actually don't need to perform memcpy(). Just rewrite pointer reference and
	// return length.
	unsigned int requested_size = (unsigned int)nmemb * (unsigned int)size;
	int bytes_read = 0;
	bytes_read = fread(ptr, 1, requested_size, me->fp);
	me->decryptor.decryptBlck(ptr, bytes_read);
	me->src_location += bytes_read;
	// DEBUG_PRINT("AUDIO; read_func(requested=%d, actually read=%d, loc=%d)", requested_size, bytes_read, me->src_location);
	return bytes_read;
}

int KLBOpenSLOldSoundAsset::seek_func(void *datasource, ogg_int64_t offset, int whence) {
	KLBOpenSLOldSoundAsset* me = (KLBOpenSLOldSoundAsset*)datasource;

	unsigned int new_pos = me->src_location;
	// DEBUG_PRINT("new_pos initiated as %d", new_pos);
	// TODO: proper seeking(could contain synchronous blocks reading)
	if (whence == SEEK_SET)
		new_pos = (unsigned int)offset;
	else if (whence == SEEK_CUR)
		new_pos += (unsigned int)offset;
	else if (whence == SEEK_END)
		new_pos = me->getSrcFileSize();
	// DEBUG_PRINT("AUDIO; seek_func(current=%d, offset=%d, whence=%d, new=%d)", me->src_location, (unsigned int)offset, whence, new_pos);
	me->src_location = new_pos;
	
	fseek(me->fp, me->src_location + me->decryptor.getHeaderSize(), SEEK_SET);
	me->decryptor.gotoOffset(me->src_location);
	// TODO: update PCM buffer offset(pcm_total_read_pos)

	return 0;
}

int KLBOpenSLOldSoundAsset::close_func(void *datasource) {
	KLBOpenSLOldSoundAsset* me = (KLBOpenSLOldSoundAsset*)datasource;
	// DEBUG_PRINT("AUDIO; close_func()");
	if (me->fp != NULL) {
		fclose(me->fp);
		me->fp = NULL;
	}
	return 0;
}

long KLBOpenSLOldSoundAsset::tell_func(void *datasource) {
	KLBOpenSLOldSoundAsset* me = (KLBOpenSLOldSoundAsset*)datasource;
	// DEBUG_PRINT("AUDIO; tell_func(): pos=%d", (unsigned int)me->src_location);

	return (long)me->src_location;
}

KLBOpenSLOldSoundAsset::KLBOpenSLOldSoundAsset(const char * path, bool is_se) :
pcm_samples(NULL),
fp(NULL),
src_location(0),
src_buf_flags(0x00),
pcm_buffer_read_pos(0),
pcm_buffer_consumed_pos(0),
pcm_total_read_pos(0),
decryptor(0x0e)
{
	KLBOpenSLOldSoundAsset::is_se = is_se;
	src_path = path;
    src_full_path = CKLBPathConv::getInstance().fullpath(path, ".ogg");
	// DEBUG_PRINT("path=%s, fullpath=%s", path, src_full_path);
	fp = fopen(src_full_path, "rb");

	fseek(fp, 0, SEEK_END);
	fgetpos(fp, (fpos_t*)&src_file_size);
	fseek(fp, 0, SEEK_SET);

	u8 hdr[4];
	hdr[0] = 0;
	hdr[1] = 0;
	hdr[2] = 0;
	hdr[3] = 0;
	fread(hdr, 1, 4, fp);

	if (is_se)
	{
		this->src_buf_flags |= 0x04;
	}

	if (CPFInterface::getInstance().platform().useEncryption()) {
		u32 headerSize = 0;
		decryptor.decryptSetup((const u8*)src_full_path, hdr, &headerSize);
		FILE* input = fp;
		if (input && headerSize >= 5) {
			u8 extendedHeader[128];
			u32 extendedHeaderSize = headerSize - 4;
			fread(extendedHeader, 1, extendedHeaderSize, input);
			decryptor.finishSetup(extendedHeader, src_full_path);
		}
		src_file_size -= headerSize;
		fseek(fp, headerSize, SEEK_SET);
	} else {
		fseek(fp, 0, SEEK_SET);
	}

	vf = (OggVorbis_File*)calloc(1, sizeof(OggVorbis_File));
    // DEBUG_PRINT("AUDIO; setting callbacks");
    ov_callbacks callbacks;
	callbacks.read_func = &KLBOpenSLOldSoundAsset::read_func;
	callbacks.seek_func = &KLBOpenSLOldSoundAsset::seek_func;
	callbacks.close_func = &KLBOpenSLOldSoundAsset::close_func;
	callbacks.tell_func = &KLBOpenSLOldSoundAsset::tell_func;
    // DEBUG_PRINT("AUDIO; opening ogg-vorbis stream");
    ov_open_callbacks(this, vf, NULL, 0, callbacks);

	vorbis_info* vi = ov_info(vf, -1);
	ogg_int64_t uiPCMSamples = ov_pcm_total(vf, -1);
    // DEBUG_PRINT("AUDIO; total PCM samples=%d", uiPCMSamples);
	if (uiPCMSamples == OV_EINVAL) {
		// TODO: proper error handling
		return;
	}
	playtime_ms = (int)((uiPCMSamples * 1000) / vi->rate);
	channels = vi->channels;
	pcm_total_samples = (unsigned int)uiPCMSamples * channels;
	pcm_buffer_size = isFullyBuffered() ? pcm_total_samples : min(pcm_total_samples, MAX_BUFFER_SAMPLES);
	pcm_samples = (short*)malloc(pcm_buffer_size * sizeof(s16));
	pcm_sampling_rate = vi->rate;
}

KLBOpenSLOldSoundAsset::~KLBOpenSLOldSoundAsset()
{
	src_buf_flags |= 0x04;
	// REPORT_METRICS("about to clear vf");
	if (vf != NULL) {
		this->closeVorbisFile();
	}
	// REPORT_METRICS("about to free pcm_samples");
	free(pcm_samples);
	// REPORT_METRICS("done");
	delete [] src_full_path;
}

void KLBOpenSLOldSoundAsset::closeVorbisFile()
{
	if (vf != NULL) {
		ov_clear(vf);
		free(vf);
		vf = NULL;
	}
	// close file for sure (actually, this should've been done in ov_clear())
	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
	}
}

void KLBOpenSLOldSoundAsset::stopAll()
{
	KLBOpenSLEngine::getInstance()->discardCorrespondingSoundHandles(this);
}

void KLBOpenSLOldSoundAsset::prepare(int frames)
{
	if (frames == -1) {
		is_se = true;
	}
	// DEBUG_PRINT("AUDIO; performing prepare(%d against %d)", frames, pcm_buffer_size);
	if (pcm_buffer_size < frames)
	{
		// TODO: proper error handling
		return;
	}
	readVorbisSamples(frames);
}

void KLBOpenSLOldSoundAsset::readVorbisSamplesImpl(int samplesToRead, ogg_int64_t sampleOffset) {
	bool close_vorbis_after_reading = false;
	if (!vf) {
		return;
	}
	if (pcm_buffer_size < samplesToRead)
	{
		// too large samples count specified
		// TODO: proper error handling
		return;
	}
	if (pcm_total_samples == pcm_total_read_pos) {
		return;
	}
	// DEBUG_PRINT("start pos=%d, total samples=%d, total read=%d, samplesToRead=%d, offset=%d", (int)pcm_samples, pcm_total_samples, pcm_total_read_pos, samplesToRead, pcm_buffer_read_pos);
	if (pcm_buffer_read_pos == pcm_buffer_size)
	{
		pcm_buffer_read_pos = 0;
	}
	if (pcm_buffer_consumed_pos == pcm_buffer_size)
	{
		pcm_buffer_consumed_pos = 0;
	}
	short* pcm_buf_read_start_pos = pcm_samples + pcm_buffer_read_pos;
	if (samplesToRead == -1)
	{
		if (isFullyBuffered()) {
			samplesToRead = pcm_buffer_size;
			close_vorbis_after_reading = true;
		}
		else {
			samplesToRead = min(DEFAULT_LOAD_SAMPLES, pcm_total_samples - pcm_total_read_pos);
		}
		// DEBUG_PRINT("auto refill. size=%d", samplesToRead);
	}
	else
	{
		int buffer_size_overflow = pcm_buffer_read_pos + samplesToRead - pcm_buffer_size;
		if (0 < buffer_size_overflow)
		{
			// this should normally never happen (or, adjust buffer size not to happen)
			unsigned int buf_read_first_phase = pcm_buffer_size - pcm_buffer_read_pos;
			readVorbisSamples(buf_read_first_phase);
			pcm_buf_read_start_pos += buf_read_first_phase;
			samplesToRead -= buf_read_first_phase;
		}
	}
	int currentSection = 0;
	s64 bytesRead = 0;
	int bufPos = 0;
	if (sampleOffset >= 0) {
		ov_pcm_seek(vf, sampleOffset);
	}
	// DEBUG_PRINT("performing actual Vorbis->PCM decode: addr=%d", (int)pcm_buf_read_start_pos);
	do
		{
			// DEBUG_PRINT("filling ptr: %d, snd buffer pos: %d", (int)(pcm_buf_read_start_pos + bufPos), bufPos);
			u16 bytesToRead = min(VORBIS_READ_BUFFER, (samplesToRead - bufPos) * 2);
			bytesRead = ov_read(vf, (char *)(pcm_buf_read_start_pos + bufPos), bytesToRead, &currentSection);
			bufPos += (int)bytesRead >> 1; /* divide by 2 for buffer=short */
		} while (0 < bytesRead && bufPos < samplesToRead);
	pcm_buffer_read_pos += samplesToRead;
	u32 continuedReadPos = pcm_total_read_pos + samplesToRead;
	s32 seekedReadPos = (s32)(sampleOffset + samplesToRead);
	pcm_total_read_pos = (sampleOffset < 0) ? continuedReadPos : seekedReadPos;
	if (close_vorbis_after_reading)
	{
		closeVorbisFile();
	}
	// DEBUG_PRINT("snd buffer filled with %d samples(pcm_buffer_read_pos=%d against pcm_buffer_size=%d)", bufPos, pcm_buffer_read_pos, pcm_buffer_size);
}

void KLBOpenSLOldSoundAsset::fetchNextPcmBuffer(KLBOpenSLOldSoundHandle* soundHandle) {
	// DEBUG_PRINT("AUDIO; fetching next pcm buf. bits=%d", soundHandle->getPcmDepth() * 8);
	if (!isFullyBuffered()) {
		// DEBUG_PRINT("AUDIO; pcm_buffer_read_pos=%d, pcm_buffer_consumed_pos=%d, pcm_total_samples=%d, pcm_total_read_pos=%d, soundHandle->consumed_pos=%d, pcm_buffer_size=%d", pcm_buffer_read_pos, pcm_buffer_consumed_pos, pcm_total_samples, pcm_total_read_pos, soundHandle->consumed_pos, pcm_buffer_size);
		if (pcm_total_read_pos == pcm_total_samples && pcm_buffer_read_pos == pcm_buffer_consumed_pos) {
			// buffer end reached
			soundHandle->head_bufsize = 0;
			return;
		}
		if (soundHandle->consumed_pos == pcm_buffer_size) {
			soundHandle->consumed_pos = 0;
		}
	}
	soundHandle->current_head = (short*)(pcm_samples + soundHandle->consumed_pos);
	soundHandle->head_bufsize = isFullyBuffered() ? DEFAULT_LOAD_SAMPLES : min(DEFAULT_LOAD_SAMPLES, pcm_buffer_read_pos - pcm_buffer_consumed_pos);
	// DEBUG_PRINT("AUDIO; consumed_pos=%d, head_bufsize=%d", soundHandle->consumed_pos, soundHandle->head_bufsize);
	unsigned int new_consumed_pos = soundHandle->consumed_pos + soundHandle->head_bufsize;
	if (pcm_buffer_size < new_consumed_pos)
	{
		soundHandle->head_bufsize -= new_consumed_pos - pcm_buffer_size;
		new_consumed_pos = pcm_buffer_size;
	}
	soundHandle->consumed_pos = new_consumed_pos;
	if (pcm_buffer_consumed_pos < new_consumed_pos)
	{
		pcm_buffer_consumed_pos = new_consumed_pos;
	}
	soundHandle->head_bufsize *= soundHandle->getPcmDepth();
	return;
}

void KLBOpenSLOldSoundAsset::close()
{

}

KLBOpenSLOldSoundHandle* KLBOpenSLOldSoundAsset::play(KLBOpenSLOldSoundAsset::REPEAT_MODE mode, s32 _milisec, float _tgtVol, float _startVol)
{
	if (!isFullyBuffered() && mode == KLBOpenSLOldSoundAsset::INFINITE_LOOP)
	{
		// TODO: implement looping for BGMs
	}
	KLBOpenSLOldSoundHandle* soundHandle = KLBOpenSLEngine::getInstance()->assignSoundHandle(this);
	soundHandle->setVolume(_tgtVol);
	soundHandle->play(_milisec, _tgtVol, _startVol);
	return soundHandle;
}

