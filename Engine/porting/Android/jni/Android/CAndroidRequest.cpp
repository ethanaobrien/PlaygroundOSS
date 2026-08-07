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
// The shipped Android object uses STLport's heterogeneous associative lookup.
#define _STLP_USE_CONTAINERS_EXTENSION

// #include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <list>
#include <map>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <jni.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <android/log.h>
#include "CAndroidPathConv.h"
#include "CAndroidRequest.h"
#include "CAndroidTmpFile.h"
#include "CAndroidWidget.h"
#include "CAndroidWriteFileStream.h"
#include "CJNI.h"
#include "KLBOpenSLAudioPlayer.h"
#include "KLBOpenSLNewEngine.h"
#include "PackageDefine.h"
#include "KLBPlatformMetrics.h"
#include "FontRendering.h"
#include "MultithreadedNetwork.h"
#include "CKLBNetAPIKeyChain.h"
#include "CKLBScriptEnv.h"
#include "CKLBCrypto.h"
#include "BacktraceExtension.h"
#include "CAndroidIFFont.h"
#include "CKLBMotionManager.h"
#include "NotificationManager.h"
#include "CKLBLocationManager.h"
#include "ImplementationMovie.h"
#include "KLBBase64.h"
#include "CAndroidIntegrityStrings.h"
#include "CKLBUtility.h"
#include "KLBPlatformExtension.h"

#include <vector>
// #include "klb_android_GameEngine_PFInterface.h"

// Android は stdarg.h をサポートしない(そのかわりincludeしてもエラーを出さない)ため、
// va_start/va_arg/va_end が定義されない。gcc のビルトインを定義してやる。
#if !defined(va_start)
#define va_start(ap, last) __builtin_va_start(ap, last)
#endif
#if !defined(va_end)
#define va_end(ap) __builtin_va_end(ap)
#endif
#if !defined(va_arg)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#endif
#if !defined(va_list)
typedef __builtin_va_list va_list;
#endif

CAndroidRequest * CAndroidRequest::ms_instance = 0;
static jclass s_pfInterfaceClass = NULL;
static char s_bundleVersion[64];
static char s_bundleId[32];
static const char s_publicKey[] =
	"-----BEGIN PUBLIC KEY-----\n"
	"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDBpUMUVjHWNI5q3ZRjF1vPnh+m\n"
	"aEGdbZkeosVvzLytBy9eYJ9qLYyFXxOY1LiggWyOLS+xEVMpV3A6frI3VewkVuCw\n"
	"na52ssCZcQSBA03Ykeb/cfHk5ChsDUP1vmAbloMb9f++Dow6Z4yubFWmBVMCHA6l\n"
	"fiUDPHjI8JqG56XJKQIDAQAB\n"
	"-----END PUBLIC KEY-----\n";

typedef std::pair<unsigned char*, jbyteArray> JNIByteArrayArgument;

void
findDeviceIntegrityFiles(const char* directory,
						 std::list<std::string>& fileNames,
						 std::vector<std::string>& matches)
{
	DIR* handle = opendir(directory);
	if(!handle) {
		return;
	}

	std::string entryName;
	dirent* entry;
	while((entry = readdir(handle)) != NULL) {
		entryName = entry->d_name;
		for(std::list<std::string>::iterator fileName = fileNames.begin();
			fileName != fileNames.end(); ++fileName)
		{
			if(entryName.find(*fileName) != std::string::npos) {
				std::stringstream fullPath("");
				fullPath << directory;
				fullPath << entryName;
				matches.push_back(fullPath.str());
			}
		}
	}
	closedir(handle);
}

typedef std::map<std::string, std::string> DevicePropertyMap;
typedef std::list<std::string> DevicePropertyNames;
typedef std::vector<std::string> DeviceIntegrityFiles;

#define DECODE_DEVICE_INTEGRITY_STRING(rounds, destination, encoded)		\
	do {																\
		const char* decodeSource = encoded;								\
		for (unsigned int decodeRound = 0;								\
			 decodeRound < rounds; ++decodeRound)						\
		{																\
			for (int decodeIndex = 0;									\
				 decodeIndex < static_cast<int>(sizeof(encoded));		\
				 ++decodeIndex) {										\
				const u8 decodeKey = static_cast<u8>(decodeIndex + 1);	\
				const u8 byte = static_cast<u8>(decodeSource[decodeIndex]); \
				destination[decodeIndex] = static_cast<char>(			\
					((byte - 1) ^ decodeKey) - 1);						\
			}															\
			decodeSource = destination;									\
		}																\
		destination[sizeof(encoded) - 1] = '\0';						\
	} while (0)

#define LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(						\
			rounds, destination, encoded)								\
	do {																\
		memcpy(destination, encoded, sizeof(encoded));					\
		const char* decodeSource = destination;							\
		for (unsigned int decodeRound = 0;							\
			 decodeRound < rounds; ++decodeRound)						\
		{															\
			for (int decodeIndex = 0;								\
				 decodeIndex < static_cast<int>(sizeof(encoded));		\
				 ++decodeIndex) {									\
				const u8 decodeKey = static_cast<u8>(decodeIndex + 1);	\
				const u8 byte = static_cast<u8>(decodeSource[decodeIndex]); \
				destination[decodeIndex] = static_cast<char>(			\
					((byte - 1) ^ decodeKey) - 1);					\
			}														\
			decodeSource = destination;								\
		}															\
		destination[sizeof(encoded) - 1] = '\0';					\
	} while (0)

#define INSERT_DEVICE_INTEGRITY_PROPERTY(								\
			result, key, method)										\
	do {																\
		callJavaMethod(NULL, result, method, 'S', "");					\
		jstring integrityJavaString =									\
			static_cast<jstring>(result.l);								\
		JNIEnv* integrityEnv = CJNI::getJNIEnv();						\
		const char* integrityValue = integrityEnv->GetStringUTFChars(	\
			integrityJavaString, NULL);									\
		properties.insert(std::make_pair(								\
			std::string(key), std::string(integrityValue)));				\
		CJNI::getJNIEnv()->ReleaseStringUTFChars(						\
			integrityJavaString, integrityValue);						\
	} while (0)

#define INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(						\
			result, destination, encodedKey, rounds, method)			\
	do {																\
		callJavaMethod(NULL, result, method, 'S', "");					\
		jstring integrityJavaString =									\
			static_cast<jstring>(result.l);								\
		JNIEnv* integrityEnv = CJNI::getJNIEnv();						\
		const char* integrityValue = integrityEnv->GetStringUTFChars(	\
			integrityJavaString, NULL);									\
		DECODE_DEVICE_INTEGRITY_STRING(									\
			rounds, destination, encodedKey);							\
		properties.insert(std::make_pair(								\
			std::string(destination), std::string(integrityValue)));		\
		CJNI::getJNIEnv()->ReleaseStringUTFChars(						\
			integrityJavaString, integrityValue);						\
	} while (0)

#define RELEASE_DEVICE_INTEGRITY_STRING(								\
			result, method)											\
	do {																\
		callJavaMethod(NULL, result, method, 'S', "");					\
		jstring integrityJavaString =									\
			static_cast<jstring>(result.l);								\
		JNIEnv* integrityEnv = CJNI::getJNIEnv();						\
		const char* integrityValue = integrityEnv->GetStringUTFChars(	\
			integrityJavaString, NULL);									\
		CJNI::getJNIEnv()->ReleaseStringUTFChars(						\
			integrityJavaString, integrityValue);						\
	} while (0)

static int
getDeviceIntegrityInteger(CAndroidRequest* request, const char* method)
{
	jvalue result;
	request->callJavaMethod(NULL, result, method, 'I', "");
	return result.i;
}

struct ScriptSourceRecord {
	ScriptSourceRecord(const char* sourceNameHash, const char* sourceName,
					   const char* contentHash);
	~ScriptSourceRecord();
	ScriptSourceRecord& operator=(const ScriptSourceRecord& other);

	std::string m_sourceName;
	std::string m_sourceNameHash;
	std::string m_contentHash;
	u16 m_sourceId;
};

typedef std::map<std::string, u16> ScriptSourceIdMap;
typedef std::map<std::string, ScriptSourceRecord*> ScriptSourceMap;
typedef std::vector<const char*> ScriptSourceNameList;

static ScriptSourceIdMap s_scriptSourceIds;
static ScriptSourceMap s_scriptSources;
static ScriptSourceNameList s_scriptSourceNames;
static ScriptSourceNameList s_scriptSourceCandidates;
static CAndroidIFFont s_fontSystem;
static const char* const s_knownScriptSourceHashes[] = {
#include "CAndroidScriptSourceHashes.inc"
};

void
readDeviceProperties(const char* path,
					 DevicePropertyMap& properties,
					 DevicePropertyNames& requestedNames,
					 const std::string& separator)
{
	std::ifstream input;
	std::string line;
	input.open(path);

	if(input.is_open()) {
		while(std::getline(input, line)) {
			if(requestedNames.empty()) break;

			for(DevicePropertyNames::iterator name = requestedNames.begin();
				name != requestedNames.end(); ++name) {
				const std::string::size_type namePosition = line.find(*name);
				if(namePosition == std::string::npos) continue;

				const int separatorPosition = line.find(separator);
				const int valuePosition =
					separatorPosition + static_cast<int>(separator.length());
				if(separatorPosition == -1 || valuePosition >= line.length()) {
					continue;
				}

				std::string value = line.substr(valuePosition);
				const std::string::iterator valueEnd = value.end();
				for(std::string::iterator character = value.begin();
					character != valueEnd; ++character) {
					const unsigned char byte = *character;
					if(byte < 'A' || byte > 'Z') {
						if(byte < '(' || byte > ':') {
							if(byte < 'a' || byte > 'z') {
								*character = ' ';
							}
						}
					}
				}

				properties.insert(
					std::pair<std::string, std::string>(*name, value));
				requestedNames.erase(name);
				break;
			}
		}
	}

	input.close();

	for(DevicePropertyNames::const_iterator name = requestedNames.begin();
		name != requestedNames.end(); ++name) {
		properties.insert(
			std::pair<std::string, std::string>(*name, "NOT_FOUND"));
	}
	requestedNames.clear();
}

CAndroidRequest::CAndroidRequest(const char* model, const char * brand, const char * board, const char * version, const char * tz)
: m_homePath(0), m_master_BGM(1.0f), m_master_SE(1.0f), m_regId(0), m_deviceIntegrityInfo(0), m_klabIdCallback(0), m_isSafeAreaScreen(false)
{
	KLBPlatformExtensionRegistry::getInstance()->initializeExtensions();
	KLBPlatformExtensionRegistry::getInstance()->invokeBeforePlatformInit();

	int len = strlen(model);
	len += strlen(brand);
	len += strlen(board);
	len += strlen(version);
	len += strlen(tz);
	len += 15;	// "Android;%s %s %s;%s"

	char * buf = new char [ len ];
	sprintf(buf, "Android;%s %s %s %s;%s", model, brand, board, version, tz);
	m_platform = (const char *)buf;
	ms_instance = this;

	const int sourceCount =
		sizeof(s_knownScriptSourceHashes) / sizeof(s_knownScriptSourceHashes[0]);
	for (int sourceId = 0; sourceId < sourceCount; ++sourceId) {
		s_scriptSourceIds.insert(
			std::pair<std::string, u16>(
				s_knownScriptSourceHashes[sourceId],
				static_cast<u16>(sourceId)));
	}

	KLBPlatformExtensionRegistry::getInstance()->invokeAfterPlatformInit();
}

CAndroidRequest::~CAndroidRequest() {
	delete [] m_homePath;
	delete [] m_platform;
	delete [] m_regId;
	delete [] m_deviceIntegrityInfo;

	s_scriptSourceNames.clear();
	s_scriptSourceCandidates.clear();
	for (ScriptSourceMap::iterator source = s_scriptSources.begin();
		 source != s_scriptSources.end(); ++source)
	{
		delete source->second;
	}
	s_scriptSources.clear();
}

void
CAndroidRequest::addExtMsg(const char* key, const char* value, bool sendImmediately)
{
	BacktraceExtension::getInstance().addExtMsg(key, value, sendImmediately);
}

void
CAndroidRequest::sendException(const char* message)
{
	BacktraceExtension::getInstance().sendException(message);
}

void
CAndroidRequest::leaveBreadcrumb(const char* message)
{
	BacktraceExtension::getInstance().leaveBreadcrumb(message);
}

char*
CAndroidRequest::createRequestIdHeader()
{
	enum {
		SOURCE_COUNT = 30,
		CONTENT_HASH_LENGTH = 40,
		SHA1_LENGTH = 20,
		HEADER_VERSION_OFFSET = 0,
		HEADER_TIMESTAMP_OFFSET = 1,
		HEADER_SIGNATURE_OFFSET = 9,
		HEADER_SOURCE_IDS_OFFSET = 29,
		HEADER_BINARY_LENGTH = 90,
		HEADER_ENCODE_ROUNDS_OFFSET = HEADER_BINARY_LENGTH - 1,
		HEADER_BUFFER_LENGTH = 256,
		HEADER_PREFIX_LENGTH = 13,
		HASH_TIMESTAMP_LENGTH = 16,
		HASH_INPUT_LENGTH =
			HASH_TIMESTAMP_LENGTH + SOURCE_COUNT * CONTENT_HASH_LENGTH
	};

	ScriptSourceRecord* selectedSources[SOURCE_COUNT] = {};
	int sourcesRemaining = SOURCE_COUNT;

	for (int index = 0;
		 sourcesRemaining;
		 ++index, --sourcesRemaining)
	{
		if (s_scriptSourceNames.empty()) {
			break;
		}
		const char* sourceName = s_scriptSourceNames.back();
		s_scriptSourceNames.pop_back();
		s_scriptSourceCandidates.push_back(sourceName);

		ScriptSourceMap::const_iterator source =
			s_scriptSources.find(sourceName);
		if (source != s_scriptSources.end()) {
			selectedSources[index] = source->second;
		}
	}

	const long remainingCount = sourcesRemaining;
	if (remainingCount > 0) {
		const int selectedCount = SOURCE_COUNT - sourcesRemaining;
		for (long index = 0;
			 index < remainingCount && !s_scriptSourceCandidates.empty();
			 ++index)
		{
			const int sourceIndex = rand() % s_scriptSourceCandidates.size();
			const char* sourceName = s_scriptSourceCandidates[sourceIndex];
			ScriptSourceMap::const_iterator source =
				s_scriptSources.find(sourceName);
			if (source != s_scriptSources.end()) {
				selectedSources[selectedCount + index] = source->second;
			}
		}
	}

	unsigned char* header = new unsigned char[HEADER_BUFFER_LENGTH]();
	header[HEADER_VERSION_OFFSET] = 2;

	const double timestamp = getUNIXTimeNow();
	memcpy(header + HEADER_TIMESTAMP_OFFSET, &timestamp, sizeof(timestamp));

	char* hashInput = new char[HASH_INPUT_LENGTH + 1]();
	const unsigned char* timestampBytes =
		reinterpret_cast<const unsigned char*>(&timestamp);
	for (unsigned int index = 0; index < sizeof(timestamp); ++index) {
		sprintf(hashInput + index * 2, "%02x", timestampBytes[index]);
	}

	for (int index = 0; index < SOURCE_COUNT; ++index) {
		const ScriptSourceRecord* source = selectedSources[index];
		memcpy(hashInput + HASH_TIMESTAMP_LENGTH +
				   index * CONTENT_HASH_LENGTH,
			   source->m_contentHash.c_str(), CONTENT_HASH_LENGTH);
		memcpy(header + HEADER_SOURCE_IDS_OFFSET +
				   index * sizeof(source->m_sourceId),
			   &source->m_sourceId, sizeof(source->m_sourceId));
	}

	cryptoSHA1(header + HEADER_SIGNATURE_OFFSET,
			   hashInput, HASH_INPUT_LENGTH, SHA1_LENGTH);

	const int randomValue = rand();
	const int encodeRounds = randomValue % 5 + 1;
	const unsigned char* encodeSource = header;
	if (encodeRounds > 0) {
		int round = 0;
		do {
			for (int index = 0; index < HEADER_BINARY_LENGTH; ++index) {
				const unsigned char value = encodeSource[index];
				hashInput[index] = static_cast<char>(
					((value + 1) ^ (index + 1)) + 1);
			}
			encodeSource = reinterpret_cast<unsigned char*>(hashInput);
			++round;
		} while (round != encodeRounds);
	}
	hashInput[HEADER_ENCODE_ROUNDS_OFFSET] =
		static_cast<char>(encodeRounds);
	hashInput[HEADER_BINARY_LENGTH] = '\0';

	u32 encodedLength = 180;
	char* encodedHeader = reinterpret_cast<char*>(header);
	memcpy(encodedHeader, "X-REQUEST-ID:", HEADER_PREFIX_LENGTH);
	KLBNetAPI_encodeBase64(
		hashInput, HEADER_BINARY_LENGTH,
		encodedHeader + HEADER_PREFIX_LENGTH, &encodedLength);

	delete [] hashInput;
	return encodedHeader;
}

void
CAndroidRequest::requestExtensionEvent(const char* eventName, ExtensionEventArgs* arguments)
{
	klb_assertNull(arguments, "wrong argument !");
	jvalue value;
	const char* firstArgument = arguments->front();
	arguments->pop_front();

	std::stringstream argumentStream("");
	ExtensionEventArgs::const_iterator iterator = arguments->begin();
	if (iterator != arguments->end()) {
		int remainingCount = arguments->size() - 1;
		for (int index = 0; index < remainingCount; ++index) {
			argumentStream << *iterator << ",";
			++iterator;
		}
		argumentStream << *iterator;
	}

	callJavaMethod(NULL, value, "onExtensionEventRequest", 'V', "SSS",
		eventName, firstArgument,
		arguments->empty() ? NULL : argumentStream.str().c_str());
}

void*
CAndroidRequest::getFontSystem()
{
	return &s_fontSystem;
}

void*
CAndroidIFFont::getFont(int size, u32 type)
{
	return new CAndroidFont(size, NULL, static_cast<s32>(type) > 3);
}

void
CAndroidIFFont::deleteFont(void* font)
{
	delete static_cast<CAndroidFont*>(font);
}

bool
CAndroidIFFont::renderText(const char* text, void* font, u32 color,
						   u16 width, u16 height, u8* buffer,
						   s16 stride, s16 baseX, s16 baseY, u32 pixelBytes,
						   float scaleX, float scaleY)
{
	CAndroidFont* androidFont = static_cast<CAndroidFont*>(font);
	if (androidFont) {
		jstring javaText = CJNI::getJNIEnv()->NewStringUTF(text);
		jclass platform = CJNI::getJNIEnv()->FindClass("klb/android/GameEngine/PFInterface");
		jmethodID drawText = CJNI::getJNIEnv()->GetStaticMethodID(
			platform,
			"drawText",
			"(Landroid/graphics/Paint;[IIIIFFLjava/lang/String;)V");

		jintArray image = CJNI::getJNIEnv()->NewIntArray(width * height);
		u32* pixels = reinterpret_cast<u32*>(
			CJNI::getJNIEnv()->GetIntArrayElements(image, NULL));
		int x;
		int y;
		for (y = 0; y < height; ++y) {
			for (x = 0; x < width; ++x) {
				pixels[x + y * width] =
					reinterpret_cast<const u32*>(buffer + stride * y)[x];
			}
		}
		CJNI::getJNIEnv()->ReleaseIntArrayElements(
			image, reinterpret_cast<jint*>(pixels), 0);

		color = (color & 0xff00ff00)
			  | ((color >> 16) & 0x000000ff)
			  | ((color << 16) & 0x00ff0000);
		CJNI::getJNIEnv()->CallStaticVoidMethod(
			platform, drawText, androidFont->getPaint(), image, color,
			width, height, static_cast<float>(baseX), static_cast<float>(baseY),
			javaText);

		pixels = reinterpret_cast<u32*>(
			CJNI::getJNIEnv()->GetIntArrayElements(image, NULL));
		for (y = 0; y < height; ++y) {
			for (x = 0; x < width; ++x) {
				reinterpret_cast<u32*>(buffer + stride * y)[x] =
					pixels[x + y * width];
			}
		}
		CJNI::getJNIEnv()->ReleaseIntArrayElements(
			image, reinterpret_cast<jint*>(pixels), 0);
		CJNI::getJNIEnv()->DeleteLocalRef(javaText);
		CJNI::getJNIEnv()->DeleteLocalRef(platform);
		CJNI::getJNIEnv()->DeleteLocalRef(image);
	}
	return true;
}

bool
CAndroidIFFont::getTextInfo(const char* text, void* font, STextInfo* info,
							float scaleX, float scaleY)
{
	CAndroidFont* androidFont = static_cast<CAndroidFont*>(font);
	if (androidFont) {
		float width = androidFont->stringWidth(text);
		info->ascent  = static_cast<int>(androidFont->ascent());
		info->descent = static_cast<int>(androidFont->descent());
		info->top     = static_cast<int>(androidFont->top());
		info->bottom  = static_cast<int>(androidFont->bottom());
		info->width   = static_cast<int>(width);
		info->height  = static_cast<int>(androidFont->top() - androidFont->bottom());
	}
	return true;
}

const char*
CAndroidRequest::getLangCodeRAW()
{
	jvalue value;
	callJavaMethod(NULL, value, "getLanguageCodeRAW", 'S', "");
	jstring string = static_cast<jstring>(value.l);
	const char* code = CJNI::getJNIEnv()->GetStringUTFChars(string, NULL);
	int length = strlen(code);
	memcpy(m_languageCode, code, length + 1);
	CJNI::getJNIEnv()->ReleaseStringUTFChars(string, code);
	return m_languageCode;
}

const char*
CAndroidRequest::getCountryCodeRAW()
{
	jvalue value;
	callJavaMethod(NULL, value, "getCountryCodeRAW", 'S', "");
	jstring string = static_cast<jstring>(value.l);
	const char* code = CJNI::getJNIEnv()->GetStringUTFChars(string, NULL);
	int length = strlen(code);
	memcpy(m_countryCode, code, length + 1);
	CJNI::getJNIEnv()->ReleaseStringUTFChars(string, code);
	return m_countryCode;
}

const char*
CAndroidRequest::getPreferredLangCodeRAW()
{
	jvalue value;
	callJavaMethod(NULL, value, "getPreferredLangCodeRAW", 'S', "");
	jstring string = static_cast<jstring>(value.l);
	const char* code = CJNI::getJNIEnv()->GetStringUTFChars(string, NULL);
	int length = strlen(code);
	memcpy(m_preferredLanguageCode, code, length + 1);
	CJNI::getJNIEnv()->ReleaseStringUTFChars(string, code);
	return m_preferredLanguageCode;
}

int
CAndroidRequest::getOptimalAudioHz()
{
	jvalue value;
	callJavaMethod(NULL, value, "getOptimalAudioHz", 'I', "");
	return value.i;
}

int
CAndroidRequest::getOptimalAudioSamples()
{
	jvalue value;
	callJavaMethod(NULL, value, "getOptimalAudioSamples", 'I', "");
	return value.i;
}

bool
CAndroidRequest::getGyroPolar(float* azimuth, float* elevation)
{
	IMotionManager* motionManager = IMotionManager::getInstance();
	if (azimuth) {
		*azimuth = motionManager->getAzimuth();
	}
	if (elevation) {
		*elevation = motionManager->getElevation();
	}
	return true;
}

CAndroidRequest *
CAndroidRequest::getInstance()
{
	return ms_instance;
}

void
CAndroidRequest::nativeSignal(int cmd, int param)
{
	enum {
		MOVIE_FINISHED = 1,
	};
	if (cmd == MOVIE_FINISHED) {
		CAndroidMovieWidget * pWidget = CAndroidMovieWidget::getWidget(param);
		if(pWidget) pWidget->m_status = 1;
	}
}

// klb_android_GameEngine_PFInterface.getVersionSDK() の名前は、デバイス
// 整合性チェックの文字列と同じ 5 段の変換で難読化されている。
static const char SDK_VERSION_METHOD[14] = "re\207\\lvtkrfpBZ";

//! OpenSL ES の実装は Android 8.0 (API 26) を境に切り替わる。
enum { ANDROID_SDK_OREO = 26 };

bool
CAndroidRequest::init()
{
	m_audio = NULL;

	char sdkVersionMethod[64];
	DECODE_DEVICE_INTEGRITY_STRING(5, sdkVersionMethod, SDK_VERSION_METHOD);

	jvalue sdkVersion;
	callJavaMethod(NULL, sdkVersion, sdkVersionMethod, 'I', "");

	if (sdkVersion.i >= ANDROID_SDK_OREO) {
		m_audio = KLBOpenSLNewEngine::getInstance();
	} else {
		m_audio = KLBOpenSLOldEngine::getInstance();
	}
	klb_assertNull(m_audio, "failed to get audio implement");
	return m_audio->init();
}

void
CAndroidRequest::validateEnvironment()
{
	const char* userId = CKLBNetAPIKeyChain::getInstance().getUserID();
	if (userId) {
		const size_t length = strlen(userId);
		if (length >= 2) {
			const char* suffixText = userId + length - 2;
			const int suffix = (suffixText[0] - '0') * 10
						   + suffixText[1] - '0';
			if ((suffix % 3) != 0) {
				return;
			}
		}
	}
	abort();
}

void
CAndroidRequest::detailedLogging(const char * basefile, const char * functionName, int lineNo, const char * format, ...)
{
}

void
CAndroidRequest::logging(const char * format, ...)
{
}

const char*
CAndroidRequest::getBundleVersion() {
    jvalue value;
    callJavaMethod(NULL, value, "getVersionName", 'S', "");
    jstring jstr = (jstring)value.l;
    const char * str = CJNI::getJNIEnv()->GetStringUTFChars(jstr, NULL);
    strncpy(s_bundleVersion, str, sizeof(s_bundleVersion));
    s_bundleVersion[sizeof(s_bundleVersion) - 1] = '\0';
    CJNI::getJNIEnv()->ReleaseStringUTFChars(jstr, str);
    return s_bundleVersion;
}

const char*
CAndroidRequest::getBundleId()
{
	if (!s_bundleId[0]) {
		jvalue value;
		callJavaMethod(NULL, value, "getPackageName", 'S', "");
		jstring string = static_cast<jstring>(value.l);
		const char* bundleId = CJNI::getJNIEnv()->GetStringUTFChars(string, NULL);
		strncpy(s_bundleId, bundleId, sizeof(s_bundleId));
		s_bundleId[sizeof(s_bundleId) - 1] = '\0';
		CJNI::getJNIEnv()->ReleaseStringUTFChars(string, bundleId);
	}
	return s_bundleId;
}

void
CAndroidRequest::beforeAssertFunction(const char* functionName, bool /*request*/)
{
	CKLBScriptEnv::getInstance().call_cbInt(functionName, 0);
}

ITmpFile *
CAndroidRequest::openTmpFile(const char * tmpPath)
{
	const char * target = "file://external/";
	int len = strlen(target);
	if(!strncmp(tmpPath, target, len)) {
		// 平成24年11月27日(火)
		// CiOSTmpFileのファイルパスの解決の仕方が'file://'を抜いた状態で解釈するため、
		// CiOSTmpFileへ渡すファイルパスのprefixを'file://'分進めて渡しています。
		tmpPath = tmpPath + 7;
		CAndroidTmpFile * pTmpFile = new CAndroidTmpFile(tmpPath);
		if(!pTmpFile->isReady()) {
			delete pTmpFile;
			pTmpFile = 0;
		}
		return pTmpFile;
	}
	return 0;
}

#include "FileDelete.h"

const char* getFullNativePath(const char* path) {
	CKLBPathConv& pathconv = CKLBPathConv::getInstance();
	return pathconv.fullpath(path + 7);
}

bool
CAndroidRequest::removeFileOrFolder(const char* filePath) {
	const char * target = "file://external/";
	int len = strlen(target);
	if(strncmp(filePath, target, len)) {
		return false;
	}

	const char* fullpath = CKLBPathConv::getInstance().fullpath(filePath + 7);
	jvalue result;
	callJavaMethod(NULL, result, "eraseFolder", 'Z', "S", fullpath);
	return result.z != 0;
}

void removeTmpFileNative(const char* filePath) {
	remove(filePath);
}

int
CAndroidRequest::removeTmpFile(const char *tmpPath)
{
	CKLBPathConv& pathconv = CKLBPathConv::getInstance();

	const char * target = "file://external/";
	int len = strlen(target);
	if(!strncmp(tmpPath, target, len)) {
		const char * fullpath = pathconv.fullpath(tmpPath + 7);
		int result = unlink(fullpath);
		delete [] fullpath;
		return result;
	}
	return 0;
}

u32
CAndroidRequest::getFreeSpaceExternalKB() {
	struct statfs diskInfo;
	const int ret = statfs(CKLBPathConv::getInstance().fullpath("external/"), &diskInfo);
	if (ret != 0) {
		// error fetching free space size. return "no more space left" for safety
		return 0;
	}
	// calculate free space in KB available to non-superusers
	unsigned long long freeKB = (diskInfo.f_bavail * diskInfo.f_bsize) >> 10;
	if (freeKB & ~0xFFFFFF) {
		return 0xFFFFFF;
	}
	return (u32)freeKB;
}

u32
CAndroidRequest::getPhysicalMemKB() {
	FILE* f = fopen("/proc/meminfo", "r");
	u32 outValue = 0; // Default if fail

	if (f) {
		int state = 0; 	// Beginning of parsing state machine.
		char title[64];	// Storage for descriptor text.
		int titleWrite = 0;
		u32 value = 0;

		while (!feof(f)) {
			char c = fgetc(f);
			
			switch (state) {
			case 0:	// Read Title
				if (c == ':') {
					state = 1;
					title[titleWrite++] = 0; // Close string
				} else if (c >' ') { // Protect from 0xA or 0xD if multiple
					title[titleWrite++] = c;
				}
				break;
			case 1:
				// Cross the sea of spaces after ':'
				// Compute the value for value 0..9
				// Detect the end of line
				if ((c >= '0') && (c <= '9')) {
					value = (value * 10) + (c - '0');
				} else if ((c == 0xA) || (c == 0xD)) {
					//
					// Here we check the value
					//
					if (strcmp("MemTotal", title) == 0) {
						outValue = value;
					}
					
					// Ready to read the next entry
					state = 0;
					value = 0;
					titleWrite = 0;
				}
			}
		}
		
		// State machine fails if no EOF in last line, check here.
		if (state == 1) {
			if (strcmp("MemTotal", title) == 0) {
				outValue = value;
			}
		}
		
		fclose(f);
	}
	return outValue;
}

void
CAndroidRequest::excludePathFromBackup(const char * fullpath)
{
	// TODO
}

bool CAndroidRequest::useEncryption() {
	return true;
}

s64
CAndroidRequest::nanotime()
{
#if 0
	jclass cls_pfif = CJNI::getJNIEnv()->FindClass(JNI_LOAD_PATH);
	jmethodID nanotime_id = CJNI::getJNIEnv()->GetStaticMethodID(cls_pfif, "nanotime", "()J");
//	jclass cls_pfif = CJNI::getJNIEnv()->FindClass("java/lang/System");
//	jmethodID nanotime_id = CJNI::getJNIEnv()->GetStaticMethodID(cls_pfif, "nanoTime", "()J");
	jlong nanotime = CJNI::getJNIEnv()->CallStaticLongMethod(cls_pfif, nanotime_id);
#endif
	struct timespec tspec;
	clock_gettime(CLOCK_MONOTONIC, &tspec);
	s64 nanotime = (s64)tspec.tv_sec * 1000000000LL + (s64)tspec.tv_nsec;
	return nanotime;
}

IReadStream *
CAndroidRequest::openReadStream(const char *pathname, bool decrypt, u32 mode)
{
	// DEBUG_PRINT("opening file: pathname=%s, decrypt=%d", pathname, decrypt);
    // ファイル名の scheme で、どのファイルを開くべきかが決まる。
    if(!strncmp(pathname, "file://", 7)) {
        // ファクトリには scheme を除いたパスが渡される。
        CAndroidReadFileStream * pRds = CAndroidReadFileStream::openStream(pathname + 7, 0, mode);
        if (decrypt) { pRds->decryptSetup((const u8*)pathname + 7); }
        return pRds;
    }
    if(!strncmp(pathname, "asset://", 8)) {
        CAndroidReadFileStream * pRds = CAndroidReadFileStream::openAssets(pathname, 0, mode);
        if (decrypt) { pRds->decryptSetup((const u8*)pathname + 8); }
        return pRds;
    }

    if(!strncmp(pathname, "socket://", 9)) {
        CSockReadStream * pRds = CSockReadStream::openStream(pathname + 9);
        logging("Socket: %s (%p)", pathname, pRds);
        return pRds;
    }

    if(!strncmp(pathname, "http:", 5)) {
        return 0;
    }
    if(!strncmp(pathname, "https:", 6)) {
        return 0;
    }
    return 0;
}

IReadStream*
CAndroidRequest::openWriteStream(const char* pathname, bool encrypt, u32 /*mode*/)
{
	// 書き込みストリームは file:// スキームのみを受け付ける。
	IReadStream* stream = 0;
	if (!strncmp(pathname, "file://", 7)) {
		// ファクトリには scheme を除いたパスが渡される。
		stream = reinterpret_cast<IReadStream*>(
			CAndroidWriteFileStream::openStream(pathname + 7, encrypt));
	}
	return stream;
}

void *
CAndroidRequest::loadAudio(const char * url, bool is_se)
{
	const char * path = (!strncmp(url, "asset://", 8)) ? url : (url + 7);
	DEBUG_PRINT("loading audio: %s", path);
	KLBOpenSLOldSoundAsset * audio = KLBOpenSLEngine::getInstance()->load(path, is_se);
	return audio;
}

bool
CAndroidRequest::preLoad(void * handle)
{
	if(!handle) return false;
	KLBOpenSLOldSoundAsset * audio = (KLBOpenSLOldSoundAsset *)handle;
	audio->prepare(-1);
	return true;
}

bool
CAndroidRequest::setBufSize(void *handle, int level)
{
    // TODO
    return true;
}

void
CAndroidRequest::playAudio(void *handle, s32 _milisec, float _tgtVol, float _startVol)
{
	if(!handle) return;
	KLBOpenSLOldSoundAsset * audio = (KLBOpenSLOldSoundAsset *)handle;
	audio->play(KLBOpenSLOldSoundAsset::ONCE, _milisec, _tgtVol, _startVol);
	// TODO:
}

void
CAndroidRequest::stopAudio(void *handle, s32 _milisec, float _tgtVol)
{
	if(!handle) return;
	//KLBOpenSLEngine::getInstance()->discardCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)handle);
	KLBOpenSLEngine::getInstance()->stopCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)handle, false, _tgtVol, _milisec);
}

void
CAndroidRequest::setMasterVolume(float volume, bool SEmode)
{
	DEBUG_PRINT("AUDIO; setting master volume. seMode=%d, vol=%f", SEmode, volume);
	KLBOpenSLEngine::getInstance()->setMasterVolume(volume, SEmode);
}

void
CAndroidRequest::setAudioVolume(void *handle, float volume)
{
    if(!handle) return;
	KLBOpenSLEngine::getInstance()->setVolumeOnCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)handle, volume);
}

void
CAndroidRequest::setAudioPan(void *handle, float pan)
{
	return;
	if(!handle) return;
	/*
	CAndroidAudio * audio = (CAndroidAudio *)handle;
	audio->setPan(pan);
	*/
}

void
CAndroidRequest::releaseAudio(void * handle)
{
	if(!handle) return;
	KLBOpenSLOldSoundAsset * audio = (KLBOpenSLOldSoundAsset *)handle;
	REPORT_METRICS("before closing audio asset");
	delete audio;
	REPORT_METRICS("after closing audio asset");
}

void
CAndroidRequest::pauseAudio(void * handle, s32 _milisec, float _tgtVol)
{
	if(!handle) return;
	KLBOpenSLEngine::getInstance()->pauseCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)handle, false, _tgtVol, _milisec);
}

void
CAndroidRequest::resumeAudio(void * handle, s32 _milisec, float _tgtVol)
{
	if(!handle) return;
	KLBOpenSLEngine::getInstance()->resumeCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)handle, false, _tgtVol, _milisec);
}

void
CAndroidRequest::seekAudio(void * handle, s32 millisec)
{
    // TODO
}

s32
CAndroidRequest::tellAudio(void * handle)
{
	if(!handle) return 0;
	return KLBOpenSLEngine::getInstance()->tellCorrespondingSoundHandle((KLBOpenSLOldSoundAsset *)handle);
}

s32
CAndroidRequest::totalTimeAudio(void * handle)
{
    if(!handle) return 0;
	KLBOpenSLOldSoundAsset * audio = (KLBOpenSLOldSoundAsset *)handle;
	return audio->totalPlayTime();
}

s32
CAndroidRequest::getState(void * handle)
{
	if(!handle) return 0;
	return KLBOpenSLEngine::getInstance()->getStateOfCorrespondingSoundHandle((KLBOpenSLOldSoundAsset *)handle);
}

//! サウンドとミュージックの並行処理タイプ設定
void
CAndroidRequest::setAudioMultiProcessType( s32 _processType )
{
	switch( _processType )
	{
		case IClientRequest::E_SOUND_MULTIPROCESS_MUSIC_CUT:
		case IClientRequest::E_SOUND_MULTIPROCESS_SOUND_CUT:
		case IClientRequest::E_SOUND_MULTIPROCESS_SOUND_BGM_CUT:
			KLBOpenSLEngine::getInstance()->setAudioMultiProcessType(_processType);
			break;
	}
}

//! サウンドの割り込み処理をエンジン側で制御するかどうか
void CAndroidRequest::setPauseOnInterruption(bool _bPauseOnInterruption)
{
    // TODO:2013/06/10 現在はiOSのみ対応が必要なのでAndroid側は特に対応なし
}

#define ANDROID_ALARM_ELAPSED_REALTIME (3)
inline void CAndroidRequest::getElapsedTimeSpec(struct timespec * ts)
{
	// ref: AOSP source code; frameworks/native/libs/utils/SystemClock.cpp
	int fd = open("/dev/alarm", O_RDONLY);
	if (fd == -1) {
		klb_assertAlways("failed to claim alarm counter.");
	}

	int result = ioctl(fd, _IOW('a', 4 | (ANDROID_ALARM_ELAPSED_REALTIME << 4), struct timespec), ts);

	close(fd);

	if (result == -1) {
		klb_assertAlways("failed to fetch alarm clock via ioctl");
	}
}

inline s64 CAndroidRequest::getElapsedNanoTime(void)
{
	struct timespec ts;
	getElapsedTimeSpec(&ts);
	return ((s64)ts.tv_sec * 1000000000) + ts.tv_nsec;
}

/*!
    @brief  経過時間を取得(sec)
    @param[in]  void
    @return     s64     経過時間(sec)
 */
s64 CAndroidRequest::getElapsedTime(void)
{
	struct timeval time;
	gettimeofday(&time, NULL);
	return (s64)time.tv_sec;
}

void
CAndroidRequest::setFadeParam(void* _handle, float _tgtVol, u32 _millisec)
{
	if(!_handle) return;
	KLBOpenSLEngine::getInstance()->setFadeParamOnCorrespondingSoundHandles((KLBOpenSLOldSoundAsset *)_handle, _tgtVol, _millisec);
}

//! フォントオブジェクト取得
void *
CAndroidRequest::getFont(int size, const char * fontName)
{
	return new CAndroidFont(size, fontName);
}

void *
CAndroidRequest::getFont(int size, const char * fontName, float* pAscent)
{
	FontObject* pFont = FontObject::createFont(fontName, size);
	if (pFont) {
		// pFont->
		if (pAscent) {
			*pAscent = pFont->getAscent();
		}
	}
	return pFont;
}

void *
CAndroidRequest::getFontSystem(int size, const char * fontName)
{
	CAndroidFont * pFont = new CAndroidFont(size, fontName);
	return (void *)pFont;
}

//! フォントオブジェクト破棄
void
CAndroidRequest::deleteFont(void * pFont)
{
	delete static_cast<CAndroidFont*>(pFont);
}

void
CAndroidRequest::deleteFontSystem(void * pFont)
{
}

extern "C" {

bool
CAndroidRequest::getTextInfo(const char* utf8String, void * pFont, STextInfo* pReturnInfo)
{
	FontObject* pF = (FontObject*)pFont;
	if (pF) {
		pF->getTextInfo(utf8String, pReturnInfo, 1.0f, 1.0f);
	} else {
		pReturnInfo->ascent		= 0.0f;
		pReturnInfo->descent	= 0.0f;
		pReturnInfo->bottom		= 0.0f;
		pReturnInfo->top		= 0.0f;
		pReturnInfo->width		= 0.0f;
		pReturnInfo->height		= 0.0f;
	}
	return true;
}

void*
CAndroidRequest::getGLExtension(const char*)
{
	return 0;
}

const char*
CAndroidRequest::getShaderExtension(int shaderType)
{
	if (shaderType == 0) {
		return "samplerExternalOES";
	}
	if (shaderType == 1) {
		return "#extension GL_OES_EGL_image_external : enable\n"
		       "#extension GL_OES_EGL_image_external : require";
	}
	return "";
}

bool
CAndroidRequest::isSafeAreaScreen()
{
	return m_isSafeAreaScreen;
}

void
CAndroidRequest::setSafeAreaScreen(bool isSafeArea)
{
	m_isSafeAreaScreen = isSafeArea;
}

void
CAndroidRequest::onKLabIdResult(int result, const char* keyValuePairs)
{
	if (m_klabIdCallback) {
		CKLBScriptEnv::getInstance().call_onKLabIdResult(
			m_klabIdCallback, result, keyValuePairs);
		delete [] m_klabIdCallback;
		m_klabIdCallback = 0;
	}
}

bool
CAndroidRequest::setFrameRate(int /* frameRate */)
{
	return false;
}

int
CAndroidRequest::getMaxFrameRate()
{
	return 60;
}

void
CAndroidRequest::getSafeAreaInset(float* insets)
{
	insets[0] = 0.0f;
	insets[1] = 0.0f;
	insets[2] = 0.0f;
	insets[3] = 0.0f;
	if (m_isSafeAreaScreen) {
		const float horizontalInset =
			(CPFInterface::getInstance().client().getPhysicalScreenWidth()
			 + CPFInterface::getInstance().client().getPhysicalScreenHeight()
			   * -16.0f / 9.0f) * 0.5f;
		insets[0] = horizontalInset;
		insets[1] = horizontalInset;
	}
}

const char *
CAndroidRequest::getFullPath(const char * assetPath,bool* isReadOnly)
{
    CKLBPathConv& pathconv = CKLBPathConv::getInstance();

    // ファイル名の scheme で、どのファイルを開くべきかが決まる。
    if(!strncmp(assetPath, "file://", 7)) {
        return pathconv.fullpath(assetPath + 7,0,isReadOnly);
    }
    // scheme が asset であれば、作るのは CiOSReadFileStream だが、EXTERN -> INSTALL の順で検索する。
    if(!strncmp(assetPath, "asset://", 8)) {
        return pathconv.fullpath(assetPath,0,isReadOnly);
    }
    return 0;
}

const char *
CAndroidRequest::getPlatform()
{
	return m_platform;
}

IWidget *
CAndroidRequest::createControl(IWidget::CONTROL type, int id, const char * caption, int x, int y, int width, int height, ...)
{
	IWidget * pWidget = 0;
	va_list ap;
	va_start(ap, height);
	switch(type)
	{
	case IWidget::TEXTBOX:
	case IWidget::PASSWDBOX:
	{
		int maxlen = va_arg(ap, int);
		CAndroidTextWidget * pTextWidget = new CAndroidTextWidget(this);
		if(!pTextWidget || !pTextWidget->create(type, id, caption, x, y, width, height)) {
			delete pTextWidget;
			pTextWidget = 0;
		} else {
			pTextWidget->setMaxlen(maxlen);
    }
		pWidget = pTextWidget;
		break;
	}
	case IWidget::WEBVIEW:
	case IWidget::WEBNOJUMP:
	{
		const char * token  = va_arg(ap, const char *);
		const char * region = va_arg(ap, const char *);
		const char * bundleVersion = va_arg(ap, const char *);
		const char * client = va_arg(ap, const char *);
		const char * cKey   = va_arg(ap, const char *);
		const char * appId  = va_arg(ap, const char *);
		const char * userId = va_arg(ap, const char *);
		const char * language = va_arg(ap, const char *);

		CAndroidWebWidget * pWebWidget = new CAndroidWebWidget(this);
		if(!pWebWidget || !pWebWidget->create(type, id, caption, x, y, width, height,
												token, region, bundleVersion, client, cKey, appId, userId, language)) {
			delete pWebWidget;
			pWebWidget = 0;
		}
		pWidget = pWebWidget;
		break;
	}
	case IWidget::MOVIEPLAYER:
	{
		CAndroidMovieWidget * pMovieWidget = new CAndroidMovieWidget(this);
		if(!pMovieWidget || !pMovieWidget->create(type, id, caption, x, y, width, height)) {
			delete pMovieWidget;
			pMovieWidget = 0;
		}
		pWidget = pMovieWidget;
		break;
	}
	case IWidget::ACTIVITYINDICATOR:
	{
		CAndroidActivityIndicator * actI = new CAndroidActivityIndicator(this);
		if(!actI || !actI->create(type, id, caption, x, y, width, height)) {
			delete actI;
			actI = 0;
		}
		pWidget = actI;
		break;
	}
	}
	va_end(ap);
	return pWidget;
}

void
CAndroidRequest::destroyControl(IWidget * pControl)
{
	delete pControl;
}

bool
CAndroidRequest::callApplication(APP_TYPE type, ...)
{

    bool result = true;

    va_list ap;
    va_start(ap, type);
	switch (type)
	{
	// メーラー起動
	case IPlatformRequest::APP_MAIL:
		{
			const char * addr = va_arg(ap, const char *);
			const char * subject = va_arg(ap, const char *);
			const char * body = va_arg(ap, const char *);
			jvalue value;
			callJavaMethod(NULL, value, "startMailer", 'V', "SSS" , addr , subject , body );
		}
		break;

	// ブラウザ起動
	case IPlatformRequest::APP_BROWSER:
		{
			const char * url = va_arg(ap, const char *);
			const char * callback = va_arg(ap, const char *);
			if(callback) {
				delete [] m_klabIdCallback;
				m_klabIdCallback = CKLBUtility::copyString(callback);
			}
			jvalue value;
			callJavaMethod(NULL, value, "startBrowser", 'V', "S" , url );
		}
		break;

	// アップデート
	case IPlatformRequest::APP_UPDATE:
		result = false;
		break;

	case IPlatformRequest::APP_MAP:
		{
			double latitude = va_arg(ap, double);
			double longitude = va_arg(ap, double);
			jvalue value;
			callJavaMethod(NULL, value, "startMap", 'Z', "DD",
						   latitude, longitude);
			result = value.z != 0;
		}
		break;

	case IPlatformRequest::APP_SETTINGS:
		{
			jvalue value;
			callJavaMethod(NULL, value, "startSettings", 'V', "");
		}
		break;

	case IPlatformRequest::APP_COLLABORATION:
		{
			const char * application = va_arg(ap, const char *);
			const char * argument = va_arg(ap, const char *);
			jvalue value;
			callJavaMethod(NULL, value, "startApp", 'V', "SS",
						   application, argument);
		}
		break;

	case IPlatformRequest::APP_SHARE_CONTENTS:
		{
			const char * application = va_arg(ap, const char *);
			const char * argument = va_arg(ap, const char *);
			const char * callback = va_arg(ap, const char *);
			jvalue value;
			callJavaMethod(NULL, value, "shareContents", 'V', "SSS",
						   application, argument, callback);
		}
		break;

	case IPlatformRequest::APP_ATT:
		{
			const char * callback = va_arg(ap, const char *);
			const bool request = va_arg(ap, int) != 0;
			beforeAssertFunction(callback, request);
		}
		break;

	default:
		result = false;
		break;
	}
	va_end(ap);
	return result;

}

void *
CAndroidRequest::createThread(s32 (*thread_func)(void * hThread, void * data), void * data)
{
	PF_THREAD * thread = new PF_THREAD;
	if(!thread) return 0;
	thread->data = data;
	thread->thread_func = thread_func;
	thread->result = 0;
	thread->running = true;
	pthread_create(&(thread->id), 0, ThreadProc, thread);
	return thread;
}

void
CAndroidRequest::exitThread(void * hThread, s32 status)
{
	PF_THREAD * pThread = (PF_THREAD *)hThread;
	longjmp(pThread->jmp, status);
}

bool
CAndroidRequest::watchThread(void * hThread, s32 * status)
{
	PF_THREAD * pThread = (PF_THREAD *)hThread;
	return pThread->running;
}

void
CAndroidRequest::deleteThread(void * hThread)
{
	PF_THREAD * pThread = (PF_THREAD *)hThread;
	pthread_join(pThread->id, 0);
	delete pThread;
}

void
CAndroidRequest::breakThread(void * hThread)
{
	PF_THREAD * pThread = (PF_THREAD *)hThread;
	pthread_kill(pThread->id, SIGKILL);
}

void *
CAndroidRequest::ThreadProc(void * data)
{
	PF_THREAD * pThread = (PF_THREAD *)data;
	if(!(pThread->result = setjmp(pThread->jmp))) {
		pThread->result = (pThread->thread_func)(pThread, pThread->data);
		pThread->running = false;
	}
	return 0;
}

int
CAndroidRequest::genUserID(char * retBuf, int maxlen)
{
	jvalue value;
	callJavaMethod(NULL, value, "genUserID", 'S', "");
	jstring jstr = (jstring)value.l;
	const char * str = CJNI::getJNIEnv()->GetStringUTFChars(jstr, NULL);
	int i = 0;
	for(i = 0; str[i] && i < maxlen - 1; i++) retBuf[i] = str[i];
	retBuf[i] = 0;
	CJNI::getJNIEnv()->ReleaseStringUTFChars(jstr, str);
	return i;
}

int
CAndroidRequest::genUserPW(const char * salt, char * retBuf, int maxlen)
{
	char buf[1024];
	time_t tm;
	int rnd = rand();
	time(&tm);
	sprintf(buf, "%d.%d.%s", rnd, (u32)tm, salt);
	return sha512(buf, retBuf, maxlen);
}

void
CAndroidRequest::registerScriptSource(const char* source, int sourceSize,
									  const char* sourceName)
{
	size_t prefixLength = 15;
	if (strncmp(sourceName, "file://install/", prefixLength)) {
		prefixLength = strncmp(sourceName, "asset://", 8) ? 0 : 8;
	}

	unsigned char* sourceNameHash = new unsigned char[41];
	unsigned char* contentHash = new unsigned char[41];
	const char* normalizedName = sourceName + prefixLength;
	cryptoSHA1(sourceNameHash, normalizedName, strlen(normalizedName), 40);
	cryptoSHA1(contentHash, source, sourceSize, 40);
	sourceNameHash[40] = '\0';
	contentHash[40] = '\0';

	ScriptSourceRecord* record =
		new ScriptSourceRecord(reinterpret_cast<const char*>(sourceNameHash),
							   sourceName,
							   reinterpret_cast<const char*>(contentHash));

	ScriptSourceIdMap::const_iterator sourceId =
		s_scriptSourceIds.find(
			reinterpret_cast<const char*>(sourceNameHash));
	if (sourceId != s_scriptSourceIds.end()) {
		record->m_sourceId = sourceId->second;
	} else {
		char userId[1024] = {};
		getDevID(userId, 512);
		addExtMsg("LPNF", userId, false);
		sendException("HDI");
	}

	ScriptSourceMap::iterator existing = s_scriptSources.find(sourceName);
	if (existing != s_scriptSources.end()) {
		*existing->second = *record;
		delete record;
	} else {
		s_scriptSources.insert(
			std::pair<std::string, ScriptSourceRecord*>(
				record->m_sourceName, record));
		s_scriptSourceNames.push_back(record->m_sourceName.c_str());
	}

	delete [] sourceNameHash;
	delete [] contentHash;
}

ScriptSourceRecord::ScriptSourceRecord(const char* sourceNameHash,
									   const char* sourceName,
									   const char* contentHash)
: m_sourceName(sourceName)
, m_sourceNameHash(sourceNameHash)
, m_contentHash(contentHash)
, m_sourceId(0xffff)
{}

ScriptSourceRecord::~ScriptSourceRecord()
{}

ScriptSourceRecord&
ScriptSourceRecord::operator=(const ScriptSourceRecord& other)
{
	if (&other != this) {
		m_sourceNameHash = other.m_sourceNameHash;
		m_contentHash = other.m_contentHash;
	}
	m_sourceId = other.m_sourceId;
	return *this;
}

char*
CAndroidRequest::getDeviceIntegrityInfo(const char* request)
{
	delete [] m_deviceIntegrityInfo;
	m_deviceIntegrityInfo = new char[2048];
	struct stat status;
	char decodedString[64];
	jvalue integrityResult;

	{
		DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
			AndroidIntegrityStrings::APK_SIGNATURE_METHOD); // getAPKSignature
		RELEASE_DEVICE_INTEGRITY_STRING(
			integrityResult, decodedString);
	}

	DevicePropertyMap properties;
	DevicePropertyNames requestedProperties;
	std::string propertySeparator;

	{
		DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
			AndroidIntegrityStrings::VERSION_RELEASE_METHOD);
		callJavaMethod(NULL, integrityResult,
			decodedString, 'S', "");
		jstring versionReleaseJavaString =
			static_cast<jstring>(integrityResult.l);
		JNIEnv* versionReleaseEnv = CJNI::getJNIEnv();
		const char* versionReleaseValue =
			versionReleaseEnv->GetStringUTFChars(
				versionReleaseJavaString, NULL);

		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(
			4, decodedString, AndroidIntegrityStrings::VERSION_RELEASE_KEY);
		properties.insert(std::make_pair(
			std::string(decodedString),
			std::string(versionReleaseValue)));
		CJNI::getJNIEnv()->ReleaseStringUTFChars(
			versionReleaseJavaString, versionReleaseValue);
	}

	bool hasBuildProperties;
	{
		DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::BUILD_PROPERTIES_PATH);
		std::ifstream buildProperties;
		buildProperties.open(decodedString);
		hasBuildProperties = false;
		if (buildProperties.is_open()) {
			buildProperties.close();
			hasBuildProperties = true;
		}
	}
	if (hasBuildProperties) {
		propertySeparator = "=";
		{
			DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
				AndroidIntegrityStrings::PRODUCT_NAME_2);
			requestedProperties.push_back(decodedString);
		}
		{
			LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
				AndroidIntegrityStrings::PRODUCT_MANUFACTURER_2);
			requestedProperties.push_back(decodedString);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
				AndroidIntegrityStrings::PRODUCT_BRAND_5);
			requestedProperties.push_back(decodedString);
		}
		{
			strcpy(decodedString, "ro.product.device");
			requestedProperties.push_back(decodedString);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
				AndroidIntegrityStrings::PRODUCT_MODEL_5);
			requestedProperties.push_back(decodedString);
		}
		{
			LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
				AndroidIntegrityStrings::MANUFACTURER_METHOD);
			callJavaMethod(NULL, integrityResult,
				decodedString, 'S', "");
			jstring manufacturerJavaString =
				static_cast<jstring>(integrityResult.l);
			JNIEnv* manufacturerEnv = CJNI::getJNIEnv();
			const char* manufacturerValue =
				manufacturerEnv->GetStringUTFChars(
					manufacturerJavaString, NULL);
			LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
				AndroidIntegrityStrings::PRODUCT_MANUFACTURER_4);
			properties.insert(std::make_pair(
				std::string(decodedString),
				std::string(manufacturerValue)));
			CJNI::getJNIEnv()->ReleaseStringUTFChars(
				manufacturerJavaString, manufacturerValue);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
				AndroidIntegrityStrings::PRODUCT_BOARD_5);
			requestedProperties.push_back(decodedString);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
				AndroidIntegrityStrings::FINGERPRINT_4);
			requestedProperties.push_back(decodedString);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
				AndroidIntegrityStrings::BUILD_TAGS_3);
			requestedProperties.push_back(decodedString);
		}
		{
			DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
				AndroidIntegrityStrings::BUILD_PROPERTIES_PATH);
			readDeviceProperties(decodedString, properties,
				requestedProperties, propertySeparator);
		}
	} else {
		{
			strcpy(decodedString, "getProductName");
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::PRODUCT_NAME_4, 4,
				decodedString);
		}

		{
			DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
				AndroidIntegrityStrings::BRAND_METHOD); // getBrand
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::PRODUCT_BRAND_4, 4,
				decodedString);
		}

		{
			DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
				AndroidIntegrityStrings::DEVICE_METHOD); // getDevice
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::PRODUCT_DEVICE, 2,
				decodedString);
		}

		{
			DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
				AndroidIntegrityStrings::MODEL_METHOD); // getModel
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::PRODUCT_MODEL_3, 3,
				decodedString);
		}

		{
			strcpy(decodedString, "getBoard");
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::PRODUCT_BOARD_3, 3,
				decodedString);
		}

		{
			LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
				AndroidIntegrityStrings::FINGERPRINT_METHOD_5);
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::FINGERPRINT_5, 5,
				decodedString);
		}

		{
			strcpy(decodedString, "getTags");
			INSERT_ENCODED_DEVICE_INTEGRITY_PROPERTY(integrityResult,
				decodedString,
				AndroidIntegrityStrings::BUILD_TAGS_4, 4,
				decodedString);
		}
		requestedProperties.clear();
	}

	propertySeparator = ": ";
	strcpy(decodedString, "Hardware");
	requestedProperties.push_back(decodedString);
	{
		DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::CPU_INFO_PATH); // /proc/cpuinfo
		readDeviceProperties(decodedString, properties,
			requestedProperties, propertySeparator);
	}
	requestedProperties.clear();

	{
		strcpy(decodedString, "vbox");
		requestedProperties.push_back(decodedString);
		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(
			2, decodedString, AndroidIntegrityStrings::GOLDFISH_NAME_2);
		requestedProperties.push_back(decodedString);
	}
	DeviceIntegrityFiles integrityFiles;
	strcpy(decodedString, "/");
	findDeviceIntegrityFiles(
		decodedString, requestedProperties, integrityFiles);
	strcpy(decodedString, "/dev/");
	findDeviceIntegrityFiles(
		decodedString, requestedProperties, integrityFiles);
	requestedProperties.clear();

	{
		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(1, decodedString,
			AndroidIntegrityStrings::BLUESTACKS_HOME_PATH);
		requestedProperties.push_back(decodedString);
	} // /sdcard/Android/data/com.bluestacks.home
	{
		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(2, decodedString,
			AndroidIntegrityStrings::BLUESTACKS_SETTINGS_PATH);
		requestedProperties.push_back(decodedString);
	} // /sdcard/Android/data/com.bluestacks.settings
	{
		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
			AndroidIntegrityStrings::MICROVIRT_GUIDE_PATH);
		requestedProperties.push_back(decodedString);
	} // /data/data/com.microvirt.guide
	strcpy(decodedString, "/data/data/com.microvirt.memuime");
	requestedProperties.push_back(decodedString);
	{
		DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
			AndroidIntegrityStrings::FSTAB_GOLDFISH_PATH);
		requestedProperties.push_back(decodedString);
	}
	{
		DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
			AndroidIntegrityStrings::INIT_GOLDFISH_PATH);
		requestedProperties.push_back(decodedString);
	}
	{
		DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
			AndroidIntegrityStrings::UEVENTD_GOLDFISH_PATH);
		requestedProperties.push_back(decodedString);
	}
	{
		DECODE_DEVICE_INTEGRITY_STRING(4, decodedString,
			AndroidIntegrityStrings::GOLDFISH_PIPE_PATH);
		requestedProperties.push_back(decodedString);
	}

	for (DevicePropertyNames::const_iterator path = requestedProperties.begin();
		 path != requestedProperties.end(); ++path)
	{
		if (stat(path->c_str(), &status) == 0 || errno != ENOENT) {
			integrityFiles.push_back(*path);
		}
	}
	requestedProperties.clear();

	{
		strcpy(decodedString, "getString");
		callJavaMethod(NULL, integrityResult,
			decodedString, 'S', "");
		jstring signatureJavaString =
			static_cast<jstring>(integrityResult.l);
		JNIEnv* signatureEnv = CJNI::getJNIEnv();
		const char* signatureValue = signatureEnv->GetStringUTFChars(
			signatureJavaString, NULL);
		char signatureHash[1024];
		sha512(signatureValue, signatureHash, sizeof(signatureHash));
		CJNI::getJNIEnv()->ReleaseStringUTFChars(
			signatureJavaString, signatureValue);
		DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::SIGNATURE_KEY); // signature
		properties.insert(std::make_pair(
			std::string(decodedString), std::string(signatureHash)));
	}

	{
		DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
			AndroidIntegrityStrings::BASE_PATH_METHOD); // getBasePath
		callJavaMethod(NULL, integrityResult,
			decodedString, 'S', "");
		jstring basePathJavaString =
			static_cast<jstring>(integrityResult.l);
		JNIEnv* basePathEnv = CJNI::getJNIEnv();
		const char* basePathValue = basePathEnv->GetStringUTFChars(
			basePathJavaString, NULL);
		strcpy(decodedString, "basePath");
		properties.insert(std::make_pair(
			std::string(decodedString), std::string(basePathValue)));
		CJNI::getJNIEnv()->ReleaseStringUTFChars(
			basePathJavaString, basePathValue);
	}

	{
		DECODE_DEVICE_INTEGRITY_STRING(5, decodedString,
			AndroidIntegrityStrings::MODE_METHOD); // getMode
		const bool adbEnabled =
			getDeviceIntegrityInteger(this, decodedString) == 1;
		char adbState[512];

		DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::ADB_ENABLED_KEY);
		const std::string adbEnabledKey(decodedString);
		const char* encodedAdbState = adbEnabled ? "dEh" : "Wc";
		const size_t encodedAdbStateLength = strlen(encodedAdbState);
		const char* decodeSource = encodedAdbState;
		for (unsigned int decodeRound = 0; decodeRound < 5; ++decodeRound) {
			for (size_t decodeIndex = 0;
				 decodeIndex <= encodedAdbStateLength; ++decodeIndex)
			{
				const u8 byte =
					static_cast<u8>(decodeSource[decodeIndex]);
				adbState[decodeIndex] = static_cast<char>(
					((byte - 1) ^ (decodeIndex + 1)) - 1);
			}
			decodeSource = adbState;
		}
		adbState[encodedAdbStateLength] = '\0';
		properties.insert(std::pair<std::string, std::string>(
			adbEnabledKey, std::string(adbState)));
	}
	// adbEnbled is the target spelling.

	{
		const char* unitDatabasePath;
		CKLBPathConv& pathConverter = CKLBPathConv::getInstance();
		LOAD_AND_DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::UNIT_DATABASE_ASSET_PATH);
		unitDatabasePath =
			pathConverter.fullpath(decodedString + 7);
		char databaseHash[40];
		CKLBUtility::sha1File(unitDatabasePath, databaseHash,
			sizeof(databaseHash));
		strcpy(decodedString, "db_sha1");
		properties.insert(std::make_pair(
			std::string(decodedString), std::string(databaseHash)));
	}
	// asset://db/unit/unit.db_

	{
		Dl_info libraryInfo;
		char libraryHash[40];
		dladdr(reinterpret_cast<void*>(remove), &libraryInfo);
		CKLBUtility::sha1File(libraryInfo.dli_fname, libraryHash,
			sizeof(libraryHash));
		DECODE_DEVICE_INTEGRITY_STRING(3, decodedString,
			AndroidIntegrityStrings::STOCK_OPTION_KEY); // GreatStockOption
		properties.insert(std::make_pair(
			std::string(decodedString), std::string(libraryHash)));
	}

	if (request) {
		char encodedKey[512];
		u32 encodedLength = 0;
		// The property key is stored obfuscated and does not decode to text
		// under the device-integrity transform; these are the shipped bytes,
		// read from .rodata at 0x3f227c.
		KLBNetAPI_encodeBase64("wibs~d\x05", 7,
			encodedKey, &encodedLength);

		const int requestLength = static_cast<int>(strlen(request));
		char transformedRequest[512];
		const char* transformSource = request;
		const int transformCount = rand() % 5 + 1;
		for (int transform = 0; transform < transformCount; ++transform) {
			for (int index = 0; index <= requestLength; ++index) {
				const u8 character =
					static_cast<u8>(transformSource[index]);
				transformedRequest[index] = static_cast<char>(
					((character + 1) ^ static_cast<u8>(index + 1)) + 1);
			}
			transformSource = transformedRequest;
		}
		transformedRequest[requestLength] = '\0';
		transformedRequest[requestLength] =
			static_cast<char>(transformCount);
		transformedRequest[requestLength + 1] = '\0';

		char encodedRequest[1024];
		KLBNetAPI_encodeBase64(transformedRequest,
			requestLength + 2, encodedRequest, &encodedLength);
		properties.insert(std::make_pair(
			std::string(encodedKey), std::string(encodedRequest)));
	}

	std::stringstream json("");
	json << "{\n";
	for (DevicePropertyMap::const_iterator property = properties.begin();
		 property != properties.end(); ++property)
	{
		json << "\t\"" << property->first.c_str() << "\":\""
			 << property->second.c_str() << "\",\n";
	}

	json << "\t\"";
	strcpy(decodedString, "SuspiciousElement");
	json << decodedString << "\":[";
	if (!integrityFiles.empty()) {
		json << "\n";
		const int lastIndex = integrityFiles.size() - 1;
		for (int index = 0; index < lastIndex; ++index) {
			json << "\t\t\"" << integrityFiles[index] << "\",\n";
		}
		json << "\t\t\"" << integrityFiles[lastIndex] << "\"]\n";
	} else {
		json << "]\n";
	}
	json << "}";

	char* const resultDestination = m_deviceIntegrityInfo;
	{
		const std::string result = json.str();
		strncpy(resultDestination, result.c_str(), 2048);
	}
	BacktraceExtension::getInstance().addExtMsg(
		"DI", m_deviceIntegrityInfo, false);
	return m_deviceIntegrityInfo;
}

bool
CAndroidRequest::readyDevID()
{
	if(m_regId) return true;	// 既に取得していれば問題無し。
	bool bResult = false;
	jvalue ret;
	callJavaMethod(NULL, ret, "getDeviceToken", 'S', "");
	JNIEnv * env = CJNI::getJNIEnv();
	const char *bytes = env->GetStringUTFChars((jstring)ret.l, NULL);
	// DEBUG_PRINT("readyDevID ret.l:%s", (char*)ret.l);
	// DEBUG_PRINT("readyDevID bytes:%s", bytes);
	if(bytes) {
		int len = strlen(bytes);
		if(len > 0) {
			char * buf = new char [len + 1];
			strcpy(buf, bytes);
			m_regId = (const char *)buf;
			bResult = true;
		}
		env->ReleaseStringUTFChars((jstring)ret.l, bytes);
	}
	// DEBUG_PRINT("readyDevID ret:%d", bResult);
	return bResult;
}

int
CAndroidRequest::getDevID(char * retBuf, int maxlen)
{
	if(!m_regId) return 0;

	int i;
	for(i = 0; m_regId[i] && i < maxlen - 1; i++) retBuf[i] = m_regId[i];
	retBuf[i] = 0;
	return i;
}

bool
CAndroidRequest::setSecureDataID(const char * service_name, const char * user_id)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "setKeyChain", 'Z', "SSS", service_name, "user_id", user_id);
	return (bool)ret.z;
}

bool
CAndroidRequest::setSecureDataPW(const char * service_name, const char * passwd)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "setKeyChain", 'Z', "SSS", service_name, "passwd", passwd);
	return (bool)ret.z;

}

int
CAndroidRequest::getSecureDataID(const char * service_name, char * retBuf, int maxlen)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "getKeyChain", 'S', "SS", service_name, "user_id");
	JNIEnv * env = CJNI::getJNIEnv();
	const char *bytes = env->GetStringUTFChars((jstring)ret.l, NULL);
	int i;
	for(i = 0; bytes[i] && i < maxlen - 1; i++) retBuf[i] = bytes[i];
	retBuf[i] = 0;

	env->ReleaseStringUTFChars((jstring)ret.l, bytes);

	return i;
}

int
CAndroidRequest::getSecureDataPW(const char * service_name, char * retBuf, int maxlen)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "getKeyChain", 'S', "SS", service_name, "passwd");
	JNIEnv * env = CJNI::getJNIEnv();
	const char *bytes = env->GetStringUTFChars((jstring)ret.l, NULL);
	int i;
	for(i = 0; bytes[i] && i < maxlen - 1; i++) retBuf[i] = bytes[i];
	retBuf[i] = 0;

	env->ReleaseStringUTFChars((jstring)ret.l, bytes);

	return i;
}
    
bool
CAndroidRequest::delSecureDataID(const char * service_name)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "delKeyChain", 'Z', "SS", service_name,"user_id");
	return (bool)ret.z;
}

bool
CAndroidRequest::delSecureDataPW(const char * service_name)
{
	jvalue ret;
	callJavaMethod(NULL, ret, "delKeyChain", 'Z', "SS", service_name,"passwd");
	return (bool)ret.z;
}

void
CAndroidRequest::startAlertDialog( const char* title , const char* message )
{
	jvalue ret;
	callJavaMethod(NULL, ret, "startAlertDialog", 'V', "SS", title , message);
}

int
CAndroidRequest::sha512(const char * string, char * buf, int maxlen)
{
	JNIEnv * env = CJNI::getJNIEnv();
	jstring jstr = env->NewStringUTF(string);
	jclass cls_pfif = env->FindClass(JNI_LOAD_PATH);
	jmethodID methodID = env->GetStaticMethodID(cls_pfif, "sha512", "(Ljava/lang/String;)[B");
	jbyteArray jbarr = (jbyteArray)env->CallStaticObjectMethod(cls_pfif, methodID, jstr);
	jbyte* digest = env->GetByteArrayElements(jbarr, NULL);

	int len = env->GetArrayLength(jbarr);
	char * ptr = buf;
	for(int i = 0; i < len && i * 2 < maxlen - 1; i++) {
		sprintf(ptr, "%02x", 0xff & (int)digest[i]);
		ptr += strlen(ptr);
	}
	env->ReleaseByteArrayElements(jbarr, digest, 0);

	CJNI::getJNIEnv()->DeleteLocalRef(jstr);
	CJNI::getJNIEnv()->DeleteLocalRef(cls_pfif);
	//CJNI::getJNIEnv()->DeleteLocalRef(methodID);
	CJNI::getJNIEnv()->DeleteLocalRef(jbarr);
	//CJNI::getJNIEnv()->DeleteLocalRef(digest);

	return strlen(buf);
}

jclass
CAndroidRequest::getJavaClass(const char* className, bool global)
{
	JNIEnv* env = CJNI::getJNIEnv();
	jclass javaClass = env->FindClass(className);
	if(global) {
		javaClass = static_cast<jclass>(env->NewGlobalRef(javaClass));
	}
	return javaClass;
}

bool
CAndroidRequest::callJavaMethod(jclass targetClass, jvalue& ret, const char * method, const char rettype, const char * form, ...)
{
	using namespace std;
	//引数の数を数える
	int count = 0;
	for(const char * ptr = form; *ptr; ptr++) {
		if(*ptr == ' ') continue;
		if(*ptr == '[') ptr++;
		count++;
	}
	//DEBUG_PRINT("args count: %d", count);
	JNIEnv* env = CJNI::getJNIEnv();

	vector<jstring> jstrs;
	vector<JNIByteArrayArgument> byteArrays;
	jvalue * arrArgs = NULL;
	jmethodID methodID = 0;
	char signature[1024];
	char * wp = signature;
	strcpy(wp, "(");
	wp += strlen(wp);
	if( count > 0 )
	{
		arrArgs = new jvalue [ count ];

		va_list ap;
		va_start(ap, form);
		count = 0;
		for(const char * ptr = form; *ptr; ptr++) {
			if(*ptr == ' ')continue;
			switch(*ptr) {
			case 'Z':
			{
				bool bval = (bool)va_arg(ap, int);
				arrArgs[count++].z = (jboolean)bval;
				strcpy(wp, "Z");
				wp += strlen(wp);
				break;
			}
			case 'C':
			{
				char cval = (char)va_arg(ap, int);
				arrArgs[count++].c = (jchar)cval;
				strcpy(wp, "C");
				wp += strlen(wp);
				break;
			}
			case 'I':
			{
				int ival = va_arg(ap, int);
				arrArgs[count++].i = (jint)ival;
				strcpy(wp, "I");
				wp += strlen(wp);
				break;
			}
			case 'F':
			{
				float fval = (float)va_arg(ap, double);
				arrArgs[count++].f = (jfloat)fval;
				strcpy(wp, "F");
				wp += strlen(wp);
				break;
			}
			case 'D':
			{
				double dval = va_arg(ap, double);
				arrArgs[count++].d = (jdouble)dval;
				strcpy(wp, "D");
				wp += strlen(wp);
				break;
			}
			case 'S':
			{
				const char * string = va_arg(ap, const char *);
				jstring jstr = env->NewStringUTF(string);
				arrArgs[count++].l = (jobject)jstr;
				strcpy(wp, "Ljava/lang/String;");
				wp += strlen(wp);
				jstrs.push_back(jstr);
				break;
			}
			case '[':
			{
				++ptr;
				if((*ptr | 0x20) != 'b') {
					break;
				}
				unsigned char* bytes = va_arg(ap, unsigned char*);
				const int length = va_arg(ap, int);
				jbyteArray array = env->NewByteArray(length);
				env->SetByteArrayRegion(array, 0, length,
									reinterpret_cast<const jbyte*>(bytes));
				arrArgs[count++].l = array;
				strcpy(wp, "[B");
				wp += strlen(wp);
				byteArrays.push_back(std::make_pair(
					*ptr == 'b' ? bytes : NULL, array));
				break;
			}
			default:
			{
				klb_assertAlways("wrong JNI signature. unknown type: %c", *ptr);
				break;
			}
			}
		}
		va_end(ap);
	}
	strcpy(wp, ")");
	wp += strlen(wp);
	jclass cls_pfif = targetClass;
	if(!cls_pfif) {
		cls_pfif = s_pfInterfaceClass;
		if(!cls_pfif) {
			jclass localClass = env->FindClass(JNI_LOAD_PATH);
			cls_pfif = static_cast<jclass>(env->NewGlobalRef(localClass));
			s_pfInterfaceClass = cls_pfif;
			if(!cls_pfif) {
				return false;
			}
		}
	}

	switch(rettype) {
	case 'V':
	{
		strcpy(wp, "V");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		env->CallStaticVoidMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	case 'Z':
	{
		strcpy(wp, "Z");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		ret.z = env->CallStaticBooleanMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	case 'I':
	{
		strcpy(wp, "I");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		ret.i = env->CallStaticIntMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	case 'F':
	{
		strcpy(wp, "F");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		ret.f = env->CallStaticFloatMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	case 'D':
	{
		strcpy(wp, "D");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		ret.d = env->CallStaticDoubleMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	case 'S':
	{
		strcpy(wp, "Ljava/lang/String;");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		//DEBUG_PRINT("static methodID:%d", methodID);
		if( methodID > 0 ) {
			ret.l = env->CallStaticObjectMethodA(cls_pfif, methodID, arrArgs);
			break;
		}

		// staticMethodから見つからなかった場合はstaticではないMethodから検索
		methodID = env->GetMethodID(cls_pfif, method, signature);
		//DEBUG_PRINT("public methodID:%d", methodID);
		if( methodID > 0 ) {
			ret.l = env->CallObjectMethodA(cls_pfif, methodID, arrArgs);
			break;
		}
		break;
	}
	case 'b':
	{
		strcpy(wp, "[B");
		methodID = env->GetStaticMethodID(cls_pfif, method, signature);
		ret.l = env->CallStaticObjectMethodA(cls_pfif, methodID, arrArgs);
		break;
	}
	}

	// 文字列を引数として渡した場合はそれを解放する。
	count = 0;
	for(const char * ptr = form; *ptr; ptr++) {
		if(*ptr == ' ')continue;
		if(*ptr != 'S') {
			count++;
			continue;
		}
		// 平成25年8月14日(水)
		// jstringをchar*に変換しているわけではないからいらないのでは.
		// env->ReleaseStringUTFChars((jstring)arrArgs[count].l, 0);
		count++;
	}
	// 引数として使用した配列を解放する
	if( arrArgs != NULL ) {
		delete [] arrArgs;
	}

	vector<jstring>::iterator it = jstrs.begin();
	while (it != jstrs.end()) {
		env->DeleteLocalRef(*it);
		++it;
	}
	for(vector<JNIByteArrayArgument>::iterator byteIt = byteArrays.begin();
		byteIt != byteArrays.end(); ++byteIt) {
		unsigned char* const output = byteIt->first;
		jbyteArray const array = byteIt->second;
		if(output) {
			const jsize length = env->GetArrayLength(array);
			env->GetByteArrayRegion(array, 0, length,
								reinterpret_cast<jbyte*>(output));
		}
		env->DeleteLocalRef(array);
	}
	//CJNI::getJNIEnv()->DeleteLocalRef(arrArgs);
	//CJNI::getJNIEnv()->DeleteLocalRef(methodID);

	return true;
}


extern "C" {

JNIEXPORT jbyteArray JNICALL APP_FUNC(internalGetLocalizedMessage)
  (JNIEnv* env, jobject obj, jstring j_key, jstring j_fallback)
{
	const char* key = env->GetStringUTFChars(j_key, NULL);
	const char* fallback = env->GetStringUTFChars(j_fallback, NULL);
	char buffer[1000];
	CPFInterface& pfif = CPFInterface::getInstance();
	u32 characterCount = 0;
	jbyteArray result = NULL;

	if (pfif.client().getString(
		buffer, sizeof(buffer), key, &characterCount, fallback)) {
		int length = strlen(buffer);
		result = env->NewByteArray(length);
		env->SetByteArrayRegion(result, 0, length,
			reinterpret_cast<const jbyte*>(buffer));
	}

	env->ReleaseStringUTFChars(j_key, key);
	env->ReleaseStringUTFChars(j_fallback, fallback);
	return result;
}

JNIEXPORT jboolean JNICALL APP_FUNC(initSequence)
  (JNIEnv *env, jobject obj, jint j_width, jint j_height, jstring j_strPath,
		  jstring j_model , jstring j_brand, jstring j_board, jstring j_version, jstring j_tz)
{
	const char * strPath  = env->GetStringUTFChars(j_strPath, 0);
	const char * strModel = env->GetStringUTFChars(j_model, 0);
	const char * strBrand = env->GetStringUTFChars(j_brand, 0);
	const char * strBoard = env->GetStringUTFChars(j_board, 0);
	const char * strVersion = env->GetStringUTFChars(j_version, 0);
	const char * strTZ = env->GetStringUTFChars(j_tz, 0);


	int width = j_width;
	int height = j_height;

	CAndroidRequest * pRequest = new CAndroidRequest(strModel, strBrand, strBoard, strVersion, strTZ);
	CPFInterface& pfif = CPFInterface::getInstance();

	CJNI::setJNIEnv(env);	//　これでC++コードの中でも簡易版 SoundPool が使える。
	pRequest->setHomePath(strPath);
	pfif.setPlatformRequest(pRequest);
	pRequest->init();

	pRequest->readyDevID();
	char deviceId[1024];
	memset(deviceId, 0, sizeof(deviceId));
	pRequest->getDevID(deviceId, 512);
	pRequest->addExtMsg("DID", deviceId, false);

	GameSetup();

	if((float)width > (float)height * 16.0f / 9.0f) {
		CAndroidRequest::getInstance()->setSafeAreaScreen(true);
	}

	// 起動シーケンス順に呼び出す。この呼び出し順は厳守されねばならない。
	pfif.client().setScreenInfo(true, width, height);

    pfif.client().setFilePath(0);
	pfif.client().initGame();

        env->ReleaseStringUTFChars(j_strPath, strPath);
        env->ReleaseStringUTFChars(j_model, strModel);
        env->ReleaseStringUTFChars(j_brand, strBrand);
        env->ReleaseStringUTFChars(j_board, strBoard);
        env->ReleaseStringUTFChars(j_version, strVersion);
        env->ReleaseStringUTFChars(j_tz, strTZ);

	srand((unsigned int)CAndroidRequest::getInstance()->nanotime());
	char* deviceIntegrityInfo = CAndroidRequest::getInstance()->getDeviceIntegrityInfo(NULL);
	BacktraceExtension::getInstance().addExtMsg("DI", deviceIntegrityInfo, false);

	// 初期化終了。
	return (jboolean)true;
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    frameFlip
 * Signature: (I)V
 */
JNIEXPORT void JNICALL APP_FUNC(frameFlip)
  (JNIEnv *env, jobject obj, jint j_deltaT)
{
	CPFInterface::getInstance().client().frameFlip(j_deltaT);
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    onKLabIdResult
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL APP_FUNC(onKLabIdResult)
  (JNIEnv* env, jobject obj, jint result, jstring j_keyValuePairs)
{
	const char* keyValuePairs = env->GetStringUTFChars(j_keyValuePairs, NULL);
	if (CPFInterface::getInstance().isPlatform()) {
		CAndroidRequest& request = static_cast<CAndroidRequest&>(
			CPFInterface::getInstance().platform());
		request.onKLabIdResult(result, keyValuePairs);
	}
}

JNIEXPORT void JNICALL APP_FUNC(inputPoint)
  (JNIEnv *env, jobject obj, jint j_id, jint j_type, jint j_x, jint j_y)
{
	static IClientRequest::INPUT_TYPE type[] = {
			IClientRequest::I_CLICK,
			IClientRequest::I_DRAG,
			IClientRequest::I_RELEASE
	};
	CPFInterface::getInstance().client().inputPoint(j_id, type[j_type], j_x, j_y);
}

JNIEXPORT void JNICALL APP_FUNC(inputDeviceKey)
  (JNIEnv *env, jobject obj, jint keyId, jchar eventType)
{
	CPFInterface::getInstance().client().inputDeviceKey(keyId, eventType);
}

JNIEXPORT void JNICALL APP_FUNC(rotateScreenOrientation)
  (JNIEnv *env, jobject obj, jint j_origin, jint j_width, jint j_height)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	int width = j_width;
	int height = j_height;
	int origin = j_origin;
	IClientRequest::SCRMODE mode = (width > height) ? IClientRequest::LANDSCAPE : IClientRequest::PORTRAIT;

	static IClientRequest::ORIGIN arr_origin[] = {
			IClientRequest::LEFT_TOP,
			IClientRequest::LEFT_BOTTOM,
			IClientRequest::RIGHT_BOTTOM,
			IClientRequest::RIGHT_TOP
	};

	pfif.client().reportScreenRotation(arr_origin[origin], mode);
}

JNIEXPORT void JNICALL APP_FUNC(toNativeSignal)
  (JNIEnv *env, jobject obj, jint j_cmd, jint j_param)
{
	CPFInterface& pfif = CPFInterface::getInstance();

	// porting layer class の instance を取得
	CAndroidRequest * instance = CAndroidRequest::getInstance();
	if(!instance) return;

	instance->nativeSignal((int)j_cmd, (int)j_param);
}

JNIEXPORT jint JNICALL APP_FUNC(getGLVersion)
  (JNIEnv * env, jobject obj)
{
#ifdef OPENGL2
	return 2;
#else
	return 1;
#endif
}

JNIEXPORT void  JNICALL APP_FUNC(resetViewport)
  (JNIEnv * env, jobject obj)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	CPFInterface::getInstance().client().resetViewport();
}

// アプリにおけるバックグラウンドに行った際の動作
JNIEXPORT void  JNICALL APP_FUNC(onActivityPause) (void)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	pfif.client().pauseGame(true);
	pfif.platform().getAudioSystem()->onActivityPause();
}

// アプリに置けるフォアグラウンドに行った際の動作
JNIEXPORT void  JNICALL APP_FUNC(onActivityResume) (void)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	pfif.platform().getAudioSystem()->onActivityResume();
	INotificationManager* notification = INotificationManager::getInstance();
	if(notification) notification->onActivityResume();
	pfif.client().pauseGame(false);
}

JNIEXPORT void JNICALL APP_FUNC(onHeadsetActive) (void)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	pfif.platform().getAudioSystem()->onHeadsetActive();
}

// WebViewの動作コールバック
JNIEXPORT void JNICALL APP_FUNC(WebViewControlEvent)
  (JNIEnv* env, jobject obj, jobject _pWeb, jint _flg, jstring j_data)
{
	CAndroidWebWidget * pWidget = CAndroidWebWidget::get_webViewItem(_pWeb);
	int status = -1;
	if(!CPFInterface::getInstance().isClient()) return;

	const char* data = NULL;
	int dataLength = 0;
	if (j_data) {
		data = env->GetStringUTFChars(j_data, NULL);
		dataLength = strlen(data);
	}
	dataLength = dataLength ? dataLength + 1 : 0;

	enum LOCAL_WEBEVENT_TYPE
	{
		E_DIDSTARTLOADWEB = 0,
		E_DIDLOADENDWEB,
		E_FAILEDLOADWEB,
		E_URLJUMP,
	};

	switch((int)_flg)
	{
		case E_DIDSTARTLOADWEB:
			status = IClientRequest::E_DIDSTARTLOADWEB;
			break;
		case E_DIDLOADENDWEB:
			status = IClientRequest::E_DIDLOADENDWEB;
			break;
		case E_FAILEDLOADWEB:
			status = IClientRequest::E_FAILEDLOADWEB;
			break;
		case E_URLJUMP:
		{
			size_t len = pWidget->getTmpTextLength();
			if(len > 0)
			{
				char* pTmpBuff = NULL;
				pTmpBuff = KLBNEWA(char, len + 1);
				pWidget->getTmpText(pTmpBuff, len + 1);
				pWidget->setTmpString(pTmpBuff);
				KLBDELETEA(pTmpBuff);
				status = IClientRequest::E_URLJUMP;
			}
			else
			{
				// リンク先が無ければreturn
				return;
			}
			break;
		}
		default:
			return; // 値が想定外なのでリターン
	}
	CPFInterface::getInstance().client().controlEvent((IClientRequest::EVENT_TYPE)status, pWidget,
		dataLength, dataLength ? const_cast<char*>(data) : NULL, 0, NULL);
}

JNIEXPORT void	JNICALL APP_FUNC(clientControlEvent)
	(JNIEnv * env, jobject obj, jint j_type, jint j_widget, jstring j_data_1, jstring j_data_2)
{
	IClientRequest& cli = CPFInterface::getInstance().client();
	// とりあえずストア関係だけ //
	static IClientRequest::EVENT_TYPE event_array[] =
	{
		IClientRequest::E_STORE_BAD_ITEMID,         // アイテムIDが無効
		IClientRequest::E_STORE_GET_PRODUCTS, // ProductListの取得.
		IClientRequest::E_STORE_PURCHASHING,        // 購入処理中
		IClientRequest::E_STORE_PURCHASHED,         // 購入処理終了
		IClientRequest::E_STORE_DEFERRED,           // 購入処理保留
		IClientRequest::E_STORE_FAILED,             // 購入処理失敗
		IClientRequest::E_STORE_RESTORE,            // リストア終了
		IClientRequest::E_STORE_RESTORE_FAILED,     // リストア失敗
	};

	const char * char_1 = env->GetStringUTFChars(j_data_1, NULL);
    const char * char_2 = env->GetStringUTFChars(j_data_2, NULL);
    // FIXME ↑の２つは ReleaseStringUTFChars されてないけど影響範囲が広いのでとりあえず後回し

    size_t len_1 = strlen(char_1);
    size_t len_2 = strlen(char_2);

    len_1 = len_1 == 0 ? 0 : len_1 + 1;
    len_2 = len_2 == 0 ? 0 : len_2 + 1;
	cli.controlEvent(event_array[j_type], 0, len_1, (void *)char_1, len_2, (void *)char_2);
}

JNIEXPORT void JNICALL APP_FUNC(jniOnLoad) (JavaVM* vm, void* reserved)
{
	CJNI::setJavaVM(vm);
}

JNIEXPORT void JNICALL APP_FUNC(onShareCallback)
  (JNIEnv* env, jobject, jstring j_callback, jboolean success, jstring j_result)
{
	const char* callback = env->GetStringUTFChars(j_callback, NULL);
	const char* result = env->GetStringUTFChars(j_result, NULL);
	CKLBScriptEnv::getInstance().call_shareCallback(callback, result, success != 0);
}

// onPauseがコールされてからonDrawFrameが初めてコールされた際の処理 //
JNIEXPORT void  JNICALL APP_FUNC(clientResumeGame) (void)
{
	CPFInterface& pfif = CPFInterface::getInstance();
	if(!pfif.isClient()) return;
	pfif.client().pauseGame(true);
	pfif.client().frameFlip(1);
}

JNIEXPORT void JNICALL APP_FUNC(OnLocationCallback)
  (JNIEnv* env, jobject obj, jint callbackIndex, jint parameter,
	  jdouble latitude, jdouble longitude, jstring j_message)
{
	const char* message = env->GetStringUTFChars(j_message, NULL);
	ILocationManager* locationManager = ILocationManager::getInstance();
	klb_assertNull(locationManager,
		"CAndroidRequest : Failed to get ILocationManagerAndroid* !!!");
	locationManager->notifyLocation(callbackIndex, parameter, latitude,
		longitude, message);
}

JNIEXPORT void JNICALL APP_FUNC(OnNotificationCallback)
  (JNIEnv* env, jobject obj, jint callbackIndex, jint parameter,
	  jstring j_message)
{
	const char* message = env->GetStringUTFChars(j_message, NULL);
	INotificationManager* notificationManager = INotificationManager::getInstance();
	klb_assertNull(notificationManager,
		"CAndroidRequest : Failed to get INotificationManagerAndroid* !!!");
	notificationManager->notify(callbackIndex, parameter, message);
}

} // extern "C"

//================================================
// 課金
void
CAndroidRequest::initStoreTransactionObserver()
{
}

void
CAndroidRequest::releaseStoreTransactionObserver()
{
}

void
CAndroidRequest::buyStoreItems(const char * item_id)
{
	// 購入処理
	jvalue value;
	callJavaMethod(NULL, value, "billingBuyItem", 'V', "S" , item_id);
}

void
CAndroidRequest::getStoreProducts(const char* json, bool currency_mode)
{
	// リスト問い合わせ
	jvalue value;
	callJavaMethod(NULL, value, "billingGetProducts", 'V', "S" , json);
}

void
CAndroidRequest::finishStoreTransaction(const char* receipt)
{
	jvalue value;
	callJavaMethod(NULL, value, "billingConsume", 'V', "S", receipt);
}

bool
CAndroidRequest::publicKeyVerify(unsigned char* message, int messageLength,
						 unsigned char* signature, int signatureLength)
{
	CJNI::attachJNIEnv();
	jvalue result;
	callJavaMethod(NULL, result, "publicKeyVerify", 'Z', "[B[B[B",
				   message, messageLength,
				   signature, signatureLength,
				   s_publicKey, sizeof(s_publicKey) - 1);
	CJNI::detachJNIEnv();
	return result.z != 0;
}

int
CAndroidRequest::publicKeyEncrypt(unsigned char* input, int inputLength,
					  unsigned char* output, int outputLength)
{
	jvalue result;
	callJavaMethod(NULL, result, "publicKeyEncrypt", 'I', "[B[B[b",
				   input, inputLength,
				   s_publicKey, sizeof(s_publicKey) - 1,
				   output, outputLength);
	return result.i;
}

bool
CAndroidRequest::randomBytes(unsigned char* output, int length)
{
	jvalue result;
	callJavaMethod(NULL, result, "getRandomBytes", 'Z', "[b", output, length);
	return result.z != 0;
}

int
CAndroidRequest::encryptAES128CBC(unsigned char* output, int outputLength,
						  const char* input, int inputLength,
						  const char* key, int keyLength)
{
	jvalue result;
	callJavaMethod(NULL, result, "encryptAES128CBC", 'I', "[B[B[b",
				   input, inputLength, key, keyLength, output, outputLength);
	return result.i;
}

int
CAndroidRequest::decryptAES128CBC(unsigned char* output, int outputLength,
						  const char* input, int inputLength,
						  const char* key, int keyLength)
{
	jvalue result;
	callJavaMethod(NULL, result, "decryptAES128CBC", 'I', "[B[B[b",
				   input, inputLength, key, keyLength, output, outputLength);
	return result.i;
}

//================================================
// HTTP transport
bool
CAndroidRequest::initNetwork()
{
	return CurlObjectInternal::initializeLibrary();
}

void
CAndroidRequest::shutdownNetwork()
{
	CurlObjectInternal::shutdownLibrary();
}

CurlObjectInternal*
CAndroidRequest::createNetworkOperation()
{
	return CurlObjectInternal::create();
}

void
CAndroidRequest::resetNetworkOperation(CurlObjectInternal* operation)
{
	operation->reset();
}

void
CAndroidRequest::cleanupNetworkOperation(CurlObjectInternal* operation)
{
	operation->cleanup();
}

int
CAndroidRequest::performNetworkOperation(CurlObjectInternal* operation)
{
	return operation->perform();
}

void
CAndroidRequest::freeNetworkFormHeaders(CurlObjectInternal* operation)
{
	operation->freeFormHeaders();
}

void
CAndroidRequest::destroyNetworkOperation(CurlObjectInternal* operation)
{
	CurlObjectInternal::destroy(operation);
}

void
CAndroidRequest::appendNetworkHeader(CurlObjectInternal* operation, const char* header)
{
	operation->appendHeader(header);
}

void
CAndroidRequest::setNetworkPostFields(CurlObjectInternal* operation)
{
	operation->setPostFields();
}

void
CAndroidRequest::setNetworkPostData(CurlObjectInternal* operation, long contentLength, const void* data)
{
	operation->setPostData(contentLength, data);
}

void
CAndroidRequest::addNetworkFormData(CurlObjectInternal* operation, const char* name,
	                                long contentLength, const void* data)
{
	operation->addFormData(name, contentLength, data);
}

void
CAndroidRequest::setupNetworkConnection(CurlObjectInternal* operation, const char* url,
	                                    const char* proxy, void* callbackContext,
	                                    void* progressCallback, void* headerCallback,
	                                    void* writeCallback)
{
	operation->setupConnection(url, proxy, callbackContext,
	                           progressCallback, headerCallback, writeCallback);
}

long
CAndroidRequest::getNetworkHttpCode(CurlObjectInternal* operation)
{
	return operation->getHttpCode();
}

//================================================
// mutex
void* CAndroidRequest::allocMutex	()
{
	pthread_mutex_t* pSection = new pthread_mutex_t();
	if (pSection) {
		if (pthread_mutex_init(pSection,NULL)) {
			delete pSection;
			return NULL;
		}
	}
	return pSection;
}

void CAndroidRequest::freeMutex		(void* mutex)
{
	if (mutex) {
		pthread_mutex_t* pSection = (pthread_mutex_t*)mutex;	
		pthread_mutex_destroy(pSection); // Error handling useless here.
		delete pSection;
	}
}

void CAndroidRequest::mutexLock		(void* mutex)
{
	if (mutex) {
		pthread_mutex_t* pSection = (pthread_mutex_t*)mutex;
		pthread_mutex_lock(pSection);
	}
}

void CAndroidRequest::mutexUnlock	(void* mutex)
{
	if (mutex) {
		pthread_mutex_t* pSection = (pthread_mutex_t*)mutex;
		pthread_mutex_unlock(pSection);
	}
}

struct EventMutex {
	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
};

void* CAndroidRequest::allocEventLock()
{
	EventMutex* pEvent = new EventMutex();
	if (pEvent) {
		bool err = false;
		if (pthread_mutex_init(&pEvent->mutex, NULL) == 0) {
			if (pthread_cond_init(&pEvent->cond, NULL) == 0) {
				// Do nothing.
			} else {
				pthread_mutex_destroy(&pEvent->mutex);
				err = true;
			}
		} else {
			err = true;
		}
		
		if (err) {
			delete pEvent;
			pEvent = NULL;
		}
	}
	return pEvent;
}

void CAndroidRequest::freeEventLock	(void* lock)
{
	EventMutex* pEvent = (EventMutex*)lock;
	if (pEvent) {
		pthread_mutex_destroy	(&pEvent->mutex);
		pthread_cond_destroy	(&pEvent->cond);		
	}
}

void CAndroidRequest::eventSleep(void* lock)
{
	EventMutex* pEvent = (EventMutex*)lock;
	if (pEvent) {
		// Own mutex [Lock]
		pthread_mutex_lock		(&pEvent->mutex);
		// [Unlock] and go to [Sleep], atomically.
		pthread_cond_wait		(&pEvent->cond, &pEvent->mutex);
		// [Lock] on wake up.
		
		// [Unlock] again.
		pthread_mutex_unlock	(&pEvent->mutex);
	}
}

void CAndroidRequest::eventWakeup(void* lock)
{
	EventMutex* pEvent = (EventMutex*)lock;
	if (pEvent) {
		// Own mutex [Lock]
		pthread_mutex_lock		(&pEvent->mutex);

		pthread_cond_broadcast	(&pEvent->cond);

		// [Unlock] again.
		pthread_mutex_unlock	(&pEvent->mutex);
	}
}

void CAndroidRequest::forbidSleep(bool is_forbidden)
{
	jvalue value;
    CAndroidRequest::getInstance()->callJavaMethod(NULL, value, "forbidSleep", 'V', "Z", is_forbidden);
}

float
CAndroidRequest::getDeviceScale()
{
	return 1.0f;
}

void
CAndroidRequest::quitGame()
{
	jvalue value;
	callJavaMethod(NULL, value, "quitGame", 'V', "");
}

IMovieInterface*
CAndroidRequest::createMoviePlayer(const char* url, int width, int height)
{
	ImplementationMovie* movie = new ImplementationMovie();
	if (!movie->init(url, width, height)) {
		delete movie;
		movie = NULL;
	}
	return movie;
}

void
CAndroidRequest::destroyMoviePlayer(IMovieInterface* player)
{
	delete player;
}

void
CAndroidRequest::clearCookies()
{
	jvalue value;
	callJavaMethod(NULL, value, "clearCookies", 'V', "");
}

void
CAndroidRequest::exitGame()
{
	jvalue value;
	callJavaMethod(NULL, value, "exitGame", 'V', "");
}

void
CAndroidRequest::copyToClipboard(const char* text)
{
	jvalue value;
	callJavaMethod(NULL, value, "copyToClipBoard", 'V', "S", text);
}

double
CAndroidRequest::getFreeMemorySize()
{
	jvalue value;
	callJavaMethod(NULL, value, "getFreeMemorySize", 'D', "");
	return value.d;
}

double
CAndroidRequest::getUsedMemorySize()
{
	jvalue value;
	callJavaMethod(NULL, value, "getUsedMemorySize", 'D', "");
	return value.d;
}

bool
CAndroidRequest::getSMode()
{
	jvalue value;
	char methodName[64];
	strcpy(methodName, "getOption");
	callJavaMethod(NULL, value, methodName, 'I', "");
	return value.i == 1;
}

void
CAndroidRequest::getDateTimeNow(char* buffer, int bufferSize)
{
	jvalue result;
	callJavaMethod(NULL, result, "getDateTimeNow", 'S', "");
	jstring string = static_cast<jstring>(result.l);
	const char* value = CJNI::getJNIEnv()->GetStringUTFChars(string, NULL);
	int length = strlen(value);
	if (length > bufferSize - 1) {
		length = bufferSize - 1;
	}
	strncpy(buffer, value, length);
	buffer[length] = '\0';
	CJNI::getJNIEnv()->ReleaseStringUTFChars(string, value);
}

double
CAndroidRequest::getUNIXTimeNow()
{
	jvalue value;
	callJavaMethod(NULL, value, "getUNIXTimeNow", 'D', "");
	return value.d;
}

void
CAndroidRequest::savePng2Album(const char* path)
{
	jvalue value;
	callJavaMethod(NULL, value, "savePng2Album", 'V', "S", path);
}

void
CAndroidRequest::setIdleTimerActivity(bool /* active */)
{
}

void
CAndroidRequest::setUserDefaults(const char* key, bool value)
{
	jvalue result;
	callJavaMethod(NULL, result, "setKeyChain", 'Z', "SSS",
				   key, key, value ? "TRUE" : "FALSE");
}

bool
CAndroidRequest::getUserDefaults(const char* key)
{
	jvalue result;
	callJavaMethod(NULL, result, "getKeyChain", 'S', "SS", key, key);
	JNIEnv* env = CJNI::getJNIEnv();
	jstring string = static_cast<jstring>(result.l);
	const char* value = env->GetStringUTFChars(string, NULL);
	bool enabled = value[0] == 'T';
	env->ReleaseStringUTFChars(string, value);
	return enabled;
}

void
CAndroidRequest::setUserDefaults(const char* key, const char* value)
{
	jvalue result;
	callJavaMethod(NULL, result, "setKeyChain", 'Z', "SSS", key, key, value);
}

void
CAndroidRequest::getUserDefaults(const char* key, char* buffer, int maxLength)
{
	jvalue result;
	callJavaMethod(NULL, result, "getKeyChain", 'S', "SS", key, key);
	JNIEnv* env = CJNI::getJNIEnv();
	jstring string = static_cast<jstring>(result.l);
	const char* value = env->GetStringUTFChars(string, NULL);
	if (value) {
		strncpy(buffer, value, maxLength);
	}
	env->ReleaseStringUTFChars(string, value);
}

//================================================
// FileIO
void*
CAndroidRequest::ifopen	(const char* name, const char* mode)
{
	return fopen(name, mode);
}

void
CAndroidRequest::ifclose(void* file)
{
	if (file)
	{
		fclose((FILE*)file);
	}
}

int
CAndroidRequest::ifseek(void* file, long int offset, int origin)
{
	return fseek((FILE*)file,offset,origin);
}

u32
CAndroidRequest::ifread(void* ptr, u32 size, u32 count, void* file )
{
	return fread(ptr, size, count, (FILE*)file);
}

u32
CAndroidRequest::ifwrite(const void * ptr, u32 size, u32 count, void* file)
{
	return fwrite(ptr, size, count, (FILE*)file);
}

int
CAndroidRequest::ifflush(void* file)
{
	return fflush((FILE*)file);
}

long int
CAndroidRequest::iftell	(void* file)
{
	return ftell((FILE*)file);
}

int
CAndroidRequest::irename(const char* oldName, const char* newName)
{
	const char* oldPath = getFullPath(oldName, NULL);
	const char* newPath = getFullPath(newName, NULL);
	int result = rename(oldPath, newPath);
	KLBDELETEA(oldPath);
	KLBDELETEA(newPath);
	return result;
}

bool
CAndroidRequest::icreateEmptyFile(const char* name)
{
	FILE* f = fopen(name,"a");
	if (f) {
		fclose(f);
		return true;
	}
	return false;
}

};
