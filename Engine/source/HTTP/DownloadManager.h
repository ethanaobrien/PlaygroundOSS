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
#ifndef DownloadManager_h
#define DownloadManager_h

#include "CKLBLuaTask.h"
#include "CKLBHTTPInterface.h"
#include "CUnZip.h"
#include <list>

class CKLBDownloadClient;
class CKLBSubThreadUnzip;

class CKLBDLTask
{
	friend class CKLBDownloadManager;
public:
	typedef void (CKLBDLTask::*Update)(s32 deltaT);

	CKLBDLTask(s32 queueId, const char* localPath, const char* url,
		const char* size, s32 timeout);
	~CKLBDLTask();
	void beginTask(s32 index);
	void update(s32 deltaT);
	void updateDownload(s32 deltaT);
	void updateUnzip(s32 deltaT);
	void releaseDownload();
	void beginUnzip(CKLBSubThreadUnzip** activeWorker);

	s32                 m_queueId;
	s32                 m_index;
	CKLBHTTPInterface*  m_http;
	const char*         m_url;
	const char*         m_localPath;
	s64                 m_size;
	s64                 m_received;
	s64                 m_lastReceived;
	s64                 m_timeout;
	s64                 m_stallTimer;
	s64                 m_elapsed;
	s32                 m_kbps;
	s32                 m_errorType;
	s32                 m_httpStatus;
	s32                 m_curlStatus;
	CKLBSubThreadUnzip* m_unzipWorker;
	Update              m_update;
};

class CKLBDownloadManager
{
	friend class CKLBDownloadClient;
	friend class CKLBDLTask;

private:
	static CKLBDownloadManager* s_instance;

	CKLBDownloadManager(CKLBDownloadClient* client);
	~CKLBDownloadManager();

	static CKLBDownloadManager* getInstance(CKLBDownloadClient* client);
	static CKLBDownloadManager* getInstance();
	void deleteInstance();
	void clearTasks();
	void CreateTasks_(CLuaState& lua, s32 timeout);
	void StartDownload_(CLuaState& lua);
	void RetryDownload_(CLuaState& lua);
	void ReUnzip_(CLuaState& lua);
	bool isFinished() const;
	s32 activeTaskCount() const;
	void insertTask(std::list<CKLBDLTask*>& tasks, CKLBDLTask* task);
	bool startUnzip(CKLBDLTask* task);
	void completeUnzip(CKLBDLTask* task);
	void completeDownload(s32 slot);
	bool finishCheck(CKLBDLTask* task);
	static void dequeueAndStart(CKLBDownloadManager* manager, s32 slot);
	void execute(u32 deltaT);

	CKLBDownloadClient*       m_client;
	std::list<CKLBDLTask*>    m_waitList;
	std::list<CKLBDLTask*>    m_doneList;
	CKLBDLTask**              m_tasks;
	s32                       m_taskCount;
	CKLBDLTask*               m_errorTask;
	CKLBSubThreadUnzip*       m_unzipWorker;
	s32                       m_minQueueId;
	s32                       m_totalCount;
	s32                       m_downloadedCount;
	s32                       m_unzippedCount;
	double                    m_downloadProgress;
	double                    m_unzipProgress;
};

/*
 * DownloadClient is a Lua task with three manager commands and seven callback
 * names.  Its constructor acquires the shared manager instance and initializes
 * the command member-function table.
 */
class CKLBDownloadClient : public CKLBLuaTask
{
private:
	typedef void (CKLBDownloadClient::*Command)(CLuaState& lua);

	CKLBDownloadClient();
	virtual ~CKLBDownloadClient();

	void StartDownload_(CLuaState& lua);
	void RetryDownload_(CLuaState& lua);
	void ReUnzip_(CLuaState& lua);

	CKLBDownloadManager* m_manager;
	bool                 m_finished;
	Command              m_commands[3];
	const char*          m_callbacks[7];
	friend class CKLBTaskFactory<CKLBDownloadClient>;

public:
	enum {
		CLASS_ID = 0x00280059
	};

	u32 getClassID() { return 0x00080000; }
	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);
	void execute(u32 deltaT);
	void die();

	void setFinished();
	void callback(s32 callbackIndex, s32 type, s32 index, s32 status,
		double speed, double progress);
};

#endif // DownloadManager_h
