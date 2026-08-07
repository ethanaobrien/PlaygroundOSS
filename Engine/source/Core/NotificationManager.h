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
#ifndef NotificationManager_h
#define NotificationManager_h

#include <list>
#include "CKLBLuaTask.h"

class CKLBNotificationManager;

// Platform notification service. Android supplies the concrete implementation;
// only the target-proven portion of its interface is declared here.
class INotificationManager
{
public:
	virtual ~INotificationManager() {}
	virtual void setLocalNotificationWithAlarm(const char* tag, int tagIndex,
		const char* message, int identifier, const char* sound) = 0;
	virtual void cancelLocalNotification(const char* tag, int tagIndex) = 0;
	virtual void requestPermission() = 0;
	virtual bool getEnableNotification() = 0;
	virtual void getRemoteToken(char* buffer, int bufferLength) = 0;
	virtual void onActivityResume() = 0;
	virtual void notify(u32 callbackIndex, int parameter, const char* message);

	static INotificationManager* create(CKLBNotificationManager* owner);
	static INotificationManager* getInstance();
	static void release();

protected:
	explicit INotificationManager(CKLBNotificationManager* owner) : m_owner(owner) {}
	CKLBNotificationManager* m_owner;

private:
	static INotificationManager* s_instance;
};

class CKLBNotificationManager : public CKLBLuaTask
{
private:
	typedef int (CKLBNotificationManager::*Command)(CLuaState& lua);

	struct NotificationEvent {
		NotificationEvent(u32 callbackIndex, int eventParameter,
			const char* eventMessage);
		~NotificationEvent();

		int			index;
		int			parameter;
		const char*	message;
	};

	Command						m_commands[5];
	const char*					m_callbacks[3];
	std::list<NotificationEvent*>	m_events;
	INotificationManager*			m_notificationService;

	CKLBNotificationManager();
	virtual ~CKLBNotificationManager();

	int cmd_setLocalNotification(CLuaState& lua);
	int cmd_requirePermission(CLuaState& lua);
	int cmd_cancelNotification(CLuaState& lua);
	int cmd_getEnableNotification(CLuaState& lua);
	int cmd_getRemoteToken(CLuaState& lua);
	void queueNotification(u32 callbackIndex, int parameter, const char* message);

	friend class INotificationManager;
	friend class CKLBTaskFactory<CKLBNotificationManager>;

public:
	u32 getClassID();
	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);
	void execute(u32 deltaT);
	void die();
};

#endif // NotificationManager_h
