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

#ifndef AudioCodec_h
#define AudioCodec_h

#include "BaseType.h"
#include "encryptFile.h"

#include <stdio.h>

struct OggVorbis_File;

/**
 * File/decryption state shared by a sound asset and its streaming decoder.
 */
class AudioCodecSource : public CDecryptBaseClass {
public:
	AudioCodecSource();

	FILE*	sourceFile;
	s64		sourceSize;
	s16*		pcmSamples;
	u32		pcmByteCount;
	u32		sampleRate;
	u32		pcmSampleCount;
	s32		durationMs;
	s8		channelCount;
	bool		interleavedStereo;
	u32		decodeSampleOffset;
	u32		pcmReadPosition;
	u32		playbackSampleRate;
};

class IAudioCodecInstance {
public:
	virtual ~IAudioCodecInstance();

	virtual bool open(AudioCodecSource* source, s32 bufferingMode) = 0;
	virtual void close(AudioCodecSource* source) = 0;
	virtual bool decode(s32 sampleCount, s64 sampleOffset, s16* output) = 0;
};

class IAudioCodec {
public:
	static u32 getCodecCount();
	static IAudioCodec* createCodec(u32 index);

	virtual ~IAudioCodec();

	virtual bool initialize() = 0;
	virtual void shutdown() = 0;
	virtual IAudioCodecInstance* createInstance() const = 0;
	virtual const char* getExtension() const = 0;
};

class OGGVorbisCodecInstance : public IAudioCodecInstance {
public:
	OGGVorbisCodecInstance();
	virtual ~OGGVorbisCodecInstance();

	virtual bool open(AudioCodecSource* source, s32 bufferingMode);
	virtual void close(AudioCodecSource* source);
	virtual bool decode(s32 sampleCount, s64 sampleOffset, s16* output);

private:
	static size_t readCallback(void* buffer, size_t size, size_t count,
						   void* dataSource);
	static int seekCallback(void* dataSource, s64 offset, int origin);
	static int closeCallback(void* dataSource);
	static long tellCallback(void* dataSource);

	OggVorbis_File*	m_file;
	size_t			m_position;
	AudioCodecSource*	m_source;
};

class OGGVorbisCodec : public IAudioCodec {
public:
	virtual ~OGGVorbisCodec();

	virtual bool initialize();
	virtual void shutdown();
	virtual IAudioCodecInstance* createInstance() const;
	virtual const char* getExtension() const;
};

#endif
