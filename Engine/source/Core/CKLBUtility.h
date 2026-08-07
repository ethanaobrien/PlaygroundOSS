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
#ifndef CKLBUtility_h
#define CKLBUtility_h

#include "CKLBAsset.h"
#include "CKLBNode.h"
#include "CKLBDataHandler.h"
#include "CLuaState.h"
#include "CKLBJsonItem.h"

class CKLBDataTask;

#ifdef DEBUG_PERFORMANCE
	#define INTERNAL_BENCH
#endif

#ifdef INTERNAL_BENCH
	void logDo			();
	void logTime		(char type, s64 time, u32 classID);
	void logTimeDump	();
	void logStartTime	();
	void logEndTime		(char type, const char* name);
#else
	#define logStartTime()		;
	#define logEndTime(a,b)		;
#endif

class CKLBUtility
{
public:

	/* Return the closest upper 2^n value */
	static u32 nearest2Pow(u32 v);

	static char * URLencode(char * retbuf, int maxlen, const char ** postForm);

	// 指定されたCKLBNodeが、直前のフレームで描画された画面座標(絶対位置)を取得する
	static void getNodePosition		(CKLBNode * pNode, float * x, float * y);

	// Get Inverse matrix from node.
	static void getNodeInverseMatrix(CKLBNode * pNode, SMatrix2D& result);

	// Compute transform.
	static void transform(SMatrix2D* pMatrix, float x, float y, float& resx, float& resy);

	// Allow to use NULL.
	static int safe_strcmp(const char* a, const char* b);

	// 文字列をコピーする:メモリトラッキング対応。取得した文字列は KLBDELETEA() で破棄
	static const char * copyString(const char * string);

	// Allocate and copy a memory block.
	static const char * copyMem(const char* buff, u64 size);

	// 64bit整数を数列文字列に変換する(printf系が対応していない環境が存在することを想定)
	static char * numString64(char * buf, u64 value);

	// 符号付きの変換。符号を含む桁数を返す。
	static s32 numStringS64(char * buf, s64 value);

	// 数列文字列を s64 に変換する
	static s64 stringNum64(const char * buf);
	static s64 stringNum(const char * buf);

	// Return the number of character inside a UTF8 string.
	static s32 charCountUtf8(const char *string);

	// 指定された文字列がnullであれば"null"という文字列を、そうでなければ""で囲んだ形の文字列を返す
	static char * quoated(const char * string);

	// 文字列を指定文字で分割し、文字列の配列として返す。各文字列と、全体の配列はKLBDELETEA()で破棄
	static const char ** splitString(const char * string, int delim);

	// splitString() で得られた分割文字列配列を破棄する。
	static void deleteSplitString(const char ** split_arr);

	// asset をメモリバッファ上からロードし、ハンドル登録も行う。loadAssetと異なり一段しか読まない(imagなどには使えない)
	static CKLBAsset * readAsset(u8 * stream, size_t streamSize,u32 * handle, IKLBAssetPlugin * plugIn = NULL);

	// asset のロード(ハンドル登録も行う)
	static CKLBAsset * loadAsset(const char * asset, u32 * handle, IKLBAssetPlugin* plugIn = NULL, bool bSimple = false);

	// loadAsset に失敗したらScriptのレベルでエラーを出す
	static CKLBAsset * loadAssetScript(const char * asset, u32 * handle, IKLBAssetPlugin* plugIn = NULL, bool bSimple = false);

	// asset をロードし、ノードを生成する(同時にハンドル登録も行う)
	static CKLBNode * createNode(const char * asset, u32 order, u32 * handle, IKLBAssetPlugin* plugIn = NULL);

	// createNode() に失敗したらScriptのレベルでエラーを出す
	static CKLBNode * createNodeScript(const char * asset, u32 order, u32 * handle, IKLBAssetPlugin* plugIn = NULL);

	// Composite asset が親タスクのポインタを必要とするので、Composite専用
	static CKLBNode * createCompositeNodeScript(CKLBUITask * pTask, const char * asset, u32 order, u32 * handle,
		IKLBAssetPlugin* plugIn = NULL, u32 formWidth = (u32)-1, u32 formHeight = (u32)-1,
		CKLBDataTask* dataTask = NULL);

	// createNode()で得られたノードとハンドルを破棄する
	static void deleteNode(CKLBNode * pNode, u32 handle);

	// 32bit RGBA のピクセル列を、無圧縮 BMP ファイルとして書き出す(デバッグ用)
	static bool saveBMP(const char * path, u32 width, u32 height, const char * pixels);

	static void sha1File(const char* path, char* outBuf, int bufSize);

	// Return whether a readable file exists at the platform path.
	static bool isFileExist(const char * path);
	static bool endsWith(const char* string, u32 stringLength, const char* suffix, u32 suffixLength);

	// Replace every occurrence of find inside string. The result is a newly
	// allocated C string owned by the caller and released with free().
	static char * replaceString(const char * string, const char * find, const char * replace);

	struct JSON_REPLACE {
		const char * key;
		const char * value;
	};

	// Luaのスタックに積まれたテーブルをJSON文字列に変換する
	static const char * lua2json(CLuaState& lua, size_t& streamSize, JSON_REPLACE * arrReplace = NULL);

	// Luaのスタックに積まれたテーブルをBJSONに変換する
	static const char * lua2BJson(CLuaState& lua, size_t& streamSize, JSON_REPLACE * arrReplace = NULL);

	// JSON文字列をLuaテーブルに変換し、Luaスタックに積む
	static void json2lua(CLuaState& lua, const char * json, u32 json_size = 0);

	// CKLBJsonItem のツリーをLuaテーブルに変換し、Luaスタックに積む
	static void jsonItem2lua(CLuaState& lua, CKLBJsonItem * pRoot);

	// Insert a directory component immediately before the logical asset path.
	// logicalPathLength describes the suffix portion of fullPath.
	static const char* insertAssetDirPrefix(const char* fullPath, const char* subFolder, size_t logicalPathLength);
	static bool hasAssetDirPrefix(const char* fullPath, const char* subFolder, size_t logicalPathLength);

	// SQLite
private:

	// Utility用定数
	enum {
		JSON_BLOCK_SIZE = 64,
		JSON_POOL_SIZE = 100
	};

	struct CPENTRY {
		const char* string;
		int			size;
	};

	struct JSON_BUF {
		char	*	buf;	// 生成先バッファのポインタ
		size_t		max;	// 生成先バッファの現在のサイズ
		size_t		idx;	// 生成先バッファの成長点index

		bool add	(const char * msg);
		bool addU8	(u8 value);
		bool addPool(const char* str,int& index);
		bool addU32	(u32 value);
		bool addU64	(u64 value);

		JSON_REPLACE * arr_replace;
		CPENTRY	poolEntries[JSON_POOL_SIZE];
		u32		poolCount;
		u32		poolSize;
	};
	static bool lua2json_rec	(CLuaState& lua, JSON_BUF * jsonBuf);
	static bool lua2BJson_rec	(CLuaState& lua, JSON_BUF * jsonBuf);

	static void json2lua_rec	(CLuaState& lua, CKLBJsonItem * pItem);
	static const char * escape	(const char * string);
};

#endif // CKLBUtility_h
