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
#include "CKLBLuaLibAPP.h"
#include "CKLBTask.h"
#include "CKLBDrawTask.h"
#include "CAndroidPathConv.h"

;
static ILuaFuncLib::DEFCONST luaConst[] = {
	{ "APP_MAIL",		IPlatformRequest::APP_MAIL },		// 各環境のメールアプリ
	{ "APP_BROWSER",	IPlatformRequest::APP_BROWSER },	// 各環境のブラウザアプリ
	{ "APP_UPDATE",		IPlatformRequest::APP_UPDATE },		// 各環境のアップデートアプリ
	{ "APP_MAP",		IPlatformRequest::APP_MAP },
	{ "APP_SETTINGS",	IPlatformRequest::APP_SETTINGS },
	{ "APP_COLLABORATION", IPlatformRequest::APP_COLLABORATION },
	{ "APP_ATT",		IPlatformRequest::APP_ATT },
	{ 0, 0 }
};

static CKLBLuaLibAPP libdef(luaConst);

CKLBLuaLibAPP::CKLBLuaLibAPP(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibAPP::~CKLBLuaLibAPP() {}

// Lua関数の追加
void
CKLBLuaLibAPP::addLibrary()
{
	addFunction("APP_CallApplication",		CKLBLuaLibAPP::luaCallApplication);
	addFunction("APP_ClearCookies",			CKLBLuaLibAPP::luaClearCookies);
	addFunction("APP_GetPhysicalMem",		CKLBLuaLibAPP::luaGetPhysicalMem);
	addFunction("APP_TimeSinceStart",		CKLBLuaLibAPP::luaTimeSinceStart);
	addFunction("APP_TimeSinceReboot",		CKLBLuaLibAPP::luaTimeSinceReboot);
	addFunction("APP_DateTimeNow",			CKLBLuaLibAPP::luaDateTimeNow);
	addFunction("APP_ScreenShot",			CKLBLuaLibAPP::luaScreenShot);
	addFunction("APP_SetIdleTimerActivity",	CKLBLuaLibAPP::luaSetIdleTimerActivity);
	addFunction("APP_GetBundleID",			CKLBLuaLibAPP::luaGetBundleId);
}

int
CKLBLuaLibAPP::luaClearCookies(lua_State * L)
{
	CLuaState lua(L);
	CPFInterface::getInstance().platform().clearCookies();
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibAPP::luaGetPhysicalMem(lua_State * L)
{
	CLuaState lua(L);

	u32 value = CPFInterface::getInstance().platform().getPhysicalMemKB();
	if (value >= 0x1000000) {
		// 24 bit significant.
		value = 0xFFFFFF;
	}
	lua.retInt(value);
	return 1;
}

int
CKLBLuaLibAPP::luaTimeSinceStart(lua_State * L)
{
	CLuaState lua(L);
	s64 milliseconds = CKLBTaskMgr::getInstance().getFrameTimeAccumulator();
	lua.retInt((int)(milliseconds / 1000));
	return 1;
}

int
CKLBLuaLibAPP::luaTimeSinceReboot(lua_State * L)
{
	CLuaState lua(L);
	s64 milliseconds = CKLBTaskMgr::getInstance().getActiveTimeAccumulator();
	lua.retInt((int)(milliseconds / 1000));
	return 1;
}

int
CKLBLuaLibAPP::luaDateTimeNow(lua_State * L)
{
	CLuaState lua(L);
	char buffer[32] = { 0 };
	CPFInterface::getInstance().platform().getDateTimeNow(buffer, sizeof(buffer));
	lua.retString(buffer);
	return 1;
}

int
CKLBLuaLibAPP::luaScreenShot(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if (argc <= 0) {
		lua.retString(NULL);
		return 1;
	}

	char path[256];
	CKLBPathConv& pathConv = CKLBPathConv::getInstance();
	sprintf(path, "%s%s", pathConv.external(), "ScreenShot.png");
	CKLBOGLWrapper::getInstance().screenshot(path);

	if (lua.getType(1) == LUA_TBOOLEAN) {
		if (lua.getBoolUnchecked(1)) {
			CPFInterface::getInstance().platform().savePng2Album(path);
		}
	} else {
		lua.errorMsg("boolean", 1);
	}

	lua.retString(path);
	return 1;
}

int
CKLBLuaLibAPP::luaSetIdleTimerActivity(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if (argc != 1) {
		lua.retBoolean(false);
		return 1;
	}

	bool active = lua.getBool(1);
	CPFInterface::getInstance().platform().setIdleTimerActivity(active);
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibAPP::luaGetBundleId(lua_State * L)
{
	CLuaState lua(L);
	lua.retString(CPFInterface::getInstance().platform().getBundleId());
	return 1;
}

int
CKLBLuaLibAPP::luaCallApplication(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 1) {
		lua.retBoolean(false);
		return 1;
	}

	bool result = false;
	IPlatformRequest::APP_TYPE type = (IPlatformRequest::APP_TYPE)lua.getInt(1);
	IPlatformRequest& pForm = CPFInterface::getInstance().platform();

	switch(type)
	{
	case IPlatformRequest::APP_MAIL:
		{
			const char * addr = lua.isNil(2) ? "" : lua.getString(2);
			const char * subject = lua.isNil(3) ? "" : lua.getString(3);
			const char * body = lua.isNil(4) ? "" : lua.getString(4);

			result = pForm.callApplication(type, addr, subject, body);
		}
		break;
	case IPlatformRequest::APP_BROWSER:
		{
			const char * url = (lua.isNil(2)) ? "" : lua.getString(2);
			const char * callback = (lua.numArgs() >= 3 && !lua.isNil(3)) ? lua.getString(3) : NULL;

			result = pForm.callApplication(type, url, callback);
		}
		break;
	case IPlatformRequest::APP_UPDATE:
		{
			const char * search_key = (argc >= 2 && !lua.isNil(2)) ? lua.getString(2) : "";
			result = pForm.callApplication(type, search_key);
		}
		break;
	case IPlatformRequest::APP_MAP:
		{
			klb_assertNull(argc == 3, "wrong arguments");
			double latitude = lua.getDouble(2);
			double longitude = lua.getDouble(3);
			result = pForm.callApplication(type, latitude, longitude);
		}
		break;
	case IPlatformRequest::APP_SETTINGS:
		result = pForm.callApplication(type);
		break;
	case IPlatformRequest::APP_COLLABORATION:
		{
			klb_assertNull(argc < 4, "Wrong arguments");
			const char * application = lua.getString(2);
			const char * argument = lua.isString(3) ? lua.getString(3) : NULL;
			result = pForm.callApplication(type, application, argument);
		}
		break;
	case IPlatformRequest::APP_SHARE_CONTENTS:
		{
			klb_assertNull(argc > 3, "wrong arguments");
			const char * application = lua.getString(2);
			const char * argument = lua.getString(3);
			const char * callback = lua.isNil(4) ? NULL : lua.getString(4);
			result = pForm.callApplication(type, application, argument, callback);
		}
		break;
	case IPlatformRequest::APP_ATT:
		{
			klb_assertNull(argc == 3, "Wrong arguments");
			const char * callback = lua.getString(2);
			bool request = lua.getBool(3);
			result = pForm.callApplication(type, callback, request);
		}
		break;
	default:
		break;
	}
	lua.retBoolean(result);
	return 1;
}

// For C#
bool CKLBLuaLibAPP::callApplication(IPlatformRequest::APP_TYPE type, const char* addr, const char* subject, const char* body)
{
	bool result = false;
	IPlatformRequest& pForm = CPFInterface::getInstance().platform();

	switch(type)
	{
	case IPlatformRequest::APP_MAIL:
		{
			result = pForm.callApplication(type, addr, subject, body);
		}
		break;
    case IPlatformRequest::APP_BROWSER:
        {
            result = pForm.callApplication(type, addr);
        }
        break;
	default:
		break;
	}
	return result;
}
