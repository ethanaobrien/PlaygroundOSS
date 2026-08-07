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
#ifndef CKLBLocationManager_h
#define CKLBLocationManager_h

#include <list>
#include "CKLBLuaTask.h"

class CKLBLocationManager;

class ILocationManager
{
public:
	virtual ~ILocationManager() {}
	virtual void requireLocation() = 0;
	virtual bool stopLocation() = 0;
	virtual int getPermissionStatus() = 0;
	virtual void requirePermission() = 0;
	virtual void notifyLocation(u32 callbackIndex, int parameter,
		double latitude, double longitude, const char* message);

	static ILocationManager* create(CKLBLocationManager* owner);
	static ILocationManager* getInstance();
	static void release();

protected:
	explicit ILocationManager(CKLBLocationManager* owner) : m_owner(owner) {}
	CKLBLocationManager* m_owner;

private:
	static ILocationManager* s_instance;
};

class CKLBLocationManager : public CKLBLuaTask
{
private:
	typedef int (CKLBLocationManager::*Command)(CLuaState& lua);

	struct LocationEvent {
		LocationEvent(u32 callbackIndex, double eventLatitude,
			double eventLongitude, int eventParameter, const char* eventMessage);
		~LocationEvent();

		int			index;
		double		latitude;
		double		longitude;
		int			parameter;
		const char*	message;
	};

	Command					m_commands[4];
	const char*				m_callbacks[3];
	std::list<LocationEvent*>	m_events;
	ILocationManager*			m_locationService;

	CKLBLocationManager();
	virtual ~CKLBLocationManager();
	virtual void die();
	virtual void execute(u32 deltaT);
	virtual bool initScript(CLuaState& lua);
	virtual int commandScript(CLuaState& lua);

	int cmd_requireLocation(CLuaState& lua);
	int cmd_getPermissionStatus(CLuaState& lua);
	int cmd_requirePermission(CLuaState& lua);
	int cmd_stopLocation(CLuaState& lua);
	void queueLocationEvent(u32 callbackIndex, int parameter,
		double latitude, double longitude, const char* message);

	friend class ILocationManager;
	friend class CKLBTaskFactory<CKLBLocationManager>;

public:
	virtual u32 getClassID();
};

#endif // CKLBLocationManager_h
