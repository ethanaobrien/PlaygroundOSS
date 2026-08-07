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
#include "CKLBLuaEnv.h"
#include "CKLBUtility.h"
#include "assert_klb.h"

enum {
	START_DL,
	RETRY_DL,
	REUNZIP
};

static IFactory::DEFCMD cmd[] = {
	{ "START_DL", START_DL },
	{ "RETRY_DL", RETRY_DL },
	{ "REUNZIP", REUNZIP },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBDownloadClient> factory(
	"DownloadClient", CKLBDownloadClient::CLASS_ID, cmd);

CKLBDownloadClient::CKLBDownloadClient()
: CKLBLuaTask()
, m_manager(NULL)
, m_finished(false)
, m_commands()
{
	m_commands[START_DL] = &CKLBDownloadClient::StartDownload_;
	m_commands[RETRY_DL] = &CKLBDownloadClient::RetryDownload_;
	m_commands[REUNZIP]  = &CKLBDownloadClient::ReUnzip_;
	m_manager = CKLBDownloadManager::getInstance(this);
	klb_assertNull(m_manager, "CKLBDownloadClient : Failed to create CKLBDownloadManager instance");
}

CKLBDownloadClient::~CKLBDownloadClient()
{
	m_manager->deleteInstance();
}

bool
CKLBDownloadClient::initScript(CLuaState& lua)
{
	lua.numArgs();
	for (s32 i = 0; i < 7; ++i) {
		const char* callback = NULL;
		if (lua.isString(i + 1)) {
			const char* value = lua.getString(i + 1);
			if (value) {
				callback = CKLBUtility::copyString(value);
			}
		}
		m_callbacks[i] = callback;
	}
	return regist(NULL, P_NORMAL);
}

int
CKLBDownloadClient::commandScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() > 1,
		"CKLBDownloadClient : Invalid arguments for commandScript");
	s32 command = lua.getInt(2);
	klb_assertNull(command < 3, "Method idx over flow");
	(this->*m_commands[command])(lua);
	return 0;
}

void
CKLBDownloadClient::StartDownload_(CLuaState& lua)
{
	m_manager->StartDownload_(lua);
}

void
CKLBDownloadClient::RetryDownload_(CLuaState& lua)
{
	m_manager->RetryDownload_(lua);
	m_finished = false;
}

void
CKLBDownloadClient::ReUnzip_(CLuaState& lua)
{
	m_manager->ReUnzip_(lua);
}

void
CKLBDownloadClient::execute(u32 deltaT)
{
	if (!m_finished) {
		m_manager->execute(deltaT);
	}
}

void
CKLBDownloadClient::die()
{
	KLBDELETEA(m_callbacks[0]);
	KLBDELETEA(m_callbacks[1]);
	KLBDELETEA(m_callbacks[2]);
	KLBDELETEA(m_callbacks[3]);
	KLBDELETEA(m_callbacks[4]);
	KLBDELETEA(m_callbacks[5]);
	KLBDELETEA(m_callbacks[6]);
}

void
CKLBDownloadClient::setFinished()
{
	m_finished = true;
}

void
CKLBDownloadClient::callback(s32 callbackIndex, s32 type, s32 index, s32 status,
	double speed, double progress)
{
	klb_assertNull(callbackIndex >= 0 && callbackIndex < 7,
		"CKLBDownloadClient : Invalid Callback index");
	const char* callbackName = m_callbacks[callbackIndex];
	CKLBScriptEnv::getInstance().call_eventDownloadClient(
		callbackName, this, type, index, status, speed, progress);
}
