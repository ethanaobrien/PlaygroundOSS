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
#include "AdManager.h"
#include "CKLBUtility.h"
#include "CKLBScriptEnv.h"

enum {
	AD_PRELOAD,
	AD_SHOW
};

static IFactory::DEFCMD cmd[] = {
	{ "AD_PRELOAD", AD_PRELOAD },
	{ "AD_SHOW", AD_SHOW },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBAdManager> factory(
	"AdManager", 139 | CLS_KLBUSERTASK, cmd);

IAdManager* g_adManager = NULL;

IAdManager*
IAdManager::getInstance()
{
	return g_adManager;
}

void
IAdManager::release()
{
	KLBDELETE(g_adManager);
	g_adManager = NULL;
}

CKLBAdManager::CKLBAdManager()
: CKLBLuaTask()
, m_results()
, m_adService(NULL)
{
	memset(m_commands, 0, sizeof(m_commands));
	m_commands[AD_PRELOAD] = &CKLBAdManager::cmdPreloadAd;
	m_commands[AD_SHOW] = &CKLBAdManager::cmdShowAd;
	m_adService = IAdManager::getInstance(this);
}

CKLBAdManager::~CKLBAdManager()
{
	KLBDELETE(g_adManager);
	g_adManager = NULL;
}

CKLBAdManager::AdResult::AdResult(int command, int parameter, const char* message)
: command(command)
, parameter(parameter)
, message(message ? CKLBUtility::copyString(message) : NULL)
{
}

CKLBAdManager::AdResult::~AdResult()
{
	KLBDELETEA(message);
}

void
IAdManager::onAdResult(int command, int parameter, const char* message)
{
	m_owner->onAdResult(command, parameter, message);
}

void
CKLBAdManager::onAdResult(int command, int parameter, const char* message)
{
	klb_assertNull((u32)command < 2,
		"CKLBLocationManager : Invalid callback index");
	AdResult* result = new AdResult(command, parameter, message);
	m_results.push_back(result);
}

u32
CKLBAdManager::getClassID()
{
	return 139 | CLS_KLBUSERTASK;
}

bool
CKLBAdManager::initScript(CLuaState& lua)
{
	m_callbackSuccess = CKLBUtility::copyString(lua.getString(1));
	m_callbackFailure = CKLBUtility::copyString(lua.getString(2));
	return regist(NULL, P_NORMAL);
}

int
CKLBAdManager::cmdPreloadAd(CLuaState& lua)
{
	bool rewarded = lua.getBool(3);
	const char* placement =
		(!lua.isNil(4) && lua.isString(4)) ? lua.getString(4) : NULL;
	m_adService->preloadAd(rewarded, placement);
	return 0;
}

int
CKLBAdManager::cmdShowAd(CLuaState& /*lua*/)
{
	m_adService->showAd();
	return 0;
}

void
CKLBAdManager::execute(u32 /*deltaT*/)
{
	while(!m_results.empty()) {
		AdResult* result = m_results.front();
		m_results.pop_front();

		klb_assertNull((u32)result->command < 2,
			"CKLBAdManager : Invalid Callback index");
		const char* callback = (&m_callbackSuccess)[result->command];
		CKLBScriptEnv::getInstance().call_adReward(
			callback, result->parameter, result->message);
		KLBDELETE(result);
	}
}

int
CKLBAdManager::commandScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() >= 2, "CMD index is required !");

	int command = lua.getInt(2);
	klb_assertNull((u32)command < 2, "Invalid Arguments");
	return (this->*m_commands[command])(lua);
}

void
CKLBAdManager::die()
{
	KLBDELETEA(m_callbackSuccess);
	KLBDELETEA(m_callbackFailure);
}
