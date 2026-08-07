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
#ifndef CKLBMotionManager_h
#define CKLBMotionManager_h

#include "CKLBLuaTask.h"

// Platform motion-sensor service. Android owns the concrete implementation;
// the task only needs the target-proven control portion of its interface.
class IMotionManager
{
public:
	virtual ~IMotionManager() {}
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual float getAzimuth() = 0;
	virtual float getElevation() = 0;

	static IMotionManager* getInstance();

protected:
	static IMotionManager* s_instance;
};

class CKLBMotionManager : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBMotionManager>;
	typedef int (CKLBMotionManager::*Command)(CLuaState& lua);

private:
	CKLBMotionManager();
	virtual ~CKLBMotionManager();

public:
	u32 getClassID();

	bool initScript(CLuaState& lua);
	int  commandScript(CLuaState& lua);
	void execute(u32 deltaT);
	void die();

private:
	int cmdStart(CLuaState& lua);
	int cmdStop(CLuaState& lua);

	Command			m_commands[2];
	IMotionManager*	m_motionManager;

	static const u32 CLASS_ID = 133 | CLS_KLBUSERTASK | CLS_NONVISUALTASK;
};

#endif // CKLBMotionManager_h
