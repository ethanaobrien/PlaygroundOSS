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
#include "DownloadManager.h"
#include "CKLBUtility.h"
#include "MultithreadedNetwork.h"
#include "assert_klb.h"
#include <cstring>
#include <new>
#include <sstream>
#include <string>

namespace {
	enum DownloadCallback {
		DL_DOWNLOAD_FINISHED = 0,
		DL_UNZIP_STARTED     = 1,
		DL_UNZIP_FINISHED    = 2,
		DL_PROGRESS          = 3,
		DL_ALL_FINISHED      = 4,
		DL_ERROR             = 5,
		DL_SPEED             = 6
	};

	enum DownloadError {
		DL_SIZE_MISMATCH = -2,
		DL_NO_DATA       = -3,
		DL_HTTP_ERROR    = -4,
		DL_UNZIP_ERROR   = -5,
		DL_TIMEOUT       = -6
	};
}

CKLBDownloadManager* CKLBDownloadManager::s_instance = NULL;

CKLBDownloadManager::CKLBDownloadManager(CKLBDownloadClient* client)
: m_client(client)
, m_tasks(NULL)
, m_errorTask(NULL)
, m_unzipWorker(NULL)
, m_minQueueId(1)
, m_totalCount(0)
, m_downloadedCount(0)
, m_unzippedCount(0)
, m_downloadProgress(0.0)
, m_unzipProgress(0.0)
{
}

CKLBDownloadManager::~CKLBDownloadManager()
{
	KLBDELETEA(m_tasks);
}

CKLBDownloadManager*
CKLBDownloadManager::getInstance(CKLBDownloadClient* client)
{
	if (!s_instance) {
		s_instance = KLBNEWC(CKLBDownloadManager, (client));
	} else {
		klb_assertNull(false, "multi-DownloadClient is NOT support !!");
	}
	return s_instance;
}

CKLBDownloadManager*
CKLBDownloadManager::getInstance()
{
	klb_assertNull(s_instance, "DownloadManager is not created ! ");
	return s_instance;
}

void
CKLBDownloadManager::deleteInstance()
{
	CKLBDownloadManager* instance = s_instance;
	if (instance) {
		delete instance;
	}
	s_instance = NULL;
}

void
CKLBDownloadManager::dequeueAndStart(CKLBDownloadManager* manager, s32 slot)
{
	if (!manager->m_waitList.empty()) {
		CKLBDLTask* task = manager->m_waitList.front();
		manager->m_waitList.pop_front();
		manager->m_tasks[slot] = task;
		task->beginTask(slot);
	}
}

void
CKLBDownloadManager::RetryDownload_(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() > 2,
		"CKLBDownloadClient : Invalid arguments for RetryDownload_");

	s32 taskCount = lua.getInt(3);
	klb_assertNull(taskCount > 0, "need at least 1 pipe for download");

	if (m_taskCount != taskCount) {
		KLBDELETEA(m_tasks);
		m_taskCount = taskCount;
		m_tasks = KLBNEWA(CKLBDLTask*, taskCount);
	}

	for (s32 i = 0; i < m_taskCount; ++i) {
		m_tasks[i] = NULL;
		if (!m_waitList.empty()) {
			CKLBDLTask* task = m_waitList.front();
			m_waitList.pop_front();
			m_tasks[i] = task;
			task->beginTask(i);
		}
	}
}

void
CKLBDownloadManager::ReUnzip_(CLuaState& lua)
{
	CreateTasks_(lua, 3000);
	klb_assertNull(!m_doneList.empty(), "Error in reunzip");
	m_minQueueId = m_doneList.front()->m_queueId;
}

bool
CKLBDownloadManager::isFinished() const
{
	if (!m_doneList.empty() || !m_waitList.empty()) {
		return false;
	}
	return activeTaskCount() == 0;
}

s32
CKLBDownloadManager::activeTaskCount() const
{
	s32 count = m_unzipWorker ? 1 : 0;
	if (m_tasks) {
		for (s32 index = 0; index < m_taskCount; ++index) {
			if (m_tasks[index]) {
				++count;
			}
		}
	}
	return count;
}

void
CKLBDownloadManager::insertTask(
	std::list<CKLBDLTask*>& tasks, CKLBDLTask* task)
{
	if (tasks.empty()) {
		tasks.push_back(task);
		return;
	}
	for (std::list<CKLBDLTask*>::iterator position = tasks.begin();
		 position != tasks.end(); ++position) {
		if ((*position)->m_queueId > task->m_queueId) {
			tasks.insert(position, task);
			return;
		}
	}
	tasks.push_back(task);
}

void
CKLBDLTask::beginTask(s32 index)
{
	m_stallTimer = 0;
	m_elapsed    = 0;

	std::stringstream connectionName("CKLBUpdate");
	m_index = index;
	connectionName << index;

	m_http = NetworkManager::createConnection();
	m_http->reuse();
	klb_assert(m_http->setDownload(m_localPath),
		"CKLBHTTPInterface::setDownload failure");
	m_http->httpGET(m_url, false, connectionName.str().c_str());
	m_update = &CKLBDLTask::updateDownload;
}

bool
CKLBDownloadManager::startUnzip(CKLBDLTask* task)
{
	klb_assertNull(!m_unzipWorker, "multi-unzip not supported");
	if (m_errorTask || task->m_queueId != m_minQueueId) {
		return false;
	}
	m_client->callback(DL_UNZIP_STARTED, task->m_queueId, 0, 0, 0.0, 0.0);
	task->beginUnzip(&m_unzipWorker);
	return true;
}

bool
CKLBDownloadManager::finishCheck(CKLBDLTask* task)
{
	CKLBDLTask* error = m_errorTask;
	if (!error || error->m_errorType != DL_UNZIP_ERROR) {
		if (task->m_errorType) {
			m_errorTask = task;
			error = task;
		} else if (!error) {
			return false;
		}
	}

	if (activeTaskCount()) {
		return true;
	}

	m_client->callback(
		DL_ERROR, error->m_errorType, error->m_httpStatus, error->m_curlStatus,
		0.0, 0.0);
	m_client->setFinished();
	m_errorTask = NULL;
	return true;
}

void
CKLBDownloadManager::completeUnzip(CKLBDLTask* task)
{
	m_unzipWorker = NULL;
	m_doneList.pop_front();

	if (task->m_errorType) {
		finishCheck(task);
		insertTask(m_waitList, task);
		return;
	}

	m_client->callback(DL_UNZIP_FINISHED, task->m_queueId, 0, 0, 0.0, 0.0);
	++m_unzippedCount;
	++m_minQueueId;
	if (!finishCheck(task) && isFinished()) {
		m_client->callback(DL_ALL_FINISHED, 0, 0, 0, 0.0, 0.0);
		KLBDELETEA(m_tasks);
		m_tasks = NULL;
	}
	KLBDELETE(task);
}

void
CKLBDownloadManager::completeDownload(s32 slot)
{
	CKLBDLTask* task = m_tasks[slot];
	m_tasks[slot] = NULL;
	task->releaseDownload();

	if (task->m_errorType) {
		insertTask(m_waitList, task);
	} else {
		m_client->callback(
			DL_SPEED, task->m_index, task->m_kbps, 0, 0.0, 0.0);
		m_client->callback(
			DL_DOWNLOAD_FINISHED,
			task->m_queueId, task->m_index, 0, 0.0, 0.0);
		insertTask(m_doneList, task);
		++m_downloadedCount;
	}

	if (finishCheck(task) || m_waitList.empty()) {
		return;
	}

	CKLBDLTask* next = m_waitList.front();
	m_waitList.pop_front();
	m_tasks[slot] = next;
	next->beginTask(slot);
}

void
CKLBDownloadManager::execute(u32 deltaT)
{
	double unzipProgress = 0.0;
	if (!m_doneList.empty()) {
		CKLBDLTask* unzipTask = m_doneList.front();
		if (m_unzipWorker) {
			double progress =
				(double)m_unzipWorker->getFinishedEntry()
				/ (double)m_unzipWorker->numEntry();
			if (progress >= 1.0) {
				progress = 0.0;
			}
			unzipProgress = progress;
			unzipTask->update(deltaT);
		} else if (!m_errorTask && unzipTask->m_queueId == m_minQueueId) {
			m_client->callback(
				DL_UNZIP_STARTED,
				unzipTask->m_queueId, 0, 0, 0.0, 0.0);
			unzipTask->beginUnzip(&m_unzipWorker);
		}
	}

	double unzipTotal = (double)m_unzippedCount + unzipProgress;
	double reportedUnzipProgress = m_unzipProgress;
	if (unzipTotal > reportedUnzipProgress) {
		m_unzipProgress = unzipTotal;
		reportedUnzipProgress = unzipTotal;
	}

	double downloadProgress = 0.0;
	if (m_tasks) {
		for (s32 index = 0; index < m_taskCount; ++index) {
			CKLBDLTask* task = m_tasks[index];
			if (!task) {
				continue;
			}
			double progress = 0.0;
			if (task->m_size > 0) {
				progress =
					(double)task->m_received / (double)task->m_size;
			}
			downloadProgress +=
				(progress >= 1.0) ? 0.0 : progress;
			task->update(deltaT);
		}
	}

	double downloadTotal =
		(double)m_downloadedCount + downloadProgress;
	double reportedDownloadProgress = m_downloadProgress;
	if (downloadTotal > reportedDownloadProgress) {
		m_downloadProgress = downloadTotal;
		reportedDownloadProgress = downloadTotal;
	}
	m_client->callback(
		DL_PROGRESS, m_totalCount, m_downloadedCount, m_unzippedCount,
		reportedDownloadProgress, reportedUnzipProgress);
}

void
CKLBDownloadManager::StartDownload_(CLuaState& lua)
{
	m_downloadProgress = 0.0;
	m_unzipProgress = 0.0;
	m_totalCount = 0;
	m_downloadedCount = 0;
	m_unzippedCount = 0;

	s32 argumentCount = lua.numArgs();
	klb_assertNull(argumentCount > 3,
		"CKLBDownloadClient : Invalid arguments for StartDownload_");

	m_taskCount = lua.getInt(3);
	klb_assertNull(m_taskCount > 0, "need at least 1 pipe for download");

	s32 timeout = 30000;
	if (argumentCount >= 5) {
		timeout = lua.getInt(5);
	}
	CreateTasks_(lua, timeout);

	m_tasks = KLBNEWA(CKLBDLTask*, m_taskCount);
	for (s32 i = 0; i < m_taskCount; ++i) {
		m_tasks[i] = NULL;
		if (!m_waitList.empty()) {
			CKLBDLTask* task = m_waitList.front();
			m_waitList.pop_front();
			m_tasks[i] = task;
			task->beginTask(i);
		}
	}
}

CKLBDLTask::CKLBDLTask(s32 queueId, const char* localPath, const char* url,
	const char* size, s32 timeout)
: m_queueId(queueId)
, m_index(-1)
, m_http(NULL)
, m_url(NULL)
, m_localPath(NULL)
, m_size(0)
, m_received(-1)
, m_lastReceived(-1)
, m_stallTimer(0)
, m_elapsed(0)
, m_kbps(0)
, m_errorType(0)
, m_httpStatus(200)
, m_curlStatus(0)
, m_unzipWorker(NULL)
, m_update(NULL)
{
	m_localPath = CKLBUtility::copyString(localPath);
	m_url = CKLBUtility::copyString(url);
	m_size = size ? CKLBUtility::stringNum64(size) : -1;
	m_timeout = timeout;
}

void
CKLBDownloadManager::CreateTasks_(CLuaState& lua, s32 timeout)
{
	klb_assertNull(lua.getType(4) == LUA_TTABLE,
		"CKLBDownloadManager::CreateTasks_ : BAD table format.");

	lua.retValue(4);
	lua.retNil();
	std::stringstream pathStream("");
	m_minQueueId = 0x7fffffff;

	while (lua.tableNext(-2)) {
		lua.retValue(-2);
		klb_assertNull(lua.getType(-2) == LUA_TTABLE,
			"[CKLBMultiUpdate::initScript] BAD table format.");
		lua.retValue(-2);
		lua.retNil();

		lua.tableNext(-2);
		s32 status = lua.getInt(-1);
		lua.pop(1);

		lua.tableNext(-2);
		const char* queue = lua.getString(-1);
		lua.pop(1);
		s32 queueId = static_cast<s32>(CKLBUtility::stringNum64(queue));

		m_minQueueId =
			(m_minQueueId > queueId) ? queueId : m_minQueueId;
		pathStream << "file://external/tmpDL/" << queueId << ".zip";

		lua.tableNext(-2);
		const char* url = lua.getString(-1);
		lua.pop(1);

		lua.tableNext(-2);
		const char* size = lua.getString(-1);
		lua.pop(1);

		CKLBDLTask* task = KLBNEWC(CKLBDLTask,
			(queueId, pathStream.str().c_str(), url, size, timeout));

		std::list<CKLBDLTask*>& tasks =
			(status == 0) ? m_waitList : m_doneList;
		tasks.push_back(task);

		pathStream.str("");
		pathStream.clear();
		lua.pop(4);
		++m_totalCount;
	}
	lua.pop(1);
	klb_assertNull(m_minQueueId != 0x7fffffff,
		"startDownload list is empty !");
}

CKLBDLTask::~CKLBDLTask()
{
	KLBDELETEA(m_localPath);
	KLBDELETEA(m_url);
}

void
CKLBDLTask::update(s32 deltaT)
{
	if (m_update) {
		(this->*m_update)(deltaT);
	}
}

void
CKLBDLTask::releaseDownload()
{
	NetworkManager::releaseConnection(m_http);
	m_http = NULL;
	m_update = NULL;
}

void
CKLBDLTask::beginUnzip(CKLBSubThreadUnzip** activeWorker)
{
	const char* archivePath =
		CPFInterface::getInstance().platform().getFullPath(m_localPath, NULL);
	m_unzipWorker = KLBNEWC(CKLBSubThreadUnzip, (archivePath));
	*activeWorker = m_unzipWorker;
	if (m_unzipWorker->unCompress("file://external/")) {
		m_update = &CKLBDLTask::updateUnzip;
		return;
	}

	m_errorType = DL_UNZIP_ERROR;
	KLBDELETE(m_unzipWorker);
	m_unzipWorker = NULL;
	CKLBDownloadManager::getInstance()->completeUnzip(this);
}

void
CKLBDLTask::updateUnzip(s32 /*deltaT*/)
{
	if (!m_unzipWorker->isFinishExtract()) {
		return;
	}

	m_errorType = m_unzipWorker->getErrorStatus();
	KLBDELETE(m_unzipWorker);
	m_unzipWorker = NULL;
	CPFInterface::getInstance().platform().removeTmpFile(m_localPath);
	CKLBDownloadManager::getInstance()->completeUnzip(this);
}

void
CKLBDLTask::updateDownload(s32 deltaT)
{
	bool complete = m_http->isDataComplete();
	m_httpStatus = m_http->getHttpStatus();
	m_curlStatus = m_http->getHttpState();
	m_received = m_http->getDwnldSize();

	if (complete) {
		m_errorType = DL_HTTP_ERROR;
		if (!m_curlStatus && m_httpStatus == 200
			&& m_received == m_size) {
			m_errorType = 0;
		} else if (m_received != m_size && m_httpStatus == 200) {
			m_errorType = DL_SIZE_MISMATCH;
		} else if (m_curlStatus == 2 && m_httpStatus == 500) {
			m_errorType = DL_NO_DATA;
		}
	} else {
		m_elapsed += deltaT;
		m_kbps = (s32)((m_received * 1000) / (m_elapsed << 10));
		if (m_lastReceived == m_received) {
			m_stallTimer += deltaT;
		} else {
			m_stallTimer = 0;
			m_lastReceived = m_received;
		}
		if (m_stallTimer < m_timeout) {
			return;
		}
		m_errorType = DL_TIMEOUT;
	}

	CKLBDownloadManager::getInstance()->completeDownload(m_index);
}
