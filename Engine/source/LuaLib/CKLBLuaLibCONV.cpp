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
#include "CKLBLuaLibCONV.h"
#include "CKLBUtility.h"

#include "KLBBase64.h"


static int base64Encode(lua_State* L);
static int base64Decode(lua_State* L);
static int xorString(lua_State* L);
static int hexlify(lua_State* L);

static CKLBLuaLibCONV libdef(0);

CKLBLuaLibCONV::CKLBLuaLibCONV(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibCONV::~CKLBLuaLibCONV() {}

// Lua関数の追加
void
CKLBLuaLibCONV::addLibrary()
{
	addFunction("CONV_Lua2Json",     CKLBLuaLibCONV::lua2json);
	addFunction("CONV_Json2Lua",     CKLBLuaLibCONV::json2lua);
	addFunction("CONV_JsonFile2Lua", CKLBLuaLibCONV::jsonfile2lua);
	addFunction("CONV_base64_encode", base64Encode);
	addFunction("CONV_base64_decode", base64Decode);
	addFunction("CONV_xor_string",    xorString);
	addFunction("CONV_hexlify",       hexlify);
}

static int
base64Encode(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		if (input) {
			size_t capacity = ((inputLength + 2) / 3) * 4 + 1;
			char* output = KLBNEWA(char, capacity);
			u32 outputLength;
			KLBNetAPI_encodeBase64(input, inputLength, output, &outputLength);
			if (outputLength) {
				lua.retString(output, outputLength - 1);
				KLBDELETEA(output);
			} else {
				lua.retBool(false);
			}
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}

static int
base64Decode(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		if (input) {
			char* output = KLBNEWA(char, inputLength + 1);
			u32 outputLength;
			KLBNetAPI_decodeBase64(input, output, &outputLength);
			lua.retString(output, outputLength);
			KLBDELETEA(output);
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}

static int
xorString(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 2) {
		size_t leftLength;
		size_t rightLength;
		const char* left = lua.getString(1, &leftLength);
		const char* right = lua.getString(2, &rightLength);
		if (left && right && leftLength == rightLength) {
			char* output = KLBNEWA(char, leftLength);
			for (size_t i = 0; i < leftLength; ++i) {
				output[i] = left[i] ^ right[i];
			}
			lua.retString(output, leftLength);
			KLBDELETEA(output);
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}

static int
hexlify(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		if (input) {
			char* output = KLBNEWA(char, inputLength * 2 + 1);
			size_t outputLength = 0;
			for (size_t i = 0; i < inputLength; ++i) {
				sprintf(output + i * 2, "%02x", static_cast<unsigned char>(input[i]));
			}
			outputLength = inputLength * 2;
			lua.retString(output, outputLength);
			KLBDELETEA(output);
		} else {
			lua.retBool(false);
		}
	} else {
		lua.retBool(false);
	}
	return 1;
}


int
CKLBLuaLibCONV::lua2json(lua_State * L)
{
	CLuaState lua(L);

	lua.retValue(1);
	size_t size; // Ignored here.
	const char * json = CKLBUtility::lua2json(lua,size);
	lua.pop(1);
	lua.retString(json);
	KLBDELETEA(json);
	return 1;
}

int
CKLBLuaLibCONV::json2lua(lua_State * L)
{
	CLuaState lua(L);
	const char * json = lua.getString(1);
	CKLBUtility::json2lua(lua, json, strlen(json));
	return 1;
}

int
CKLBLuaLibCONV::jsonfile2lua(lua_State * L)
{
	CLuaState lua(L);

	const char * asset = lua.getString(1);
	IReadStream * pStream;

	IPlatformRequest& pltf = CPFInterface::getInstance().platform();
	const u32 allowedEncryptionFormats = 0x0e;
	pStream = pltf.openReadStream(asset, pltf.useEncryption(), allowedEncryptionFormats);
	if(!pStream || pStream->getStatus() != IReadStream::NORMAL) {
		delete pStream;
		lua.retNil();
		return 1;
	}
	int size = pStream->getSize();
	u8 * buf = KLBNEWA(u8, size + 1);
	if(!buf) {
		delete pStream;
		lua.retNil();
		return 1;
	}
	pStream->readBlock((void *)buf, size);
	delete pStream;

	buf[size] = 0;
	const char * json = (const char *)buf;

	CKLBUtility::json2lua(lua, json, size);
	KLBDELETEA(buf);

	return 1;
}
