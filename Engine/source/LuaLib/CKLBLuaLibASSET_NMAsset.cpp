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
#include "CKLBLuaLibASSET.h"
#include "CKLBCrypto.h"
#include "CPFInterface.h"

s32
CKLBLuaLibASSET::luaGetNMAssetSize(lua_State* L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBool(false);
		return 1;
	}

	int length = lua.getInt(1);
	if(length > 0) {
		unsigned char* output = KLBNEWA(unsigned char, length);
		if(CPFInterface::getInstance().platform().randomBytes(output, length)) {
			lua.retString(reinterpret_cast<const char*>(output), length);
		} else {
			lua.retBool(false);
		}
		KLBDELETEA(output);
	} else {
		lua.retBool(false);
	}
	return 1;
}

s32
CKLBLuaLibASSET::luaSetNMAssetSize(lua_State* L)
{
	CLuaState lua(L);
	if(lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		if(input) {
			char* output = KLBNEWA(char, inputLength * 2 + 1);
			for(size_t i = 0; i < inputLength; i++) {
				sprintf(output + i * 2, "%02x", static_cast<unsigned char>(input[i]));
			}
			lua.retString(output, inputLength * 2);
			KLBDELETEA(output);
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}

s32
CKLBLuaLibASSET::luaSetNMAsset(lua_State* L)
{
	CLuaState lua(L);
	if(lua.numArgs() == 2) {
		size_t lhsLength;
		const char* lhs = lua.getString(1, &lhsLength);
		size_t rhsLength;
		const char* rhs = lua.getString(2, &rhsLength);
		if(lhs && rhs && lhsLength == rhsLength) {
			char* output = KLBNEWA(char, lhsLength);
			for(size_t i = 0; i < lhsLength; i++) {
				output[i] = lhs[i] ^ rhs[i];
			}
			lua.retString(output, lhsLength);
			KLBDELETEA(output);
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}

s32
CKLBLuaLibASSET::luaGetNMAsset(lua_State* L)
{
	CLuaState lua(L);
	if(lua.numArgs() == 0) {
		const char* key = getApplicationCryptoKeyData();
		int length = getApplicationCryptoKeyLength();
		lua.retString(key, length);
	} else {
		lua.retBool(false);
	}
	return 1;
}
