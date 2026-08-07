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
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "CJNI.h"
#include "CAndroidPathConv.h"
#include "CPFInterface.h"
#include "CKLBGameApplication.h"
#include "CKLBUtility.h"
#include "PackageDefine.h"

bool KLBCreateDirectories(const char * path);

CKLBPathConv CKLBPathConv::m_instance;

CKLBPathConv::CKLBPathConv() : m_build(false), m_external(0), m_install(0) {}
CKLBPathConv::~CKLBPathConv() {
    delete [] m_external;
    delete [] m_install;
}

CKLBPathConv&
CKLBPathConv::getInstance()
{
    return m_instance;
}

const char *
CKLBPathConv::makePath(const char * path, const char * suffix, const char * base)
{
    size_t extlen = (suffix) ? strlen(suffix) : 0;
    size_t len = strlen(path) + strlen(base) + extlen + 2;
    char * buf = new char [ len ];
    strcpy(buf, base);
    //strcat(buf, "/");
    strcat(buf, path);
    if(suffix) strcat(buf, suffix);
    //DEBUG_PRINT("fullpath: %s", buf);
    return (const char *)buf;
}

bool
CKLBPathConv::checkExists(const char * path)
{
    if (!path) return false;
    bool bResult = true;
    struct stat st;
    int iRes = stat(path, &st);
    if((iRes != 0) && (errno == ENOENT)) bResult = false;
    return bResult;
}

void
CKLBPathConv::ensureExternalDirectory()
{
	if (!checkExists(m_external)) {
		KLBCreateDirectories(m_external);
	}
}

const char *
CKLBPathConv::fullpath(const char * url, const char * suffix, bool* isReadOnly)
{
    build();
	size_t urlLength = strlen(url);
	const char * assetDirPrefix = static_cast<CKLBGameApplication&>(CPFInterface::getInstance().client()).getAssetDirPrefix();

	// Default
	if( isReadOnly ) *isReadOnly = true;

    // assets が指定されている場合、まずは external から探し、
    // 見つからなければ install から探す。どちらもなければ 0 を返す。
    if (!strncmp(url, "asset://", 8)) {
		const char * installPath = makePath(url + 8, suffix, m_install);
		const char * externalPath = makePath(url + 8, suffix, m_external);

		if (!checkExists(installPath)) {
			delete [] installPath;
			if (checkExists(externalPath)) return externalPath;
			delete [] externalPath;
			return 0;
		}

		if (!checkExists(externalPath)) {
			delete [] externalPath;
			return installPath;
		}

		int externalVersion = 0;
		int installVersion = 0;
		if (CPFInterface::getInstance().platform().useEncryption()) {
			u8 header[6];
			installVersion = -1;
			externalVersion = -1;

			FILE * file = fopen(installPath, "rb");
			if (file) {
				fread(header, 1, sizeof(header), file);
				fclose(file);
				installVersion = (header[4] << 8) | header[5];
			}

			file = fopen(externalPath, "rb");
			if (file) {
				fread(header, 1, sizeof(header), file);
				fclose(file);
				externalVersion = (header[4] << 8) | header[5];
			}
		}

		if ((installVersion & externalVersion) == -1) {
			delete [] installPath;
			delete [] externalPath;
			return 0;
		}

		if (externalVersion >= installVersion) {
			delete [] installPath;
			if (isReadOnly) *isReadOnly = false;
			return externalPath;
		}

		delete [] externalPath;
		return installPath;
    }

	const char * path = url;
	if( !strncmp(url, "external/", 9) )
	{
		if( isReadOnly ) { *isReadOnly = false; }
		path = makePath(url + 9, suffix, m_external);
		size_t logicalPathLength = urlLength - 9;
		if (CKLBUtility::hasAssetDirPrefix(path, assetDirPrefix, logicalPathLength)) return path;

		const char * prefixedPath = CKLBUtility::insertAssetDirPrefix(path, assetDirPrefix, logicalPathLength);
		if (prefixedPath && checkExists(prefixedPath)) {
			delete [] path;
			return prefixedPath;
		}
		delete [] prefixedPath;
		return path;
    }
    if (!strncmp(url, "install/", 8)) {
		path = makePath(url + 8, suffix, m_install);
		return checkExists(path) ? path : 0;
	}
    return 0;
}

void
CKLBPathConv::create_external()
{
    // Java 側で生成した、file://external に相当するpathを初回のみ取得し、以後保持する。
	jclass cls_pfif = CJNI::getJNIEnv()->FindClass(JNI_LOAD_PATH);
	jfieldID id_external = CJNI::getJNIEnv()->GetStaticFieldID(cls_pfif, "m_path_external", "Ljava/lang/String;");
	jstring obj_external = (jstring)CJNI::getJNIEnv()->GetStaticObjectField(cls_pfif, id_external);
	const char* str1 = CJNI::getJNIEnv()->GetStringUTFChars(obj_external, 0);
	char * buf = new char [ strlen(str1) + 1 ];
	strcpy(buf, str1);
	m_external = (const char *)buf;

	CJNI::getJNIEnv()->DeleteLocalRef(cls_pfif);
	//CJNI::getJNIEnv()->DeleteLocalRef(id_external);
	CJNI::getJNIEnv()->DeleteLocalRef(obj_external);
}

void
CKLBPathConv::create_install()
{
    // Java 側で生成した、file://install に相当するpathを初回のみ取得し、以後保持する。
	jclass cls_pfif = CJNI::getJNIEnv()->FindClass(JNI_LOAD_PATH);
	jfieldID id_install = CJNI::getJNIEnv()->GetStaticFieldID(cls_pfif, "m_path_install", "Ljava/lang/String;");
	jstring obj_install = (jstring)CJNI::getJNIEnv()->GetStaticObjectField(cls_pfif, id_install);
	const char* str1 = CJNI::getJNIEnv()->GetStringUTFChars(obj_install, 0);
	char * buf = new char [ strlen(str1) + 1 ];
	strcpy(buf, str1);
	m_install = (const char *)buf;

	CJNI::getJNIEnv()->DeleteLocalRef(cls_pfif);
	//CJNI::getJNIEnv()->DeleteLocalRef(id_install);
	CJNI::getJNIEnv()->DeleteLocalRef(obj_install);
}

void
CKLBPathConv::build()
{
    if(m_build) return;
    create_install();
    create_external();
    m_build = true;
}
