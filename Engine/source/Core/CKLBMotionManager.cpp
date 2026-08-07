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
#include "CKLBMotionManager.h"

IMotionManager* IMotionManager::s_instance = NULL;

enum {
	MM_START,
	MM_STOP
};

static IFactory::DEFCMD cmd[] = {
	{ "MM_START", MM_START },
	{ "MM_STOP",  MM_STOP  },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBMotionManager> factory("MotionManager",
	133 | CLS_KLBUSERTASK | CLS_NONVISUALTASK, cmd);

CKLBMotionManager::CKLBMotionManager()
: CKLBLuaTask()
, m_commands()
, m_motionManager(NULL)
{
	m_commands[MM_START] = &CKLBMotionManager::cmdStart;
	m_commands[MM_STOP]  = &CKLBMotionManager::cmdStop;
	m_motionManager = IMotionManager::getInstance();
}

CKLBMotionManager::~CKLBMotionManager()
{
}

u32
CKLBMotionManager::getClassID()
{
	return CLASS_ID;
}

int
CKLBMotionManager::cmdStart(CLuaState& /*lua*/)
{
	m_motionManager->start();
	return 0;
}

int
CKLBMotionManager::cmdStop(CLuaState& /*lua*/)
{
	m_motionManager->stop();
	return 0;
}

bool
CKLBMotionManager::initScript(CLuaState& /*lua*/)
{
	return regist(NULL, P_NORMAL);
}

int
CKLBMotionManager::commandScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	klb_assertNull(argc >= 2, "CMD index is required !");

	int command = lua.getInt(2);
	klb_assertNull((u32)command < 2, "Invalid Arguments");
	return (this->*m_commands[command])(lua);
}

void
CKLBMotionManager::execute(u32 /*deltaT*/)
{
}

void
CKLBMotionManager::die()
{
}
