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
#include "CKLBUtility.h"
#include "CKLBUpdate.h"
#include "KLBBase64.h"
#include "TextureManagement.h"
#include "encryptFile.h"
#include <dirent.h>
#ifdef __ANDROID__
#include "CAndroidPathConv.h"
#endif

// Encoding codes a script gives to the grid texture loader.  Three and four
// are the raw channel counts TextureAssetPixelFormat uses; five and six select
// the two compressed decoders CKLBGridTextureObject::updateCell dispatches on.
enum {
	ASSET_ENCODING_RGB	= 3,
	ASSET_ENCODING_RGBA	= 4,
	ASSET_ENCODING_PNG	= 5,
	ASSET_ENCODING_JPG	= 6
};

static ILuaFuncLib::DEFCONST luaConst[] = {
//	{ "DBG_M_SWITCH",	DBG_MENU::M_SWITCH },
	{ "ASSET_RGB",	ASSET_ENCODING_RGB },
	{ "ASSET_RGBA",	ASSET_ENCODING_RGBA },
	{ "ASSET_PNG",	ASSET_ENCODING_PNG },
	{ "ASSET_JPG",	ASSET_ENCODING_JPG },
	{ 0, 0 }
};

static CKLBLuaLibASSET libdef(luaConst);

static inline void
returnPointerOrFalse(CLuaState& lua, void* pointer)
{
	if(pointer) {
		lua.retPointer(pointer);
	} else {
		lua.retBool(false);
	}
}

// Asset information queries must observe a real miss rather than a configured
// not-found replacement. Link streams remain enabled so an existing link can
// still resolve; the manager's probe sentinel is released and reported as a
// miss before returning to Lua.
static CKLBAsset*
loadAssetInfoProbe(const char* asset, u32* handle)
{
	static const u16 ASSET_INFO_PROBE_ID = 0xFFFE;
	CKLBAssetManager& manager = CKLBAssetManager::getInstance();
	manager.setAssetNotFoundEnable(false);
	manager.setKeepLinkStream(true);
	CKLBImageAsset* result = (CKLBImageAsset*)CKLBUtility::loadAsset(asset, handle);
	manager.setAssetNotFoundEnable(true);
	manager.setKeepLinkStream(false);
	if(result && result->getAssetID() == ASSET_INFO_PROBE_ID) {
		result->resetAssetID();
		result->decrementRefCount();
		result = NULL;
	}
	return result;
}

CKLBLuaLibASSET::CKLBLuaLibASSET(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibASSET::~CKLBLuaLibASSET() {}

// Lua関数の追加
void
CKLBLuaLibASSET::addLibrary()
{
	addFunction("ASSET_getImageSize",		CKLBLuaLibASSET::luaGetImageSize);
	addFunction("ASSET_getBoundSize",		CKLBLuaLibASSET::luaGetBoundSize);
	addFunction("ASSET_getAssetInfo",		CKLBLuaLibASSET::luaGetAssetInfo);
	addFunction("ASSET_delExternal",		CKLBLuaLibASSET::luaDelExternal);
	addFunction("ASSET_getExternalFree",	CKLBLuaLibASSET::luaGetExternalFree);
	addFunction("ASSET_getAssetPathIfNotExist", CKLBLuaLibASSET::luaGetAssetPathIfNotExist);
	addFunction("ASSET_getFileList",		CKLBLuaLibASSET::luaGetFileList);
	addFunction("ASSET_CreateImageTexture",	CKLBLuaLibASSET::luaCreateImageTexture);
	addFunction("ASSET_GridRequestCache",	CKLBLuaLibASSET::luaGridRequestCache);
	addFunction("ASSET_LoadImage",			CKLBLuaLibASSET::luaLoadImage);
	addFunction("ASSET_GridLock",			CKLBLuaLibASSET::luaGridLock);
	addFunction("ASSET_GridError",			CKLBLuaLibASSET::luaGridError);
	addFunction("ASSET_MipmapOnce",		CKLBLuaLibASSET::luaMipmapOnce);
	addFunction("ASSET_GridSetDieCallback",	CKLBLuaLibASSET::luaGridSetDieCallback);
	addFunction("ASSET_registerNotFound",	CKLBLuaLibASSET::luaRegisterNotFound);
	addFunction("ASSET_setPlaceHolder",		CKLBLuaLibASSET::luaSetPlaceHolder);
	addFunction("ASSET_startDownload",		CKLBLuaLibASSET::luaStartDownload);
	addFunction("ASSET_pauseDownload",		CKLBLuaLibASSET::luaPauseDownload);
	addFunction("ASSET_killDownload",		CKLBLuaLibASSET::luaKillDownload);
	addFunction("ASSET_BGFilterSetup",		CKLBLuaLibASSET::luaBGFilterSetup);
	addFunction("ASSET_registerFilter",		CKLBLuaLibASSET::luaRegisterFilter);
	addFunction("ASSET_setEID",				CKLBLuaLibASSET::luaSetEID);
	addFunction("ASSET_enableTextureBorderPatch", CKLBLuaLibASSET::luaEnableTextureBorderPatch);
	addFunction("Asset_getNMAssetSize",		CKLBLuaLibASSET::luaGetNMAssetSize);
	addFunction("Asset_setNMAssetSize",		CKLBLuaLibASSET::luaSetNMAssetSize);
	addFunction("Asset_setNMAsset",			CKLBLuaLibASSET::luaSetNMAsset);
	addFunction("Asset_getNMAsset",			CKLBLuaLibASSET::luaGetNMAsset);
}

s32
CKLBLuaLibASSET::luaSetPlaceHolder(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBool(false);
		return 1;
	}

	CKLBAssetManager& manager = CKLBAssetManager::getInstance();
	const char* asset = lua.getString(1);
	lua.retBool(manager.setPlaceHolder(asset));
	return 1;
}

s32
CKLBLuaLibASSET::luaGetImageSize(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retNil();
		lua.retNil();
		return 2;
	}

	const char * asset_name = lua.getString(1);
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)loadAssetInfoProbe(asset_name, &handle);
	if(!pAsset) {
		lua.retNil();
		lua.retNil();
		return 2;
	}

	SKLBRect * rect = pAsset->getSize();

	s32 width = rect->getWidth();
	s32 height = rect->getHeight();

	CKLBDataHandler::releaseHandle(handle);

	lua.retInt(width);
	lua.retInt(height);
	return 2;	
}

s32
CKLBLuaLibASSET::luaGetBoundSize(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retNil();
		lua.retNil();
		return 2;
	}
	const char * asset_name = lua.getString(1);
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)loadAssetInfoProbe(asset_name, &handle);
	if(!pAsset) {
		lua.retNil();
		lua.retNil();
		return 2;
	}
	float width = pAsset->m_boundWidth;
	float height = pAsset->m_boundHeight;

	CKLBDataHandler::releaseHandle(handle);

	lua.retDouble(width);
	lua.retDouble(height);
	return 2;	

}

s32
CKLBLuaLibASSET::luaGetAssetInfo(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retNil();
		lua.retNil();
		lua.retNil();
		lua.retNil();
		return 4;
	}
	const char * asset_name = lua.getString(1);
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)loadAssetInfoProbe(asset_name, &handle);
	if(!pAsset) {
		lua.retNil();
		lua.retNil();
		lua.retNil();
		lua.retNil();
		return 4;
	}
	SKLBRect * rect = pAsset->getSize();
	s32 img_width = rect->getWidth();
	s32 img_height = rect->getHeight();
	float bound_width = pAsset->m_boundWidth;
	float bound_height = pAsset->m_boundHeight;

	CKLBDataHandler::releaseHandle(handle);

	lua.retInt(img_width);
	lua.retInt(img_height);
	lua.retDouble(bound_width);
	lua.retDouble(bound_height);

	return 4;
}

s32
CKLBLuaLibASSET::luaDelExternal(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retBool(false);
		return 1;
	}
	const char * asset_name = lua.getString(1);
	bool res = CPFInterface::getInstance().platform().removeFileOrFolder(asset_name);
#ifdef __ANDROID__
	CKLBPathConv::getInstance().ensureExternalDirectory();
#endif
	lua.retBool(res);
	return 1;
}

s32
CKLBLuaLibASSET::luaGetExternalFree(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc != 0) {
		lua.retInt(0);
		return 1;
	}
	s32 res = (s32)CPFInterface::getInstance().platform().getFreeSpaceExternalKB(); // Never return more than 0xFFFFFF
	lua.retInt(res);
	return 1;
}

s32
CKLBLuaLibASSET::luaGetAssetPathIfNotExist(lua_State* L)
{
	CLuaState lua(L);
	char assetPath[2000];

	if(lua.numArgs() > 0 && lua.isString(1)) {
		const char* filePath = lua.getString(1);
		if(filePath) {
				u32 length = strlen(filePath);
				bool isTexture = CKLBUtility::endsWith(filePath, length, ".texb", 5);
				bool isImage = CKLBUtility::endsWith(filePath, length, ".imag", 5);

				klb_assertNull(
					!CKLBUtility::endsWith(filePath, length, ".ogg", 4) &&
					!CKLBUtility::endsWith(filePath, length, ".mp3", 4),
					"ASSET_getAssetPathIfNotExist : Never use a .ogg or .mp3 extension. Audio Asset have none, automatically detected inside."
				);

				if(!isImage && !isTexture) {
					sprintf(assetPath, "asset://%s.ogg", filePath);
					if(CKLBUtility::isFileExist(assetPath)) {
						lua.retNil();
						return 1;
					}
					sprintf(assetPath, "asset://%s.mp3", filePath);
					if(CKLBUtility::isFileExist(assetPath)) {
						lua.retNil();
						return 1;
					}
				} else if(isTexture) {
					sprintf(assetPath, "asset://%s", filePath);
					if(!CKLBUtility::isFileExist(assetPath)) {
						lua.retString(filePath);
					} else {
						lua.retNil();
					}
					return 1;
				}

				CKLBAbstractAsset* loadedAsset = NULL;
				CKLBAssetManager& manager = CKLBAssetManager::getInstance();
				manager.setAssetNotFoundEnable(false);
				manager.setLastNotFoundName(filePath);
				u16 assetID = manager.getAssetIDFromName(
					filePath,
					0,
					0,
					&loadedAsset
				);
				manager.setAssetNotFoundEnable(true);

				if(assetID == NULL_IDX) {
					lua.retString(manager.getLastNotFoundName());
					return 1;
				}
				if(loadedAsset) {
					if(assetID == 0xFFFE) {
						loadedAsset->resetAssetID();
					}
					loadedAsset->incrementRefCount();
					loadedAsset->decrementRefCount();
				}
		}
	}
	lua.retNil();
	return 1;
}

s32
CKLBLuaLibASSET::luaGetFileList(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retNil();
		return 1;
	}

	const char* path = lua.getString(1);
	if(strncmp(path, "file://", 7) != 0) {
		lua.retNil();
		return 1;
	}

	const char* fullPath = CPFInterface::getInstance().platform().getFullPath(path);
	DIR* directory = opendir(fullPath);
	if(directory) {
		lua_newtable(L);
		int index = 1;
		struct dirent* entry;
		while((entry = readdir(directory)) != NULL) {
			lua_newtable(L);
			lua_pushstring(L, entry->d_name);
			lua_setfield(L, -2, "name");
			lua_rawseti(L, -2, index++);
		}
		closedir(directory);
	} else {
		lua.retNil();
	}
	KLBDELETEA(fullPath);
	return 1;
}

s32
CKLBLuaLibASSET::luaCreateImageTexture(lua_State* L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 4) {
		lua.retBool(false);
		return 1;
	}

	s32 cellWidth = lua.getInt(1);
	s32 cellHeight = lua.getInt(2);
	s32 cellCount = lua.getInt(3);
	bool rgba = lua.getBool(4);
	bool border = argc >= 5 ? lua.getBool(5) : false;

	CKLBGridTextureObject* texture = CKLBGridTextureObject::create(
		cellWidth,
		cellHeight,
		cellCount,
		rgba,
		border
	);
	returnPointerOrFalse(lua, texture);
	return 1;
}

s32
CKLBLuaLibASSET::luaGridRequestCache(lua_State* L)
{
	AssetGridSource source;
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 4) {
		lua.retBool(false);
		return 1;
	}

	bool decoded = argc >= 5 ? lua.getBool(5) : false;
	size_t stringLength;
	u32 sourceLength;
	const char* sourceData;
	if(decoded) {
		sourceData = lua.getString(2);
		sourceLength = static_cast<u32>(strlen(sourceData));
	} else {
		stringLength = 0;
		sourceData = lua.getString(2, &stringLength);
		sourceLength = static_cast<u32>(stringLength);
	}

	int cached = 0;
	if(sourceLength) {
		u32 dataLength = sourceLength;
		if(decoded) {
			char* decodedData = KLBNEWA(char, dataLength);
			KLBNetAPI_decodeBase64(sourceData, decodedData, &dataLength);
			sourceData = decodedData;
		}
		if(sourceData) {
			source.option = 0;
			source.data = reinterpret_cast<u8*>(const_cast<char*>(sourceData));
			source.length = dataLength;
			source.option = static_cast<u8>(lua.getInt(4));
			void* grid = const_cast<void*>(lua.getPointer(1));
			const char* name = lua.getString(3);
			bool cacheResult = gridRequestCache(grid, &source, name);
			if(decoded) {
				delete [] sourceData;
			}
			cached = cacheResult;
		}
	}

	lua.retBool(cached);
	return 1;
}

s32
CKLBLuaLibASSET::luaLoadImage(lua_State* L)
{
	AssetGridSource source;
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 5) {
		lua.retBool(false);
		return 1;
	}

	bool decoded = argc >= 6 ? lua.getBool(6) : false;
	size_t stringLength;
	u32 sourceLength;
	const char* sourceData;
	if(decoded) {
		sourceData = lua.getString(2);
		sourceLength = static_cast<u32>(strlen(sourceData));
	} else {
		stringLength = 0;
		sourceData = lua.getString(2, &stringLength);
		sourceLength = static_cast<u32>(stringLength);
	}

	if(sourceLength) {
		u32 dataLength = sourceLength;
		if(decoded) {
			char* decodedData = KLBNEWA(char, dataLength);
			KLBNetAPI_decodeBase64(sourceData, decodedData, &dataLength);
			sourceData = decodedData;
		}
		if(sourceData) {
			source.option = 0;
			source.data = reinterpret_cast<u8*>(const_cast<char*>(sourceData));
			source.length = dataLength;
			source.option = static_cast<u8>(lua.getInt(5));
			void* grid = const_cast<void*>(lua.getPointer(1));
			const char* name = lua.getString(3);
			bool reload = lua.getBool(4);
			u32 image = gridLoadImage(grid, &source, name, reload);
			if(decoded) {
				delete [] sourceData;
			}
			if(image != 0xffff) {
				lua.retInt(image);
				return 1;
			}
		}
	}

	lua.retBool(false);
	return 1;
}

s32
CKLBLuaLibASSET::luaGridLock(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc <= 1) {
		lua.retBool(false);
		return 1;
	}
	const void* grid = lua.getPointer(1);
	bool locked = lua.getBool(2);
	lua.retBool(setGridLocked(grid, locked));
	return 1;
}

s32
CKLBLuaLibASSET::luaGridError(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	bool clear = (argc > 0) ? lua.getBool(1) : false;
	lua.retInt(getGridError(clear));
	return 1;
}

s32
CKLBLuaLibASSET::luaMipmapOnce(lua_State * L)
{
	CLuaState lua(L);
	KLBTextureAssetPlugin* texturePlugin = static_cast<KLBTextureAssetPlugin*>(
		CKLBAssetManager::getInstance().getPlugin('T'));
	klb_assertNull(texturePlugin, "NULL PTR");
	klb_assertNull(texturePlugin->charHeader() == 'T', "NOT FOUND");
	texturePlugin->setMipmapOnce();
	lua.retBool(true);
	return 1;
}

s32
CKLBLuaLibASSET::luaGridSetDieCallback(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc <= 0) {
		lua.retBool(false);
		return 1;
	}
	const char* callback = NULL;
	if(lua.isString(1)) {
		callback = lua.getString(1);
	}
	setGridDieCallback(callback);
	lua.retBool(true);
	return 1;
}

s32
CKLBLuaLibASSET::luaRegisterNotFound(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBool(false);
		return 1;
	}

	CKLBAssetManager& manager = CKLBAssetManager::getInstance();
	const char* callback = lua.getString(1);
	lua.retBool(manager.setAssetNotFoundHandler(callback));
	return 1;
}

s32
CKLBLuaLibASSET::luaStartDownload(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc >= 3) {
		const char* callback = lua.isString(1) ? lua.getString(1) : NULL;
		const char* expectedSize = NULL;
		u32 timeout = 10000;
		if(argc >= 4 && !lua.isNil(4)) {
			expectedSize = lua.getString(4);
		}
		if(argc >= 5 && !lua.isNil(5)) {
			timeout = lua.getInt(5);
		}

		const char* targetName = lua.getString(2);
		const char* url = lua.getString(3);
		CKLBUpdate* update = CKLBUpdate::createAssetDownload(
			callback, targetName, url, expectedSize, timeout);
		lua.retBool(update != NULL);
	} else {
		lua.retBool(false);
	}
	return 1;
}

s32
CKLBLuaLibASSET::luaPauseDownload(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	bool accepted = false;
	if(argc > 0) {
		pauseDownloads(lua.getBool(1));
		accepted = true;
	}
	lua.retBool(accepted);
	return 1;
}

s32
CKLBLuaLibASSET::luaKillDownload(lua_State * L)
{
	CLuaState lua(L);
	lua.numArgs();
	teardownUpdateLists();
	lua.retBool(true);
	return 1;
}

s32
CKLBLuaLibASSET::luaBGFilterSetup(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	bool valid = (argc == 7);
	lua.retBool(valid);
	if(valid) {
		setupTextureBackgroundFilter(
			lua.getFloat(1),
			lua.getFloat(2),
			lua.getFloat(3),
			lua.getFloat(4),
			lua.getInt(5),
			lua.getInt(6),
			lua.getInt(7));
	}
	return 1;
}

s32
CKLBLuaLibASSET::luaRegisterFilter(lua_State * L)
{
	CLuaState lua(L);
	bool valid = lua.isString(1);
	if(valid) {
		KLBDELETEA(NMAsset::g_assetFilterCallback);
		NMAsset::g_assetFilterCallback = CKLBUtility::copyString(lua.getString(1));
	}
	lua.retBool(valid);
	return 1;
}

s32
CKLBLuaLibASSET::luaSetEID(lua_State * L)
{
	CLuaState lua(L);
	bool valid = lua.isNum(1);
	if(valid) {
		NMAsset::g_nmAssetKeyLength = lua.getInt(1);
	}
	lua.retBool(valid);
	return 1;
}

s32
CKLBLuaLibASSET::luaEnableTextureBorderPatch(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc == 1) {
		g_enableTextureBorderPatch = lua.getBool(1);
	}
	lua.retBool(argc == 1);
	return 1;
}

void CKLBLuaLibASSET::cmdGetImageSize(const char* asset_name, s32* pReturnWidth, s32* pReturnHeight)
{
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)CKLBUtility::loadAssetScript( asset_name, &handle);
	if(pAsset)
	{
		SKLBRect * rect = pAsset->getSize();

		*pReturnWidth = rect->getWidth();
		*pReturnHeight = rect->getHeight();

		CKLBDataHandler::releaseHandle(handle);
	}
}

void CKLBLuaLibASSET::cmdGetBoundSize(const char* asset_name, float* pReturnWidth, float* pReturnHeight)
{
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)CKLBUtility::loadAssetScript( asset_name, &handle);
	if(pAsset)
	{
		*pReturnWidth = pAsset->m_boundWidth;
		*pReturnHeight = pAsset->m_boundHeight;

		CKLBDataHandler::releaseHandle(handle);
	}
}

void CKLBLuaLibASSET::cmdGetAssetInfo(const char* asset_name, s32* pReturnImgWidth, s32* pReturnImgHeight, float* pReturnBoundWidth, float* pReturnBoundHeight)
{
	u32 handle;
	CKLBImageAsset * pAsset = (CKLBImageAsset *)CKLBUtility::loadAssetScript( asset_name, &handle);
	if(pAsset) {
		SKLBRect * rect = pAsset->getSize();
		
		*pReturnImgWidth = rect->getWidth();
		*pReturnImgHeight = rect->getHeight();
		*pReturnBoundWidth = pAsset->m_boundWidth;
		*pReturnBoundHeight = pAsset->m_boundHeight;

		CKLBDataHandler::releaseHandle(handle);
	}
}
