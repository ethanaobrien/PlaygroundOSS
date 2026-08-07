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
#ifndef CKLBLuaLibCRYPTO_h
#define CKLBLuaLibCRYPTO_h

#include "ILuaFuncLib.h"

class CKLBLuaLibCRYPTO : public ILuaFuncLib
{
private:
	CKLBLuaLibCRYPTO();

public:
	CKLBLuaLibCRYPTO(DEFCONST* arrConstDef);
	virtual ~CKLBLuaLibCRYPTO();

	void addLibrary();

private:
	static int luaPublicEncrypt(lua_State* L);
	static int luaRandomBytes(lua_State* L);
	static int luaSha1(lua_State* L);
	static int luaHmacSha1(lua_State* L);
	static int luaEncryptAES128CBC(lua_State* L);
	static int luaDecryptAES128CBC(lua_State* L);
	static int luaKey(lua_State* L);
};

#endif // CKLBLuaLibCRYPTO_h
