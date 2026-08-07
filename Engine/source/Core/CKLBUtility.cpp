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
#include "CKLBUtility.h"
#include "CKLBDataTask.h"
#include "CKLBNode.h"
#include "CKLBTextInputNode.h"
#include "CKLBWebViewNode.h"
#include "CompositeManagement.h"
#include "CKLBScriptEnv.h"
#include "hash_sha1.h"
#include <string.h>
#include <stdio.h>

void
CKLBUtility::getNodePosition(CKLBNode * pNode, float * x, float * y)
{
	*x = pNode->m_composedMatrix.m_matrix[ MAT_TX ];
	*y = pNode->m_composedMatrix.m_matrix[ MAT_TY ];
}

void
CKLBUtility::getNodeInverseMatrix(CKLBNode* pNode, SMatrix2D& result) {
	/*

	detA	= ad - cb

	Matrix
	A' 	=  d / detA
	B' 	= -b / detA
	C' 	= -c / detA
	D' 	=  a / detA
	TX'	= (b*ty - tx*d) / detA
	TY'	= (tx*c - a*ty) / detA

	*/

	float* mat = pNode->m_composedMatrix.m_matrix;
	float invDetA = 1.0f / ((mat[MAT_A] * mat[MAT_D]) - (mat[MAT_C] * mat[MAT_B])); 

	result.m_matrix[MAT_A] =  mat[MAT_D] * invDetA;
	result.m_matrix[MAT_B] =(-mat[MAT_B])* invDetA;
	result.m_matrix[MAT_C] =(-mat[MAT_C])* invDetA;
	result.m_matrix[MAT_D] =  mat[MAT_A] * invDetA;

	result.m_matrix[MAT_TX] = ((mat[MAT_B]*mat[MAT_TY]) - (mat[MAT_D]*mat[MAT_TX])) * invDetA;
	result.m_matrix[MAT_TY] = ((mat[MAT_C]*mat[MAT_TX]) - (mat[MAT_A]*mat[MAT_TY])) * invDetA;

	result.m_type = MATRIX_TG;
}

void
CKLBUtility::transform(SMatrix2D* pMatrix, float x, float y, float& resx, float& resy) {
	resx = (pMatrix->m_matrix[MAT_A] * x) + (pMatrix->m_matrix[MAT_B] * y) + pMatrix->m_matrix[MAT_TX];
	resy = (pMatrix->m_matrix[MAT_C] * x) + (pMatrix->m_matrix[MAT_D] * y) + pMatrix->m_matrix[MAT_TY];
}

s32 CKLBUtility::charCountUtf8(const char *s)
{
	// Using this code, added null string support.
	// http://porg.es/blog/counting-characters-in-utf-8-strings-is-faster
	//
	// This one seems nicer
	// but didnt had time to make sure it is correct, as it is more complex
	// : http://www.daemonology.net/blog/2008-06-05-faster-utf8-strlen.html
	// Saved the gem in case the URL disappear.
	/**
		#define ONEMASK ((size_t)(-1) / 0xFF)

		static size_t
		cp_strlen_utf8(const char * _s)
		{
			const char * s;
			size_t count = 0;
			size_t u;
			unsigned char b;

			// Handle any initial misaligned bytes.
			for (s = _s; (uintptr_t)(s) & (sizeof(size_t) - 1); s++) {
				b = *s;

				// Exit if we hit a zero byte.
				if (b == '\0')
					goto done;

				// Is this byte NOT the first byte of a character?
				count += (b >> 7) & ((~b) >> 6);
			}

			// Handle complete blocks.
			for (; ; s += sizeof(size_t)) {
				// Prefetch 256 bytes ahead.
				// __builtin_prefetch(&s[256], 0, 0);

				// Grab 4 or 8 bytes of UTF-8 data.
				u = *(size_t *)(s);

				// Exit the loop if there are any zero bytes.
				if ((u - ONEMASK) & (~u) & (ONEMASK * 0x80))
					break;

				// Count bytes which are NOT the first byte of a character.
				u = ((u & (ONEMASK * 0x80)) >> 7) & ((~u) >> 6);
				count += (u * ONEMASK) >> ((sizeof(size_t) - 1) * 8);
			}

			// Take care of any left-over bytes.
			for (; ; s++) {
				b = *s;

				// Exit if we hit a zero byte.
				if (b == '\0')
					break;

				// Is this byte NOT the first byte of a character?
				count += (b >> 7) & ((~b) >> 6);
			}

		done:
			return ((s - _s) - count);
		}
	 */


    s32 count = 0;
	if (s) {
		s32 i = 0;
		s32 iBefore = 0;

		while (s[i] > 0)
				ascii:  i++;
 
		count += i-iBefore;
		while (s[i])
		{
				if (s[i] > 0)
				{
						iBefore = i;
						goto ascii;
				}
				else
				switch (0xF0 & s[i])
				{
						case 0xE0: i += 3; break;
						case 0xF0: i += 4; break;
						default:   i += 2; break;
				}
				++count;
		}
	}
    return count;
}

u32 
CKLBUtility::nearest2Pow(u32 v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

int
CKLBUtility::safe_strcmp(const char* a, const char* b) 
{
	// Same string, same null : OK
	if (a == b) {
		return 0;
	}

	if (a && b) {
		// Both valid pointer
		return strcmp(a,b);
	} else {
		// One of the pointer is null.
		return -1;
	}
}

// Numeric conversion helpers.
char *
CKLBUtility::numString64(char * buf, u64 value)
{
	u64 v = 1000000000000000000ULL;
	char * ptr = buf;
	while(v > 0) {
		int num = value / v;
		value = value % v;
		v = v / 10LL;
		if(num || ptr > buf) *ptr++ = num + '0';
	}
	if(ptr == buf) *ptr++ = '0';
	*ptr = 0;
	return buf;
}

s64
CKLBUtility::stringNum64(const char * string)
{
	s64 sign = 1;
	s64 val = 0;
	const char * p = string;
	while(*p) {
		if(p == string && *p == '-') {
			sign = -1;
			p++;
			continue;
		}
		if ((*p >= '0') && (*p <= '9')) {
			val = val * 10LL;
			val += *p - '0';
		}
		p++;
	}
	val = val * sign;
	return val;
}

const char *
CKLBUtility::copyString(const char * string)
{
	klb_assert(string, "copy string is NULL.");
	char * buf = KLBNEWA(char, strlen(string) + 1);
    if(!buf) { return NULL; }
	strcpy(buf, string);
	return (const char *)buf;
}

const char *
CKLBUtility::copyMem(const char * src, u64 size)
{
	char * buf = KLBNEWA(char, size);
    if(!buf) { return NULL; }
	memcpy(buf, src, size);
	return (const char *)buf;
}

s64
CKLBUtility::stringNum(const char * string)
{
	s64 value = 0;
	char current = *string;
	while(current) {
		value = (value * 10LL) + (current - '0');
		current = *++string;
	}
	return value;
}

char *
CKLBUtility::quoated(const char * string)
{
	size_t size = (!string) ? 5 : (strlen(string) + 3);
	char * buf = KLBNEWA(char, size);
    if(!buf) { return NULL; }
	if(!string) {
		strcpy(buf, "null");
	} else {
		sprintf(buf, "\"%s\"", string);
	}
	return buf;
}


const char **
CKLBUtility::splitString(const char * string, int delim)
{
	// pass-1: delim で分割される総数を得る
	int cnt = 1;
	for(const char * ptr = string; *ptr; ptr++) {
		if(*ptr == delim) cnt++;
	}

	// 外枠の配列生成
	const char ** arr = KLBNEWA(const char *, cnt + 1);
	arr[cnt] = 0;	// 終端は NULL

	const char * ptr = string;
	for(int i = 0; i < cnt; i++) {
		int len = 0;
        while(ptr[len] && ptr[len] != delim) { len++; }
		char * buf = KLBNEWA(char, len + 1);
		strncpy(buf, ptr, len);
		buf[len] = 0;
		arr[i] = (const char *)buf;
		ptr += len;
		if(*ptr == delim) ptr++;
	}

	return arr;
}

void
CKLBUtility::deleteSplitString(const char ** split_arr)
{
	for(int i = 0; split_arr[i]; i++) {
		const char * str = split_arr[i];
		KLBDELETEA(str);
	}
	KLBDELETEA(split_arr);
}

CKLBAsset *
CKLBUtility::loadAssetScript(const char * asset, u32 * handle, IKLBAssetPlugin* plugIn, bool bSimple)
{
	CKLBAsset * pAsset = loadAsset(asset, handle, plugIn, bSimple);
	if(!pAsset) {
		CKLBScriptEnv::getInstance().error("CKLBUtility : could not load asset: %s", asset);
	}
	return pAsset;
}

CKLBAsset *
CKLBUtility::readAsset(u8 * stream, size_t streamSize, u32 * handle, IKLBAssetPlugin * plugIn)
{
	CKLBAssetManager& pAssetManager = CKLBAssetManager::getInstance();
	CKLBAsset * pAsset;
	bool bResult = pAssetManager.loadAsset(stream, streamSize, (CKLBAbstractAsset **)&pAsset, plugIn);
    if(!bResult || !pAsset) { return NULL; }

	*handle = CKLBDataHandler::allocateHandle(pAsset);
	return pAsset;
}

CKLBAsset *
CKLBUtility::loadAsset(const char * asset, u32 * handle, IKLBAssetPlugin* plugIn, bool bSimple)
{
	CKLBAssetManager& pAssetManager = CKLBAssetManager::getInstance();
	// CPFInterface& pfif = CPFInterface::getInstance();
	CKLBAsset * pAsset;

	pAsset = (CKLBAsset *)pAssetManager.loadAssetByFileName(asset, plugIn);

	if(!pAsset) {
		return NULL;
	}
	if (handle) {
		*handle = CKLBDataHandler::allocateHandle(pAsset);
	}

	// bSimple が true のときは、texture であっても image の取得を行わない。
	// あくまで texture のみを必要とするケースもある。
	if(!bSimple && (pAsset->getAssetType() == ASSET_TEXTURE)) {
		// 画像asset名を与えると、帰ってくるのは texture asset であるため、
		// 改めてその中から画像 asset を取得する処理が必要となる。

		// 直接引数で与えられるasset名は、asset://foo.bar や file://install/foo.bar のように、
		// scheme がついているため、scheme に相当する部分を除去した名称(パス含む)を使用する。
		const char * scheme_pattern[] = {
			"file://install/",
			"file://external/",
			"asset://",
			0
		};
		const char * name = NULL;
		for(int i = 0; scheme_pattern[i]; i++) {
			const char * pattern = scheme_pattern[i];
			int len = strlen(pattern);
			if(!strncmp(asset, pattern, len)) {
				name = asset + len;
				break;
			}
		}
		if(!name) {
			if (handle) { CKLBDataHandler::releaseHandle(*handle); }
			return NULL;
		}
		pAsset = ((CKLBTextureAsset*)pAsset)->getImage(name);
		if(!pAsset) {
			if (handle) { CKLBDataHandler::releaseHandle(*handle); }
			return NULL;
		}
	}
	return pAsset;
}

CKLBNode *
CKLBUtility::createNode(const char * asset, u32 order, u32 * handle, IKLBAssetPlugin* plugIn)
{
	CKLBAsset * pAsset = loadAsset(asset, handle, plugIn);
    if(!pAsset) { return NULL; }
	CKLBNode * pNode = pAsset->createSubTree(order);
	return pNode;
}

CKLBNode *
CKLBUtility::createNodeScript(const char * asset, u32 order, u32 * handle, IKLBAssetPlugin* plugIn)
{
	CKLBAsset * pAsset = loadAssetScript(asset, handle, plugIn);
	if(!pAsset) { return NULL; }
	CKLBNode * pNode = pAsset->createSubTree(order);
	if(!pNode) {
		CKLBScriptEnv::getInstance().error("Node create failed. [asset: %s ]", asset);
	}
	return pNode;
}

CKLBNode *
CKLBUtility::createCompositeNodeScript(CKLBUITask * pTask, const char * asset, u32 order, u32 * handle,
	IKLBAssetPlugin* plugIn, u32 formWidth, u32 formHeight, CKLBDataTask* dataTask)
{
	CKLBAsset * pAsset = loadAssetScript(asset, handle, plugIn);
	if(!pAsset) { return NULL; }
	CKLBCompositeAsset* pComposite = (CKLBCompositeAsset *)pAsset;
	pComposite->setFormSize(formWidth, formHeight);
	pComposite->setDirectComposite(false);
	if(dataTask) {
		IDataSourceUpdateNotifier* notifier = dataTask;
		pComposite->setRecord(notifier->getDataSource());
	}
	CKLBNode * pNode = pComposite->createSubTree(pTask, order);

	if(!pNode) {
		CKLBScriptEnv::getInstance().error("Node create failed. [asset: %s ]", asset);
	}
	return pNode;
}


void
CKLBUtility::deleteNode(CKLBNode * pNode, u32 handle)
{
	KLBDELETE(pNode);
	CKLBDataHandler::releaseHandle(handle);
}

void
CKLBUtility::sha1File(const char* path, char* outBuf, int bufSize)
{
	*outBuf = '\0';
	if (path) {
		FILE* file = fopen(path, "rb");
		if (file) {
			unsigned char chunk[1024];
			SHA1Context context;
			SHA1Reset(&context);

			size_t size = fread(chunk, 1, sizeof(chunk), file);
			while ((int)size > 0) {
				SHA1Input(&context, chunk, (unsigned)size);
				size = fread(chunk, 1, sizeof(chunk), file);
			}
			fclose(file);

			if (SHA1Result(&context)) {
				unsigned char digest[20];
				for (int i = 0; i < 5; i++) {
					unsigned word = context.Message_Digest[i];
					digest[i * 4 + 0] = (unsigned char)(word >> 24);
					digest[i * 4 + 1] = (unsigned char)(word >> 16);
					digest[i * 4 + 2] = (unsigned char)(word >> 8);
					digest[i * 4 + 3] = (unsigned char)word;
				}

				if (bufSize == 20) {
					memcpy(outBuf, digest, sizeof(digest));
				} else if (bufSize == 40) {
					char* output = outBuf;
					for (int i = 0; i < 20; i++) {
						sprintf(output, "%02x", digest[i]);
						output += strlen(output);
					}
				} else {
					klb_assertNull(false, "sha1 buf size must be 20 or 40");
				}
			}
		}
	}
}

bool
CKLBUtility::isFileExist(const char * path)
{
	if(!path) { return false; }

	IReadStream * stream = CPFInterface::getInstance().platform().openReadStream(path, false, (u32)-1);
	if(stream) {
		IReadStream::ESTATUS status = stream->getStatus();
		KLBDELETE(stream);
		if (status == IReadStream::NORMAL) {
			return true;
		}
	}
	return false;
}

bool
CKLBUtility::endsWith(const char* string, u32 stringLength, const char* suffix, u32 suffixLength)
{
	if(!string || !suffix) {
		return string == suffix;
	}
	if(stringLength < suffixLength) {
		return false;
	}
	return strcmp(string + (stringLength - suffixLength), suffix) == 0;
}

const char *
CKLBUtility::lua2json(CLuaState&lua, size_t& streamSize, JSON_REPLACE * arrReplace)
{
	JSON_BUF jsonBuf;	// 1 KB on stack.
	jsonBuf.max = JSON_BLOCK_SIZE;
	jsonBuf.buf = KLBNEWA(char, jsonBuf.max);
	if(!jsonBuf.buf) { return NULL; }
	jsonBuf.idx			= 0;
	jsonBuf.poolCount	= 0;
	jsonBuf.poolSize	= 0;
	jsonBuf.arr_replace = arrReplace;

	bool bResult = lua2json_rec(lua, &jsonBuf);
	if(!bResult) {
		KLBDELETEA(jsonBuf.buf);
		return NULL;
	}

	// End of string.
	streamSize = jsonBuf.idx;
	return (const char *)jsonBuf.buf;
}


bool
CKLBUtility::lua2json_rec(CLuaState& lua, JSON_BUF * jsonBuf)
{
	char reversedDigits[24];
	switch(lua.getType(-1))
	{
	default:
		return false;
		break;
	case LUA_TNIL:
		{
			const char * msg = "null";
			return jsonBuf->add(msg);
		}
		break;
	case LUA_TLIGHTUSERDATA:
		{
			const void * p = lua.getPointer(-1);
			if(!p) {
				const char * msg = "null";
				return jsonBuf->add(msg);
			}
			char buf[64];
			sprintf(buf, "%p", p);
			return jsonBuf->add(buf);
		}
		break;
	case LUA_TNUMBER:
		{
			/*
			 * Preserve integral Lua numbers across the full signed 64-bit range.
			 * Build magnitude digits in reverse, then emit most-significant first.
			 * Handle the minimum signed value without overflowing during negation.
			 * Emit zero explicitly because its division loop produces no digits.
			 * This avoids narrowing through the platform integer formatter.
			 */
			char buf[64];
			// 整数か実数か判定
			s64     num_i   = (s64)lua.getDoubleUnchecked(-1);
			double  num_f   = lua.getDouble(-1);
			double  sub     = fabs((double)num_i - num_f);
			if(sub < 0.00001) {	// 整数扱い
				u32 digitCount = 0;
				bool negative = false;
				bool minimumValue = false;
				s64 magnitude = num_i;
				if(magnitude < 0) {
					minimumValue = magnitude == (-9223372036854775807LL - 1);
					magnitude += minimumValue;
					magnitude = -magnitude;
					negative = true;
				}
				while(magnitude) {
					reversedDigits[digitCount++] = '0' + (magnitude % 10);
					magnitude /= 10;
				}
				if(digitCount) {
					if(minimumValue) { reversedDigits[0]++; }
				} else {
					reversedDigits[digitCount++] = '0';
				}
				char * output = buf;
				if(negative) { *output++ = '-'; }
				for(int i = digitCount - 1; i >= 0; i--) {
					*output++ = reversedDigits[i];
				}
				*output = 0;
			} else {					// 実数扱い
				sprintf(buf, "%f", (float)num_f);
			}
			return jsonBuf->add(buf);
		}
		break;
	case LUA_TBOOLEAN:
		{
			bool b = lua.getBool(-1);
			return jsonBuf->add((b) ? "true":"false");
		}
		break;
	case LUA_TSTRING:
		{
			const char * string = lua.getString(-1);
			bool bResult = jsonBuf->add("\"");
			const char * escape_string = escape(string);
			bResult = jsonBuf->add(escape_string) && bResult;
			KLBDELETEA(escape_string);
			return  jsonBuf->add("\"") && bResult;
		}
		break;
	case LUA_TTABLE:
		{
			// Lua table は JSON的に2種類の使用方法(map/array)があるため、
			// PASS-1: 登場した table がどちらの扱いになるかを確認する
			// PASS-2: 判定結果の方法で巡回する

			// PASS-1: 文字列index付きの値があるか確認する

			int min, max;
			bool bMap = false;
			min = -1;
			max = 0;
			bool bArrResult = true;

			lua.retNil();
			while(lua.tableNext()) {
				lua.retValue(-2);

				// この段階で、(-1) = index, (-2) = 値

				// 数値index配列の場合は array 扱い
				if(lua.isNum(-1)) {
					int idx = lua.getInt(-1);
                    if(min < 0 || idx < min) { min = idx; }
                    if(max < idx)            { max = idx; }
				} else {
					bMap = true;
				}
				lua.pop(2);
				if(bMap) {
					lua.pop(1);
					break;	// 結果が出たので PASS-2 へ
				}
			}

			// PASS-2: bMap の値によって、この配列をmapまたはarrayとして取り扱う。
			if(bMap) {
				// Map として扱う
				jsonBuf->add("{");

				bool bFirst = false;

				lua.retNil();
				while(lua.tableNext()) {

					// 2番目以降の項目であれば、前の値との区切りとして ','を入れる
					if(bFirst) {
						bArrResult = jsonBuf->add(",") && bArrResult;
					}
					bFirst = true;

					// キーを出力
					lua.retValue(-2);

					// IT/KEY/VAL/KEYCOPY
					const char * key = lua.getString(-1);
					bArrResult = jsonBuf->add("\"") && bArrResult;
					bArrResult = jsonBuf->add(key) && bArrResult;
					bArrResult = jsonBuf->add("\":") && bArrResult;

					lua.pop(1);

					// 実際の Luaテーブル値を値として出力する前に、
					// JSON_REPLACEの配列を確認し、値を置き換えるべきキーは
					// 指定された値で置き換える。
					bool bReplaced = false;
					if(jsonBuf->arr_replace) {
						for(int i = 0; jsonBuf->arr_replace[i].key; i++) {
							if(!strcmp(key, jsonBuf->arr_replace[i].key)) {
								bArrResult = jsonBuf->add(jsonBuf->arr_replace[i].value) && bArrResult;
								bReplaced = true;
								break;
							}
						}
					}

					if(!bReplaced) {
						// 値を出力する
						bArrResult = lua2json_rec(lua, jsonBuf) && bArrResult;
					}

					lua.pop(1);
				}
//				lua.pop(1);	// 巡回用indexを取り除く

				bArrResult = jsonBuf->add("}") && bArrResult;

			} else {
				// Array の扱い
				// lua.tableNext() による巡回はindexの順が保障されないので、
				// 取得した min ～ max にかけて値を取得し、出力する。

				bArrResult = jsonBuf->add("[") && bArrResult;

				bool bFirst = false;

				if(min != -1 || max) {
					for(int i = min; i <= max; i++) {
						lua.retInt(i);

						// 2番目以降の項目であれば、前の値との区切りとして ','を入れる
						if(bFirst) jsonBuf->add(",");
						bFirst = true;

						lua.tableGet();

						// 値を出力する
						bArrResult = lua2json_rec(lua, jsonBuf) && bArrResult;

						lua.pop(1);
					}
				}
				bArrResult = jsonBuf->add("]") && bArrResult;
			}
			return bArrResult;
		}
		break;
	}
	//return false; // Dead code
}

const char *
CKLBUtility::escape(const char * string)
{
	size_t len = strlen(string);
	char * buf = KLBNEWA(char, len * 2 + 1);
	char * ptr = buf;
	for(int i = 0; string[i]; i++) {
		switch(string[i])
		{
		default:
			*ptr++ = string[i];
			break;
		case '"':
		case '\\':
		case '/':
			*ptr++ = '\\';
			*ptr++ = string[i];
			break;
		case '\t':
			*ptr++ = '\\';
			*ptr++ = 't';
			break;
		case '\n':
			*ptr++ = '\\';
			*ptr++ = 'n';
			break;
		case '\r':
			*ptr++ = '\\';
			*ptr++ = 'r';
			break;
		}
	}
	*ptr = 0;
	return (const char *)buf;
}

bool
CKLBUtility::JSON_BUF::add(const char * msg)
{
	size_t len = strlen(msg);

	// 現在のバッファが足りないなら、足りるようになるまで追加する
	
	while(idx + len >= max) {
		size_t size = max + (JSON_BLOCK_SIZE*2);
		char * n_buf = KLBNEWA(char, size);
        if(!n_buf) { return false; }
		memcpy(n_buf, buf, idx);
		KLBDELETEA(buf);
		buf = n_buf;
		max = size;
	}
	strcpy(buf + idx, msg);
	idx += len;

	return true;
}

bool
CKLBUtility::JSON_BUF::addU8(u8 byte)
{
	// 現在のバッファが足りないなら、足りるようになるまで追加する
	while(idx + 1 >= max) {
		size_t size = max + (JSON_BLOCK_SIZE*2);
		char * n_buf = KLBNEWA(char, size);
        if(!n_buf) { return false; }
		memcpy(n_buf, buf, idx);
		KLBDELETEA(buf);
		buf = n_buf;
		max = size;
	}
	buf[idx++] = byte;
	return true;
}

bool
CKLBUtility::JSON_BUF::addPool(const char* str, int& idx)
{
	size_t strL = strlen(str);
	u32 cnt = 0;
	while (cnt < poolCount) {
		if (poolEntries[cnt].size == strL) {
			if (strcmp(str,poolEntries[cnt].string) == 0) {
				idx = cnt;
				return true;
			}
		}
		cnt++;
	}

	// Add new entry.
	if (poolCount <= (JSON_POOL_SIZE - 1)) {
		poolEntries[poolCount].size = strL;
		poolEntries[poolCount].string = str;
		idx = poolCount++;
		poolSize += strL;
		return true;
	}
	klb_assertAlways("Max Reached");
}

bool
CKLBUtility::JSON_BUF::addU64(u64 value)
{
	addU32((u32)(value >> 32));
	addU32((u32)value);
	return true;
}

bool
CKLBUtility::JSON_BUF::addU32(u32 value) 
{
	// 現在のバッファが足りないなら、足りるようになるまで追加する
	while(idx + 4 >= max) {
		size_t size = max + (JSON_BLOCK_SIZE*2);
		char * n_buf = KLBNEWA(char, size);
        if(!n_buf) { return false; }
		memcpy(n_buf, buf, idx);
		KLBDELETEA(buf);
		buf = n_buf;
		max = size;
	}

	buf[idx++] = (value >> 24);
	buf[idx++] = (value >> 16);
	buf[idx++] = (value >> 8 );
	buf[idx++] = (value      );
	return true;
}


#include "../../libs/JSonParser/api/yajl_parse.h"

const char *
CKLBUtility::lua2BJson(CLuaState&lua, size_t& streamSize, JSON_REPLACE * arrReplace)
{
	JSON_BUF jsonBuf;	// 1 KB on stack.
	jsonBuf.max = JSON_BLOCK_SIZE * 2;
	jsonBuf.buf = KLBNEWA(char, jsonBuf.max);
    if(!jsonBuf.buf) { return NULL; }
	jsonBuf.idx			= 0;
	jsonBuf.poolCount	= 0;
	jsonBuf.poolSize	= 0;
	jsonBuf.arr_replace = arrReplace;

	bool bResult = lua2BJson_rec(lua, &jsonBuf);
	bResult = jsonBuf.addU8(BJSN_END) && bResult;
	if(!bResult) {
		KLBDELETEA(jsonBuf.buf);
		return 0;
	}

	u32 headerAndCP = (2 + 4 + 4 + (4 * jsonBuf.poolCount) + jsonBuf.poolSize); // Pool size include size with zero.
	u8* block = KLBNEWA(u8, jsonBuf.idx + headerAndCP);
	if (block) {
		memcpy(&block[headerAndCP],jsonBuf.buf,jsonBuf.idx);

		// Header
		block[0] = 0xFF;
		block[1] = 0xFF;

		// Count
		block[2] = jsonBuf.poolCount >> 24;
		block[3] = jsonBuf.poolCount >> 16;
		block[4] = jsonBuf.poolCount >>  8;
		block[5] = jsonBuf.poolCount;

		// String Buf Size
		block[6] = jsonBuf.poolSize >> 24;
		block[7] = jsonBuf.poolSize >> 16;
		block[8] = jsonBuf.poolSize >>  8;
		block[9] = jsonBuf.poolSize;

		u8* wrt = &block[10];
		for (u32 n=0; n < jsonBuf.poolCount; n++) {
			// Size
			u32 size = jsonBuf.poolEntries[n].size;
			wrt[0] = size >> 24;
			wrt[1] = size >> 16;
			wrt[2] = size >>  8;
			wrt[3] = size;

			memcpy(&wrt[4], jsonBuf.poolEntries[n].string, size);
			wrt += size + 4;
		}

		KLBDELETEA(jsonBuf.buf);
	} else {
		KLBDELETEA(jsonBuf.buf);
		return NULL;
	}

	jsonBuf.buf = (char*)block;
	jsonBuf.idx += headerAndCP;

	// End of BJson stream.
	streamSize = jsonBuf.idx;
	return (const char *)jsonBuf.buf;
}

/*static*/
bool
CKLBUtility::lua2BJson_rec(CLuaState& lua, JSON_BUF * jsonBuf)
{
	switch(lua.getType(-1))
	{
	default:
		return false;
		break;
	case LUA_TNIL:
		{
			return jsonBuf->addU8(BJSN_CTE_NULL);
		}
		break;
	case LUA_TLIGHTUSERDATA:
		{
			const void * p = lua.getPointer(-1);
			if(!p) {
				return jsonBuf->addU8(BJSN_CTE_NULL);
			} 
			char buf[64];
			sprintf(buf, "%p", p);
			bool bResult  = jsonBuf->addU8(BJSN_STRING_DIRECT);
			bResult = bResult && jsonBuf->addU32(strlen(buf));
			bResult = bResult && jsonBuf->add(buf);
			return bResult;
		}
		break;
	case LUA_TNUMBER:
		{
			// 整数か実数か判定
			s64 num_i = (s64)lua.getDoubleUnchecked(-1);
			double num_f = lua.getDouble(-1);

			double sub = fabs((double)num_i - num_f);
			bool result;
			if(sub < 0.00001) {	// 整数扱い
				result  = jsonBuf->addU8(BJSN_NUMBER_I64);
				result &= jsonBuf->addU32((u32)(((u64)num_i) >> 32));
				result &= jsonBuf->addU32((u32)num_i);
			} else {					// 実数扱い
				float f = (float)num_f;
				int* pI = (int*)&f;
				result  = jsonBuf->addU8(BJSN_NUMBER_FLT);
				result &= jsonBuf->addU32(*pI);
			}
			return result;
		}
		break;
	case LUA_TBOOLEAN:
		{
			bool b = lua.getBool(-1);
			return jsonBuf->addU8((b) ? BJSN_CTE_TRUE:BJSN_CTE_FALSE);
		}
		break;
	case LUA_TSTRING:
		{
			const char * string = lua.getString(-1);
			//
			// DO NOT ESCAPE ENCODE THE STRING in BJSON !
			// --> Encode is made only inside std Json because Json itself is a string.
			// const char * escape_string = escape(string);
			//
			// int len = 0;
			bool bResult;
			bResult = jsonBuf->addU8(BJSN_STRING_DIRECT);
			bResult = bResult && jsonBuf->addU32(strlen(string));
			bResult = bResult && jsonBuf->add(string);
			return bResult;
		}
		break;
	case LUA_TTABLE:
		{
			// Lua table は JSON的に2種類の使用方法(map/array)があるため、
			// PASS-1: 登場した table がどちらの扱いになるかを確認する
			// PASS-2: 判定結果の方法で巡回する

			// PASS-1: 文字列index付きの値があるか確認する

			int min, max;
			bool bMap = false;
			min = -1;
			max = 0;
			bool bArrResult = true;

			lua.retNil();
			while(lua.tableNext()) {
				lua.retValue(-2);

				// この段階で、(-1) = index, (-2) = 値

				// 数値index配列の場合は array 扱い
				if(lua.isNum(-1)) {
					int idx = lua.getInt(-1);
                    if(min < 0 || idx < min) { min = idx; }
                    if(max < idx)            { max = idx; }
				} else {
					bMap = true;
				}
				lua.pop(2);
				if(bMap) {
					lua.pop(1);
					break;	// 結果が出たので PASS-2 へ
				}
			}

			// PASS-2: bMap の値によって、この配列をmapまたはarrayとして取り扱う。
			if(bMap) {
				// Map として扱う
				bArrResult = jsonBuf->addU8(BJSN_OPEN_OBJ);
				bArrResult = jsonBuf->addU32(0xFFFFFFFF) && bArrResult;

				lua.retNil();
				while(lua.tableNext()) {
					// キーを出力
					lua.retValue(-2);

					// IT/KEY/VAL/KEYCOPY
					const char * key = lua.getString(-1);
					bArrResult = jsonBuf->addU8(BJSN_MEMBER) && bArrResult;
					int idx;
					bArrResult = jsonBuf->addPool(key, idx) && bArrResult;
					bArrResult = jsonBuf->addU32(idx) && bArrResult;

					lua.pop(1);

					// 実際の Luaテーブル値を値として出力する前に、
					// JSON_REPLACEの配列を確認し、値を置き換えるべきキーは
					// 指定された値で置き換える。
					bool bReplaced = false;
					if(jsonBuf->arr_replace) {
						for(int i = 0; jsonBuf->arr_replace[i].key; i++) {
							if(!strcmp(key, jsonBuf->arr_replace[i].key)) {
								bArrResult = jsonBuf->add(jsonBuf->arr_replace[i].value) && bArrResult;

								bArrResult  = jsonBuf->addU8(BJSN_STRING_DIRECT);
								bArrResult &= jsonBuf->addU32(strlen(jsonBuf->arr_replace[i].value));
								bArrResult &= jsonBuf->add(jsonBuf->arr_replace[i].value);

								bReplaced = true;
								break;
							}
						}
					}

					if(!bReplaced) {
						// 値を出力する
						bArrResult = lua2BJson_rec(lua, jsonBuf) && bArrResult;
					}

					lua.pop(1);
				}
//				lua.pop(1);	// 巡回用indexを取り除く

				bArrResult = jsonBuf->addU8(BJSN_CLOSE_OBJ) && bArrResult;

			} else {
				// Array の扱い
				// lua.tableNext() による巡回はindexの順が保障されないので、
				// 取得した min ～ max にかけて値を取得し、出力する。

				bArrResult  = jsonBuf->addU8(BJSN_OPEN_ARR) && bArrResult;
				bArrResult &= jsonBuf->addU32(0xFFFFFFFF);
				bArrResult &= jsonBuf->addU32(0xFFFFFFFF); // Mask

				if(min != -1 || max) {
					for(int i = min; i <= max; i++) {
						lua.retInt(i);
	
						lua.tableGet();
		
						// 値を出力する
						bArrResult = lua2BJson_rec(lua, jsonBuf) && bArrResult;

						lua.pop(1);
					}
				}
				bArrResult = jsonBuf->addU8(BJSN_CLOSE_ARR) && bArrResult;
			}
			return bArrResult;
		}
		break;
	}
	//return false;	// Dead code.
}


// JSON文字列をLuaテーブルに変換し、Luaスタックに積む
void
CKLBUtility::json2lua(CLuaState& lua, const char * json, u32 json_size)
{
	CKLBJsonItem * pRoot = CKLBJsonItem::ReadJsonData(json, json_size);
	if(pRoot) {
		jsonItem2lua(lua, pRoot);
		KLBDELETE(pRoot);
	} else {
		lua.retNil();
	}
}

// CKLBJsonItemのツリーをLuaテーブルに変換し、Luaスタックに積む
void
CKLBUtility::jsonItem2lua(CLuaState& lua, CKLBJsonItem * pRoot)
{
	if (pRoot) {
		json2lua_rec(lua, pRoot);
	}
}

void
CKLBUtility::json2lua_rec(CLuaState& lua, CKLBJsonItem * pItem)
{
	switch(pItem->getType())
	{
	case CKLBJsonItem::J_MAP:	// 文字列indexの連想配列
		{
			lua.tableNew();
			CKLBJsonItem * child = pItem->child();
			while(child) {
				lua.retString(child->key());
				json2lua_rec(lua, child);
				lua.tableSet();
				child = child->next();
			}
		}
		break;
	case CKLBJsonItem::J_ARRAY:	// 数値indexの連想配列
		{
			lua.tableNew();
			CKLBJsonItem * child = pItem->child();
			int index = 1;
			while(child) {
				lua.retInt(index);
				json2lua_rec(lua, child);
				lua.tableSet();
				child = child->next();
				index++;
			}
		}
		break;
	case CKLBJsonItem::J_BOOLEAN:
		{
			lua.retBoolean(pItem->getBool());
		}
		break;
	case CKLBJsonItem::J_INT:
		{
			lua.retDouble(pItem->getInt64());
		}
		break;
	case CKLBJsonItem::J_DOUBLE:
		{
			lua.retDouble(pItem->getDouble());
		}
		break;
	case CKLBJsonItem::J_STRING:
		{
			lua.retString(pItem->getString());
		}
		break;
	case CKLBJsonItem::J_NULL:
		{
			lua.retPointer(0);
//			lua.retNil();
		}
		break;
	}
}

const char*
CKLBUtility::insertAssetDirPrefix(const char* fullPath, const char* subFolder, size_t logicalPathLength)
{
	klb_assertNull(subFolder, "Do not support empty subFolder");
	size_t subFolderLength = strlen(subFolder);
	klb_assertNull(subFolderLength, "Do not support empty subFolder");
	size_t fullPathLength = strlen(fullPath);
	char* result = KLBNEWA(char, fullPathLength + subFolderLength + 2);
	size_t basePathLength = fullPathLength - logicalPathLength;

	memcpy(result, fullPath, basePathLength);
	memcpy(result + basePathLength, subFolder, subFolderLength);
	size_t outputOffset = basePathLength + subFolderLength;
	result[outputOffset++] = fullPath[fullPathLength - (logicalPathLength + 1)];
	memcpy(result + outputOffset,
		   fullPath + basePathLength,
		   logicalPathLength);
	result[outputOffset + logicalPathLength] = 0;
	return result;
}

bool
CKLBUtility::hasAssetDirPrefix(const char* fullPath, const char* subFolder, size_t logicalPathLength)
{
	bool hasPrefix = true;
	if (subFolder) {
		size_t subFolderLength = strlen(subFolder);
		if (subFolderLength && subFolderLength < logicalPathLength) {
			size_t fullPathLength = strlen(fullPath);
			size_t basePathLength = fullPathLength - logicalPathLength;
			const char* logicalPath = fullPath + basePathLength;
			char separator = logicalPath[subFolderLength];
			bool hasSeparator = separator == '/' || separator == '\\';
			hasPrefix = !strncmp(subFolder, logicalPath, subFolderLength) & hasSeparator;
		}
	}
	return hasPrefix;
}

s32
CKLBUtility::numStringS64(char * buf, s64 value)
{
	// 符号付き64bit整数を数列文字列に変換し、符号を含む桁数を返す。
	const s64	MINIMUM_VALUE	= (s64)0x8000000000000000LL;

	char	digits[32];
	s32		length		= 0;
	bool	negative	= false;
	bool	minimum		= false;

	if (value < 0) {
		// INT64_MIN は符号反転できないので、1 を足してから反転し最後に戻す。
		minimum		= (value == MINIMUM_VALUE);
		value		= -(value + (minimum ? 1 : 0));
		negative	= true;
	}
	while (value) {
		digits[length++] = (char)('0' + (value % 10));
		value /= 10;
	}
	if (length) {
		if (minimum) { digits[0]++; }
	} else {
		digits[0] = '0';
		length = 1;
	}
	if (negative) { *buf++ = '-'; }
	s32 source = length;
	s32 output = 0;
	while (--source >= 0) { buf[output++] = digits[source]; }
	buf[length] = 0;
	return length + (negative ? 1 : 0);
}

char *
CKLBUtility::replaceString(const char * string, const char * find, const char * replace)
{
	// 置換位置を先にすべて集めてから必要な長さを確定し、バッファを一度だけ確保する。
	const size_t	GROWTH_START	= 16;
	const size_t	GROWTH_LIMIT	= 1024 * 1024;

	size_t			findLength	= strlen(find);
	const char *	found		= strstr(string, find);
	const char *	rest		= string;
	size_t *		positions	= NULL;
	size_t			count		= 0;
	size_t			capacity	= 0;
	size_t			growth		= GROWTH_START;
	char *			result		= NULL;
	bool			outOfMemory	= false;

	while (found) {
		if (capacity < count + 1) {
			capacity += growth;
			positions = (size_t *)realloc(positions, sizeof(size_t) * capacity);
			if (!positions) {
				outOfMemory = true;
				break;
			}
			growth *= 3;
			if (growth > GROWTH_LIMIT) { growth = GROWTH_LIMIT; }
		}
		positions[count] = found - string;
		count++;
		rest = found + findLength;
		found = strstr(rest, find);
	}

	if (!outOfMemory) {
		size_t sourceLength	= rest - string;
		size_t length		= strlen(rest);
		sourceLength += length;
		size_t resultLength	= sourceLength;
		if (count) {
			length = strlen(replace);
			resultLength = sourceLength + (length - findLength) * count;
		}
		char * buffer = (char *)malloc(resultLength + 1);
		if (buffer) {
			if (count) {
				memcpy(buffer, string, positions[0]);
				char *			out			= buffer + positions[0];
				const char *	tail		= NULL;
				size_t			end			= 0;
				size_t			tailLength	= 0;
				for (size_t i = 0; i < count; i++) {
					memcpy(out, replace, length);
					out += length;
					tail		= string + positions[i] + findLength;
					end			= (i == count - 1) ? sourceLength : positions[i + 1];
					tailLength	= end - positions[i] - findLength;
					memcpy(out, tail, tailLength);
					out += tailLength;
				}
				buffer[resultLength] = 0;
			} else {
				strcpy(buffer, string);
			}
			result = buffer;
		}
	}
	free(positions);
	return result;
}

/*static*/
char *
CKLBUtility::URLencode(char * retbuf, int maxlen, const char ** postForm)
{
	// 指定された postForm から、POST文字列を生成する。
	// postForm は URLencodeされておらず、個々の項目が連結されていない状態。
	char * basebuf = retbuf;
	int now_size = maxlen;

    if(!basebuf) { return NULL; } 

	char * ptr = basebuf;
	for(int i = 0; postForm[i]; i++) {
		const char * formItem = postForm[i];

		if(i > 0) {
			*ptr++ = '&';
			if(ptr - basebuf >= now_size - 1) {
				// バッファが与えられている場合はエラー終了
				return 0;
			}
		}
		for(const char * src = formItem; *src; src++) {
			int reqsize = 1;
			char tmpbuf[10];
			const char * data = NULL;
			if(('0'<= *src && *src <= '9') ||
				('a'<= *src && *src <= 'z') ||
				('A'<= *src && *src <= 'Z') ||
				*src == '_' || *src == '-' ||
				*src == '.' || *src == '*' || *src == '=') {
	
				// そのままいける文字
				data = src;
			} else if(*src == ' ') {

				// 空白の置き換え
				data = "+";

			} else {
				reqsize = 3;
				sprintf(tmpbuf, "%%%02x", (int)*(unsigned char *)src);
				data = (const char *)tmpbuf;
			}
			if((ptr + reqsize) - basebuf >= now_size - 1) {
				// バッファが与えられている場合はエラー終了
				return 0;
			}
			memcpy(ptr, data, reqsize);
			ptr += reqsize;
		}
	}
	*ptr = 0;
	return basebuf;
}

bool
CKLBUtility::saveBMP(const char * path, u32 width, u32 height, const char * pixels)
{
	FILE * file = fopen(path, "wb");
	if (!file) { return false; }

	u32 fileSize = 14 + 40 + (width * height) * 4;

	u8 fileHeader[14] = { 0 };
	fileHeader[ 0]	= 'B';
	fileHeader[ 1]	= 'M';
	fileHeader[ 2]	= (u8)(fileSize      );
	fileHeader[ 3]	= (u8)(fileSize >>  8);
	fileHeader[ 4]	= (u8)(fileSize >> 16);
	fileHeader[ 5]	= (u8)(fileSize >> 24);
	fileHeader[10]	= 14 + 40;

	u8 infoHeader[40] = { 0 };
	infoHeader[ 0]	= 40;
	infoHeader[ 4]	= (u8)(width       );
	infoHeader[ 5]	= (u8)(width  >>  8);
	infoHeader[ 6]	= (u8)(width  >> 16);
	infoHeader[ 7]	= (u8)(width  >> 24);
	infoHeader[ 8]	= (u8)(height      );
	infoHeader[ 9]	= (u8)(height >>  8);
	infoHeader[10]	= (u8)(height >> 16);
	infoHeader[11]	= (u8)(height >> 24);
	infoHeader[12]	= 1;	// plane count
	infoHeader[14]	= 32;	// bit per pixel

	fwrite(fileHeader, 1, sizeof(fileHeader), file);
	fwrite(infoHeader, 1, sizeof(infoHeader), file);

	// BMP は最下段の走査線から順に格納するため、行を逆順に書き出す。
	for (s32 y = height - 1; y >= 0; y--) {
		const char * src = pixels + (y * width) * 4;
		for (u32 x = 0; x < width; x++) {
			u8 bgra[4];
			bgra[0] = src[2];
			bgra[1] = src[1];
			bgra[2] = src[0];
			if (src[3] >= 0) {
				// 半透明以下のピクセルは色を反転させて可視化する
				bgra[0] = ~bgra[0];
				bgra[1] = ~bgra[1];
				bgra[2] = ~bgra[2];
			}
			bgra[3] = 0xFF;
			fwrite(bgra, sizeof(bgra), 1, file);
			src += 4;
		}
	}
	fclose(file);
	return true;
}

// The shipped codec follows Apache APR-util's apr_base64.c implementation.
static const u8 s_base64DecodeTable[256] = {
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
	64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
	64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static const char s_base64EncodeTable[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int Base64decode_len(const char* bufcoded)
{
	int nbytesdecoded;
	register const unsigned char* bufin;
	register int nprbytes;

	bufin = reinterpret_cast<const unsigned char*>(bufcoded);
	while (s_base64DecodeTable[*(bufin++)] <= 63);

	nprbytes = bufin - reinterpret_cast<const unsigned char*>(bufcoded) - 1;
	nbytesdecoded = ((nprbytes + 3) / 4) * 3;

	return nbytesdecoded + 1;
}

int Base64decode(char* bufplain, const char* bufcoded)
{
	int nbytesdecoded;
	register const unsigned char* bufin;
	register unsigned char* bufout;
	register int nprbytes;

	bufin = reinterpret_cast<const unsigned char*>(bufcoded);
	while (s_base64DecodeTable[*(bufin++)] <= 63);
	nprbytes = bufin - reinterpret_cast<const unsigned char*>(bufcoded) - 1;
	nbytesdecoded = ((nprbytes + 3) / 4) * 3;

	bufout = reinterpret_cast<unsigned char*>(bufplain);
	bufin = reinterpret_cast<const unsigned char*>(bufcoded);

	while (nprbytes > 4) {
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[*bufin] << 2
			| s_base64DecodeTable[bufin[1]] >> 4);
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[bufin[1]] << 4
			| s_base64DecodeTable[bufin[2]] >> 2);
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[bufin[2]] << 6
			| s_base64DecodeTable[bufin[3]]);
		bufin += 4;
		nprbytes -= 4;
	}

	if (nprbytes > 1) {
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[*bufin] << 2
			| s_base64DecodeTable[bufin[1]] >> 4);
	}
	if (nprbytes > 2) {
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[bufin[1]] << 4
			| s_base64DecodeTable[bufin[2]] >> 2);
	}
	if (nprbytes > 3) {
		*(bufout++) = static_cast<unsigned char>(
			s_base64DecodeTable[bufin[2]] << 6
			| s_base64DecodeTable[bufin[3]]);
	}

	*bufout = 0;
	nbytesdecoded -= (4 - nprbytes) & 3;
	return nbytesdecoded;
}

int Base64encode_len(int len)
{
	return ((len + 2) / 3 * 4) + 1;
}

int Base64encode(char* encoded, const char* string, int len)
{
	int i;
	char* p;

	p = encoded;
	for (i = 0; i < len - 2; i += 3) {
		*p++ = s_base64EncodeTable[(string[i] >> 2) & 0x3f];
		*p++ = s_base64EncodeTable[((string[i] & 3) << 4)
								| ((string[i + 1] & 0xf0) >> 4)];
		*p++ = s_base64EncodeTable[((string[i + 1] & 0xf) << 2)
								| ((string[i + 2] & 0xc0) >> 6)];
		*p++ = s_base64EncodeTable[string[i + 2] & 0x3f];
	}
	if (i < len) {
		*p++ = s_base64EncodeTable[(string[i] >> 2) & 0x3f];
		if (i == (len - 1)) {
			*p++ = s_base64EncodeTable[((string[i] & 3) << 4)];
			*p++ = '=';
		} else {
			*p++ = s_base64EncodeTable[((string[i] & 3) << 4)
									| ((string[i + 1] & 0xf0) >> 4)];
			*p++ = s_base64EncodeTable[((string[i + 1] & 0xf) << 2)];
		}
		*p++ = '=';
	}

	*p++ = 0;
	return p - encoded;
}

void
KLBNetAPI_encodeBase64(const char* source, int sourceLength, char* destination, u32* outputLength)
{
	*outputLength = Base64encode(destination, source, sourceLength);
}

void
KLBNetAPI_decodeBase64(const char* source, char* destination, u32* outputLength)
{
	*outputLength = Base64decode(destination, source);
}
