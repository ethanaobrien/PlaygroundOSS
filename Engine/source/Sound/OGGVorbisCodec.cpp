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

#include "AudioCodec.h"
#include "CPFInterface.h"
#include "assert_klb.h"
#include "ivorbisfile.h"

#include <stdlib.h>
#include <string.h>

u32
IAudioCodec::getCodecCount() {
	return 1;
}

IAudioCodec*
IAudioCodec::createCodec(u32 index) {
	if (index == 0) {
		return KLBNEW(OGGVorbisCodec);
	}
	return NULL;
}

OGGVorbisCodec::~OGGVorbisCodec() {
}

bool
OGGVorbisCodec::initialize() {
	return true;
}

void
OGGVorbisCodec::shutdown() {
}

const char*
OGGVorbisCodec::getExtension() const {
	return ".ogg";
}

IAudioCodecInstance*
OGGVorbisCodec::createInstance() const {
	return KLBNEW(OGGVorbisCodecInstance);
}

OGGVorbisCodecInstance::OGGVorbisCodecInstance()
: m_file(NULL)
, m_position(0) {
}

OGGVorbisCodecInstance::~OGGVorbisCodecInstance() {
}

size_t
OGGVorbisCodecInstance::readCallback(void* buffer, size_t size, size_t count,
									 void* dataSource) {
	OGGVorbisCodecInstance* instance =
		static_cast<OGGVorbisCodecInstance*>(dataSource);
	const u32 byteCount = static_cast<u32>(size * count);
	const size_t bytesRead = fread(buffer, 1, byteCount,
								 instance->m_source->sourceFile);
	if (CPFInterface::getInstance().platform().useEncryption()) {
		instance->m_source->decryptBlck(buffer, static_cast<u32>(bytesRead));
	}
	instance->m_position += bytesRead;
	return bytesRead;
}

int
OGGVorbisCodecInstance::seekCallback(void* dataSource, s64 offset, int origin) {
	OGGVorbisCodecInstance* instance =
		static_cast<OGGVorbisCodecInstance*>(dataSource);
	s64 position = instance->m_position;
	if (origin == SEEK_SET) {
		position = static_cast<u32>(offset);
	} else if (origin == SEEK_CUR) {
		position += static_cast<u32>(offset);
	} else if (origin == SEEK_END) {
		position = instance->m_source->sourceSize;
	}
	instance->m_position = position;
	fseek(instance->m_source->sourceFile,
		  instance->m_position + instance->m_source->getHeaderSize(), SEEK_SET);
	instance->m_source->gotoOffset(static_cast<u32>(instance->m_position));
	return 0;
}

int
OGGVorbisCodecInstance::closeCallback(void* dataSource) {
	OGGVorbisCodecInstance* instance =
		static_cast<OGGVorbisCodecInstance*>(dataSource);
	if (instance->m_source->sourceFile) {
		fclose(instance->m_source->sourceFile);
		instance->m_source->sourceFile = NULL;
	}
	return 0;
}

long
OGGVorbisCodecInstance::tellCallback(void* dataSource) {
	OGGVorbisCodecInstance* instance =
		static_cast<OGGVorbisCodecInstance*>(dataSource);
	return static_cast<long>(instance->m_position);
}

bool
OGGVorbisCodecInstance::open(AudioCodecSource* source, s32 bufferingMode) {
	m_file = static_cast<OggVorbis_File*>(calloc(1, sizeof(OggVorbis_File)));
	if (!m_file) {
		return false;
	}

	m_source = source;
	ov_callbacks callbacks;
	callbacks.read_func  = readCallback;
	callbacks.seek_func  = seekCallback;
	callbacks.close_func = closeCallback;
	callbacks.tell_func  = tellCallback;
	klb_assertNull(ov_open_callbacks(this, m_file, NULL, 0, callbacks) == 0, "");

	vorbis_info* info = ov_info(m_file, -1);
	const ogg_int64_t frames = ov_pcm_total(m_file, -1);
	klb_assertNull(frames > 0, "");

	const u32 durationMs = static_cast<u32>((frames * 1000) / info->rate);
	AudioCodecSource* output = m_source;
	output->durationMs = durationMs;
	output->channelCount = static_cast<s8>(info->channels);
	const u32 totalSamples = static_cast<u32>(frames)
								 * static_cast<s32>(output->channelCount);
	output->pcmByteCount = totalSamples;
	output->pcmSampleCount = bufferingMode == 2 ? 0 : totalSamples;
	AudioCodecSource* finalOutput = m_source;
	finalOutput->interleavedStereo = finalOutput->channelCount == 2;
	finalOutput->sampleRate = static_cast<u32>(info->rate);
	return true;
}

void
OGGVorbisCodecInstance::close(AudioCodecSource*) {
	if (m_file) {
		ov_clear(m_file);
		free(m_file);
		m_file = NULL;
	}
}

bool
OGGVorbisCodecInstance::decode(s32 sampleCount, s64 sampleOffset, s16* output) {
	int bitstream = 0;
	u32 remainingBytes = sampleCount * static_cast<s32>(sizeof(s16));
	if (ov_pcm_seek(m_file, sampleOffset) != 0) {
		memset(output, 0, remainingBytes);
		return false;
	}

	char* cursor = reinterpret_cast<char*>(output);
	long bytesRead;
	do {
		bytesRead = ov_read(m_file, cursor,
						remainingBytes > 4096 ? 4096 : remainingBytes,
						&bitstream);
		cursor += bytesRead & ~1L;
		remainingBytes -= bytesRead;
	} while (bytesRead > 0 && remainingBytes != 0);

	if (remainingBytes != 0) {
		memset(cursor, 0, remainingBytes);
	}
	return true;
}
