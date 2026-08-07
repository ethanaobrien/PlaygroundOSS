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
#include "CKLBUpdate.h"
#include "CKLBLuaEnv.h"
#include "CKLBUtility.h"
#include "CKLBAsset.h"
#include "CKLBLuaLibASSET.h"
#include <stdio.h>

/**
	.lock file structure :
	Byte 0000..0999 : Tmp Zip file full path
	Byte 1000..2503 : Zip URL
	Byte 2504..2512 : Size.
	Byte 2513..2999 : UNUSED.
 */
static const char*	gUpdateFile = "file://external/_Upload_Task_Marker_.lock";

static ILuaFuncLib::DEFCONST defcmd[] = {
	{ "CKLBUPDATE_DOWNLOAD_FORBIDDEN",	CKLBUpdate::DOWNLOAD_FORBIDDEN },
	{ "CKLBUPDATE_DOWNLOAD_INVALID_SIZE",	CKLBUpdate::DOWNLOAD_INVALID_SIZE },
	{ "CKLBUPDATE_DOWNLOAD_NODATA",	CKLBUpdate::DOWNLOAD_NODATA },
	{ "CKLBUPDATE_DOWNLOAD_ERROR",	CKLBUpdate::DOWNLOAD_ERROR },
	{ "CKLBUPDATE_UNZIP_ERROR",	CKLBUpdate::UNZIP_ERROR },
	{ 0, 0 }
};
static CKLBLuaLibUPDATE libdef(defcmd);
CKLBLuaLibUPDATE::CKLBLuaLibUPDATE(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibUPDATE::~CKLBLuaLibUPDATE() {}
void 
CKLBLuaLibUPDATE::addLibrary()
{
	addFunction("ONLINE_hasLock",		CKLBLuaLibUPDATE::luaUpdateHasLock);
	addFunction("ONLINE_killLock",		CKLBLuaLibUPDATE::luaUpdateKillLock);
}
int
CKLBLuaLibUPDATE::luaUpdateHasLock(lua_State * L) 
{
	CLuaState lua(L);
	bool res = CKLBUpdate::lockExist();
	lua.retBool(res);
	return 1;
}
int
CKLBLuaLibUPDATE::luaUpdateKillLock(lua_State * L) 
{
	CLuaState lua(L);
	CPFInterface::getInstance().platform().removeTmpFile(gUpdateFile);
	return 0;
}

enum {
	UPDATE_HASLOCK			= 0,
	UPDATE_KILL_LOCK		= 1,
};

static IFactory::DEFCMD cmd[] = {
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUpdate>		factoryA("ONLINE_Update",			CLS_KLBAPPUPDATE,		cmd);
static CKLBTaskFactory<CKLBUpdateZip>	factoryB("ONLINE_CompleteZip",		CLS_KLBAPPUPDATEZIP,	cmd);

CKLBUpdate* s_waitingUpdates = NULL;
static CKLBUpdate* s_activeUpdates = NULL;
static s32 s_activeUpdateCount = 0;
u8 s_downloadsPaused = 0;

void
CKLBLuaLibASSET::pauseDownloads(bool paused)
{
	if(paused) {
		s_downloadsPaused = true;
	} else {
		s_downloadsPaused = false;
		do {
			CKLBUpdate* update = s_waitingUpdates;
			if(!update) {
				break;
			}
			s_waitingUpdates = update->m_nextUpdate;
			update->m_nextUpdate = s_activeUpdates;
			s_activeUpdates = update;
			update->regist(NULL, CKLBTask::P_NORMAL);
			++s_activeUpdateCount;
		} while(s_activeUpdateCount < 3);
	}
}

void
CKLBUpdate::startWaitingUpdates()
{
	do {
		CKLBUpdate* update = s_waitingUpdates;
		if(!update) {
			break;
		}
		s_waitingUpdates = update->m_nextUpdate;
		update->m_nextUpdate = s_activeUpdates;
		s_activeUpdates = update;
		update->regist(NULL, CKLBTask::P_NORMAL);
		++s_activeUpdateCount;
	} while(s_activeUpdateCount < 3);
}

void
teardownActiveConnectionList()
{
	CKLBUpdate* update = s_waitingUpdates;
	while(update) {
		CKLBUpdate* next = update->m_nextUpdate;
		KLBDELETE(update);
		update = next;
	}
}

void
teardownUpdateLists()
{
	CKLBUpdate* update = s_activeUpdates;
	while(update) {
		CKLBUpdate* next = update->m_nextUpdate;
		update->kill();
		update->m_progress.step = CKLBUpdate::S_FINISHED;
		update = next;
	}
	s_activeUpdates = NULL;
	s_activeUpdateCount = 0;

	update = s_waitingUpdates;
	while(update) {
		CKLBUpdate* next = update->m_nextUpdate;
		update->regist(NULL, CKLBTask::P_NORMAL);
		update->kill();
		update->m_progress.step = CKLBUpdate::S_FINISHED;
		update = next;
	}
	s_waitingUpdates = NULL;
}

enum {
	ARG_ZIPURL = 1,
	ARG_ZIPSIZE,

	ARG_TMPNAME,

	ARG_DOWNLOAD_CALLBACK,
	ARG_UNZIP_CALLBACK,
	ARG_FINISH_CALLBACK,
	ARG_ERROR_CALLBACK,
	ARG_OUT_PATH,

	ARG_REQUIRE = ARG_TMPNAME,
	ARG_NUM = ARG_OUT_PATH
};

enum {
	ARG_ZIPTMPNAME = 1,

	ARG_ZIPUNZIP_CALLBACK,
	ARG_ZIPFINISH_CALLBACK,
	ARG_ZIPERROR_CALLBACK,
	ARG_ZIPOUT_PATH,

	ARG_ZIPREQUIRE	= ARG_ZIPTMPNAME,
	ARG_ZIPNUM		= ARG_ZIPOUT_PATH
};


CUpdateUnZip::CUpdateUnZip(const char * zipPath) : CUnZip(zipPath) {}
CUpdateUnZip::~CUpdateUnZip() {}

bool
CUpdateUnZip::afterExtract(const char * extract_path, bool isDirectory, size_t size)
{
	if(extract_path && (size == 0) && !isDirectory) {
		// ディレクトリではないファイルがサイズ0の場合、そのファイルは削除対象となる。
		if(CPFInterface::getInstance().platform().removeTmpFile(extract_path) != 0) {
			CPFInterface::getInstance().platform().addExtMsg(
				"CUpdateUnZip:removeTmpFile", "failed", false);
		}
	}
	return true;
}

CKLBUpdateZip::CKLBUpdateZip()
:CKLBUpdate()
{
}

CKLBUpdateZip::~CKLBUpdateZip()
{
}

bool
CKLBUpdateZip::initScript(CLuaState& lua)
{
	// bool res = true;

	int argc = lua.numArgs();

	// 引数チェック
	if(argc < ARG_ZIPREQUIRE || argc > ARG_ZIPNUM) {
		return false;
	}

	const char * callbackUnzip		= (argc >= ARG_ZIPUNZIP_CALLBACK)   ? lua.getString(ARG_ZIPUNZIP_CALLBACK)  : NULL;
	const char * callbackFinish		= (argc >= ARG_ZIPFINISH_CALLBACK)  ? lua.getString(ARG_ZIPFINISH_CALLBACK) : NULL;
	const char * callbackError		= (argc >= ARG_ZIPERROR_CALLBACK)   ? lua.getString(ARG_ZIPERROR_CALLBACK)  : NULL;
	const char * callbackDetailedError = (argc >= ARG_ZIPOUT_PATH) ? lua.getString(ARG_ZIPOUT_PATH) : NULL;
	
	const char * tmp_name			= lua.getString(ARG_ZIPTMPNAME);
	m_tmpPath						= CKLBUtility::copyString(tmp_name);

	// Load "Update" info if any
	if (lockExist()) {
		m_progress.step = S_INIT_SUBTHREAD_UNZIP;

		m_zipEntry			= 0;
		m_callbackZIP		= CKLBUtility::copyString(callbackUnzip);
		m_callbackFinish	= CKLBUtility::copyString(callbackFinish);
		m_callbackError		= callbackError ? CKLBUtility::copyString(callbackError) : NULL;
		m_callbackDetailedError = callbackDetailedError ? CKLBUtility::copyString(callbackDetailedError) : NULL;
		m_downloadComplete	= true;

		return regist(NULL, P_NORMAL);
	} else {
		return false;
	}
}

u32 
CKLBUpdateZip::getClassID() 
{
	return CLS_KLBAPPUPDATEZIP;
}

CKLBUpdate::CKLBUpdate(bool createConnection)
: CKLBLuaTask   ()
{
	m_progress.step         = S_INIT_DL;
	m_zipOnly               = false;
	m_dlSize                = 0;
	m_zipEntry              = 0;
	m_httpIF                = NULL;
	m_unzip                 = NULL;
	m_subThreadUnzip        = NULL;
	m_zipURL                = NULL;
	m_zipSize               = 0;
	m_callbackDetailedError = NULL;
	m_tmpPath               = NULL;
	m_callbackFinish        = NULL;
	m_callbackError         = NULL;
	m_callbackDL            = NULL;
	m_callbackZIP           = NULL;
	m_httpStatusCode        = 0;
	m_downloadIdleTime      = 0;
	m_downloadTimeout       = 0;
	m_downloadComplete      = false;
	m_unzipComplete         = false;
	if(createConnection) {
		m_httpIF = NetworkManager::createConnection();
	}
}

CKLBUpdate*
CKLBUpdate::createAssetDownload(
	const char* callback, const char* targetName, const char* url,
	const char* expectedSize, u32 timeout)
{
	klb_assertNull(strlen(targetName) < 950, "Invalid length for dest file path");

	char temporaryPath[1000];
	if(strncmp(targetName, "asset://", 8) == 0) {
		targetName += 8;
		sprintf(temporaryPath, "file://external/%s_", targetName);
	} else {
		sprintf(temporaryPath, "%s_", targetName);
	}

	for(CKLBUpdate* update = s_waitingUpdates; update; update = update->m_nextUpdate) {
		if(strcmp(update->m_tmpPath, temporaryPath) == 0) {
			return update;
		}
	}
	for(CKLBUpdate* update = s_activeUpdates; update; update = update->m_nextUpdate) {
		if(strcmp(update->m_tmpPath, temporaryPath) == 0) {
			return update;
		}
	}

	CKLBUpdate* update = KLBNEWC(CKLBUpdate, (false));
	CKLBUpdate* result = NULL;
	if(update) {
		bool initialized = update->initAssetDownload(
			callback, temporaryPath, url, expectedSize, timeout);
		result = update;
		if(!initialized) {
			KLBDELETE(update);
			result = NULL;
		}
	}
	return result;
}

bool
CKLBUpdate::initAssetDownload(
	const char* callback, const char* temporaryPath, const char* url,
	const char* expectedSize, u32 timeout)
{
	m_tmpPath = CKLBUtility::copyString(temporaryPath);
	m_zipURL = CKLBUtility::copyString(url);
	m_zipSize = expectedSize ? CKLBUtility::stringNum64(expectedSize) : -1;
	m_progress.step = S_INIT_DL;
	m_zipOnly = true;
	m_dlSize = -1;
	m_zipEntry = 0;
	m_downloadTimeout = timeout;
	if(callback) {
		m_callbackFinish = CKLBUtility::copyString(callback);
	}

	if(s_activeUpdateCount <= 2) {
		u8 paused = s_downloadsPaused;
		if(paused) {
			// Paused downloads remain queued until resume.
		} else {
			++s_activeUpdateCount;
			m_nextUpdate = s_activeUpdates;
			s_activeUpdates = this;
			return regist(NULL, P_NORMAL);
		}
	}

	m_nextUpdate = s_waitingUpdates;
	s_waitingUpdates = this;
	return true;
}

CKLBUpdate::~CKLBUpdate() {
	if (m_httpIF) {
		NetworkManager::releaseConnection(m_httpIF);
	}
	m_httpIF = NULL;
}

u32 
CKLBUpdate::getClassID() 
{
	return CLS_KLBAPPUPDATE;
}

/*static*/
bool 
CKLBUpdate::lockExist() 
{
	IPlatformRequest& ptf = CPFInterface::getInstance().platform();
	IReadStream* pStream = ptf.openReadStream(gUpdateFile, false, 0);
	// Does the file exist ?
	if (pStream->getStatus() == IReadStream::NORMAL) {
		delete pStream; // Do not use KLBDELETE : object created by porting layer.
		return true;
	}
	delete pStream;
	return false;
}

void 
CKLBUpdate::cleanUpdate(const char* tmpFile) 
{
	// - DELETE Tmp Zip file if exist
	CPFInterface::getInstance().platform().removeTmpFile(tmpFile);
}

bool 
CKLBUpdate::saveUpdate() 
{
	if (m_zipOnly) {
		return true;
	}
	// - Create Tmp Operation file with all data inside
	ITmpFile* file	= CPFInterface::getInstance().platform().openTmpFile(gUpdateFile);
	if (file) {
		// Close file
		delete file;
		return true;
	} else {
		return false;
	}
}

/* No Command for now.
int
CKLBUpdate::commandScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 2) {
		lua.retBoolean(false);
		return 1;
	}

	int cmd = lua.getInt(2);
	switch(cmd)
	{
	}
}*/

bool
CKLBUpdate::initScript(CLuaState& lua)
{
	// bool res = true;

	int argc = lua.numArgs();

	// 引数チェック
	if(argc < ARG_REQUIRE || argc > ARG_NUM) {
		return false;
	}

	const char * callbackDownload	= (argc >= ARG_DOWNLOAD_CALLBACK)	? lua.getString(ARG_DOWNLOAD_CALLBACK)	: NULL;
	const char * callbackUnzip		= (argc >= ARG_UNZIP_CALLBACK)		? lua.getString(ARG_UNZIP_CALLBACK)		: NULL;
	const char * callbackFinish		= (argc >= ARG_FINISH_CALLBACK)		? lua.getString(ARG_FINISH_CALLBACK)	: NULL;
	const char * callbackError		= (argc >= ARG_ERROR_CALLBACK)		? lua.getString(ARG_ERROR_CALLBACK)		: NULL;
	const char * callbackDetailedError = (argc >= ARG_OUT_PATH) ? lua.getString(ARG_OUT_PATH) : NULL;
	
	const char * zip_url;
	const char * zip_size;
	const char * tmp_name;
	zip_url				= lua.getString(ARG_ZIPURL);
	zip_size			= lua.getString(ARG_ZIPSIZE);	// サイズは狂いがあると困るのでstringで受ける
	tmp_name			= lua.getString(ARG_TMPNAME);

	m_tmpPath			= CKLBUtility::copyString(tmp_name);
	m_zipURL			= CKLBUtility::copyString(zip_url);
	m_zipSize			= CKLBUtility::stringNum64(zip_size);

	m_progress.step	= S_INIT_DL;
	// Start from scratch and download
	m_dlSize			= -1;
	m_zipEntry			= 0;
	m_callbackDL		= CKLBUtility::copyString(callbackDownload);
	m_callbackZIP		= CKLBUtility::copyString(callbackUnzip);
	m_callbackFinish	= CKLBUtility::copyString(callbackFinish);
	m_callbackError		= callbackError ? CKLBUtility::copyString(callbackError) : NULL;
	m_callbackDetailedError = callbackDetailedError ? CKLBUtility::copyString(callbackDetailedError) : NULL;

	return regist(NULL, P_NORMAL);
}

void
CKLBUpdate::execute(u32 deltaT)
{
	switch(m_progress.step)
	{
	case S_INIT_DL:		exec_init_download(deltaT); break;
	case S_DOWNLOAD:	exec_download(deltaT);		break;
	case S_INIT_UNZIP:	exec_init_unzip(deltaT);	break;
	case S_UNZIP:		exec_unzip(deltaT);		break;
	case S_COMPLETE:	exec_complete(deltaT);		break;
	case S_INIT_SUBTHREAD_UNZIP: exec_init_subthread_unzip(deltaT); break;
	case S_SUBTHREAD_UNZIP: exec_subthread_unzip(deltaT); break;
	}
}

void
CKLBUpdate::die()
{
	KLBDELETEA(m_zipURL);
	KLBDELETEA(m_tmpPath);
	KLBDELETEA(m_callbackZIP);
	KLBDELETEA(m_callbackDL);
	KLBDELETEA(m_callbackFinish);
	KLBDELETEA(m_callbackError);
	KLBDELETEA(m_callbackDetailedError);

	KLBDELETE(m_unzip);
	KLBDELETE(m_subThreadUnzip);
}

void
CKLBUpdate::exec_init_download(u32 /*deltaT*/)
{
	if (!m_httpIF) {
		m_httpIF = NetworkManager::createConnection();
	}

	char connectionBuffer[100];
	const char* connectionName;
	sprintf(connectionBuffer, "CKLBUpdate/%d", 0);
	if(CKLBHTTPInterface::isConnectionAvailable(connectionBuffer)) {
		connectionName = connectionBuffer;
	} else {
		sprintf(connectionBuffer, "CKLBUpdate/%d", 1);
		if(CKLBHTTPInterface::isConnectionAvailable(connectionBuffer)) {
			connectionName = connectionBuffer;
		} else {
			sprintf(connectionBuffer, "CKLBUpdate/%d", 2);
			connectionName = CKLBHTTPInterface::isConnectionAvailable(connectionBuffer)
				? connectionBuffer
				: "CKLBUpdate";
		}
	}

	m_httpIF->reuse();
	klb_assert(m_httpIF->setDownload(m_tmpPath),
			   // The request owns this output path until the transfer completes.
			   "CKLBHTTPInterface::setDownload failure");
	m_httpIF->httpGET(m_zipURL, false, connectionName);	// zip取得のrequestを投げる
	SDownloadProgress progress = { -1.0f, S_DOWNLOAD };
	m_progress = progress; // Force first callback when set to 0.0f
	m_downloadIdleTime = 0;
}

void
CKLBUpdate::exec_download(u32 deltaT)
{
	bool completed = m_httpIF->isDataComplete();
	s64 size = m_httpIF->getDwnldSize();

	if(size != m_dlSize) {
		m_dlSize = size;	// 読み込み済サイズを更新
		m_downloadIdleTime = 0;
		if(m_callbackDL) {
			float progress = (m_dlSize * 1000 / m_zipSize) / 1000.0f;
			if(progress < 0.0f) {
				progress = 0.0f;
			} else if(progress >= 0.999f) {
				progress = 0.999f;
			}

			char buf[64];
			CKLBUtility::numString64(buf, (u64)(m_zipSize * progress));
			if(progress > m_progress.maximum) {
				m_progress.maximum = progress;
				if(!completed) {
					CKLBScriptEnv::getInstance().call_eventUpdateDownload(m_callbackDL, this, (double)progress, buf);
				}
			}
		}
	}

	if(!completed) {
		m_downloadIdleTime += deltaT;
		if(m_downloadTimeout &&
		   (m_downloadIdleTime >= m_downloadTimeout) &&
		   (m_progress.step == S_DOWNLOAD)) {
			m_httpStatusCode = 500;
			m_progress.step = S_COMPLETE;
		}
		return;
	}

	int httpState = m_httpIF->getHttpState();
	int httpStatus = m_httpIF->getHttpStatus();
	bool success = (httpState == 0) && (httpStatus == 200);

	if(m_zipOnly) {
		if(success) {
			m_downloadComplete = true;
		}
		m_httpStatusCode = httpStatus;
		m_progress.step = S_COMPLETE;
		return;
	}

	if(success && (size == m_zipSize)) {
		char buf[64];
		CKLBUtility::numString64(buf, size);
		CKLBScriptEnv::getInstance().call_eventUpdateDownload(m_callbackDL, this, 1.0, buf);
		saveUpdate();
		m_downloadComplete = true;
		m_progress.step = S_INIT_SUBTHREAD_UNZIP;
		return;
	}

	if(m_callbackDetailedError) {
		if((size != m_zipSize) && (httpStatus == 200)) {
			CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackDetailedError, this, DOWNLOAD_INVALID_SIZE, 200, httpState);
		} else if((httpState == 2) && (httpStatus == 500)) {
			CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackDetailedError, this, DOWNLOAD_NODATA, 500, 2);
		} else {
			CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackDetailedError, this, DOWNLOAD_ERROR, httpStatus, httpState);
		}
		m_progress.step = S_COMPLETE;
	} else {
		CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackError, this);
		m_progress.step = S_INIT_DL;
	}
}

void
CKLBUpdate::exec_init_unzip(u32 /*deltaT*/)
{
	const char * fullpath = CPFInterface::getInstance().platform().getFullPath(m_tmpPath);
	m_unzip = KLBNEWC(CUpdateUnZip, (fullpath));

	if (!m_unzip->getStatus()) {	// invalid zip file
		CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackError, this);
		// do not change m_eStep, thus it will retry again as the download step do
		return;
	}

	m_zipEntry   = m_unzip->numEntry();	// あらかじめエントリ数を取得しておく
	m_extracting = false;
	m_progress.step = S_UNZIP;

}

void
CKLBUpdate::exec_unzip(u32 /*deltaT*/)
{
	bool hasNext = true;
	bool extractSucceeded;
	if(m_extracting) {
		if(m_unzip->isFinishExtract(&extractSucceeded)) {
			// 戻り値が true であれば読み込みが終わっている
			m_extracting = false;
			if(extractSucceeded) {
				hasNext = m_unzip->gotoNextFile();	// 次のファイルへ

				// 現在展開済みのファイル数を得る
				int finished = m_unzip->getFinishedEntry();
				CKLBScriptEnv::getInstance().call_eventUpdateZIP(m_callbackZIP, this, finished, m_zipEntry);
			} else {
				CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackError, this);
				hasNext = false;
			}

			if(!hasNext) {
				// 展開終了
				KLBDELETE(m_unzip);
				m_unzip = NULL;
				// テンポラリzip削除
				CPFInterface::getInstance().platform().removeTmpFile(gUpdateFile);
				CPFInterface::getInstance().platform().removeTmpFile(m_tmpPath);
				m_progress.step = S_COMPLETE;
				m_unzipComplete = true;
			}
		}
	}
	// 次のファイルがあり、なおかつ展開処理が終了していれば次のファイルの読み込みを開始する
	if(hasNext && !m_extracting) {
		if(m_unzip->readCurrentFileInfo()) {
			m_unzip->extractCurrentFile("file://external/");
			m_extracting = true;
		}
	}
}

void
CKLBUpdate::exec_init_subthread_unzip(u32 /*deltaT*/)
{
	const char * fullpath = CPFInterface::getInstance().platform().getFullPath(m_tmpPath);
	CKLBSubThreadUnzip * unzip = KLBNEWC(CKLBSubThreadUnzip, (fullpath));
	m_subThreadUnzip = unzip;
	m_progress.step = S_SUBTHREAD_UNZIP;

	bool started = unzip->unCompress("file://external/");
	CKLBScriptEnv::getInstance().call_eventUpdateZIP(m_callbackZIP, this, 0, 1);
	if(!started) {
		const char * errorCallback = m_callbackDetailedError;
		if(errorCallback) {
			s32 errorStatus = m_subThreadUnzip->getErrorStatus();
			CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackDetailedError, this, -5, errorStatus, 0);
			CPFInterface::getInstance().platform().removeTmpFile(gUpdateFile);
			m_progress.step = S_COMPLETE;
		}
	}
}

void
CKLBUpdate::exec_subthread_unzip(u32 /*deltaT*/)
{
	if(m_subThreadUnzip->isFinishExtract()) {
		if(m_subThreadUnzip->hasError()) {
			const char * errorCallback = m_callbackDetailedError;
			if(errorCallback) {
				s32 errorStatus = m_subThreadUnzip->getErrorStatus();
				CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackDetailedError, this, -5, errorStatus, 0);
			} else {
				CKLBScriptEnv::getInstance().call_eventUpdateError(m_callbackError, this);
			}
		} else {
			m_unzipComplete = true;
			m_zipEntry = m_subThreadUnzip->numEntry();
			CKLBScriptEnv::getInstance().call_eventUpdateZIP(m_callbackZIP, this, m_zipEntry, m_zipEntry);
		}

		KLBDELETE(m_subThreadUnzip);
		m_subThreadUnzip = NULL;
		CPFInterface::getInstance().platform().removeTmpFile(gUpdateFile);
		CPFInterface::getInstance().platform().removeTmpFile(m_tmpPath);
		m_progress.step = S_COMPLETE;
	} else if(m_subThreadUnzip->getStatus()) {
		m_zipEntry = m_subThreadUnzip->numEntry();
		int finished = m_subThreadUnzip->getFinishedEntry();
		CKLBScriptEnv::getInstance().call_eventUpdateZIP(m_callbackZIP, this, finished, m_zipEntry);
	}
}

bool
CKLBUpdate::isUpdating()
{
	if(strncmp(&m_tmpPath[strlen(m_tmpPath) - 5], ".texb", 5)) {
		return true;
	}

	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	IReadStream* stream = platform.openReadStream(m_tmpPath, platform.useEncryption(), 14);
	bool valid = false;
	if(stream->getStatus() == IReadStream::NORMAL) {
		valid = (stream->readU32() == CHUNK_TAG('T', 'E', 'X', 'B'));
	}
	delete stream;
	return valid;
}

void
CKLBUpdate::exec_complete(u32 /*deltaT*/)
{
	if(m_zipOnly) {
		if(s_downloadsPaused) {
			return;
		}

		if(m_downloadComplete) {
			// Complete-zip updates use a trailing marker character while the
			// archive is being produced. Preserve the source path, strip that
			// marker from the owned destination path, and publish atomically.
			size_t pathLength = strlen(m_tmpPath);
			const s32 signedPathLength = static_cast<s32>(pathLength);
			char sourcePath[1000];
			memcpy(sourcePath, m_tmpPath,
				static_cast<size_t>(signedPathLength + 1));

			char* destinationPath = const_cast<char*>(m_tmpPath);
			destinationPath[signedPathLength - 1] = 0;

			if(CPFInterface::getInstance().platform().irename(
				sourcePath, m_tmpPath) != 0) {
				m_downloadComplete = false;
				CPFInterface::getInstance().platform().addExtMsg(
					"CUpdateUnZip:irename", "failed", false);
			} else {
				sprintf(sourcePath, "asset://%s",
					m_tmpPath + strlen("file://external/"));
				pathLength = strlen(sourcePath);
			}

			// A malformed generated texture must not replace the last valid
			// external asset.
			if(!isUpdating()) {
				CPFInterface::getInstance().platform().removeTmpFile(
					m_tmpPath);
			} else if(strncmp(".texb",
				sourcePath + static_cast<s32>(pathLength - 5), 5) == 0) {
				CKLBAssetManager::getInstance().reloadAssetByFileName(
					sourcePath);
			}
		}

		CKLBUpdate* previous = NULL;
		CKLBUpdate* active = s_activeUpdates;
		while(active) {
			if(active == this) {
				if(previous) {
					previous->m_nextUpdate = m_nextUpdate;
				} else {
					s_activeUpdates = m_nextUpdate;
				}
				break;
			}
			previous = active;
			active = active->m_nextUpdate;
		}

		CKLBScriptEnv::getInstance().call_eventMdlFinish(
			m_callbackFinish, this, m_tmpPath, m_zipURL,
			m_downloadComplete, m_httpStatusCode);
		--s_activeUpdateCount;
		startWaitingUpdates();
		kill();
		return;
	}

	if(m_downloadComplete && m_unzipComplete) {
		CKLBScriptEnv::getInstance().call_eventUpdateComplete(
			m_callbackFinish, this);
	}

	kill();
}

CKLBUpdate*
popActiveUpdate()
{
	// Detaches the head of the waiting download list and hands it back; the
	// caller owns the update it just removed.
	CKLBUpdate* update = s_waitingUpdates;
	if(update) {
		s_waitingUpdates = update->m_nextUpdate;
	}
	return update;
}
