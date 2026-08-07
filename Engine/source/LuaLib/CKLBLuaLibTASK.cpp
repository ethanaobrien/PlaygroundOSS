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
#include "CKLBLuaLibTASK.h"
#include "CKLBBinArray.h"
#include "CKLBLuaPropTask.h"
#include "CKLBUITask.h"
#include "CLuaState.h"
#include "CKLBLuaEnv.h"
;
static CKLBLuaLibTASK libdef(0);

CKLBLuaLibTASK::CKLBLuaLibTASK(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibTASK::~CKLBLuaLibTASK() {}

// Lua関数の追加
void
CKLBLuaLibTASK::addLibrary()
{
	// プロパティタスク操作
    addFunction("TASK_getProperty",		CKLBLuaLibTASK::getProperty);
    addFunction("TASK_setProperty",		CKLBLuaLibTASK::setProperty);

	// タスク破棄
	addFunction("TASK_kill",			CKLBLuaLibTASK::killTask);
	addFunction("TASK_isKilled",		CKLBLuaLibTASK::isKill);
	addFunction("TASK_registerkill",	CKLBLuaLibTASK::registerKill);
	addFunction("TASK_isKillComplete",	CKLBLuaLibTASK::isKillComplete);

	// ステージタスク操作
    addFunction("TASK_StageOnly",		CKLBLuaLibTASK::setStageTask);
    addFunction("TASK_StageClear",		CKLBLuaLibTASK::clearStageTask);

	// タスクのpause状態設定
	addFunction("TASK_Pause",			CKLBLuaLibTASK::setPause);

	// タスクマネージャレベルでpauseをかける
	addFunction("TASK_ManagerPause",	CKLBLuaLibTASK::setManagerPause);

	// UIタスクのノード状態設定
	addFunction("TASKUI_setMatrix",		CKLBLuaLibTASK::setUIMatrix);
	addFunction("TASKUI_setRenderState",	CKLBLuaLibTASK::setUIRenderState);
}

int
CKLBLuaLibTASK::getProperty(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) return 0;
    if(lua.isNil(1)) return 0;

	CKLBLuaTask * pTask = (CKLBLuaTask *)CKLBTask::getFromScriptHandle((size_t)lua.getPointer(1));
	if(!pTask) return 0;

	CHECKTASK(pTask);

#if defined (DEBUG_RT_CHECK)
    klb_assert(pTask->getTaskType() >= CKLBTask::TASK_LUA_PROPERTY, "SCRIPT ERROR %s(%d): the task does not have property.",
               CKLBLuaEnv::getInstance().nowFile(), lua.getNumLine());
#endif
    if(pTask->getTaskType() < CKLBTask::TASK_LUA_PROPERTY) return 0;
	return ((CKLBLuaPropTask *)pTask)->getPropertyByScript(L);
}

int
CKLBLuaLibTASK::setProperty(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 2) return 0;
    if(lua.isNil(1)) return 0;
    
    CKLBLuaTask * pTask = (CKLBLuaTask *)CKLBTask::getFromScriptHandle((size_t)lua.getPointer(1));
    if(!pTask) return 0;

	CHECKTASK(pTask);

#if defined (DEBUG_RT_CHECK)
    klb_assert(pTask->getTaskType() >= CKLBTask::TASK_LUA_PROPERTY, "SCRIPT ERROR %s(%d): the task does not have property.",
               CKLBLuaEnv::getInstance().nowFile(), lua.getNumLine());
#endif
    if(pTask->getTaskType() < CKLBTask::TASK_LUA_PROPERTY) return 0;
    return ((CKLBLuaPropTask *)pTask)->setPropertyByScript(L);    
}

int
CKLBLuaLibTASK::killTask(lua_State *L)
{
    CLuaState lua(L);
    if(lua.numArgs() != 1) return 0;
    if(!lua.isNil(1)) {
        CKLBLuaTask * pTask = (CKLBLuaTask *)lua.findScriptPtr(1);
        if(!pTask) return 0;
		CHECKTASK(pTask);
        pTask->kill();
    }
    lua.retNil();
    return 1;
}

int
CKLBLuaLibTASK::isKill(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) return 0;
	bool isRemove = true;
	if(!lua.isNil(1)) {
		CKLBLuaTask * pTask = (CKLBLuaTask *)lua.findScriptPtr(1);
		if(pTask) {
			CHECKTASK(pTask);
			CKLBTaskMgr& mgr = CKLBTaskMgr::getInstance();
			isRemove = mgr.is_remove(pTask);
		}
	}
	lua.retBool(isRemove);
	return 1;
}

int
CKLBLuaLibTASK::registerKill(lua_State * L)
{
	CLuaState lua(L);
	bool bResult = false;
	if(lua.numArgs() == 2) {
		CKLBLuaTask * pTask = (CKLBLuaTask *)lua.findScriptPtr(1);
		if(!pTask) return 0;
		CHECKTASK(pTask);

		const char* cb = NULL;

		if (lua.isString(2)) {
			cb = lua.getString(2);
        }

		CKLBTaskMgr::getInstance().setCurrentTask(pTask);
		pTask->setKillCallback(cb);
		CKLBTaskMgr::getInstance().setCurrentTask(NULL);
		bResult = true;
	}
	lua.retBool(bResult);
	return 1;
}

int
CKLBLuaLibTASK::isKillComplete(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) return 0;
	bool isComplete = true;
	if(!lua.isNil(1)) {
		const void* handle = lua.getPointer(1);
		isComplete = CKLBTask::findFromScriptHandle(reinterpret_cast<size_t>(handle)) == NULL;
	}
	lua.retBool(isComplete);
	return 1;
}

int
CKLBLuaLibTASK::setStageTask(lua_State * L)
{
    CLuaState lua(L);
    if(lua.numArgs() != 1) return 0;
    if(lua.isNil(1)) return 0;
    CKLBLuaTask * pTask = (CKLBLuaTask *)lua.getScriptPtr(1);
    if(!pTask) return 0;
	CHECKTASK(pTask);

	CKLBTaskMgr::getInstance().registStageTask(pTask);
    return 0;
}

int
CKLBLuaLibTASK::clearStageTask(lua_State *L)
{
    CLuaState lua(L);
    if(lua.numArgs() > 0) return 0;
    CKLBTaskMgr::getInstance().clearStageTask();
    return 0;    
}

int
CKLBLuaLibTASK::setPause(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 2 || argc > 3) {
		lua.retBoolean(false);
		return 1;
	}

    CKLBLuaTask * pTask = (CKLBLuaTask *)lua.getScriptPtr(1);
    if(!pTask) return 0;
	CHECKTASK(pTask);

	bool bPause = lua.getBool(2);
	bool bRecursive = (argc >= 3) ? lua.getBool(3) : true;

	pTask->setPause(bPause, bRecursive);

	return 0;
}

int
CKLBLuaLibTASK::setManagerPause(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retBoolean(false);
		return 1;
	}
	bool bPause = lua.getBool(1);
	CKLBTaskMgr& mgr = CKLBTaskMgr::getInstance();
	mgr.setPause(bPause);

	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibTASK::setUIMatrix(lua_State * L)
{
	CLuaState lua(L);
	CKLBUITask * pTask = (CKLBUITask *)lua.getScriptPtr(1);
	if(!pTask) return 0;

	int argc = lua.numArgs();
	if(argc >= 7) {
		float * matrix = pTask->getNode()->setMatrix();
		for(int idx = 0; idx < 6; idx++) {
			matrix[idx] = lua.getFloat(idx + 2);
		}
	}

	lua.retBoolean(argc >= 7);
	return 1;
}

int
CKLBLuaLibTASK::setUIRenderState(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	int result = 0;
	if(argc > 0) {
		CKLBUITask * pTask = (CKLBUITask *)lua.getScriptPtr(1);
		if(!pTask) return result;

		SRenderState * pState = NULL;
		int state = (lua.numArgs() >= 2) ? lua.getInt(2) : 0;
		CKLBNode * nodeStack[40];
		int nodeCount = 1;
		nodeStack[0] = pTask->getNode();
		switch(state) {
		case 0:
			pState = CKLBRenderingManager::getInstance().getNoAlphaState();
			break;
		case 1:
			pState = CKLBRenderingManager::getInstance().getAlphaState();
			break;
		case 2:
			pState = CKLBRenderingManager::getInstance().getAdditiveState();
			break;
		}

		if(pState) {
			while(nodeCount) {
				CKLBNode * pNode = nodeStack[--nodeCount];
				CKLBRenderCommand ** renderList = pNode->getRenderList();
				for(size_t index = 0; index < pNode->getRenderCount(); index++) {
					CKLBRenderCommand * pCommand = renderList[index];
					if(pCommand->getCommandType() == RENDERCOMMAND_SPRITE) {
						((CKLBSprite *)pCommand)->setRenderState(pState);
					}
				}

				CKLBNode * pChild = pNode->getChild();
				while(pChild) {
					if(nodeCount < 40) {
						nodeStack[nodeCount++] = pChild;
					}
					pChild = pChild->getBrother();
				}
			}
		}
	}

	lua.retBoolean(argc > 0);
	result = 1;
	return result;
}
