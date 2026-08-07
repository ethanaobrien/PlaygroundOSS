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
#include "CKLBLocationManager.h"

enum {
	LM_REQUIRE_LOC = 0,
	LM_GET_PERMISSION_STATUS,
	LM_REQUIRE_PERMISSION,
	LM_STOP_LOC
};

static IFactory::DEFCMD commandList[] = {
	{ "LM_REQUIRE_LOC",             LM_REQUIRE_LOC },
	{ "LM_GET_PERMISSION_STATUS",   LM_GET_PERMISSION_STATUS },
	{ "LM_REQUIRE_PERMISSION",      LM_REQUIRE_PERMISSION },
	{ "LM_STOP_LOC",                LM_STOP_LOC },
	{ NULL,                           0 }
};

static CKLBTaskFactory<CKLBLocationManager> factory(
	"LocationManager", 0x00280083, commandList);

ILocationManager* ILocationManager::s_instance = NULL;

ILocationManager*
ILocationManager::getInstance()
{
	return s_instance;
}

void
ILocationManager::release()
{
	KLBDELETE(s_instance);
	s_instance = NULL;
}

CKLBLocationManager::CKLBLocationManager()
: CKLBLuaTask()
, m_events()
, m_locationService(NULL)
{
	memset(m_commands, 0, sizeof(m_commands));
	m_commands[LM_REQUIRE_LOC]           = &CKLBLocationManager::cmd_requireLocation;
	m_commands[LM_GET_PERMISSION_STATUS] = &CKLBLocationManager::cmd_getPermissionStatus;
	m_commands[LM_REQUIRE_PERMISSION]    = &CKLBLocationManager::cmd_requirePermission;
	m_commands[LM_STOP_LOC]              = &CKLBLocationManager::cmd_stopLocation;
	m_locationService = ILocationManager::create(this);
}

CKLBLocationManager::~CKLBLocationManager()
{
	ILocationManager::release();
}

CKLBLocationManager::LocationEvent::LocationEvent(u32 callbackIndex,
	double eventLatitude, double eventLongitude, int eventParameter,
	const char* eventMessage)
: index(callbackIndex)
, latitude(eventLatitude)
, longitude(eventLongitude)
, parameter(eventParameter)
, message(eventMessage ? CKLBUtility::copyString(eventMessage) : NULL)
{
}

CKLBLocationManager::LocationEvent::~LocationEvent()
{
	KLBDELETEA(message);
}

void
ILocationManager::notifyLocation(u32 callbackIndex, int parameter,
	double latitude, double longitude, const char* message)
{
	m_owner->queueLocationEvent(callbackIndex, parameter, latitude, longitude, message);
}

void
CKLBLocationManager::queueLocationEvent(u32 callbackIndex, int parameter,
	double latitude, double longitude, const char* message)
{
	klb_assertNull(callbackIndex < 3, "CKLBLocationManager : Invalid callback index");

	LocationEvent* event = KLBNEWC(LocationEvent,
		(callbackIndex, latitude, longitude, parameter, message));
	m_events.push_back(event);
}

u32
CKLBLocationManager::getClassID()
{
	return 0x00280083;
}

bool
CKLBLocationManager::initScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 3, "Invalid Arguments");

	for(u32 index = 0; index < 3; ++index) {
		m_callbacks[index] = CKLBUtility::copyString(lua.getString(index + 1));
	}
	return regist(NULL, P_NORMAL);
}

void
CKLBLocationManager::die()
{
	for(u32 index = 0; index < 3; ++index) {
		KLBDELETEA(m_callbacks[index]);
	}
}

void
CKLBLocationManager::execute(u32 /*deltaT*/)
{
	while(!m_events.empty()) {
		LocationEvent* event = m_events.front();
		m_events.pop_front();
		klb_assertNull((u32)event->index < 3,
			"CKLBDownloadClient : Invalid Callback index");

		const char* callback = m_callbacks[event->index];
		CKLBScriptEnv& scriptEnv = CKLBScriptEnv::getInstance();
		scriptEnv.call_locationEvent(
			callback, event->parameter, event->latitude,
			event->longitude, event->message);
		KLBDELETE(event);
	}
}

int
CKLBLocationManager::commandScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 2, "Invalid Arguments");

	int command = lua.getInt(2);
	klb_assertNull((u32)command < 4, "Invalid Arguments");
	return (this->*m_commands[command])(lua);
}

int
CKLBLocationManager::cmd_requireLocation(CLuaState& /*lua*/)
{
	m_locationService->requireLocation();
	return 0;
}

int
CKLBLocationManager::cmd_getPermissionStatus(CLuaState& lua)
{
	lua.retInt(m_locationService->getPermissionStatus());
	return 1;
}

int
CKLBLocationManager::cmd_requirePermission(CLuaState& /*lua*/)
{
	m_locationService->requirePermission();
	return 0;
}

int
CKLBLocationManager::cmd_stopLocation(CLuaState& lua)
{
	lua.retBool(m_locationService->stopLocation());
	return 1;
}
