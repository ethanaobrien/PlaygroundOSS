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
#include "CKLBLuaLibCRYPTO.h"
#include "CKLBCrypto.h"

static CKLBLuaLibCRYPTO libdef(0);

CKLBLuaLibCRYPTO::CKLBLuaLibCRYPTO(DEFCONST* arrConstDef)
: ILuaFuncLib(arrConstDef)
{
}

CKLBLuaLibCRYPTO::~CKLBLuaLibCRYPTO()
{
}

void
CKLBLuaLibCRYPTO::addLibrary()
{
	addFunction("CRYPTO_public_encrypt", luaPublicEncrypt);
	addFunction("CRYPTO_random_bytes", luaRandomBytes);
	addFunction("CRYPTO_sha1", luaSha1);
	addFunction("CRYPTO_hmac_sha1", luaHmacSha1);
	addFunction("CRYPTO_encrypt_aes_128_cbc", luaEncryptAES128CBC);
	addFunction("CRYPTO_decrypt_aes_128_cbc", luaDecryptAES128CBC);
	addFunction("CRYPTO_key", luaKey);
}

int
CKLBLuaLibCRYPTO::luaPublicEncrypt(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		unsigned char output[1000];
		int result = CPFInterface::getInstance().platform().publicKeyEncrypt(
			reinterpret_cast<unsigned char*>(const_cast<char*>(input)),
			inputLength, output, sizeof(output));
		if (result > 0) {
			lua.retString(reinterpret_cast<const char*>(output), result);
			return 1;
		}

		char error[1000];
		sprintf(error, "public_encrypt: error in publicKeyEncrypt %d", result);
		lua.retBool(false);
		lua.retString(error);
		return 2;
	}

	lua.retBool(false);
	lua.retString("public_encrypt: invalid args");
	return 2;
}

int
CKLBLuaLibCRYPTO::luaRandomBytes(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() != 1) {
		lua.retBool(false);
		return 1;
	}

	int length = lua.getInt(1);
	if (length <= 0) {
		lua.retBool(false);
		return 1;
	}

	unsigned char* output = KLBNEWA(unsigned char, length);
	if (CPFInterface::getInstance().platform().randomBytes(output, length)) {
		lua.retString(reinterpret_cast<const char*>(output), length);
	} else {
		lua.retBool(false);
	}
	KLBDELETEA(output);
	return 1;
}

int
CKLBLuaLibCRYPTO::luaSha1(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 1) {
		size_t inputLength;
		const char* input = lua.getString(1, &inputLength);
		if (input) {
			unsigned char output[20];
			if (cryptoSHA1(output, input, inputLength, sizeof(output))) {
				lua.retString(reinterpret_cast<const char*>(output), sizeof(output));
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

int
CKLBLuaLibCRYPTO::luaHmacSha1(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 2) {
		size_t inputLength;
		size_t keyLength;
		const char* input = lua.getString(1, &inputLength);
		const char* key = lua.getString(2, &keyLength);
		if (key && input) {
			unsigned char output[20];
			if (cryptoHmacSHA1(output, input, inputLength, key, keyLength)) {
				lua.retString(reinterpret_cast<const char*>(output), sizeof(output));
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

int
CKLBLuaLibCRYPTO::luaEncryptAES128CBC(lua_State* L)
{
	CLuaState lua(L);
	int returnCount;
	if (lua.numArgs() == 2) {
		size_t inputLength;
		size_t keyLength;
		const char* input = lua.getString(1, &inputLength);
		const char* key = lua.getString(2, &keyLength);
		if (key && input) {
			int outputCapacity = static_cast<int>(inputLength) + 100;
			unsigned char* output = KLBNEWA(unsigned char, outputCapacity);
			int result = CPFInterface::getInstance().platform().encryptAES128CBC(
				output, outputCapacity, input, inputLength, key, keyLength);
			if (result >= 0) {
				lua.retString(reinterpret_cast<const char*>(output), result);
				KLBDELETEA(output);
				returnCount = 1;
			} else {
				char error[1000];
				sprintf(error, "encrypt_aes_128_cbc: error in encryptAES128CBC %d", result);
				lua.retBool(false);
				lua.retString(error);
				KLBDELETEA(output);
				returnCount = 2;
			}
		} else {
			lua.retBool(false);
			returnCount = 2;
			lua.retString("encrypt_aes_128_cbc: invalid args");
		}
	} else {
		lua.retBool(false);
		returnCount = 2;
		lua.retString("encrypt_aes_128_cbc: invalid args");
	}
	return returnCount;
}

int
CKLBLuaLibCRYPTO::luaDecryptAES128CBC(lua_State* L)
{
	CLuaState lua(L);
	int returnCount;
	if (lua.numArgs() == 2) {
		size_t inputLength;
		size_t keyLength;
		const char* input = lua.getString(1, &inputLength);
		const char* key = lua.getString(2, &keyLength);
		if (key && input) {
			int outputCapacity = static_cast<int>(inputLength) + 100;
			unsigned char* output = KLBNEWA(unsigned char, outputCapacity);
			int result = CPFInterface::getInstance().platform().decryptAES128CBC(
				output, outputCapacity, input, inputLength, key, keyLength);
			if (result >= 0) {
				lua.retString(reinterpret_cast<const char*>(output), result);
				KLBDELETEA(output);
				returnCount = 1;
			} else {
				char error[1000];
				sprintf(error, "decrypt_aes_128_cbc: error in decryptAES128CBC %d", result);
				lua.retBool(false);
				lua.retString(error);
				KLBDELETEA(output);
				returnCount = 2;
			}
		} else {
			lua.retBool(false);
			returnCount = 2;
			lua.retString("decrypt_aes_128_cbc: invalid args");
		}
	} else {
		lua.retBool(false);
		returnCount = 2;
		lua.retString("decrypt_aes_128_cbc: invalid args");
	}
	return returnCount;
}

int
CKLBLuaLibCRYPTO::luaKey(lua_State* L)
{
	CLuaState lua(L);
	if (lua.numArgs() == 0) {
		const char* key = getApplicationCryptoKeyData();
		int length = getApplicationCryptoKeyLength();
		lua.retString(key, length);
	} else {
		lua.retBool(false);
	}
	return 1;
}
