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
//
//  CLuaState.cpp
//
#include <stdio.h>
#include "klb_vararg.h"
#include "CLuaState.h"
#include "CKLBLuaEnv.h"
#include "CKLBTask.h"

#include "KLBBase64.h"

namespace
{

const char*
encodeLuaError(const char* message, size_t messageLength)
{
	u32 encodedCapacity = (messageLength + 1) * 2;
	u32 encodedLength = encodedCapacity;
	char* encoded = KLBNEWA(char, encodedCapacity);
	KLBNetAPI_encodeBase64(message, messageLength + 1, encoded, &encodedLength);
	return encoded;
}

int
traceback(lua_State* L)
{
  const char* message = lua_tostring(L, 1);
  if (message)
    {
      luaL_traceback(L, L, message, 1);
    }
  else if (!lua_isnoneornil(L, 1))
    {
      if(!luaL_callmeta(L, 1, "__tostring"))
        {
          lua_pushliteral(L, "(noeror message)");
        }
    }

  return 1;
}

} // noname namespace

CLuaState::CLuaState(lua_State * L)
: m_L(L)
{
}

CLuaState::~CLuaState() 
{
}

const char *
CLuaState::getScriptName()
{
    return CKLBLuaEnv::getInstance().nowFile();
}

void
CLuaState::errorMsg(const char *type_name, int argnum)
{
    char buf[128];
    sprintf(buf, "invalid argment type (arg:%d is not %s.)", argnum, type_name);
    //CKLBLuaEnv::getInstance().errMsg(buf);
    error("%s", buf);
}

bool
CLuaState::callback(const char * func, const char * argform, ...)
{
	va_list ap;
	va_start(ap, argform);
	bool result = call_luafunction(0, func, argform, ap);
	va_end(ap);
	return result;
}

bool
CLuaState::retcall(int retnum, const char * func, const char * argform, ...)
{
	va_list ap;
	va_start(ap, argform);
	bool result = call_luafunction(retnum, func, argform, ap);
	va_end(ap);
	return result;
}

bool
CLuaState::call_luafunction(int retnum, const char *func, const char *argform, va_list ap)
{
    // lua関数の名称をスタックに積む
    lua_getglobal(m_L, func);

    int count = 0;
        
    // 引数をスタックに積む
    if(argform) {
        for(const char * sp = argform; *sp; sp++) {
            switch(*sp)
            {
                case 'B': {
                    int b = va_arg(ap, int);
                    retBoolean((b) ? true : false);
                    count++;
                    break;
                }
                case 'I': {
                    int i = va_arg(ap, int);
                    retInt(i);
                    count++;
                    break;
                }
                case 'N': {
                    double d = va_arg(ap, double);
                    retDouble(d);
                    count++;
                    break;
                }
                case 'S':{
                    const char * str = va_arg(ap, const char *);
					retString(str);
                    count++;
                    break;
                }
                case 'P':{
                    void * p = va_arg(ap, void *);
                    retPointer(p);
                    count++;
                    break;
                }
                case 'W': {
                    CKLBTask * task = va_arg(ap, CKLBTask *);
                    retPointer(reinterpret_cast<void *>(task->getTaskTrackHandle()));
                    count++;
                    break;
                }
                case 'G': {
                    const char * p = va_arg(ap, const char *);
                    retGlobal(p);
                    count++;
                    break;
                }
            }
        }
    }
	return call(count, func, retnum);
}

bool
CLuaState::call(int args, const char * func, int nresults)
{
    // call stackを取れるようにtraceback函数をpushしpcallの第四引数を修正.
    int base = lua_gettop(m_L) - args;
    lua_pushcfunction(m_L, traceback);
    lua_insert(m_L, base);
    int result = lua_pcall(m_L, args, nresults, base);
    lua_remove(m_L, base);

    if(result) {
    	const char * msg = NULL;
        
    	switch(result)
    	{
            default:            msg = "unknown error: %s (%s)";             break;
            case LUA_ERRRUN:    msg = "runtime error: %s (%s)";             break;
            case LUA_ERRMEM:    msg = "memory allocation error: %s (%s)";   break;
            case LUA_ERRERR:    msg = "error in error: %s (%s)";            break;
    	}
        // 呼び出しエラー: 指定の関数呼び出しに失敗
		const char * errmsg = getString(-1);
        size_t buff_len = strlen(msg) + strlen(errmsg) + strlen(func);
        char* buffer = KLBNEWA(char, buff_len + 1);
#if defined(_WIN32)
		sprintf_s(buffer, buff_len + 1, msg, errmsg, func);
#else
        snprintf(buffer, buff_len + 1, msg, errmsg, func);
#endif // #if defined(_WIN32)
        CKLBLuaEnv::getInstance().errMsg(buffer);
		const char* encoded = encodeLuaError(buffer, buff_len);
		klb_assertAlways("%s", encoded);
        KLBDELETEA(buffer); // assert発生するとここまで来ない予感はする.
        return false;
    }
    return true;
}

int
CLuaState::error(const char * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char msg[1024];
	vsprintf(msg, fmt, ap);
	va_end(ap);

	return luaL_error(m_L, msg);
}
