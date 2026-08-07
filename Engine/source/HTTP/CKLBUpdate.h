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
#ifndef CKLBUpdate_h
#define CKLBUpdate_h

#include "CKLBLuaTask.h"
#include "CKLBHTTPInterface.h"
#include "CUnZip.h"
#include "ILuaFuncLib.h"
#include "MultithreadedNetwork.h"

/*!
* \class CKLBLuaLibUPDATE
* \brief Lua Lib Update Class
* 
* 
*/
class CKLBLuaLibUPDATE : public ILuaFuncLib
{
private:
	CKLBLuaLibUPDATE();
public:
	CKLBLuaLibUPDATE(DEFCONST * arrConstDef);
	virtual ~CKLBLuaLibUPDATE();

	void addLibrary();
private:
	static int luaUpdateHasLock		(lua_State * L);
	static int luaUpdateKillLock	(lua_State * L);
};

class CUpdateUnZip : public CUnZip
{
protected:
	bool afterExtract(const char * extract_path, bool isDirectory, size_t size);
public:
	CUpdateUnZip(const char * zipPath);
	virtual ~CUpdateUnZip();
};

/*!
* \class CKLBUpdate
* \brief Updater Task class.
* 
* CKLBUpdate is used to update files.
* It downloads it from a specified URL and unzips it.
* Specific callbacks can be registered to be called after different steps
* such as the end of the dowload step, the unzipping step or when the whole 
* process is over.
*/
class CKLBUpdate : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBUpdate>;
	friend class CKLBLuaLibASSET;
	friend void teardownActiveConnectionList();
	friend void teardownUpdateLists();
	friend CKLBUpdate* popActiveUpdate();
protected:
	CKLBUpdate(bool createConnection = true);
	virtual ~CKLBUpdate();
public:
	// Reported to the detailed error callback, so these cross into Lua.
	enum UpdateError {
		DOWNLOAD_FORBIDDEN		= -1,
		DOWNLOAD_INVALID_SIZE	= -2,
		DOWNLOAD_NODATA			= -3,
		DOWNLOAD_ERROR			= -4,
		UNZIP_ERROR				= -5
	};

	virtual u32  getClassID	();
	virtual bool initScript	(CLuaState& lua);

	void execute			(u32 deltaT);
	void die				();

	static bool lockExist	();
	static CKLBUpdate* createAssetDownload(
		const char* callback, const char* targetName, const char* url,
		const char* expectedSize, u32 timeout);
protected:
	static void startWaitingUpdates();
	bool initAssetDownload(
		const char* callback, const char* temporaryPath, const char* url,
		const char* expectedSize, u32 timeout);
	void exec_init_download	(u32 deltaT);
	void exec_download		(u32 deltaT);
	void exec_init_unzip	(u32 deltaT);
	void exec_unzip			(u32 deltaT);
	void exec_init_subthread_unzip(u32 deltaT);
	void exec_subthread_unzip(u32 deltaT);
	void exec_complete		(u32 deltaT);

	bool isUpdating			();
	void cleanUpdate		(const char* tmpFile);
	bool saveUpdate			();

protected:
	CKLBHTTPInterface	*	m_httpIF;
	CUpdateUnZip		*	m_unzip;
	CKLBSubThreadUnzip	*	m_subThreadUnzip;
	CKLBUpdate			*	m_nextUpdate;

	enum STEP {
		S_INIT_DL,		// ダウンロード初期化
		S_DOWNLOAD,		// ダウンロード中
		S_INIT_UNZIP,	// ZIP展開初期化
		S_UNZIP,		// ZIP展開中
		S_COMPLETE,		// Ensure that zip is fully unzipped
		S_INIT_SUBTHREAD_UNZIP,
		S_SUBTHREAD_UNZIP,
		S_FINISHED,
	};
	struct SDownloadProgress {
		float maximum;
		STEP step;
	};

	const char			*	m_callbackDL;
	const char			*	m_callbackZIP;
	const char			*	m_callbackFinish;
	const char			*	m_callbackError;
	
	const char			*	m_callbackDetailedError;
	const char			*	m_tmpPath;
	const char			*	m_zipURL;
	s64						m_zipSize;
	SDownloadProgress		m_progress;
	bool					m_extracting;	// 展開中
	bool					m_zipOnly;
	s32						m_httpStatusCode;
	s32						m_downloadIdleTime;
	s32						m_downloadTimeout;
	bool					m_downloadComplete;
	bool					m_unzipComplete;

	s64						m_dlSize;	// ダウンロード終了サイズ
	int						m_zipEntry;	// zip内のエントリ数
};

void teardownActiveConnectionList();
void teardownUpdateLists();
CKLBUpdate* popActiveUpdate();

/*!
* \class CKLBUpdateZip
* \brief Updater through Zip Task class.
* 
* CKLBUpdateZip works the same way as CKLBUpdate but only unzips a file already downloaded.
*/
class CKLBUpdateZip : public CKLBUpdate
{
public:
	CKLBUpdateZip();
	virtual ~CKLBUpdateZip();

public:
	virtual u32  getClassID();
	virtual bool initScript(CLuaState& lua);
};

#endif // CKLBUpdate_h
