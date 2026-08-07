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
#include "NotificationManager.h"
#include "CKLBUtility.h"
#include "CKLBScriptEnv.h"

enum {
	NM_SET_NOTIFICATION,
	NM_REQUIRE_PERMISSION,
	NM_CANCEL_NOTIFICATION,
	NM_GET_ENABLE_NOTIFICATION,
	NM_GET_REMOTE_TOKEN
};

static IFactory::DEFCMD cmd[] = {
	{ "NM_SET_NOTIFICATION", NM_SET_NOTIFICATION },
	{ "NM_REQUIRE_PERMISSION", NM_REQUIRE_PERMISSION },
	{ "NM_CANCEL_NOTIFICATION", NM_CANCEL_NOTIFICATION },
	{ "NM_GET_ENABLE_NOTIFICATION", NM_GET_ENABLE_NOTIFICATION },
	{ "NM_GET_REMOTE_TOKEN", NM_GET_REMOTE_TOKEN },
	{ NULL, 0 }
};

// The shipped static initializer registers the factory as 0x280083 even
// though live notification tasks report the following 0x280084 class ID.
static const u32 NOTIFICATION_FACTORY_CLASS_ID =
	131 | CLS_KLBUSERTASK | CLS_NONVISUALTASK;
static CKLBTaskFactory<CKLBNotificationManager> factory(
	"NotificationManager", NOTIFICATION_FACTORY_CLASS_ID, cmd);

static const char* NOTIFICATION_TAGS[] = {
	"lpMax",
	"newEvent"
};

INotificationManager* INotificationManager::s_instance = NULL;

INotificationManager*
INotificationManager::getInstance()
{
	return s_instance;
}

void
INotificationManager::release()
{
	KLBDELETE(s_instance);
	s_instance = NULL;
}

CKLBNotificationManager::~CKLBNotificationManager()
{
	INotificationManager::release();
}

CKLBNotificationManager::CKLBNotificationManager()
: CKLBLuaTask()
, m_events()
, m_notificationService(NULL)
{
	memset(m_commands, 0, sizeof(m_commands));
	m_commands[NM_SET_NOTIFICATION] =
		&CKLBNotificationManager::cmd_setLocalNotification;
	m_commands[NM_REQUIRE_PERMISSION] =
		&CKLBNotificationManager::cmd_requirePermission;
	m_commands[NM_CANCEL_NOTIFICATION] =
		&CKLBNotificationManager::cmd_cancelNotification;
	m_commands[NM_GET_ENABLE_NOTIFICATION] =
		&CKLBNotificationManager::cmd_getEnableNotification;
	m_commands[NM_GET_REMOTE_TOKEN] =
		&CKLBNotificationManager::cmd_getRemoteToken;

	m_notificationService = INotificationManager::create(this);
}

CKLBNotificationManager::NotificationEvent::NotificationEvent(
	u32 callbackIndex, int eventParameter, const char* eventMessage)
: index(callbackIndex)
, parameter(eventParameter)
, message(eventMessage ? CKLBUtility::copyString(eventMessage) : NULL)
{
}

CKLBNotificationManager::NotificationEvent::~NotificationEvent()
{
	KLBDELETEA(message);
}

void
CKLBNotificationManager::queueNotification(u32 callbackIndex, int parameter,
	const char* message)
{
	klb_assertNull(callbackIndex < 3,
		"CKLBLocationManager : Invalid callback index");

	NotificationEvent* event = KLBNEWC(NotificationEvent,
		(callbackIndex, parameter, message));
	m_events.push_back(event);
}

u32
CKLBNotificationManager::getClassID()
{
	return 132 | CLS_KLBUSERTASK | CLS_NONVISUALTASK;
}

bool
CKLBNotificationManager::initScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 3, "Invalid Arguments");

	m_callbacks[0] = CKLBUtility::copyString(lua.getString(1));
	m_callbacks[1] = CKLBUtility::copyString(lua.getString(2));
	m_callbacks[2] = CKLBUtility::copyString(lua.getString(3));

	return regist(NULL, P_NORMAL);
}

int
CKLBNotificationManager::cmd_setLocalNotification(CLuaState& lua)
{
	if(lua.numArgs() <= 4) {
		lua.retBoolean(false);
		return 1;
	}

	int tagIndex = lua.getInt(3);
	klb_assertNull((u32)tagIndex < 2,
		"Error : wrong notification tag index");

	const char* tag = NOTIFICATION_TAGS[tagIndex];
	const char* message = lua.getString(4);
	int identifier = lua.getInt(5);
	const char* sound = lua.isNil(6) ? NULL : lua.getString(6);

	m_notificationService->setLocalNotificationWithAlarm(
		tag, tagIndex, message, identifier, sound);
	return 0;
}

int
CKLBNotificationManager::cmd_requirePermission(CLuaState& /*lua*/)
{
	m_notificationService->requestPermission();
	return 0;
}

int
CKLBNotificationManager::cmd_cancelNotification(CLuaState& lua)
{
	if(lua.numArgs() != 3) {
		lua.retBoolean(false);
		return 1;
	}

	int tagIndex = lua.getInt(3);
	klb_assertNull((u32)tagIndex < 2,
		"Error : wrong notification tag index");

	const char* tag = NOTIFICATION_TAGS[tagIndex];
	m_notificationService->cancelLocalNotification(
		tag, tagIndex);
	return 0;
}

int
CKLBNotificationManager::cmd_getEnableNotification(CLuaState& lua)
{
	bool enabled = m_notificationService->getEnableNotification();
	lua.retBoolean(enabled);
	return 1;
}

int
CKLBNotificationManager::cmd_getRemoteToken(CLuaState& lua)
{
	char* buffer = KLBNEWA(char, 0x100);
	buffer[0] = '\0';
	m_notificationService->getRemoteToken(buffer, 0x100);
	lua.retString(buffer);
	KLBDELETEA(buffer);
	return 1;
}

void
CKLBNotificationManager::execute(u32 /*deltaT*/)
{
	while(!m_events.empty()) {
		NotificationEvent* event = m_events.front();
		m_events.pop_front();

		klb_assertNull((u32)event->index < 3,
			"CKLBNotificationManager : Invalid Callback index");

		const char* callback = m_callbacks[event->index];
		CKLBScriptEnv::getInstance().call_notificationEvent(
			callback, event->parameter, event->message);
		KLBDELETE(event);
	}
}

int
CKLBNotificationManager::commandScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() > 1, "CMD index is required !");

	int command = lua.getInt(2);
	klb_assertNull((u32)command < 5, "Invalid Arguments");
	return (this->*m_commands[command])(lua);
}

void
CKLBNotificationManager::die()
{
	KLBDELETEA(m_callbacks[0]);
	KLBDELETEA(m_callbacks[1]);
	KLBDELETEA(m_callbacks[2]);
}
