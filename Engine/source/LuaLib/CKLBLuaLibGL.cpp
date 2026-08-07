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
#include "CKLBLuaLibGL.h"
#include "CKLBDrawTask.h"
#include "TextureManagement.h"
#include "CKLBTouchPad.h"
#include "CUICover.h"
#include "shaderSource.inl"

// The constant table and the library instance are defined at the end of file.

CKLBLuaLibGL::CKLBLuaLibGL(DEFCONST * arrConstDef) : ILuaFuncLib(arrConstDef) {}
CKLBLuaLibGL::~CKLBLuaLibGL() {}

int CKLBLuaLibGL::luaGLIsSafeAreaScreen(lua_State * L)
{
	CLuaState lua(L);
	lua.retBool(CPFInterface::getInstance().platform().isSafeAreaScreen());
	return 1;
}

int CKLBLuaLibGL::luaGLGetResolution(lua_State * L)
{
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retInt(draw.width());
	lua.retInt(draw.height());
	return 2;
}

int CKLBLuaLibGL::luaGLGetPhysicalSize(lua_State * L)
{
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retInt(draw.phisicalWidth());
	lua.retInt(draw.phisicalHeight());
	return 2;
}

int CKLBLuaLibGL::luaGLGetCoordinate(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 1) {
		lua.retBoolean(false);
		return 1;
	}

	const char* name = NULL;
	if(!lua.isNil(1)) {
		name = lua.getString(1);
	}

	float worldX = 0.0f;
	float worldY = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	if(argc >= 3) {
		x = lua.getFloat(2);
		y = lua.getFloat(3);
	}

	if(name) {
		CKLBNode* node = CKLBDrawResource::getInstance().getRoot()->search(name);
		if(node) {
			node->getWorldPosition(x, y, &worldX, &worldY);
		}
	}

	lua.retFloat(worldX);
	lua.retFloat(worldY);
	return 2;
}

int CKLBLuaLibGL::luaGLGetScreenScale(lua_State * L)
{
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retFloat(draw.scaleX());
	lua.retFloat(draw.scaleY());
	return 2;
}

int CKLBLuaLibGL::luaGLGetUniformScaleFromSafeToUnsafe(lua_State * L)
{
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retFloat(draw.uniformScaleSafeToUnsafe());
	return 1;
}

// Lua関数の追加
void
CKLBLuaLibGL::addLibrary()
{
	addFunction("GL_SetResolution",			CKLBLuaLibGL::luaGLSetResolution		);
	addFunction("GL_IsSafeAreaScreen",		CKLBLuaLibGL::luaGLIsSafeAreaScreen		);
	addFunction("GL_GetResolution",			CKLBLuaLibGL::luaGLGetResolution		);
	addFunction("GL_GetPhysicalSize",		CKLBLuaLibGL::luaGLGetPhysicalSize		);
	addFunction("GL_GetCoordinate",			CKLBLuaLibGL::luaGLGetCoordinate		);
	addFunction("GL_GetScreenScale",			CKLBLuaLibGL::luaGLGetScreenScale		);
	addFunction("GL_GetUniformScaleFromSafeToUnsafe", CKLBLuaLibGL::luaGLGetUniformScaleFromSafeToUnsafe);
	addFunction("GL_GetHorizontalBorder",	CKLBLuaLibGL::luaGLGetHorizontalBorder	);
	addFunction("GL_GetVerticalBorder",		CKLBLuaLibGL::luaGLGetVerticalBorder	);
	addFunction("GL_ComputeMatrixFromToRect", CKLBLuaLibGL::luaGLComputeMatrixFromToRect);
	addFunction("GL_ClearColor",			CKLBLuaLibGL::luaGLClearColor			);
	addFunction("GL_UseImageOffset",		CKLBLuaLibGL::luaGLUseImageOffset		);
	addFunction("GL_LoadAsQuarterTexture",	CKLBLuaLibGL::luaGLSetQuarter			);
	addFunction("GL_CreateScreenAsset",		CKLBLuaLibGL::luaGLCreateScreenAsset	);
	addFunction("GL_FreeScreenAsset",		CKLBLuaLibGL::luaGLFreeScreenShot		);
	addFunction("SG_GetGuardBand",			CKLBLuaLibGL::luaGetGuardBand			);
	addFunction("SG_SetGuardBand",			CKLBLuaLibGL::luaSetGuardBand			);
	addFunction("GL_Unloadtexture",			CKLBLuaLibGL::luaGLUnloadTexture		);
	addFunction("GL_Reloadtexture",			CKLBLuaLibGL::luaGLReloadTexture		);
	addFunction("GL_DoScreenShot",			CKLBLuaLibGL::luaGLDoScreenShot			);
	addFunction("GL_StackShaderParam",		CKLBLuaLibGL::luaGLStackShaderParam		);
	addFunction("GL_CreateShader",			CKLBLuaLibGL::luaGLCreateShader			);
	addFunction("GL_DestroyShader",			CKLBLuaLibGL::luaGLDestroyShader		);
	addFunction("GL_GetRenderingAPI",		CKLBLuaLibGL::luaGLGetRenderingAPI		);
	addFunction("GL_RegisterSafeAreaChangeCallback", CKLBLuaLibGL::luaGLRegisterSafeAreaChangeCallback);
	addFunction("GL_GetUnsafeAreaSize",		CKLBLuaLibGL::luaGLGetUnsafeAreaSize		);
	addFunction("GL_BGBorder",				CKLBLuaLibGL::luaGLBGBorder				);
	addFunction("GL_SetState",				CKLBLuaLibGL::luaGLSetState				);
	addFunction("GL_SetFrameRate",			CKLBLuaLibGL::luaGLSetFrameRate			);
	addFunction("GL_GetMaxFrameRate",		CKLBLuaLibGL::luaGLGetMaxFrameRate		);
}

int
CKLBLuaLibGL::luaGLStackShaderParam(lua_State * L)
{
	CLuaState lua(L);
	int argc = lua.numArgs();
	if(argc < 3) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	bool pixelShader = lua.getInt(1) == 1;
	const char* name = lua.getString(2);
	int type = lua.getInt(3);
	int quality = (argc >= 4) ? lua.getInt(4) : 0;
	rendering.stackParameter(name, static_cast<u8>(type), static_cast<QUALITY_TYPE>(quality), pixelShader);
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibGL::luaGLCreateShader(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 3) {
		lua.retBoolean(false);
		return 1;
	}

	const char* name = lua.getString(1);
	const char* vertexShaderCode = lua.isNil(2) ? NULL : lua.getString(2);
	const char* pixelShaderCode = lua.isNil(3) ? NULL : lua.getString(3);
	u16 shader = createShader(name, vertexShaderCode, pixelShaderCode);
	lua.retBoolean(shader != 0xFFFF);
	return 1;
}

int
CKLBLuaLibGL::luaGLDestroyShader(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	u16 shader = rendering.getShaderDefinition(lua.getString(1));
	if(shader != 0xFFFF) {
		rendering.destroyShaderDefinition(shader);
	}
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibGL::luaGLSetState(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	rendering.selectDefaultSpriteState(lua.getInt(1));
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibGL::luaGLSetFrameRate(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() != 1) {
		lua.retBoolean(false);
		return 1;
	}

	int frameRate = lua.getInt(1);
	bool result = CPFInterface::getInstance().platform().setFrameRate(frameRate);
	lua.retBoolean(result);
	return 1;
}

int
CKLBLuaLibGL::luaGLGetMaxFrameRate(lua_State * L)
{
	CLuaState lua(L);
	int frameRate = CPFInterface::getInstance().platform().getMaxFrameRate();
	lua.retInt(frameRate);
	return 1;
}

int
CKLBLuaLibGL::luaGLGetRenderingAPI(lua_State * L)
{
	CLuaState lua(L);
	lua.retInt(2);
	return 1;
}

int
CKLBLuaLibGL::luaGLRegisterSafeAreaChangeCallback(lua_State * L)
{
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	bool result = false;
	if(lua.numArgs() > 0) {
		const char* callback = lua.isString(1) ? lua.getString(1) : NULL;
		CKLBDrawResource::getInstance().registerSafeAreaChangeCallback(callback);
		result = true;
	}
	lua.retBoolean(result);
	return 1;
}

int
CKLBLuaLibGL::luaGLGetUnsafeAreaSize(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() > 0) {
		int edge = lua.getInt(1);
		if((u32)edge < 4) {
			CKLBDrawResource& draw = CKLBDrawResource::getInstance();
			lua.retFloat(draw.unsafeArea(edge));
			return 1;
		}
	}
	lua.retBoolean(false);
	return 1;
}

int
CKLBLuaLibGL::luaGLBGBorder(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() < 2) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBUITask* parent = lua.isNil(1) ? NULL : (CKLBUITask*)lua.getScriptPtr(1);
	if(lua.numArgs() < 10) {
		return 1;
	}

	u32 order = lua.getInt(2);
	CKLBUICover* cover = CKLBUICover::create(parent, NULL, order, 0xFFFFFFFF);
	if(!cover) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	s32 width = lua.getInt(8);
	s32 height = lua.getInt(9);
	s32 innerBorder = lua.getBool(10);
	s32 horizontalDifference = width - draw.width();
	s32 verticalDifference = height - draw.height();
	s32 x = innerBorder - horizontalDifference / 2;
	s32 y = innerBorder - verticalDifference / 2;
	cover->addCover(x, y, width, height);

	const char* asset = lua.getString(3);
	bool repeatX = lua.getBool(4);
	bool repeatY = lua.getBool(5);
	float scaleX = lua.getFloat(6);
	float scaleY = lua.getFloat(7);
	cover->setup(asset, repeatX, repeatY, scaleX, scaleY);
	lua.retScriptPtr(cover);
	return 1;
}

/*static*/
int CKLBLuaLibGL::luaGLUnloadTexture (lua_State * L) {
	CLuaState lua(L);
	CKLBAssetManager::getInstance().unloadAsset();
	lua.retBoolean(true);
	return 1;
}

/*static*/
int CKLBLuaLibGL::luaGLReloadTexture (lua_State * L) {
	CLuaState lua(L);
	CKLBAssetManager::getInstance().restoreAsset();
	lua.retBoolean(true);
	return 1;
}

/*static*/
int CKLBLuaLibGL::luaGLGetHorizontalBorder	(lua_State * L) {
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retFloat(draw.borderX());
	return 1;
}

/*static*/
int CKLBLuaLibGL::luaGLGetVerticalBorder	(lua_State * L) {
	CLuaState lua(L);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	lua.retFloat(draw.borderY());
	return 1;
}

int
CKLBLuaLibGL::luaGLComputeMatrixFromToRect(lua_State * L)
{
	CLuaState lua(L);
	if(lua.numArgs() < 8) {
		lua.retBoolean(false);
		return 1;
	}

	float fromLeft = lua.getFloat(1);
	float fromTop = lua.getFloat(2);
	float fromRight = lua.getFloat(3);
	float fromBottom = lua.getFloat(4);
	float toLeft = lua.getFloat(5);
	float toTop = lua.getFloat(6);
	float toRight = lua.getFloat(7);
	float toBottom = lua.getFloat(8);

	float scaleX = fromRight - fromLeft;
	float scaleY = fromBottom - fromTop;
	scaleX = (toRight - toLeft) / scaleX;
	scaleY = (toBottom - toTop) / scaleY;
	lua.retFloat(scaleX);
	lua.retFloat(0.0f);
	lua.retFloat(0.0f);
	lua.retFloat(scaleY);
	lua.retFloat(toLeft - fromLeft);
	lua.retFloat(toTop - fromTop);
	return 6;
}

int
CKLBLuaLibGL::luaGLUseImageOffset(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retBoolean(false);
		return 1;
	}
	bool b = lua.getBool(1);
	useOffsetForImages(b);
	lua.retBoolean(true);
	return 1;
}

int
CKLBLuaLibGL::luaGLClearColor(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc != 4) {
		lua.retBoolean(false);
		return 1;
	}
	float r = lua.getFloat(1);
	float g = lua.getFloat(2);
	float b = lua.getFloat(3);
	float a = lua.getFloat(4);

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	bool bResult = rendering.setClearColor(r, g, b, a);
	lua.retBoolean(bResult);
	return 1;
}

int
CKLBLuaLibGL::luaGLSetResolution(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc < 2) {
		lua.retBoolean(false);
		return 1;
	}
	int width	= lua.getInt(1);
	int height	= lua.getInt(2);
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	
	if (argc >= 3) {
		draw.setBorderless(lua.getBool(3));
	}
	
	if (argc == 4) {
		bool b = lua.getBool(4);
		CKLBTouchPadQueue::getInstance().setIgnoreOutside(b);
	} else {
		CKLBTouchPadQueue::getInstance().setIgnoreOutside(false);
	}


	// 論理解像度の指定を行う
	draw.setLogicalResolution(width, height);

	lua.retBoolean(true);
	return 1;
}

int CKLBLuaLibGL::luaGetGuardBand(lua_State * L) {
	CLuaState lua(L);
	
	lua.retFloat(CKLBNode::s_fLeftBorder);
	lua.retFloat(CKLBNode::s_fRightBorder);
	lua.retFloat(CKLBNode::s_fTopBorder);
	lua.retFloat(CKLBNode::s_fBottomBorder);

	return 4;
}

int CKLBLuaLibGL::luaSetGuardBand(lua_State * L) {
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc != 4) {
		lua.retBoolean(false);
		return 1;
	}

	CKLBNode::s_fLeftBorder		= lua.getFloat(1);
	CKLBNode::s_fRightBorder	= lua.getFloat(2);
	CKLBNode::s_fTopBorder		= lua.getFloat(3);
	CKLBNode::s_fBottomBorder	= lua.getFloat(4);

	lua.retBoolean(true);
	return 1;
}

/*static*/
bool
CKLBLuaLibGL::GLCreateScreenAsset(const char* name)
{
	IClientRequest& itf	= CPFInterface::getInstance().client();
	int width			= itf.getPhysicalScreenWidth();
	int height			= itf.getPhysicalScreenHeight();

	return createScreenAsset(name,width,height);
}

int
CKLBLuaLibGL::luaGLCreateScreenAsset(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if (argc != 1) {
		lua.retBoolean(false);
		return 1;
	}
	const char* name	= lua.getString(1);
	
	lua.retBoolean(GLCreateScreenAsset(name));
	return 1;
}

/*static*/
bool
CKLBLuaLibGL::GLDoScreenShot(const char* name)
{
	IClientRequest& itf	= CPFInterface::getInstance().client();
	int width			= itf.getPhysicalScreenWidth();
	int height			= itf.getPhysicalScreenHeight();

	return doScreenShot(name, 0, 0, width, height, 0, 0);
}

int
CKLBLuaLibGL::luaGLDoScreenShot(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc < 1) {
		lua.retBoolean(false);
		return 1;
	}

	const char* name	= lua.getString(1);
	
	lua.retBoolean(GLDoScreenShot(name));
	return 1;
}

/*static*/
void
CKLBLuaLibGL::GLFreeScreenAsset(const char* name)
{
	freeScreenAsset(name);
}

int
CKLBLuaLibGL::luaGLFreeScreenShot(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retBoolean(false);
		return 1;
	}

	const char* name	= lua.getString(1);
	freeScreenAsset(name);
	lua.retBoolean(true);
	return 1;
}

#include "CKLBAsset.h"

int
CKLBLuaLibGL::luaGLSetQuarter(lua_State * L)
{
	CLuaState lua(L);

	int argc = lua.numArgs();
	if(argc != 1) {
		lua.retBoolean(false);
		return 1;
	}

	bool val = lua.getBoolean(1);

	// Get 
	KLBTextureAssetPlugin* pTexPlug = (KLBTextureAssetPlugin*)CKLBAssetManager::getInstance().getPlugin('T');
	if (pTexPlug) {
		pTexPlug->setQuarterTexture(val);
	}

	lua.retBoolean(pTexPlug != NULL);
	return 1;
}

/*
 * Shader definitions are created after the public Lua wrappers because this
 * helper owns the policy shared by built-in and caller-supplied programs.
 *
 * Built-in names select renderer-defined shader types. Their parameter
 * declarations must be stacked before the definition is requested, since the
 * renderer consumes that pending parameter stream during record construction.
 *
 * Saturation uses one low-precision pixel scalar. Mosaic exposes matching
 * medium-precision power values to both pixel and vertex stages. Colorize
 * combines a scalar power with a three-component color, while brightness uses
 * one four-component color value. Grading binds a texture table.
 *
 * These declarations are part of the built-in shader contract. Moving them
 * into Lua-facing code would make native callers create incomplete records,
 * and stacking them afterward would attach them to the next shader.
 *
 * Any other name denotes a custom shader. Names are unique in the renderer's
 * definition table, so an existing definition is a programming error rather
 * than an instruction to replace live shader instances.
 *
 * Custom pixel code is required by the Lua wrapper. Vertex code is optional:
 * omitting it selects the engine's standard colored-texture vertex program.
 * The default is resolved here so native and Lua callers share one rule.
 *
 * The custom definition marker is distinct from every built-in shader type.
 * It tells the manager to compile the supplied sources and retain parameters
 * established through GL_StackShaderParam.
 *
 * A null name identifies neither a built-in nor custom definition and is
 * rejected before the rendering singleton is acquired. The invalid shader
 * handle is also the renderer's normal failure sentinel.
 *
 * Keep the comparisons explicit. They are the compatibility surface used by
 * shipped scripts and document each renderer-owned effect's parameter layout.
 *
 * Keep pixel and vertex source order at the rendering-manager boundary. The
 * helper accepts vertex code first to match Lua, while the renderer stores
 * pixel code before vertex code.
 *
 * The renderer's handle is returned without translating failures, allowing
 * every caller to apply the same 0xFFFF sentinel check.
 *
 * This creates only reusable definitions. Render-state users remain
 * responsible for allocating shader instances after successful registration.
 *
 * Parameter stacking is intentionally stateful and synchronous. Callers must
 * finish declaring one shader before beginning another, and this helper must
 * consume the accumulated declarations exactly once.
 *
 * Built-in definitions follow the same lifetime and handle rules as custom
 * definitions even though their source programs are owned by the renderer.
 */
u16
CKLBLuaLibGL::createShader(const char* name, const char* vertexShaderCode, const char* pixelShaderCode)
{
	if(!name) {
		return 0xFFFF;
	}

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	u16 shaderType;
	if(!strcmp("saturate", name)) {
		rendering.stackParameter("power", VEC1F, LOWP, true);
		shaderType = 0x8003;
	} else if(!strcmp("mosaic", name)) {
		rendering.stackParameter("power", VEC1F, MEDP, true);
		rendering.stackParameter("power", VEC1F, MEDP, false);
		shaderType = 0x8006;
	} else if(!strcmp("colorize", name)) {
		rendering.stackParameter("power", VEC1F, LOWP, true);
		rendering.stackParameter("colorize", VEC3, LOWP, true);
		shaderType = 0x8002;
	} else if(!strcmp("brightness", name)) {
		rendering.stackParameter("colorize", VEC4, LOWP, true);
		shaderType = 0x8001;
	} else if(!strcmp("grading", name)) {
		rendering.stackParameter("table", TEX2D, LOWP, true);
		shaderType = 0x8005;
	} else {
		u16 shader = rendering.getShaderDefinition(name);
		klb_assert(shader == 0xFFFF, "A shader definition with name %s is already defined", name);
		if(!vertexShaderCode) {
			vertexShaderCode = sh_vertColTexture;
		}
		return rendering.createShaderDefinition(name, pixelShaderCode, vertexShaderCode, 0xFFFF);
	}

	return rendering.createShaderDefinition(name, shaderType);
}


// Shader parameter constants the script side needs to call GL_stackParameter:
// which shader the parameter belongs to, its data type, and its precision.
static ILuaFuncLib::DEFCONST luaConst[] = {
	{ "VSHADER_PARAM",	CKLBOGLWrapper::VERTEX_SHADER },
	{ "PSHADER_PARAM",	CKLBOGLWrapper::PIXEL_SHADER },
	{ "SHD_VEC1",	VEC1F },
	{ "SHD_VEC2",	VEC2 },
	{ "SHD_VEC3",	VEC3 },
	{ "SHD_VEC4",	VEC4 },
	{ "SHD_TEX2D",	TEX2D },
	{ "SHD_LOW",	LOWP },
	{ "SHD_MED",	MEDP },
	{ "SHD_HIGH",	HIGHP },
	{ 0, 0 }
};

static CKLBLuaLibGL libdef(luaConst);
