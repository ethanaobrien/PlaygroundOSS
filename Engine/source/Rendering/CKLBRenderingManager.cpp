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
#include "CKLBRendering.h"
#include "mem.h"
#include "CKLBDrawTask.h"
#include "CKLBSprite3D.h"
#include "CKLBUtility.h"
#include "CKLBDataHandler.h"
#include "CKLBTask.h"

extern void KLBUnregisterObjectName(void* object, const char* className);
extern void KLBRegisterObjectName(void* object, const char* className, int flags);

u32	gUseOffsetSystem = 1;

// Prototypes
static inline u32 searchID(u8* stream, const char* name);
u32  getColor(u32 code);
bool crossLine(float x1, float y1, float x2, float y2, float px, float py, float cx, float cy);

void useOffsetForImages(bool use) {
	gUseOffsetSystem = use ? 1 : 0;
}

//
// #define USE_PREMULALPHA

CKLBRenderingManager::CKLBRenderingManager():
	m_pListStart			(NULL),
	m_pDefaultStateCommand	(NULL),
	m_pAllocatedSpriteList	(NULL),
	m_pWhiteTexture			(NULL),
	m_pWhiteTextureUsage	(NULL),
	m_pIdxBuffer			(NULL),
	m_pVerBuffer			(NULL),
	m_pColBuffer			(NULL),
	m_pMaskUVBuffer		(NULL),
	m_pRenderWatchDog		(&m_innerWatchDog),
	m_pRenderLastModify		(NULL),
	m_pVShader				(NULL),
	m_pPShader				(NULL),
	m_pShaderSet			(NULL),
	m_pShaderInstance		(NULL),
	m_pCurrShader			(NULL),
	m_renderMode			(0),
	m_bRenderOverDraw		(false),
	m_coloring				(false)
{
	KLBRegisterObjectName(this, "CKLBRenderingManager", 0);
	initShaderSystem();
	setRenderMode(m_renderMode);
}

CKLBRenderingManager::~CKLBRenderingManager() {
	KLBUnregisterObjectName(this, "CKLBRenderingManager");
	_release();
}

void CKLBRenderingManager::_release() {
	while (m_pAllocatedSpriteList) {
		releaseCommand(m_pAllocatedSpriteList);
	}
	m_pDefaultStateCommand = NULL;
	m_pListStart = m_pRenderWatchDog;

	CKLBOGLWrapper&		pOGLMgr			= CKLBOGLWrapper::getInstance();
	if (m_pIdxBuffer) {
		pOGLMgr.releaseIndexBuffer(m_pIdxBuffer);
		m_pIdxBuffer = NULL;
	}

	if (m_pVerBuffer) {
		pOGLMgr.releaseVertexBuffer(m_pVerBuffer);
		m_pVerBuffer = NULL;
	}

	if (m_pColBuffer) {
		pOGLMgr.releaseVertexBuffer(m_pColBuffer);
		m_pColBuffer = NULL;
	}

	if (m_pMaskUVBuffer) {
		pOGLMgr.releaseVertexBuffer(m_pMaskUVBuffer);
		m_pMaskUVBuffer = NULL;
	}

	if (m_pShaderInstance) {
		m_pShaderSet->releaseInstance(m_pShaderInstance);
		m_pShaderInstance = NULL;
	}

	if (m_pShaderSet) {
		pOGLMgr.releaseShaderSet(m_pShaderSet);
		m_pShaderSet = NULL;
	}

	if (m_pWhiteTextureUsage) {
		m_pWhiteTexture->releaseUsage(m_pWhiteTextureUsage);
		m_pWhiteTextureUsage = NULL;
	}

	if (m_pWhiteTexture) {
		pOGLMgr.releaseTexture(m_pWhiteTexture);
		m_pWhiteTexture = NULL;
	}

	destroyShaderSystem();

	/* Destroyed by ref counter from ShaderSet
	if (m_pVShader) {
		pOGLMgr.releaseShader(m_pVShader);
		m_pVShader = NULL;
	}

	if (m_pPShader) {
		pOGLMgr.releaseShader(m_pPShader);
		m_pPShader = NULL;
	}*/
}

static const SVertexEntry cteListVertex[3] = {
	//	StreamID	Offset	VBO?	Type
#if (VERTEX_SIZE == 4) 
	{		1,		0,		false,	VEC2	 | VERTEX},	// Coordinate.
#else
	{		1,		0,		false,	VEC3	 | VERTEX},	// Coordinate.
#endif
	{		2,		0,		false,	VEC2	 | TEXTURE},	// Texture UV.
	{ /*NA*/0,/*NA*/0,/*NA*/false,	END_LIST}	// Mark end of list.
};


static const SVertexEntry cteListColor[2] = {
	//	StreamID	Offset	VBO?	Type
	{		3,		0,		false,	VEC4BYTE | COLOR},
	{ /*NA*/0,/*NA*/0,/*NA*/false,	END_LIST}	// Mark end of list.
};

static const SVertexEntry cteListMaskUV[2] = {
	//	StreamID	Offset	VBO?	Type
	{		4,		0,		false,	VEC2 | TEXTURE},
	{ /*NA*/0,/*NA*/0,/*NA*/false,	END_LIST}
};

bool CKLBRenderingManager::setup(u16 maxVertexCount, u16 maxIndexCount) {
	// -------------------------------------------------------------------
	//   OpenGL Initialize.
	// -------------------------------------------------------------------

	//--------------------------------------------------------------------
	// Setup Blending between Tex0 and Tex1 in multi texture mode
	//

	// Disable Color0 * Tex0
	//dglActiveTexture(GL_TEXTURE0);
	//dglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

#ifndef OPENGL2
	dglActiveTexture(GL_TEXTURE1);
	dglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
#endif
	//--------------------------------------------------------------------

	CKLBOGLWrapper&		pOGLMgr
					= CKLBOGLWrapper::getInstance();
	m_pIdxBuffer	= pOGLMgr.createIndexBuffer(maxIndexCount, false);
	m_pVerBuffer	= pOGLMgr.createVertexBuffer(maxVertexCount, &cteListVertex[0]);
	m_pColBuffer	= pOGLMgr.createVertexBuffer(maxVertexCount, &cteListColor[0]);
	m_pMaskUVBuffer = pOGLMgr.createVertexBuffer(maxVertexCount, &cteListMaskUV[0]);

	// TODO RP : Hardcoded shader, vertex format.
	//   Render state,
	// -------------------------------------- SHADER
	const static SParam paramsVert[] = {
		// Name				Uniform?	StreamID	Data Type
		//								or
		//								UniformID
#if (VERTEX_SIZE == 4) 
		{	"pos_attr"		,false		,1			,VEC2	| VERTEX },	// Coordinate.
#else
		{	"pos_attr"		,false		,1			,VEC3	| VERTEX },	// Coordinate.
#endif
		{	"uv_attr"		,false		,2			,VEC2	| TEXTURE},	// UV.
		{   "col_attr"		,false		,3			,VEC4BYTE | COLOR},
		{	""				,false		,0			,END_LIST}	// Mark end of list.
	};

	const static SParam paramsShader[] = {
		// Name				Uniform?	StreamID	Data Type
		//								or
		//								UniformID
		{	"texture"		,true		,1			,TEX2D    | TEXTURE },
//		{	"power"			,true		,2			,VEC1F				},
//		{	"table"			,true		,1			,TEX2D    | TEXTURE },
		{	""				,false		,0			,END_LIST}		// Mark end of list.
	};

	// Create shader vertex & pixel
	SRenderState::RENDER_MODE mode = SRenderState::TEXTURE_MUL_COLOR;
	const char* vertexSource = pOGLMgr.getShaderSource(mode, CKLBOGLWrapper::VERTEX_SHADER);
	m_pVShader		= pOGLMgr.createShader(vertexSource, CKLBOGLWrapper::VERTEX_SHADER, paramsVert);
	const char* pixelSource  = pOGLMgr.getShaderSource(mode, CKLBOGLWrapper::PIXEL_SHADER);
	m_pPShader		= pOGLMgr.createShader(pixelSource, CKLBOGLWrapper::PIXEL_SHADER, paramsShader);

	// Map the shader together.
	m_pShaderSet	= pOGLMgr.createShaderSet(m_pVShader, m_pPShader);
	// Create instance of shader for param
	m_pShaderInstance = m_pShaderSet->createInstance();
	// ----------------------------------------------

	static u32 whitePixel = 0xFFFFFFFF;
	m_pWhiteTexture = pOGLMgr.createTexture(1, 1, GL_UNSIGNED_BYTE, CKLBOGLWrapper::RGBA, &whitePixel, sizeof(whitePixel));
	m_pWhiteTextureUsage = m_pWhiteTexture->createUsage();

/*
	float val = 0.45f;
	m_pShaderInstance->setUniformF(CShaderInstance::PIXEL_SHADER, 2, &val);
*/
	// -------------------------------------------------------------------
	//   Manager Initialize.
	// -------------------------------------------------------------------

	m_innerWatchDog.m_pPrev			= NULL;
	m_innerWatchDog.m_pNext			= NULL;
	m_pListStart					= m_pRenderWatchDog;
	m_pRenderWatchDog->m_uiOrder	= 0x7FFFFFFF;	// Always at the end.
	m_pRenderLastModify				= m_pRenderWatchDog;

	m_pDefaultStateCommand = allocateCommandState();
	m_pDefaultStateCommand->setRenderTarget(pOGLMgr.getDefaultFrame());
	m_pDefaultStateCommand->setClearColor(true, 0.0f, 0.0f, 0.0f, 0.0f);
	m_pDefaultStateCommand->setScissor(false);
	m_pDefaultStateCommand->setUse(true, true, NULL);
	addToRendering(m_pDefaultStateCommand, 0x80000000);

	alphaState.setBlend(SRenderState::ALPHA);
	noAlphaState.setBlend(SRenderState::NO_ALPHA);
	additiveState.setBlend(SRenderState::ADDITIVE);
	additiveAlphaState.setBlend(SRenderState::ADDITIVE_ALPHA);
	subtractiveState.setBlend(SRenderState::SUBTRACTIVE);

	// Dest
	/*
		D_ONE						= GL_ONE,
		D_SRC_COLOR					= GL_SRC_COLOR,
		D_ONE_MINUS_SRC_COLOR		= GL_ONE_MINUS_SRC_COLOR,
		D_SRC_ALPHA					= GL_SRC_ALPHA,
		D_ONE_MINUS_SRC_ALPHA		= GL_ONE_MINUS_SRC_ALPHA,
		D_DST_ALPHA					= GL_DST_ALPHA,
		D_ONE_MINUS_DST_ALPHA		= GL_ONE_MINUS_DST_ALPHA
	 */

	// Src
	/*enum BLEND_SRC {
		S_ZERO						= GL_ZERO,
		S_ONE						= GL_ONE,
		S_DST_COLOR					= GL_DST_COLOR,
		S_ONE_MINUS_DST_COLOR		= GL_ONE_MINUS_DST_COLOR,
		S_SRC_ALPHA_SATURATE		= GL_SRC_ALPHA_SATURATE,
		S_SRC_ALPHA					= GL_SRC_ALPHA,
		S_ONE_MINUS_SRC_ALPHA		= GL_ONE_MINUS_SRC_ALPHA,
		S_DST_ALPHA					= GL_DST_ALPHA,
		S_ONE_MINUS_DST_ALPHA		= GL_ONE_MINUS_DST_ALPHA
	*/
//	textState.setBlendAdvance(SRenderState::S_ONE, SRenderState::D_ONE_MINUS_SRC_ALPHA,SRenderState::ADD /*Add is mandatory in ES2.0*/);
	textState.setTextMode(true);
	//	setBlendAdvance(SRenderState::S_ONE, SRenderState::D_ONE_MINUS_SRC_COLOR,SRenderState::ADD /*Add is mandatory in ES2.0*/);

	initShaderSystem();

	// ===========================================================
	//
	// Test Code : Shader that modify the color strength (grey-scale <--> saturation)
	//
	/*
	// Create Parameter
	this->stackParameter("power", VEC1F, MEDP);
	u32 def = this->createShaderDefinition(
		"varying lowp vec4 col_var;\n"			// Cte
		"varying mediump vec2 uv_var;\n"		// Cte
		"uniform lowp sampler2D texture;\n"		// Cte
		"uniform mediump float power;\n"		// Param
		"const lowp vec3 coef = vec3(0.2125, 0.7154, 0.0721);"
		"void main()\n"
		"{\n"
		"   lowp vec4 color     = texture2D(texture,uv_var) * col_var;\n"			// Read Texture
		"	gl_FragColor        = mix(vec4(vec3(dot(color.rgb, coef)),color.a),color,power);\n"
//		"   gl_FragColor        = texture2D(texture,floor(uv_var*power)/power) * col_var;\n" // Mosaic
		"}"
	);

	// Create Shader Instance
	void* shaderI = this->instanceShader(def, 2000);

	// Setup Shader Parameter
	float valuef = 300.0f;
	this->setShaderParamF(shaderI, "power", &valuef);
	// ===========================================================
	*/

	return (m_pIdxBuffer && m_pVerBuffer && m_pColBuffer && m_pMaskUVBuffer && m_pVShader && m_pShaderSet && m_pShaderInstance && m_pDefaultStateCommand);
}

void CKLBRenderingManager::setRenderMode(u32 mode) {
	m_renderMode = mode;
	switch (mode) {
	case 0:
		m_callMode		= GL_TRIANGLES;
		m_useTextures	= true;
		m_useColor		= true;
		break;
	case 1:
		m_callMode		= GL_LINE_STRIP;
		m_useTextures	= false;
		m_useColor		= false;
		break;
	}
}

void CKLBRenderingManager::registerCommand(CKLBRenderCommand* pCommand) {
	pCommand->m_pAllocNext = m_pAllocatedSpriteList;
	if (m_pAllocatedSpriteList) {
		m_pAllocatedSpriteList->m_pAllocPrev = pCommand;
	}
	m_pAllocatedSpriteList = pCommand;
}

CKLBSprite* CKLBRenderingManager::allocateCommandSprite(CKLBImageAsset* pImage, u32 priority, bool deferScale9Recompute) {
	klb_assertNull(m_pRenderWatchDog, "CKLBRenderingManager::setup not done first");
	klb_assertNull(pImage, "Null Image definition");
	bool isScale9	= (pImage->hasStandardAttribute(CKLBImageAsset::IS_SCALE9) != 0);
	bool isStdRect	= (pImage->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT) != 0);
	CKLBSprite* pSpr = isScale9 ? KLBNEW(CKLBSpriteScale9) : isStdRect ? KLBNEW(CKLBSprite4_6) : KLBNEW(CKLBSprite);
	if (pSpr) {
		if (isScale9) {
			if (((CKLBDynSprite*)pSpr)->setTriangleCount(16, 54, true)) {
				// SETUP BEFORE USE IMAGE !
				pSpr->m_renderOffset	= (pImage->m_renderOffset * gUseOffsetSystem);
				pSpr->m_uiOrder			= priority + pSpr->m_renderOffset;

				CKLBSpriteScale9* scale9 = (CKLBSpriteScale9*)pSpr;
				if (deferScale9Recompute) {
					scale9->beginSizeUpdate();
				}
				scale9->useImage(pImage);
				pSpr->m_pAllocNext			= m_pAllocatedSpriteList;
				if (m_pAllocatedSpriteList) {
					m_pAllocatedSpriteList->m_pAllocPrev = pSpr;
				}
				m_pAllocatedSpriteList		= pSpr;
				pSpr->m_uiStatus			= FLAG_XYUPDATE | FLAG_COLORUPDATE | FLAG_UVUPDATE;
				return pSpr;
			} else {
				KLBDELETE(pSpr);
				pSpr = NULL;
			}
		} else {
			u32 floatCount		= pImage->getVertexCount() * VERTEX_SIZE;	// X,Y,(Z),U,V
			float*  arr			= isStdRect ? ((CKLBSprite4_6*)pSpr)->m_pBuffer : KLBNEWA(float, floatCount + pImage->getVertexCount());
			u32* arrCol			= (u32*)&arr[floatCount];

			if (arr) {
				pSpr->m_pImageAsset		= pImage;
				pSpr->m_pVertex			= arr;
				pSpr->m_pColors			= arrCol;
				pSpr->m_pIndex			= pImage->getIndexBuffer();	// Cache
				pSpr->m_uiVertexCount	= (u16)pImage->getVertexCount();
				pSpr->m_uiIndexCount	= (u16)pImage->getIndexCount();
				pSpr->m_uiMaxVertexCount= (u16)pImage->getVertexCount();
				pSpr->m_uiMaxIndexCount = (u16)pImage->getIndexCount();
				pSpr->m_renderOffset	= (pImage->m_renderOffset * gUseOffsetSystem);

				pSpr->m_uiColor = 0xFFFFFFFF;	// Set default local color.
				memset32(arrCol, 0xFFFFFFFF, pSpr->m_uiVertexCount*sizeof(u32));

				if (VERTEX_SIZE > 4) {
					memset32(arr, 0, pSpr->m_uiVertexCount * VERTEX_SIZE * sizeof(float));
				}

				pSpr->m_pTexture			= pImage->getTexture()->m_pTextureUsage;
				pSpr->m_pAllocNext			= m_pAllocatedSpriteList;
				if (m_pAllocatedSpriteList) {
					m_pAllocatedSpriteList->m_pAllocPrev = pSpr;
				}
				m_pAllocatedSpriteList		= pSpr;
				pSpr->m_uiStatus			= FLAG_XYUPDATE | FLAG_COLORUPDATE | FLAG_UVUPDATE;
				pSpr->m_uiOrder				= priority + (pImage->m_renderOffset * gUseOffsetSystem);
			} else {
				if (arr)	{ KLBDELETEA(arr);		}

				KLBDELETE(pSpr);
				pSpr = NULL;
			}
		}
	}
	return pSpr;
}

CKLBDynSprite* CKLBRenderingManager::allocateCommandDynSprite(u16 vertexCount, u16 indexCount, u32 priority) {
	CKLBDynSprite* pSpr = KLBNEW(CKLBDynSprite);
	if (pSpr) {
		if (pSpr->setTriangleCount(vertexCount, indexCount, true)) {
			pSpr->m_pAllocNext			= m_pAllocatedSpriteList;
			if (m_pAllocatedSpriteList) {
				m_pAllocatedSpriteList->m_pAllocPrev = pSpr;
			}
			m_pAllocatedSpriteList		= pSpr;
			pSpr->m_uiOrder				= priority;
			return pSpr;
		}
		KLBDELETE(pSpr);
	}
	return NULL;
}

CKLBSprite* CKLBRenderingManager::allocateCommandSprite(u16 maxVertexCount, u16 maxIndexCount, u32 priority) {
	bool isPreAlloc = (maxVertexCount==4 && maxIndexCount==6);
	CKLBSprite* pSpr = isPreAlloc ? KLBNEW(CKLBSprite4_6) : KLBNEW(CKLBSprite);
	if (pSpr) {
		u32 floatCount		= maxVertexCount * VERTEX_SIZE;	// X,Y,U,V

		float*	arr			= isPreAlloc ? ((CKLBSprite4_6*)pSpr)->m_pBuffer : KLBNEWA(float,floatCount + maxIndexCount);
		u32* arrCol			= (u32*)&arr[floatCount];

		if (arr) {
			pSpr->m_pImageAsset		= NULL;
			pSpr->m_pVertex			= arr;
			if (VERTEX_SIZE > 4) { memset32(arr, 0, floatCount * sizeof(float)); }
			pSpr->m_pColors			= arrCol;
			pSpr->m_pIndex			= NULL;	// Cache
			pSpr->m_uiVertexCount	= 0;
			pSpr->m_uiIndexCount	= 0;
			pSpr->m_uiMaxVertexCount= maxVertexCount;
			pSpr->m_uiMaxIndexCount = maxIndexCount;

			pSpr->m_uiColor = 0xFFFFFFFF;	// Set default local color.
			pSpr->m_uiOrder				= priority;
			memset32(arrCol, 0xFFFFFFFF, maxVertexCount*sizeof(u32));

			pSpr->m_pTexture			= NULL;
			pSpr->m_pAllocNext			= m_pAllocatedSpriteList;
			if (m_pAllocatedSpriteList) {
				m_pAllocatedSpriteList->m_pAllocPrev = pSpr;
			}
			m_pAllocatedSpriteList		= pSpr;
		} else {
			if (arr)	{ KLBDELETEA(arr);		}

			KLBDELETE(pSpr);
			pSpr = NULL;
		}
	}
	return pSpr;
}

CKLBRenderState* CKLBRenderingManager::allocateCommandState() {
	CKLBRenderState* pComm = KLBNEW(CKLBRenderState);
	if (pComm) {
		pComm->pShaderInstance		= NULL;

		pComm->m_scissor[0]			= 0.0f;
		pComm->m_scissor[1]			= 0.0f;
		pComm->m_scissor[2]			= 0.0f;
		pComm->m_scissor[3]			= 0.0f;

		pComm->m_scissorPost[0]		= 0.0f;
		pComm->m_scissorPost[1]		= 0.0f;
		pComm->m_scissorPost[2]		= 0.0f;
		pComm->m_scissorPost[3]		= 0.0f;

		pComm->m_stencilClear		= 0;
		pComm->m_depthClear			= 0.0f;
		pComm->m_colorClearRed		= 0.0f;
		pComm->m_colorClearGreen	= 0.0f;
		pComm->m_colorClearBlue		= 0.0f;
		pComm->m_colorClearAlpha	= 0.0f;
		pComm->m_depthStart			= 0.0f;
		pComm->m_depthEnd			= 0.0f;

		/*
		memset(&pComm->internalState,0,sizeof(SRenderState));
		*/
		pComm->m_pAllocNext			= m_pAllocatedSpriteList;
		if (m_pAllocatedSpriteList) {
			m_pAllocatedSpriteList->m_pAllocPrev = pComm;
		}
		m_pAllocatedSpriteList		= pComm;
		pComm->m_uiStatus = 0;
	}
	return pComm;
}

CKLBPolyline* CKLBRenderingManager::allocateCommandPolyline(u16 maxPointCount, u32 priority) {
	CKLBPolyline* pLine = KLBNEW(CKLBPolyline);
	if (pLine) {
		if (pLine->setMaxPointCount(maxPointCount)) {
			pLine->m_uiOrder			= priority;
			pLine->m_pAllocNext			= m_pAllocatedSpriteList;
			if (m_pAllocatedSpriteList) {
				m_pAllocatedSpriteList->m_pAllocPrev = pLine;
			}
			m_pAllocatedSpriteList		= pLine;
		}
	}
	return pLine;
}

bool CKLBRenderingManager::setClearColor(float r, float g, float b, float alpha) {
	m_pDefaultStateCommand->setClearColor(true, r, g, b, alpha);
	return true;
}

void CKLBRenderingManager::releaseCommand(CKLBRenderCommand* pCommand) {
	klb_assertNull(m_pRenderWatchDog, "CKLBRenderingManager::setup not done first");
	klb_assertNull(pCommand, "null pointer");

	//
	// Remove from rendering path.
	//
	if (pCommand->m_pNext || pCommand->m_pPrev) {
		removeFromRendering(pCommand);
	}

	if (pCommand->decrementCount() != 0) { return; }

	//
	// Remove from link list.
	//
	if (pCommand->m_pAllocPrev) {
		pCommand->m_pAllocPrev->m_pAllocNext = pCommand->m_pAllocNext;
	} else {
		m_pAllocatedSpriteList		= pCommand->m_pAllocNext;
	}

	if (pCommand->m_pAllocNext) {
		pCommand->m_pAllocNext->m_pAllocPrev	= pCommand->m_pAllocPrev;
	}

	KLBDELETE(pCommand);
}

void CKLBRenderingManager::removeFromRendering(CKLBRenderCommand* pRender) {
	klb_assertNull(m_pRenderWatchDog, "CKLBRenderingManager::setup not done first");
	klb_assertNull(pRender,"null pointer");
	klb_assertNull((pRender->m_pNext || pRender->m_pPrev),"Item already not in rendering list");

	pRender->m_pNext->m_pPrev	= pRender->m_pPrev;
	if (pRender->m_pPrev) {
		pRender->m_pPrev->m_pNext	= pRender->m_pNext;
	} else {
		m_pListStart				= pRender->m_pNext;
	}

	if (pRender == m_pRenderLastModify) {
		m_pRenderLastModify = m_pRenderLastModify->m_pNext;
	}

	// Update renderer that reuse of buffer is becoming useless from this point.
	pRender->m_pNext->m_uiStatus |= FLAG_BUFFERSHIFT;
	pRender->m_pNext = NULL;
	pRender->m_pPrev = NULL;
}

void CKLBRenderingManager::initShaderSystem() {
	for (int n=0; n < SHADER_DEF_MAX; n++) {
		m_shaderDef[n].m_name			= NULL;
		m_shaderDef[n].m_definition		= NULL;
		m_shaderDef[n].m_pixelShader	= NULL;
		m_shaderDef[n].m_pixelParamList	= NULL;
		m_shaderDef[n].m_vertexParamList = NULL;
	}
	m_shaderInstanceList = NULL;
	m_stackParamFiller[0] = m_stackParam[0];
	m_stackParamFiller[1] = m_stackParam[1];
}

void CKLBRenderingManager::destroyShaderSystem() {
	CKLBOGLWrapper&		pOGLMgr	= CKLBOGLWrapper::getInstance();
	S_SHADERINSTANCE* pShader = m_shaderInstanceList;
	while (pShader) {
		S_SHADERINSTANCE* pNext = pShader->m_pNext;
		// Do not : KLBDELETEA(pShader->m_paramList);  Simple cache here.
		KLBDELETE(pShader);
		// Shader Instance shader freed by Definition.
		pShader = pNext;
	}
	m_shaderInstanceList = NULL;

	for (int n=0; n < SHADER_DEF_MAX; n++) {
		if (m_shaderDef[n].m_definition) {
			pOGLMgr.releaseShaderSet(m_shaderDef[n].m_definition);
		}
		KLBDELETEA(m_shaderDef[n].m_pixelParamList);
		KLBDELETEA(m_shaderDef[n].m_vertexParamList);
		KLBDELETEA(m_shaderDef[n].m_name);
		m_shaderDef[n].m_name			= NULL;
		m_shaderDef[n].m_definition		= NULL;
		m_shaderDef[n].m_pixelShader	= NULL;
		m_shaderDef[n].m_pixelParamList	= NULL;
		m_shaderDef[n].m_vertexParamList = NULL;
	}
	pOGLMgr.resetShader();
}

void CKLBRenderingManager::onResume() {
	CKLBOGLWrapper::getInstance().onResume();
}

void CKLBRenderingManager::stackParameter(const char* name, u8 type, QUALITY_TYPE quality, bool pixelShader) {
	u32 strLen = strlen(name) + 1;
	klb_assertNull(strLen < 256, "Shader Param less than 255 char");
	u8* end = pixelShader
		? &m_stackParam[CKLBOGLWrapper::PIXEL_SHADER][SHADER_PARAM_STREAM_SIZE]
		: &m_stackParam[CKLBOGLWrapper::VERTEX_SHADER][SHADER_PARAM_STREAM_SIZE];
	u8** fillerReference = &m_stackParamFiller[pixelShader];
	u8* filler = *fillerReference;
	klb_assert(filler + 4 + strLen <= end,
			   "Vertex or Pixel Shader build stack FULL (500 byte).");
	filler[0] = type;		// Param Type
	filler[1] = strLen;		// String Size
	filler[2] = 0;			// Mapped Index
	filler[3] = (u8)quality;
	*fillerReference += 4;

	// Copy C String after size and param.
	memcpy(*fillerReference, name, strLen);
	*fillerReference += strLen;
}

void CKLBRenderingManager::completeParameter(bool pixelShader) {
	u8* end = pixelShader
		? &m_stackParam[CKLBOGLWrapper::PIXEL_SHADER][SHADER_PARAM_STREAM_SIZE]
		: &m_stackParam[CKLBOGLWrapper::VERTEX_SHADER][SHADER_PARAM_STREAM_SIZE];
	u8*& filler = m_stackParamFiller[pixelShader];
	klb_assert(filler + 4 <= end, "Vertex or Pixel Shader build stack FULL (500 byte).");
	memset(filler, 0, 4);
	filler += 4;
}

u8* CKLBRenderingManager::buildShaderParameters(bool pixelShader, SParam* parameters, u32 parameterCapacity, u32 parameterCount) {
	completeParameter(pixelShader);

	u32 streamOffset = pixelShader ? SHADER_PARAM_STREAM_SIZE : 0;
	u8* streamStart = &m_stackParam[0][0] + streamOffset;
	int streamLength = m_stackParamFiller[pixelShader] - streamStart;
	u8* parameterStream = KLBNEWA(u8, streamLength);
	memcpy(parameterStream, streamStart, streamLength);

	SParam parameter;
	u8* entry = parameterStream;
	while (entry[1] != 0) {
		parameter.dType = entry[0];
		entry[2] = parameterCount + 1;
		klb_assert(parameterCount < parameterCapacity, "Too many shader parameters.");
		parameter.name = (const char*)&entry[4];
		parameter.isUniform = true;
		parameter.vertexORuniformID = parameterCount + 1;
		parameters[parameterCount++] = parameter;
		entry += entry[1] + 4;
	}

	parameters[parameterCount].name = "";
	parameters[parameterCount].isUniform = false;
	parameters[parameterCount].vertexORuniformID = 0;
	parameters[parameterCount].dType = END_LIST;
	return parameterStream;
}

bool CKLBRenderingManager::isShaderWhitespace(char character) {
	return character < '!';
}

const char* CKLBRenderingManager::readShaderToken(
	const char* source,
	s32* tokenLength)
{
	if (!source) {
		return NULL;
	}
	while (*source && isShaderWhitespace(*source)) {
		source++;
	}
	const char* end = source;
	while (*end && !isShaderWhitespace(*end)) {
		end++;
	}
	*tokenLength = end - source;
	return *source ? source : NULL;
}

bool CKLBRenderingManager::shaderTokenEquals(
	const char* token,
	s32 tokenLength,
	const char* expected)
{
	if (!token || strlen(expected) != (size_t)tokenLength) {
		return false;
	}
	for (s32 index = 0; index < tokenLength; index++) {
		if ((u8)token[index] != (u8)expected[index]) {
			return false;
		}
	}
	return true;
}

// GLSL ES requires a precision qualifier on every attribute, varying and uniform declaration;
// this reports the first one lacking it, honouring "#ifdef GL_ES" / "#ifndef GL_ES" nesting.
bool CKLBRenderingManager::checkPrecisionQualifier(const char* source, const char* shaderName) {
	if (!source) { return true; }
	const s32 CONDITION_MAX = 10;
	bool conditionActive[CONDITION_MAX];	// the enclosing block was compiled in
	bool conditionOnGLES[CONDITION_MAX];	// the open #if tested GL_ES
	s32  depth = 0;
	bool active = true, onGLES = false;
	s32  declarationPending = 0;			// a storage keyword was just read
	const char* token = source; s32 tokenLength = 0;
	do {
		token = readShaderToken(token + tokenLength, &tokenLength);
		if (shaderTokenEquals(token, tokenLength, "/*")) {
			while (*token && !((token[0] == '*') && (token[1] == '/'))) { token++; }
			token = *token ? token + 2 : NULL;
		} else if (shaderTokenEquals(token, tokenLength, "//")) {
			while (*token && (*token != '\n')) { token++; }
		} else {
			bool isIfdef  = shaderTokenEquals(token, tokenLength, "#ifdef");
			bool isIfndef = shaderTokenEquals(token, tokenLength, "#ifndef");
			bool isElse   = shaderTokenEquals(token, tokenLength, "#else");
			bool isEndif  = shaderTokenEquals(token, tokenLength, "#endif");
			if (isIfdef || isIfndef) {
				conditionOnGLES[depth] = onGLES; conditionActive[depth] = active;
				token  = readShaderToken(token + tokenLength, &tokenLength);
				onGLES = shaderTokenEquals(token, tokenLength, "GL_ES");
				depth++;
				active = active && !(isIfndef && onGLES);
			} else if (isElse) {
				if (onGLES && conditionActive[depth - 1]) { active = !active; }
			} else if (isEndif) {
				depth--;
				active = conditionActive[depth];
				onGLES = conditionOnGLES[depth];
			} else if (active) {
				if (declarationPending == 1) {
					declarationPending = 0;
					if (shaderTokenEquals(token, tokenLength, "vec2")
					 || shaderTokenEquals(token, tokenLength, "vec3")
					 || shaderTokenEquals(token, tokenLength, "vec4")
					 || shaderTokenEquals(token, tokenLength, "float")) {
						klb_assertAlways("%s Shader has no precision qualifier.", shaderName);
					}
				} else if (declarationPending == 0) {
					declarationPending = shaderTokenEquals(token, tokenLength, "attribute")
									  || shaderTokenEquals(token, tokenLength, "varying")
									  || shaderTokenEquals(token, tokenLength, "uniform");
				}
			}
		}
	} while (token);
	return true;
}

u16 CKLBRenderingManager::createShaderDefinition(const char* name, const char* pixelShaderCode, const char* vertexShaderCode, u32 variant) {
#ifdef OPENGL2
	const u32 SHADER_SOURCE_MAX = 10000;
	CKLBOGLWrapper& ogl = CKLBOGLWrapper::getInstance();
	SParam pixelParameters[32] = {
		{ "texture", true, 1, TEX2D | TEXTURE },
		{ "mask",    true, 2, TEX2D | TEXTURE },
		{ NULL,      true, 0, VEC1I           }
	};
	int pixelParameterCount = 0;
	while (pixelParameters[pixelParameterCount].name) { pixelParameterCount++; }
	u8* pixelParameterStream = buildShaderParameters(true, pixelParameters, 32, pixelParameterCount);
	SParam vertexParameters[32] = {
#if (VERTEX_SIZE == 4)
		{ "pos_attr",     false, 1, VEC2     | VERTEX  },
#else
		{ "pos_attr",     false, 1, VEC3     | VERTEX  },
#endif
		{ "uv_attr",      false, 2, VEC2     | TEXTURE },
		{ "col_attr",     false, 3, VEC4BYTE | COLOR   },
		{ "uv_mask_attr", false, 4, VEC2     | TEXTURE },
		{ NULL,           true,  0, VEC1I              }
	};
	int vertexParameterCount = 0;
	while (vertexParameters[vertexParameterCount].name) { vertexParameterCount++; }
	u8* vertexParameterStream = buildShaderParameters(false, vertexParameters, 32, vertexParameterCount);

	char shaderSource[SHADER_SOURCE_MAX];
	SRenderState::RENDER_MODE mode = (SRenderState::RENDER_MODE)variant;
	if (!pixelShaderCode)  { pixelShaderCode  = ogl.getShaderSource(mode, CKLBOGLWrapper::PIXEL_SHADER);  }
	if (!vertexShaderCode) { vertexShaderCode = ogl.getShaderSource(mode, CKLBOGLWrapper::VERTEX_SHADER); }
	// A shader sampling a movie texture must declare the OES external texture extension first, then use the platform's sampler keyword in place of the "movieSampler2D" placeholder.
	if (strstr(pixelShaderCode, "movieSampler2D")) {
		u32 length = snprintf(shaderSource, SHADER_SOURCE_MAX, "%s\n%s", CPFInterface::getInstance().platform().getShaderExtension(1), pixelShaderCode);
		pixelShaderCode = shaderSource;
		klb_assert(length < SHADER_SOURCE_MAX, "Shader source more than 10 KB !");
	}
	char* pixelSource = CKLBUtility::replaceString(pixelShaderCode, "movieSampler2D", CPFInterface::getInstance().platform().getShaderExtension(0));
	if (strstr(vertexShaderCode, "movieSampler2D")) {
		u32 length = snprintf(shaderSource, SHADER_SOURCE_MAX, "%s\n%s", CPFInterface::getInstance().platform().getShaderExtension(1), vertexShaderCode);
		vertexShaderCode = shaderSource;
		klb_assert(length < SHADER_SOURCE_MAX, "Shader source more than 10 KB !");
	}
	char* vertexSource = CKLBUtility::replaceString(vertexShaderCode, "movieSampler2D", CPFInterface::getInstance().platform().getShaderExtension(0));
	CShader* pixelShader = NULL;
	if (pixelSource) { pixelShader = ogl.createShader(pixelSource, CKLBOGLWrapper::PIXEL_SHADER, pixelParameters); }
	CShader* vertexShader = NULL;
	if (vertexSource) { vertexShader = ogl.createShader(vertexSource, CKLBOGLWrapper::VERTEX_SHADER, vertexParameters); }
	m_stackParamFiller[CKLBOGLWrapper::VERTEX_SHADER] = m_stackParam[CKLBOGLWrapper::VERTEX_SHADER];
	m_stackParamFiller[CKLBOGLWrapper::PIXEL_SHADER] = m_stackParam[CKLBOGLWrapper::PIXEL_SHADER];
	if (pixelShader && vertexShader) {
		CShaderSet* shaderSet = ogl.createShaderSet(vertexShader, pixelShader);
		if (shaderSet) {
			for (int shader = 0; shader < SHADER_DEF_MAX; shader++) {
				S_SHADERDEF& definition = m_shaderDef[shader];
				if (!definition.m_definition) {
					definition.m_definition = shaderSet;
					// Both handles are retained only so they can be released with the set.
					definition.m_pixelShader = pixelShader;
					definition.m_vertexShader = vertexShader;
					definition.m_pixelParamList = pixelParameterStream;
					definition.m_vertexParamList = vertexParameterStream;
					definition.m_refCount = 1;
					definition.m_variant = variant;
					definition.m_name = (char*)CKLBUtility::copyString(name);
					free(pixelSource);
					free(vertexSource);
					return shader;
				}
			}
			ogl.releaseShaderSet(shaderSet);
		}
	}
	if (pixelShader) ogl.releaseShader(pixelShader);
	if (vertexShader) ogl.releaseShader(vertexShader);
	free(pixelSource);
	free(vertexSource);
#else
	klb_assertAlways("OpenGL 1.1 Profile does not support shader APIs");
#endif
	return NULL_IDX;
}

void CKLBRenderingManager::destroyShaderDefinition(u16 shaderDefinition) {
	S_SHADERDEF& definition = m_shaderDef[shaderDefinition];
	if (definition.m_definition && (--definition.m_refCount == 0)) {
		CKLBOGLWrapper&		pOGLMgr	= CKLBOGLWrapper::getInstance();

		KLBDELETEA(definition.m_pixelParamList);
		definition.m_pixelParamList = NULL;
		KLBDELETEA(definition.m_vertexParamList);
		definition.m_vertexParamList = NULL;

		pOGLMgr.releaseShaderSet(definition.m_definition);
		definition.m_definition = NULL;
		definition.m_pixelShader = NULL;
		definition.m_vertexShader = NULL;
		definition.m_variant = 0xFFFF;
	}
}

u16 CKLBRenderingManager::getShaderDefinition(const char* name) {
	for(int shader = 0; shader < SHADER_DEF_MAX; shader++) {
		if(m_shaderDef[shader].m_definition && !strcmp(m_shaderDef[shader].m_name, name)) {
			return static_cast<u16>(shader);
		}
	}
	return 0xFFFF;
}

u16 CKLBRenderingManager::createShaderDefinition(const char* name, u32 variant) {
	for (int shader = 0; shader < SHADER_DEF_MAX; shader++) {
		if (m_shaderDef[shader].m_variant == variant) {
			m_shaderDef[shader].m_refCount = 1;
			return shader;
		}
	}
	return createShaderDefinition(name, NULL, NULL, variant);
}

// Instance Slot
void* CKLBRenderingManager::instanceShader(u32 shaderDefinition,
										   u32 startRange) {
	klb_assertNull(shaderDefinition < SHADER_DEF_MAX, "Invalid Shader Index");

	CKLBRenderState* pStartState = allocateCommandState();
	S_SHADERINSTANCE* pInst = KLBNEW(S_SHADERINSTANCE);
	u16 shaderIndex = shaderDefinition;
	S_SHADERDEF& definition = m_shaderDef[shaderIndex];
	if (definition.m_definition) {
		pInst->m_pInstanceShader = definition.m_definition->createInstance();
		if (pInst->m_pInstanceShader) {
			pStartState->setUse(false, false, pInst->m_pInstanceShader);
			pInst->m_pixelParamList	= definition.m_pixelParamList;
			pInst->m_vertexParamList = definition.m_vertexParamList;
			pInst->m_pStartState	= pStartState;
			pInst->m_pDefinition	= definition.m_definition;
			pInst->m_shaderDefinition = shaderIndex;
			pInst->m_pNext			= m_shaderInstanceList;
			m_shaderInstanceList	= pInst;
			definition.m_refCount++;
			addToRendering(pStartState, startRange);

			return pInst;
		}
	}

	KLBDELETE(pInst);
	releaseCommand(pStartState);
	return NULL;
}

void CKLBRenderingManager::removeShader(void* instanceShader) {
	S_SHADERINSTANCE* previous = NULL;
	for (S_SHADERINSTANCE* instance = m_shaderInstanceList;
		 instance;
		 previous = instance, instance = instance->m_pNext) {
		if (instance != instanceShader) {
			continue;
		}
		if (previous) {
			previous->m_pNext = instance->m_pNext;
		} else {
			m_shaderInstanceList = instance->m_pNext;
		}
		instance->m_pDefinition->releaseInstance(instance->m_pInstanceShader);
		destroyShaderDefinition(instance->m_shaderDefinition);
		if (instance->m_pStartState) {
			releaseCommand(instance->m_pStartState);
		}
		KLBDELETE(instance);
		return;
	}
}

u32 CKLBRenderingManager::getShaderParamID(void* instanceShader, const char* name) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	return searchID(pInst->m_pixelParamList, name);
}

void CKLBRenderingManager::setShaderParamI(void* instanceShader, const char* name, GLint* value) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	u32 uniformID = searchID(pInst->m_pixelParamList, name);
	pInst->m_pInstanceShader->setUniformI(CShaderInstance::PIXEL_SHADER, uniformID, value);
}

void CKLBRenderingManager::setShaderParamF(void* instanceShader, const char* name, GLfloat* value) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	u32 uniformID = searchID(pInst->m_pixelParamList, name);
	pInst->m_pInstanceShader->setUniformF(CShaderInstance::PIXEL_SHADER, uniformID, value);
}

void CKLBRenderingManager::setVertexShaderParamF(void* instanceShader, const char* name, GLfloat* value) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	u32 uniformID = searchID(pInst->m_vertexParamList, name);
	pInst->m_pInstanceShader->setUniformF(CShaderInstance::VERTEX_SHADER, uniformID, value);
}

void CKLBRenderingManager::setShaderParamTexture(void* instanceShader, const char* name, CTextureUsage* value) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	u32 uniformID = searchID(pInst->m_pixelParamList, name);
	pInst->m_pInstanceShader->setUniformTexture(CShaderInstance::PIXEL_SHADER, uniformID, value);
}

void CKLBRenderingManager::setShaderParamTexture(void* instanceShader, u32 uniformID, CTextureUsage* value) {
	S_SHADERINSTANCE* pInst = (S_SHADERINSTANCE*)instanceShader;
	pInst->m_pInstanceShader->setUniformTexture(CShaderInstance::PIXEL_SHADER, uniformID, value);
}






void CKLBRenderingManager::addToRendering(CKLBRenderCommand* pRender, s32 index) {
	klb_assertNull(m_pRenderWatchDog, "CKLBRenderingManager::setup not done first");
	// Roll back when bug found.
	// klb_assert(index != 0xFFFFFFFF, " 0xFFFFFFFF Priority should not be used");
	klb_assertNull((pRender->m_pNext == NULL) && (pRender->m_pPrev == NULL), "Item already in list.");

	//
	// Perform insertion.
	//
	CKLBRenderCommand* pInsertPoint = m_pRenderLastModify;
	if (index < m_pRenderLastModify->m_uiOrder) {
		// <
		CKLBRenderCommand* pPrevPoint = pInsertPoint; 
		while (pInsertPoint->m_uiOrder > index) {
			pPrevPoint   = pInsertPoint;
			pInsertPoint = pInsertPoint->m_pPrev;
			if (!pInsertPoint) { break; }
		}

		// Roll back to next item.
		pInsertPoint = pPrevPoint;
	} else {
		// loop until >=
		while (pInsertPoint->m_uiOrder < index) {
			pInsertPoint = pInsertPoint->m_pNext;
		}
	}

	//
	// Object inserted BEFORE insertPoint.
	//

	// Beginning of list
	if (pInsertPoint->m_pPrev == NULL) {
		pRender->m_pNext			= this->m_pListStart;
		pRender->m_pPrev			= NULL;
		this->m_pListStart->m_pPrev	= pRender;
		this->m_pListStart			= pRender;

	} else {
	// Middle of list
		pRender->m_pNext			= pInsertPoint;
		pRender->m_pPrev			= pInsertPoint->m_pPrev; 
		pInsertPoint->m_pPrev		= pRender;
		pRender->m_pPrev->m_pNext	= pRender;
	}

//	if (pRender->m_commandType == 1) {	// Buffer shift only with sprites.
		pRender->m_uiStatus |= FLAG_BUFFERSHIFT;
//	}
	pRender->m_uiOrder  = index;

	m_pRenderLastModify = pRender;

	if (index == 0x7FFFFFFF) {
		// Scene graph
		CKLBDrawResource& res = CKLBDrawResource::getInstance();
		res.getRoot()->dump(0, 0x7FFFFFFF);
		// Dump Rendering Queue
		dump(0x7FFFFFFF);

		// Put back at beginning once we did it.
		klb_assertNull(index != 0x7FFFFFFF, " 0xFFFFFFFF Priority should not be used");
	}
}

// Dirty Hack for now : 250 sprites with mask in ONE call max.
float	m_maskUVPtr[10000];

CTextureUsage* g_textureMask = NULL;
bool g_useTextureLast = false;
bool g_useColorLast = false;

void CKLBRenderingManager::emitDrawCall(	u16*			pIndexCounter,
											u16*			offsetIndex,
											u16*			offsetVertex,
											u16				offsetVertexHead,
											CTextureUsage**	textures,
											s32*			uniformIDs,
											CBuffer**		buffers
											) {
	u32 indexCount = *pIndexCounter;

	CKLBOGLWrapper&		pOGLMgr	= CKLBOGLWrapper::getInstance();
	pOGLMgr.applyState(m_pCurrState);

	if (indexCount) {
		m_pIdxBuffer->setDrawOffset(*offsetIndex);
		m_pVerBuffer->setDrawOffset(*offsetVertex);
		m_pColBuffer->setDrawOffset(*offsetVertex);
		m_pMaskUVBuffer->setDrawOffset(*offsetVertex);

		pOGLMgr.draw(	m_callMode,
						m_pCurrShader,
						buffers,
						3,
						m_pIdxBuffer,
						textures,
						uniformIDs,
						indexCount);

		*pIndexCounter = 0;
	#ifdef DEBUG_PERFORMANCE
		m_drawCall++;
		m_vertexCount	+= offsetVertexHead - (*offsetVertex); // Amount of vertex.
		m_indexCount	+= indexCount;
	#endif
	}

	*offsetVertex	 = offsetVertexHead;
	*offsetIndex	+= indexCount;  
}

void
CKLBRenderingManager::draw(CBuffer** buffers,
						   CIndexBuffer* indexBuffer,
						   CTextureUsage** textures,
						   s32* uniformIDs,
						   s32 indexCount)
{
	CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();
	renderer.draw(m_callMode,
				  m_pCurrShader,
				  buffers,
				  2,
				  indexBuffer,
				  textures,
				  uniformIDs,
				  indexCount);
}

void CKLBRenderingManager::dumpMetrics() {
	FILE* pFile = CPFInterface::getInstance().client().getShellOutput();
#ifdef DEBUG_PERFORMANCE
	fprintf(pFile,"\n==== Rendering Metrics ===\n");
	fprintf(pFile,"Total vertex Count : %i\n", m_vertexCount);
	fprintf(pFile,"Total index  Count : %i\n", m_indexCount);
	fprintf(pFile,"Total sprite Count : %i\n", m_spriteCount);
	fprintf(pFile,"Texture      Change: %i\n", m_textureChange);
	fprintf(pFile,"Render State Change: %i\n", m_renderStateChange);
	fprintf(pFile,"Total float transfer to GPU : %i\n", m_totalTransferSize);
	fprintf(pFile,"Total internal copy: %i\n", m_memCopySize);
	fprintf(pFile,"Total DrawCall     : %i\n", m_drawCall);
	fprintf(pFile,"Time %lli uSec\n", m_drawTime / 1000);
	fprintf(pFile,"==== Rendering Metrics End ===\n\n");
#else
	fprintf(pFile,"==== Not Available (Compile option DEBUG_PERFORMANCE not set\n\n");
#endif
}

void CKLBRenderingManager::dump(u32 /*mask*/) {
	int count = 0;
	FILE* pFile = CPFInterface::getInstance().client().getShellOutput();
	fprintf(pFile,"\n==== Rendering Queue Start ===\n");
	CKLBRenderCommand*	pCommand		= this->m_pListStart;
	while (pCommand != m_pRenderWatchDog) {
		fprintf(pFile,"@%p %c Priority:[%8i] Type : ",pCommand, pCommand->ignore ? '!' : ' ',pCommand->getOrder());
		u32 command = pCommand->m_commandType;
		if (command & RENDERCOMMAND_SPRITE) {
			CKLBSprite* pSpr = (CKLBSprite*)pCommand;
			fprintf(pFile,"[Sprite");

			if (pCommand->m_uiStatus & FLAG_BUFFERSHIFT) {
				fprintf(pFile," Shft");
			}
			if (pCommand->m_uiStatus & FLAG_XYUPDATE) {
				fprintf(pFile," Geom");
			}
			if (pCommand->m_uiStatus & FLAG_COLORUPDATE) {
				fprintf(pFile," Col");
			}
			fprintf(pFile," Vertex:%i Index:%i Texture:%p] ", pSpr->m_uiVertexCount, pSpr->m_uiIndexCount, pSpr->m_pImageAsset->getTexture());
		}

		if (command & RENDERCOMMAND_CHANGERENDERSTATE) {
			fprintf(pFile,"[Render State");
			CKLBRenderState* pRCom = (CKLBRenderState*)pCommand;
			SRenderState* pState = pRCom->getState();
			pState->dump();

			fprintf(pFile,"] ");
		}

		if (command & RENDERCOMMAND_SETSHADER) {
			fprintf(pFile,"[Shader] ");
		}

		if (command & RENDERCOMMAND_EXECUTECOMMAND) {
			if (command & RENDERCOMMAND_CLEARCOLOR)	{	
				fprintf(pFile,"[Clear Color] ");
			}
			if (command & RENDERCOMMAND_CLEARDEPTH)	{
				fprintf(pFile,"[Clear Depth] ");
			}
			if (command & RENDERCOMMAND_CLEARSTENCIL) {
				fprintf(pFile,"[Clear Stencil] ");
			}
		}
		
		fprintf(pFile,"\n");
		pCommand = pCommand->m_pNext;
		count++;
	}
	fprintf(pFile,"[WatchDog]\n==== Rendering Queue End : %i items ===\n", count);
}

u32 getColor(u32 code) {
	u8 col[4];
	col[0] = (code & 0x07) << 5;
	col[1] = (code & 0x38) << 2;
	col[2] = (code & 0x1C) >> 1;
	col[3] = 0xFF;

	return (*((u32*)col));
}

// Shader parameter records use a compact byte stream:
// byte 0 stores the parameter type,
// byte 1 stores the record's name length,
// byte 2 stores the mapped uniform identifier,
// byte 3 stores the requested precision,
// and the zero-terminated parameter name begins at byte 4.
static inline u32 searchID(u8* stream, const char* name) {
	if (!stream) {
		return NULL_IDX;
	}
	while (stream[1]!=0) {								// Name Length
		if (strcmp(name, (const char*)&stream[4])==0) {	// Name
			return stream[2];							// Index ID
		}
		stream += stream[1] + 4;						// Go to next item (3+String)
	}
	klb_assertAlways("Uniform '%s' not found.", name);
	return NULL_IDX;
}

CKLBRenderCommand* CKLBRenderingManager::drawClick(u32 x, u32 y) {
	float fx = x;
	float fy = y;

	CKLBRenderCommand*	pCommand = m_pRenderWatchDog->m_pPrev;
	while (pCommand) {
		if (pCommand->m_commandType & RENDERCOMMAND_SPRITE) {
			CKLBSprite* pSpr = (CKLBSprite*)pCommand;
			if (pSpr->m_click) {
				if (pSpr->clicked(fx,fy)) {
					return pSpr;
				}
			}
		}
		pCommand = pCommand->m_pPrev;
	}

	return NULL;
}
/*
CKLBRenderCommand* CKLBRenderingManager::drawClick(u32 x, u32 y) {
	
	m_useTextures = false;
	m_coloring	  = true;

	// Clear
	dglClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	dglClear		(GL_COLOR_BUFFER_BIT);

	dglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,	GL_COMBINE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB,		GL_REPLACE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA,		GL_REPLACE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB,			GL_TEXTURE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA,		GL_TEXTURE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB,		GL_SRC_COLOR);
	dglTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA,	GL_SRC_ALPHA);

	// Use vertex color but alpha from texture.
	//dglTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB,			GL_PRIMARY_COLOR);
	
	//
	// Test : to remove.
	//
	{
		int assignID = 1;
		CKLBRenderCommand*	pCommand = this->m_pListStart;
		CKLBRenderCommand*	found	 = NULL;
		while (pCommand != m_pRenderWatchDog) {
			if (pCommand->m_commandType & RENDERCOMMAND_SPRITE) {
				CKLBSprite* pSpr = (CKLBSprite*)pCommand;
				pSpr->m_clickID = assignID++;
			}
			pCommand = pCommand->m_pNext;
		}

		if (this->m_pListStart) {
			this->m_pListStart->m_uiStatus |= FLAG_BUFFERSHIFT;
		}
	}

	// TODO : Modify Scissor
	draw();

	// TODO : Restore Scissor

	// Get X,Y
	u8 col[8];
	dglReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&col);

	// Convert RGBA into Code
	// MSB 3 Bit of RGB -> BBBGGGRRR code;
	col[0] &= 0xE0;
	col[1] &= 0xE0;
	col[3] &= 0xE0;
	col[4]  = 0xFF;

	u32 id = (*((u32*)col));

	// Parse all rendering objects.
	CKLBRenderCommand*	pCommand = this->m_pListStart;
	CKLBRenderCommand*	found	 = NULL;
	if (id != 0) {
		while (pCommand != m_pRenderWatchDog) {
			if (pCommand->m_commandType & RENDERCOMMAND_SPRITE) {
				CKLBSprite* pSpr = (CKLBSprite*)pCommand;
				if (pSpr->m_clickColor == id) {
					found = pCommand;
					break;
				}
			}
			pCommand = pCommand->m_pNext;
		}
	}

	// Restore Initial value
	
	dglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,	GL_MODULATE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB,		GL_MODULATE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB,			GL_TEXTURE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB,			GL_PRIMARY_COLOR);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA,		GL_TEXTURE);
	dglTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA,		GL_PRIMARY_COLOR);
	
	m_useTextures = true;
	m_coloring	  = false;

	// draw();

	return found;
}*/

void CKLBRenderingManager::enableRange(s32 start, s32 end, bool active) {
	if (end == 0xFFFFFFFF) {
		end--;
	}
	CKLBRenderCommand*	pCommand = m_pListStart;
	while (pCommand != m_pRenderWatchDog) {
		if (pCommand->m_uiOrder >= start && pCommand->m_uiOrder <= end) {
			pCommand->ignore = !active;
		}
		pCommand = pCommand->m_pNext;
	}
}

void CKLBRenderingManager::drawOverdraw() {
	CKLBRenderCommand*	renderStack[10];

	u8					bufferShift		= true;
	CKLBRenderCommand*	pCommand		= this->m_pListStart;
	CTextureUsage*		pLastTexture	= NULL;
	u16*				pDstIndexBuffer = (u16*)m_pIdxBuffer->updateStart(0);

	u16					strideVertex;
	float*				pDstVertexBuffer		= m_pVerBuffer->updateStart(1, 0, &strideVertex);
	u32*				pDstColBuffer			= (u32*)m_pColBuffer->updateStart(3, 0, null);
	float*				pDstMaskUVBuffer;
	u16		indexCount			= 0;
	u16		indexVCount			= 0;
	u16		offsetVertex		= 0;
	u16		offsetVertexHead	= 0;
	u16		offsetIndex			= 0;
	s32		stackDepth			= 0;
	CTextureUsage*		pLastTextureMask = NULL;

	CKLBOGLWrapper& pOGLMgr	= CKLBOGLWrapper::getInstance();
	pOGLMgr.resetSamplers();
	CTextureUsage* textures[2];
	s32 uniformIDs[2];
	textures[0] = NULL;
	uniformIDs[0] = 1;
	textures[1] = NULL;
	uniformIDs[1] = 2;
	CBuffer* buffers[3] = { m_pVerBuffer, m_pColBuffer, m_pMaskUVBuffer };
	/*
	dump(0);
	dumpMetrics();
	*/

	// Default
	m_pCurrState	= &noAlphaState;
	// Default
	m_pCurrShader	= m_pShaderInstance;
	pDstMaskUVBuffer = m_pMaskUVBuffer->updateStart(4, 0, null);


#ifdef DEBUG_PERFORMANCE
	m_drawTime			= CPFInterface::getInstance().platform().nanotime();
	m_vertexCount		= 0;	// DONE
	m_indexCount		= 0;	// DONE
	m_spriteCount		= 0;	// DONE
	m_renderStateChange	= 0;	// DONE
	m_textureChange		= 0;	// DONE
	m_totalTransferSize	= 0;	// Total Transfer to GPU	
	m_memCopySize		= 0;	// Internal Move
	m_drawCall			= 0;	// DONE
#endif
	float* ptrUVMask	= pDstMaskUVBuffer;

#ifndef OPENGL2
	dglActiveTexture(GL_TEXTURE1);
	dglClientActiveTexture(GL_TEXTURE1);
	dglDisable(GL_TEXTURE_2D);
	dglDisableClientState(GL_TEXTURE_COORD_ARRAY);

	dglActiveTexture(GL_TEXTURE0);
	dglClientActiveTexture(GL_TEXTURE0);
	g_textureMask = NULL;
#endif

	float savedClearColor[4] = {
		m_pDefaultStateCommand->m_colorClearRed,
		m_pDefaultStateCommand->m_colorClearGreen,
		m_pDefaultStateCommand->m_colorClearBlue,
		m_pDefaultStateCommand->m_colorClearAlpha
	};
	m_pDefaultStateCommand->m_commandType |= RENDERCOMMAND_CLEARCOLOR;
	m_pDefaultStateCommand->m_colorClearRed	 = 1.0f;
	m_pDefaultStateCommand->m_colorClearGreen = 1.0f;
	m_pDefaultStateCommand->m_colorClearBlue	 = 1.0f;
	m_pDefaultStateCommand->m_colorClearAlpha = 1.0f;
	bool bTex = (m_bRenderOverDraw == 2);
	renderStack[0]	= m_pRenderWatchDog;
	renderStack[1]	= pCommand;


	#define COLOR_COUNT			(7)
	static u8 g_colors[] = {
		// RGBA
		0xFF,0x00,0x00,0x40,	// Red			1
		0x00,0xFF,0x00,0x40,	// Green		2
		0x00,0x00,0xFF,0x40,	// Blue			3
		0xFF,0xFF,0x00,0x40,	// Yellow		4
		0x00,0xFF,0xFF,0x40,	// Cyan			5
		0xFF,0x00,0xFF,0x40,	// Magenta		6
		0x00,0x00,0x00,0x20,	// Black		7
	};

	u32 colorIndex = 0;

	// OPTIMIZE RP : have mecanism to remember FOR EACH draw call (render state change)
	//						avoid buffer shift in first draw call to impact further draw calls. (ie layers)
	//						and have different buffer to avoid crush by previous call.

	do {
		CKLBRenderCommand* pEnd = renderStack[stackDepth];
		pCommand				= renderStack[stackDepth+1];
		while (pCommand != pEnd) {
			if (pCommand->m_commandType & RENDERCOMMAND_SPRITE) {
				CKLBSprite* pSpr = (CKLBSprite*)pCommand;

				if ((pSpr->m_uiVertexCount != 0) && (!(pCommand->m_commandType & RENDERCOMMAND_IGNORE))) {	// TODO OPTIMIZE : Empty sprite could be optimized to be skipped once.
					if ((pSpr->m_pTexture != pLastTexture) || (m_pCurrState != (pSpr->m_pState ? pSpr->m_pState : &alphaState)) || (pSpr->m_pMaskTexture != pLastTextureMask)) {
						textures[0] = bTex ? pLastTexture : m_pWhiteTextureUsage;
						textures[1] = pLastTextureMask;
						emitDrawCall		(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
						colorIndex++;
						indexVCount  = 0; // Reset index counter.
						pLastTexture		= pSpr->m_pTexture;
						pLastTextureMask	= pSpr->m_pMaskTexture;
					}

					if (pCommand->m_commandType & RENDERCOMMAND_3D) {
						((CKLBSprite3D*)pCommand)->draw();
					} else {
						u16 sprIndexCount	= pSpr->m_uiIndexCount;
						u16 skipSize		= pSpr->m_uiVertexCount * strideVertex;

						bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);
						// Copy chunk of complete vertex. 
						// MemCopy UV
						// MemCopy XY
						if (bufferShift) {
							// Copy X,Y,U,V
							memcpy32(pDstVertexBuffer,	pSpr->m_pVertex,	skipSize              * sizeof(float));

							{
								u32 col = colorIndex % COLOR_COUNT;
								for (u32 n=0; n < pSpr->m_uiVertexCount; n++) {  pDstColBuffer[n] = *((u32*)&g_colors[col*4]); }
							}

							if (pLastTextureMask) {
								memcpy32(ptrUVMask, pSpr->m_pVertexMaskUV,	(skipSize *sizeof(float)) >> 1);
								ptrUVMask += (skipSize & ~1) >> 1;
							}

							// Index buffer recompute
							u16 lCount		= sprIndexCount;
							u16* pSprIdx	= pSpr->m_pIndex;

							//
							// Unroll loop by block of 8 indexes
							//
						loopSwitch:
							switch (lCount) {
							case 7: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 6: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 5: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 4: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 3: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 2: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 1: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
								break;
							default:
								if (lCount >= 8) {
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									lCount -= 8;
									goto loopSwitch;
								}
							}
						} else {
							// --- Geometry or color update : triangle do not change ---
							if (pCommand->m_uiStatus & (FLAG_XYUPDATE | FLAG_UVUPDATE)) {
								memcpy32(pDstVertexBuffer, pSpr->m_pVertex, skipSize * sizeof(float));
								if (pLastTextureMask) {
									memcpy32(ptrUVMask, pSpr->m_pVertexMaskUV,	(skipSize *sizeof(float)) >> 1);
									ptrUVMask += (skipSize & ~1) >> 1;
								}
							}

							{
								u32 col = colorIndex % COLOR_COUNT;
								for (u32 n=0; n < pSpr->m_uiVertexCount; n++) {  pDstColBuffer[n] = *((u32*)&g_colors[col*4]); }
							}

							// Index buffer untouched.
							pDstIndexBuffer		+= sprIndexCount;
						}

						pDstVertexBuffer	+= skipSize;
						pDstColBuffer		+= pSpr->m_uiVertexCount;
						indexCount			+= sprIndexCount;
						indexVCount			+= pSpr->m_uiVertexCount;
						offsetVertexHead	+= pSpr->m_uiVertexCount;
					}
				} else {
					// In case a sprite changed from n -> 0 vertex : global buffer is shifted.
					bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);
				}

				// Reset flag (processed)
				pCommand->m_uiStatus = 0;
				// Go next command.
				pCommand = pCommand->m_pNext;
			} else {
				textures[0] = bTex ? pLastTexture : m_pWhiteTextureUsage;
				textures[1] = pLastTextureMask;
				emitDrawCall	(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
				colorIndex++;
				indexVCount = 0;
				
				// Support render state change.
				CKLBRenderState* pRCom = (CKLBRenderState*)pCommand;

				int cmdType = pRCom->m_commandType;
				if (cmdType & RENDERCOMMAND_CHANGERENDERSTATE) {
					m_pCurrState	= pRCom->getState();
				}

				if (cmdType & RENDERCOMMAND_SETSHADER) {
					m_pCurrShader	= pRCom->getShader();
				}

				if (cmdType & RENDERCOMMAND_EXECUTECOMMAND) {
					// case RENDERCOMMAND_CLEARCOLOR:
					// case RENDERCOMMAND_CLEARDEPTH:
					// case RENDERCOMMAND_CLEARSTENCIL:
					pRCom->executeCommand();
					if (cmdType & RENDERCOMMAND_STATECALLBACK) {
						m_pCurrState = NULL;
						pLastTexture = NULL;
					}
				}

				if (cmdType & RENDERCOMMAND_CHANGETARGET) {
				}

				if (cmdType & RENDERCOMMAND_UNSETSHADER) {
					m_pCurrShader	= m_pShaderInstance;
				}

				bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);

				// Reset flag (processed)
				pCommand->m_uiStatus = 0;
				// Go next command.
				pCommand = pCommand->m_pNext;

			}
		}
		textures[0] = bTex ? pLastTexture : m_pWhiteTextureUsage;
		textures[1] = pLastTextureMask;
		emitDrawCall		(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
		colorIndex++;
	} while ((stackDepth -= 2) >= 0);
	m_pDefaultStateCommand->m_commandType |= RENDERCOMMAND_CLEARCOLOR;
	m_pDefaultStateCommand->m_colorClearRed	 = savedClearColor[0];
	m_pDefaultStateCommand->m_colorClearGreen = savedClearColor[1];
	m_pDefaultStateCommand->m_colorClearBlue	 = savedClearColor[2];
	m_pDefaultStateCommand->m_colorClearAlpha = savedClearColor[3];
}

// Rendering.
void CKLBRenderingManager::draw() {
	if (m_bRenderOverDraw) {
		drawOverdraw();
		return;
	}

	klb_assertNull(m_pRenderWatchDog, "CKLBRenderingManager::setup not done first");

	CKLBRenderCommand*	renderStack[10];

	CTextureUsage*		pLastTexture	= NULL;
	CKLBRenderCommand*	pCommand		= this->m_pListStart;
	u16*				pDstIndexBuffer = (u16*)m_pIdxBuffer->updateStart(0);

	u8					bufferShift		= true;
	u16					strideVertex;
	float*				pDstVertexBuffer		= m_pVerBuffer->updateStart(1, 0, &strideVertex);
	u32*				pDstColBuffer			= (u32*)m_pColBuffer->updateStart(3, 0, null);
	float*				pDstMaskUVBuffer;
	u16		indexCount			= 0;
	u16		indexVCount			= 0;
	u16		offsetVertex		= 0;
	u16		offsetVertexHead	= 0;
	u16		offsetIndex			= 0;
	s32		stackDepth			= 0;
	CTextureUsage*		pLastTextureMask = NULL;

	CKLBOGLWrapper& pOGLMgr	= CKLBOGLWrapper::getInstance();
	pOGLMgr.resetSamplers();
	CTextureUsage* textures[2];
	s32 uniformIDs[2];
	textures[0] = NULL;
	uniformIDs[0] = 1;
	textures[1] = NULL;
	uniformIDs[1] = 2;
	CBuffer* buffers[3] = { m_pVerBuffer, m_pColBuffer, m_pMaskUVBuffer };
	/*
	dump(0);
	dumpMetrics();
	*/

	// Default
	m_pCurrState	= &noAlphaState;
	// Default
	m_pCurrShader	= m_pShaderInstance;
	pDstMaskUVBuffer = m_pMaskUVBuffer->updateStart(4, 0, null);
	g_textureMask = NULL;
	renderStack[0]	= m_pRenderWatchDog;
	renderStack[1]	= pCommand;


#ifdef DEBUG_PERFORMANCE
	m_drawTime			= CPFInterface::getInstance().platform().nanotime();
	m_vertexCount		= 0;	// DONE
	m_indexCount		= 0;	// DONE
	m_spriteCount		= 0;	// DONE
	m_renderStateChange	= 0;	// DONE
	m_textureChange		= 0;	// DONE
	m_totalTransferSize	= 0;	// Total Transfer to GPU	
	m_memCopySize		= 0;	// Internal Move
	m_drawCall			= 0;	// DONE
#endif
	float* ptrUVMask	= pDstMaskUVBuffer;

#ifndef OPENGL2
	dglActiveTexture(GL_TEXTURE1);
	dglClientActiveTexture(GL_TEXTURE1);
	dglDisable(GL_TEXTURE_2D);
	dglDisableClientState(GL_TEXTURE_COORD_ARRAY);

	dglActiveTexture(GL_TEXTURE0);
	dglClientActiveTexture(GL_TEXTURE0);
	g_textureMask = NULL;
#endif

	// OPTIMIZE RP : have mecanism to remember FOR EACH draw call (render state change)
	//						avoid buffer shift in first draw call to impact further draw calls. (ie layers)
	//						and have different buffer to avoid crush by previous call.

	do {
		CKLBRenderCommand* pEnd = renderStack[stackDepth];
		pCommand				= renderStack[stackDepth+1];
		while (pCommand != pEnd) {
			/* Comment out code concerning disabling of draw command.
			if (pCommand->ignore == true) {
				pCommand = pCommand->m_pNext;
				continue;
			}*/

			if (pCommand->m_commandType & RENDERCOMMAND_SPRITE) {
				CKLBSprite* pSpr = (CKLBSprite*)pCommand;

				if ((pSpr->m_uiVertexCount != 0) && (!(pCommand->m_commandType & RENDERCOMMAND_IGNORE))) {	// TODO OPTIMIZE : Empty sprite could be optimized to be skipped once.
				#ifdef DEBUG_PERFORMANCE
					m_spriteCount++;
				#endif
					SRenderState* spriteState = pSpr->m_pState ? pSpr->m_pState : &alphaState;
					if ((pSpr->m_pTexture != pLastTexture) || (m_pCurrState != spriteState) || (pSpr->m_pMaskTexture != pLastTextureMask)) {
					#ifdef DEBUG_PERFORMANCE  
						m_textureChange++;
					#endif
						textures[0] = pLastTexture;
						textures[1] = pLastTextureMask;
						emitDrawCall		(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
						indexVCount  = 0; // Reset index counter.
						pLastTexture		= pSpr->m_pTexture;
						pLastTextureMask	= pSpr->m_pMaskTexture;
						m_pCurrState		= spriteState;
					}

					if (pCommand->m_commandType & RENDERCOMMAND_3D) {
						((CKLBSprite3D*)pCommand)->draw();
					} else {
						u16 sprIndexCount	= pSpr->m_uiIndexCount;
						u16 skipSize		= pSpr->m_uiVertexCount * strideVertex;

						bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);
						// Copy chunk of complete vertex. 
						// MemCopy UV
						// MemCopy XY
	#ifdef DEBUG_PERFORMANCE
						m_totalTransferSize	+= skipSize + pSpr->m_uiVertexCount;	// X,Y,U,V,Color
	#endif
						if (bufferShift) {
							// Copy X,Y,U,V
							memcpy32(pDstVertexBuffer,	pSpr->m_pVertex,	skipSize              * sizeof(float));
							// Modify color only on shift.
							memcpy32(pDstColBuffer,		pSpr->m_pColors,	pSpr->m_uiVertexCount * sizeof(u32  ));

							if (pLastTextureMask) {
								memcpy32(ptrUVMask, pSpr->m_pVertexMaskUV,	(skipSize *sizeof(float)) >> 1);
							} else {
								memset(ptrUVMask, 0, (skipSize * sizeof(float)) >> 1);
							}
							ptrUVMask += (skipSize & ~1) >> 1;
	#ifdef DEBUG_PERFORMANCE
							m_memCopySize	+= (skipSize + pSpr->m_uiVertexCount);	// X,Y,U,V,Color
	#endif

							// Index buffer recompute
							u16 lCount		= sprIndexCount;
							u16* pSprIdx	= pSpr->m_pIndex;

							//
							// Unroll loop by block of 8 indexes
							//
						loopSwitch:
							switch (lCount) {
							case 7: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 6: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 5: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 4: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 3: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 2: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
							case 1: *pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
								break;
							default:
								if (lCount >= 8) {
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									*pDstIndexBuffer++	= (*pSprIdx++) + indexVCount;
									lCount -= 8;
									goto loopSwitch;
								}
							}

		//					printf("cpy xyuv cpy col by shift");

						} else {
							// --- Geometry or color update : triangle do not change ---
							if (pCommand->m_uiStatus & (FLAG_XYUPDATE | FLAG_UVUPDATE)) {
								memcpy32(pDstVertexBuffer, pSpr->m_pVertex, skipSize * sizeof(float));
								if (pLastTextureMask) {
									memcpy32(ptrUVMask, pSpr->m_pVertexMaskUV,	(skipSize *sizeof(float)) >> 1);
									ptrUVMask += (skipSize & ~1) >> 1;
								}

	#ifdef DEBUG_PERFORMANCE
								m_memCopySize	+= skipSize;	// Number of float XYUV
	#endif
		//						printf("cpy xyuv ");
							} else {
								// else XYUV untouched
		//						printf("skp xyuv ");
							}

							if (pCommand->m_uiStatus & FLAG_COLORUPDATE) {
								memcpy32(pDstColBuffer, pSpr->m_pColors, pSpr->m_uiVertexCount * sizeof(u32));	// Modify color only on shift.
	#ifdef DEBUG_PERFORMANCE
								m_memCopySize	+= pSpr->m_uiVertexCount; // Number of color.
	#endif
		//						printf("cpy col");
							} else {
								// else Color untouched.
		//						printf("skp col");
							}

							// Index buffer untouched.
							pDstIndexBuffer		+= sprIndexCount;
						}

						pDstVertexBuffer	+= skipSize;
						pDstColBuffer		+= pSpr->m_uiVertexCount;
						indexCount			+= sprIndexCount;
						indexVCount			+= pSpr->m_uiVertexCount;
						offsetVertexHead	+= pSpr->m_uiVertexCount;
					}
				} else {
					// In case a sprite changed from n -> 0 vertex : global buffer is shifted.
					bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);
	//				printf("skip %i shift",bufferShift);
				}

				// Reset flag (processed)
				pCommand->m_uiStatus = 0;
				// Go next command.
				pCommand = pCommand->m_pNext;
			} else {
				#ifdef DEBUG_PERFORMANCE  
					m_renderStateChange++;
				#endif
				textures[0] = pLastTexture;
				textures[1] = pLastTextureMask;
				emitDrawCall	(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
				indexVCount = 0;
				
				// Support render state change.
				CKLBRenderState* pRCom = (CKLBRenderState*)pCommand;

				int cmdType = pRCom->m_commandType;
				if (cmdType & RENDERCOMMAND_CHANGERENDERSTATE) {
					m_pCurrState	= pRCom->getState();
				}

				if (cmdType & RENDERCOMMAND_SETSHADER) {
					m_pCurrShader	= pRCom->getShader();
				}

				if (cmdType & RENDERCOMMAND_EXECUTECOMMAND) {
					// case RENDERCOMMAND_CLEARCOLOR:
					// case RENDERCOMMAND_CLEARDEPTH:
					// case RENDERCOMMAND_CLEARSTENCIL:
					pRCom->executeCommand();
					if (cmdType & RENDERCOMMAND_STATECALLBACK) {
						m_pCurrState = pRCom->getState();
						pLastTexture = NULL;
					}
				}

				if (cmdType & RENDERCOMMAND_CHANGETARGET) {
				}

				if (cmdType & RENDERCOMMAND_UNSETSHADER) {
					m_pCurrShader	= m_pShaderInstance;
				}

				bufferShift |= (pCommand->m_uiStatus & FLAG_BUFFERSHIFT);

				// Reset flag (processed)
				pCommand->m_uiStatus = 0;
				// Go next command.
				pCommand = pCommand->m_pNext;

			}
		}
		textures[0] = pLastTexture;
		textures[1] = pLastTextureMask;
		emitDrawCall		(&indexCount, &offsetIndex, &offsetVertex, offsetVertexHead, textures, uniformIDs, buffers);
	} while ((stackDepth -= 2) >= 0);
#ifdef DEBUG_PERFORMANCE
	m_drawTime			= CPFInterface::getInstance().platform().nanotime() - m_drawTime;
#endif
}

// ------------------------------------------
//   Render Command.
// ------------------------------------------

CKLBRenderCommand::CKLBRenderCommand():
	m_pNext				(NULL),
	m_pPrev				(NULL),
	m_pAllocPrev		(NULL),
	m_pAllocNext		(NULL),
	m_uiLocalColor		(0xFFFFFFFF),	// Most likely never used color to force refresh at first setup.
	m_uiOrder			(0),
	m_renderOffset		(0),
	m_uiRefCount		(0),
	ignore				(false)
{
	m_commandType	= 0;
	m_uiStatus		= 0;
}

void CKLBRenderCommand::changeOrder(CKLBRenderingManager& pRdr, u32 newOrder) {
	newOrder += m_renderOffset;

	// Can change the order ONLY if the object is already in the list
	if (this->m_uiOrder == newOrder) {
		return;
	}

//	if (this->m_pNext || this->m_pNext) {
	if (this->m_pPrev || this->m_pNext) {
		pRdr.removeFromRendering(this);
		pRdr.addToRendering(this, newOrder);
	} else {
		this->m_uiOrder = newOrder;
	}
}

// ------------------------------------------
//   Sprite.
// ------------------------------------------

CKLBSprite::CKLBSprite():
	CKLBRenderCommand	(),
	m_pTexture			(NULL),
	m_pMaskTexture		(NULL),
	m_pVertex			(NULL),
	m_pVertexMaskUV		(NULL),
	m_pIndex			(NULL),
	m_pColors			(NULL),
	m_uiVertexCount		(0),
	m_uiIndexCount		(0),
	m_preserveImageCenter(false),
	m_uiMaxVertexCount	(0),
	m_uiMaxIndexCount	(0),
	m_pImageAsset		(NULL),
	m_pState			(NULL),
	m_uiColor			(0x03EF02F6),
	m_bAllocated		(1),
	m_click				(1)
{
	m_uiStatus			= 0;
	m_commandType		= RENDERCOMMAND_SPRITE;
	m_pState			= CKLBRenderingManager::getInstance().getDefaultSpriteState();
}

CKLBSprite::~CKLBSprite() {
	if (m_pNext || m_pPrev) {
		CKLBRenderingManager::getInstance().removeFromRendering(this);
	}

	if (m_pVertex && m_bAllocated) {
		KLBDELETEA(m_pVertex);
		m_pVertex = NULL;
	}
}

bool crossLine( float x1 , float y1 , float x2 , float y2 , float px , float py , float cx , float cy )
{
	float check=0.0f;

	check = ((x1-x2)*(py-y1) + (y1-y2)*(x1-px)) * ((x1-x2)*(cy-y1) + (y1-y2)*(x1-cx));

	if( check<0 ) return true;

	return false;
}
bool CKLBSprite::clicked(float u, float v) {
	float inputX = u;
	float inputY = v;

	u16 tri_max = m_uiMaxVertexCount-2;
	u16 tri_count = 0;
	
	float p1[4];
	float p2[4];
	float p3[4];

	//
	// 1. Test by bounding box search.
	//
	bool inside = false;
	float* pVert = m_pVertex;
	float minX = 99999.0f,  minY = 99999.0f;
	float maxX = -99999.0f, maxY = -99999.0f;

	for( tri_count=0 ; tri_count<m_uiMaxVertexCount; tri_count++ ) {
		float v = *pVert++;		// X
		if (v < minX) {	minX = v;	}
		if (v > maxX) {	maxX = v;	}
		v = *pVert;	pVert +=3 ;	// Y, skip UV
		if (v < minY) {	minY = v;	}
		if (v > maxY) {	maxY = v;	}
	}

	// Outside of bounding box -> Early reject.
	if ((inputX < minX) || (inputX > maxX)) { return false; }
	if ((inputY < minY) || (inputY > maxY)) { return false; }

	//
	// 2. Test if inside triangle.
	//
	for( tri_count=0 ; tri_count<tri_max ; tri_count++ )
	{
		u16	idx[3];
		idx[0] = tri_count*3;
		idx[1] = tri_count*3+1;
		idx[2] = tri_count*3+2;

		u16 vertexID = m_pIndex[idx[0]]*VERTEX_SIZE;
		p1[0] = m_pVertex[vertexID++];		// x
		p1[1] = m_pVertex[vertexID];		// y

		vertexID = m_pIndex[idx[1]]*VERTEX_SIZE;
		p2[0] = m_pVertex[vertexID++];		// x
		p2[1] = m_pVertex[vertexID];		// y

		vertexID = m_pIndex[idx[2]]*VERTEX_SIZE;
		p3[0] = m_pVertex[vertexID++];		// x
		p3[1] = m_pVertex[vertexID];		// y

		float cx = (p1[0] + p2[0] + p3[0])/3.0f;
		float cy = (p1[1] + p2[1] + p3[1])/3.0f;

		if( crossLine( p1[0], p1[1], p2[0], p2[1], inputX, inputY, cx, cy ) ) continue;
		if( crossLine( p1[0], p1[1], p3[0], p3[1], inputX, inputY, cx, cy ) ) continue;
		if( crossLine( p2[0], p2[1], p3[0], p3[1], inputX, inputY, cx, cy ) ) continue;

		p1[2] = m_pVertex[m_pIndex[idx[0]]*VERTEX_SIZE+VERTEX_U_IDX];	// u
		p1[3] = m_pVertex[m_pIndex[idx[0]]*VERTEX_SIZE+VERTEX_V_IDX];	// v
		p2[2] = m_pVertex[m_pIndex[idx[1]]*VERTEX_SIZE+VERTEX_U_IDX];	// u
		p2[3] = m_pVertex[m_pIndex[idx[1]]*VERTEX_SIZE+VERTEX_V_IDX];	// v
		p3[2] = m_pVertex[m_pIndex[idx[2]]*VERTEX_SIZE+VERTEX_U_IDX];	// u
		p3[3] = m_pVertex[m_pIndex[idx[2]]*VERTEX_SIZE+VERTEX_V_IDX];	// v

		inside = true;
		break;
	}

	if( inside )
	{
		float testX = inputX - p1[0];
		float testY = inputY - p1[1];
		
		float TX[2],TY[2];
		/*
			p3
			|
			p1----p2

			T1 = p3-p1
			T2 = p2-p1
		*/
		TY[0] = p3[0]-p1[0];
		TY[1] = p3[1]-p1[1];

		TX[0] = p2[0]-p1[0];
		TX[1] = p2[1]-p1[1];

		float invDet = 1.0f / (TX[0]*TY[1]-TX[1]*TY[0]);
		float mat[4];
		/*
			mat[0] mat[1]
			mat[2] mat[3]
		*/
		mat[0] =  TY[1]*invDet;	mat[1] = -TY[0]*invDet;
		mat[2] = -TX[1]*invDet;	mat[3] =  TX[0]*invDet;

		float R[2];
		R[0] = testX*mat[0]+testY*mat[1];
		R[1] = testX*mat[2]+testY*mat[3];

		if ((R[0] < 0.0f) || (R[1] < 0.0f) || (R[0] + R[1] > 1.0f)) {
			// Early reject.
			return false;
		}

		float retU = p1[2]+R[0]*(p2[2]-p1[2])+R[1]*(p3[2]-p1[2]);
		float retV = p1[3]+R[0]*(p2[3]-p1[3])+R[1]*(p3[3]-p1[3]);

		return this->m_pTexture->pTexture->isAlpha(retU,retV) != 0;
	}

	return false;
}

CKLBSprite4_6::CKLBSprite4_6():CKLBSprite() {
	m_bAllocated = 0;
}

CKLBSprite4_6::~CKLBSprite4_6() {
	// Do nothing.
}

#include "CKLBNode.h"

/*virtual*/
void CKLBSprite::applyNode(CKLBNode* pNode, float stx, float sty) {
	const SMatrix2D* pMat = &pNode->m_composedMatrix;
	if (this->m_pImageAsset) {
		float* srcUV  = this->m_pImageAsset->getUVBuffer();
		float* srcXY  = this->m_pImageAsset->getXYBuffer();
		int    vCount = this->m_uiVertexCount;

	#ifdef DEBUG_PERFORMANCE
		CKLBNode::s_vertexRecomputeCount += vCount;
	#endif
	
		// We are ok here because we garantee to modify coordinate only when matrix changes.
		this->m_uiStatus |= FLAG_XYUPDATE;

		float* dstXY;
		if (this->m_uiStatus & FLAG_UVUPDATE) {
//			printf("update UV");

			// X,Y,U,V
			dstXY  =  this->m_pVertex + (VERTEX_SIZE - 2);

			for (int n=0; n < vCount; n++) {
				*dstXY++ = (*srcUV++);
				*dstXY = (*srcUV++);
				dstXY += (VERTEX_SIZE-1);
			}

		}

//		printf("\n");
		dstXY  =  this->m_pVertex;

		switch (pMat->m_type) {
		case MATRIX_ID:	// Identity
			for (int n=0; n < vCount; n++) {
				*dstXY++ = (*srcXY++) + stx;
				*dstXY   = (*srcXY++) + sty;
				dstXY   += (VERTEX_SIZE - 1);
			}
			break;
		case MATRIX_T:
			{
				float tx = pMat->m_matrix[MAT_TX] + stx;
				float ty = pMat->m_matrix[MAT_TY] + sty;

				for (int n=0; n < vCount; n++) {
					*dstXY++ = (*srcXY++) + tx;
					*dstXY   = (*srcXY++) + ty;
					dstXY   += (VERTEX_SIZE - 1);
				}
			}
			break;
		case MATRIX_TS:
			{
				float tx = pMat->m_matrix[MAT_TX];
				float ty = pMat->m_matrix[MAT_TY];
				float sx = pMat->m_matrix[MAT_A];
				float sy = pMat->m_matrix[MAT_D];

				for (int n=0; n < vCount; n++) {
					*dstXY++ = (((*srcXY++) + stx) * sx) + tx;
					*dstXY   = (((*srcXY++) + sty) * sy) + ty;
					dstXY   += (VERTEX_SIZE - 1);
				}
			}
			break;
		case MATRIX_TG:
			{
				float tx  = pMat->m_matrix[MAT_TX];
				float ty  = pMat->m_matrix[MAT_TY];
				float sx  = pMat->m_matrix[MAT_A];
				float nsx = pMat->m_matrix[MAT_B];
				float sy  = pMat->m_matrix[MAT_D];
				float nsy = pMat->m_matrix[MAT_C];

				for (int n=0; n < vCount; n++) {
					float lx = (*srcXY++) + stx;
					float ly = (*srcXY++) + sty;

					*dstXY++ = (lx * sx) + (ly * nsx) + tx;
					*dstXY   = (ly * sy) + (lx * nsy) + ty;
					dstXY   += (VERTEX_SIZE - 1);
				}
			}
			break;
		}
	}
}

void CKLBSprite::applyNode(CKLBNode* pNode) {
	applyNode(pNode, 0.0f, 0.0f);
}

/**
	WARNING : This function MUST be used with marking the NODE
	for recomputation.
 */
void CKLBSprite::switchImage(CKLBImageAsset* pImage) {
	if (m_pImageAsset != pImage) {
		u32 oldOrder = this->m_uiOrder - m_renderOffset; // Original order
		m_pImageAsset = pImage;

		if (pImage)	{
			u32 vCount = pImage->getVertexCount();
			m_uiStatus |= FLAG_UVUPDATE; // XY modified by node marking.
			if (m_uiVertexCount	!= vCount) {
				m_uiStatus |= FLAG_BUFFERSHIFT;
			}

			u32 color = m_pColors[0];	// Backup transformed color.

			if (vCount > m_uiMaxVertexCount) {
				//
				// Delete old buffers
				//
				if (m_bAllocated) {
					KLBDELETEA(m_pVertex);
				}
				
				//
				// Allocate new buffers
				//
				int add = ((pImage->m_attribMask & CKLBImageAsset::IS_3DMODEL) ? 2 : 0);
				float* pVertex		= KLBNEWA(float, vCount * (5 + add));	// Add Z&W for 3D object.
				u32*   pColors		= (u32*)&pVertex[vCount * 4 + add];
				if (pVertex) {
					m_bAllocated		= 1;
					m_pVertex			= pVertex;
					m_pColors			= pColors;
					m_uiMaxVertexCount	= m_uiVertexCount;
				} else {
					klb_assertAlways("Memory Allocation error");
				}
			}

			m_uiIndexCount		= (u16)pImage->getIndexCount();
			m_uiVertexCount		= (u16)vCount;
			m_pIndex			= pImage->getIndexBuffer();
			m_pTexture			= pImage->getTexture()->m_pTextureUsage;
			m_renderOffset		= pImage->m_renderOffset * gUseOffsetSystem;
			memset32(m_pColors, color, vCount*sizeof(u32));
		} else {
			m_uiStatus |= FLAG_BUFFERSHIFT;

			m_uiVertexCount		= 0;
			m_uiIndexCount		= 0;
			m_pIndex			= NULL;
			m_pTexture			= NULL;
			m_renderOffset		= 0;
		}
		
		this->changeOrder(CKLBRenderingManager::getInstance(), oldOrder); // Set original order.
	}
}

void CKLBSprite::setMask(CKLBImageAsset* pImage) {
	if (pImage) {
		if ((pImage->getVertexCount() != m_uiVertexCount) || (pImage->getIndexCount() != m_uiIndexCount)) {
			klb_assertAlways("Mask vertices does not match sprite image vertices");
		}
		this->m_pVertexMaskUV	= pImage->getUVBuffer();
		this->m_pMaskTexture	= pImage->getTexture()->m_pTextureUsage;
	} else {
		this->m_pVertexMaskUV	= NULL;
		this->m_pMaskTexture	= NULL; 
	}
}

// ------------------------------------------
//   Dynamic Sprites.
// ------------------------------------------


CKLBPolyline::CKLBPolyline()
:CKLBDynSprite	()
,m_points		(NULL)
,m_maxPts		(0)
{
}

CKLBPolyline::~CKLBPolyline() {
	release();
}

void CKLBPolyline::release() {
	KLBDELETEA(_internalImg.m_pXYCoord);
	_internalImg.m_pXYCoord = NULL;

	KLBDELETEA(m_pVertex);
	m_pVertex = NULL;
}

bool CKLBPolyline::setMaxPointCount	(u32 ptsCount) {
	release();
	u32 vertCount = (ptsCount-1) * 4;
	u32 idxCount  = (ptsCount-1) * 6;

	_internalImg.m_bAllocatedOutsideTexture = true;
	
	_internalImg.m_uiIndexCount		= 0;
	_internalImg.m_uiVertexCount	= 0;
	_internalImg.m_pTextureAsset	= NULL;

	float* buf	=	KLBNEWA(float, (vertCount*4) + ((idxCount + 2)>>1)); 
	
	// Use as one memory block.
	_internalImg.m_pXYCoord			= buf;
	_internalImg.m_pUVCoord			= &buf[vertCount*2];
	_internalImg.m_pIndex			= (u16*)&buf[vertCount*4];

	// Post Transform array.
	float*	arr			= KLBNEWA(float,(vertCount * (VERTEX_SIZE + 2)) + (ptsCount*2) );
	u32* arrCol			= (u32*)&arr[vertCount*VERTEX_SIZE];
	m_pLocalColors		= &arrCol[vertCount];
	m_points			= (float*)&m_pLocalColors[vertCount];
	
	bool res = _internalImg.m_pXYCoord && arr;
	if (res) {

		//
		// Fill arrays
		//
		for (u32 n=0; n < vertCount*2; n++) {
			_internalImg.m_pUVCoord[n] = 0.0f;
			_internalImg.m_pXYCoord[n] = 0.0f;
		}

		u16 m = 0;
		for (u32 n=0; n < idxCount; n+=6) {
			_internalImg.m_pIndex[n  ]	= 0 + m;
			_internalImg.m_pIndex[n+1]	= 1 + m;
			_internalImg.m_pIndex[n+2]	= 3 + m;
			_internalImg.m_pIndex[n+3]	= 1 + m;
			_internalImg.m_pIndex[n+4]	= 2 + m;
			_internalImg.m_pIndex[n+5]	= 3 + m;
			m += 4;
		}

		for (u32 n=0; n < vertCount; n++) {
			arrCol[n]					= 0xFFFFFFFF;
			m_pLocalColors[n]			= 0xFFFFFFFF;
		}

		for (u32 n=0; n < ptsCount*2; n++) {
			m_points[n] = 0.0f;
		}

		m_pImageAsset		= &_internalImg;
		m_pVertex			= arr;
		m_pColors			= arrCol;
		m_pIndex			= _internalImg.m_pIndex;	// Cache
		m_uiVertexCount		= 0;
		m_uiIndexCount		= 0;
		m_uiMaxVertexCount	= (u16)vertCount;
		m_uiMaxIndexCount	= (u16)idxCount;

		m_pTexture			= NULL;
		m_uiStatus			= FLAG_XYUPDATE | FLAG_COLORUPDATE | FLAG_UVUPDATE;

		this->switchImage(&_internalImg);
	
		m_maxPts			= (u16)ptsCount;
	} else {
		KLBDELETEA(arr);
		// Other arrays destroyed when this object is destroyed.
	}
	return res;
}

void CKLBPolyline::setPointCount	(u32 ptsCount) {
	u32 vertCount = (ptsCount-1) * 4;
	u32 idxCount  = (ptsCount-1) * 6;

	klb_assertNull(vertCount <= m_uiMaxVertexCount, "setPointCount reached limit.");
	klb_assertNull(idxCount  <= m_uiMaxIndexCount , "setPointCount reached limit.");

	m_uiVertexCount					= (u16)vertCount;
	m_uiIndexCount					= (u16)idxCount;
	_internalImg.m_uiIndexCount		= (u16)idxCount;
	_internalImg.m_uiVertexCount	= (u16)vertCount;

	for (u32 n=0; n < ptsCount>>1; n++) {
		recomputeSegment(n);
	}
	m_uiStatus			|= FLAG_BUFFERSHIFT | FLAG_XYUPDATE | FLAG_COLORUPDATE | FLAG_UVUPDATE;
}

void CKLBPolyline::recomputeSegment(u32 idxSegment) {
	int id = idxSegment * 2;

	// Start
	float dx = m_points[id + 2] - m_points[id];
	float dy = m_points[id + 3] - m_points[id + 1];

	// Normalize Vector.
	float norm = sqrt((dx*dx) + (dy*dy));
	dx	= dx / norm;
	dy	= dy / norm;

	// Width for now
	dx *= 0.75f; 
	dy *= 0.75f; 

	float* arr = &_internalImg.m_pXYCoord[idxSegment * 8];

	//
	arr[0] = m_points[id  ] - dy;	// x0
	arr[1] = m_points[id+1] + dx;	// y0

	arr[2] = m_points[id+2] - dy;	// x1
	arr[3] = m_points[id+3] + dx;	// y1

	arr[4] = m_points[id+2] + dy;	// x2
	arr[5] = m_points[id+3] - dx;	// y2

	arr[6] = m_points[id  ] + dy;	// x3
	arr[7] = m_points[id+1] - dx;	// y3
}

void CKLBPolyline::setPoint			(u32 idx, float x, float y) {
	klb_assertNull(idx < m_maxPts, "setPointCount reached limit.");

	u32 id = idx * 2;
	m_points[id  ] = x;
	m_points[id+1] = y;

	if (idx == 0) {
		recomputeSegment(0);
	} else if (idx == (m_maxPts-1)) {
		idx++;
		idx >>= 1;
		recomputeSegment(idx  );
	} else {
		// Intermediate
		idx >>= 1;
		recomputeSegment(idx  );
		recomputeSegment(idx+1);
	}
	m_uiStatus			|= FLAG_XYUPDATE;
}

void CKLBPolyline::setColor			(u32 colorARGB) {
	// Conversion u32 ARGB platform dependant to RGBA byte order.
	u8 col[4];
	col[3] = colorARGB>>24;
	col[0] = colorARGB>>16;
	col[1] = colorARGB>>8 ;
	col[2] = colorARGB>>0 ;
	u32 color = *((u32*)col);

	for (u32 n=0; n < m_uiMaxVertexCount; n++) {
		m_pColors[n]				= color;
		m_pLocalColors[n]			= color;
	}
	m_uiStatus			|= FLAG_COLORUPDATE;
}

CKLBDynSprite::CKLBDynSprite()
:CKLBSprite()
,m_pLocalColors(NULL)
{
	_internalImg.m_pTextureAsset	= NULL;
	this->m_pTexture				= NULL;
	m_useTranslation				= false;
}

CKLBDynSprite::~CKLBDynSprite() {
}

bool CKLBDynSprite::setTriangleCount(u16 vertexCount, u16 indexCount, bool resetTexture) {
	_internalImg.m_bAllocatedOutsideTexture = true;

	if ((m_uiMaxIndexCount >= indexCount) && (m_uiMaxVertexCount >= vertexCount)) {
		return true;
	}

	KLBDELETEA(_internalImg.m_pXYCoord);
	KLBDELETEA(m_pVertex);

	// Vertex & index count

	_internalImg.m_uiIndexCount		= indexCount;
	_internalImg.m_uiVertexCount	= vertexCount;
	_internalImg.m_pTextureAsset	= NULL;

	float* buf	=	KLBNEWA(float, (vertexCount*4) + ((indexCount + (indexCount&1))>>1));

	_internalImg.m_pXYCoord			= buf;
	_internalImg.m_pUVCoord			= &buf[vertexCount*2];
	_internalImg.m_pIndex			= (u16*)&buf[vertexCount*4];

	//
	float*	arr			= KLBNEWA(float,vertexCount * (VERTEX_SIZE + 2) );
	u32* arrCol			= (u32*)&arr[vertexCount * VERTEX_SIZE];
	m_pLocalColors		= &arrCol[vertexCount];

	bool res = (_internalImg.m_pXYCoord && arr);
	if (res) {

		//
		// Fill arrays
		//
		for (u32 n=0; n < (u32)(vertexCount<<1); n++) {
			_internalImg.m_pUVCoord[n] = 0.0f;
			_internalImg.m_pXYCoord[n] = 0.0f;
		}

		for (u32 n=0; n < vertexCount; n++) {
			arrCol[n]					= 0xFFFFFFFF;
			m_pLocalColors[n]			= 0xFFFFFFFF;
		}

		for (u32 n=0; n < indexCount; n++) {
			_internalImg.m_pIndex[n]	= (u16)n;
		}

		if (VERTEX_SIZE > 4) {
			memset32(arr, 0, vertexCount * VERTEX_SIZE * sizeof(float));
		}

		m_pImageAsset		= &_internalImg;
		m_pVertex			= arr;
		m_pColors			= arrCol;
		m_pIndex			= _internalImg.m_pIndex;	// Cache
		m_uiVertexCount		= vertexCount;
		m_uiIndexCount		= indexCount;
		m_uiMaxVertexCount	= vertexCount;
		m_uiMaxIndexCount	= indexCount;

		if (resetTexture) {
			m_pTexture = NULL;
		}
		m_uiStatus			= FLAG_XYUPDATE | FLAG_COLORUPDATE | FLAG_UVUPDATE;
	} else {
		if	(arr)				{ KLBDELETEA(arr);				}
		// Other arrays destroyed when this object is destroyed.
	}
	return res;
}

void CKLBDynSprite::setVertexXY		(u32 index, float x, float y) {
	if (index < m_uiVertexCount) {
		index *= 2;
		_internalImg.m_pXYCoord[index++] = x;
		_internalImg.m_pXYCoord[index  ] = y;
		m_uiStatus |= FLAG_XYUPDATE;
	}
}

void CKLBDynSprite::setVertexUV		(u32 index, float u, float v) {
	if (index < m_uiVertexCount) {
		index *= 2;
		_internalImg.m_pUVCoord[index++] = u;
		_internalImg.m_pUVCoord[index  ] = v;
		m_uiStatus |= FLAG_UVUPDATE;
	}
}

/*virtual*/
void CKLBSprite::setColor(const float* vec4) {
	u32 col = getLocalColor();
	u8* pLocalCol = (u8*)&col;

	//-----------------------------------
	// Combine with node color
	//-----------------------------------
	s32 alpha	 = (vec4[3] * pLocalCol[3]); // A
		if (alpha >= 256) {	alpha = 255;	}
		if (alpha <    0) { alpha = 0;		}
	pLocalCol[3] = alpha;
#ifdef USE_PREMULALPHA
	alpha += (alpha & 0x80)>>7; // 0..255 -> 0..256
#endif
	s32 v		 = (vec4[0] * pLocalCol[0]); // R
		if (v >= 256) {	v = 255;	}
		if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[0] = (v * alpha) >> 8;
#else
		pLocalCol[0] = v;
#endif
		v		 = (vec4[1] * pLocalCol[1]); // G
		if (v >= 256) {	v = 255;	}
		if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[1] = (v * alpha) >> 8;
#else
		pLocalCol[1] = v;
#endif
		v		 = (vec4[2] * pLocalCol[2]); // B
		if (v >= 256) {	v = 255;	}
		if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[2] = (v * alpha) >> 8;
#else
		pLocalCol[2] = v;
#endif

	#ifdef DEBUG_PERFORMANCE
	CKLBNode::s_colorRecomputeCount  += 1;
	#endif

	// Color changed ?
	if ((col != m_uiColor) || (this->m_uiStatus & FLAG_BUFFERSHIFT)) {
		m_uiColor = col;
		u32* pCol = this->m_pColors;
		this->m_uiStatus |= FLAG_COLORUPDATE;

		// Fill with RGBA 32 bit color.
		int    vCount = this->m_uiVertexCount;
		memset32(pCol, col, vCount * sizeof(u32));
	}
}

/*virtual*/
void CKLBDynSprite::setColor(const float* vec4) {

	#ifdef DEBUG_PERFORMANCE
	CKLBNode::s_colorRecomputeCount  += this->m_uiVertexCount;
	#endif

	for (u32 n=0; n<this->m_uiVertexCount; n++) {
		u32 col = m_pLocalColors[n];
		u8* pLocalCol = (u8*)&col;

		//-----------------------------------
		// Combine with node color
		//-----------------------------------
		s32 alpha	 = (vec4[3] * pLocalCol[3]); // A
			if (alpha >= 256) {	alpha = 255;	}
			if (alpha <    0) { alpha = 0;		}
		pLocalCol[3] = alpha;
#ifdef USE_PREMULALPHA
		alpha += (alpha & 0x80)>>7; // 0..255 -> 0..256
#endif

		s32  v		 = (vec4[0] * pLocalCol[0]); // R
			if (v >= 256) {	v = 255;	}
			if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[0] = (v * alpha) >> 8;
#else
		pLocalCol[0] = v;
#endif
			v		 = (vec4[1] * pLocalCol[1]); // G
			if (v >= 256) {	v = 255;	}
			if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[1] = (v * alpha) >> 8;
#else
		pLocalCol[1] = v;
#endif

			v		 = (vec4[2] * pLocalCol[2]); // B
			if (v >= 256) {	v = 255;	}
			if (v <    0) { v = 0;		}
#ifdef USE_PREMULALPHA
		pLocalCol[2] = (v * alpha) >> 8;
#else
		pLocalCol[2] = v;
#endif

		u32* pCol = this->m_pColors;
		if (pCol[n] != col) {
			pCol[n] = col;
			this->m_uiStatus |= FLAG_COLORUPDATE;
		}
	}
}

CKLBSprite::GEOMETRY_TYPE CKLBSprite::getGeometryType() const {
	return GEOMETRY_STATIC;
}

CKLBSprite::GEOMETRY_TYPE CKLBDynSprite::getGeometryType() const {
	return GEOMETRY_DYNAMIC;
}

CKLBSprite::GEOMETRY_TYPE CKLBSpriteScale9::getGeometryType() const {
	return GEOMETRY_SCALE9;
}

void CKLBDynSprite::setVertexColor(CKLBNode* owner, u32 index, u32 color) {
	// !!! WARNING !!!
	// color is 4x8 bit in memory with RGBA order.
	// IT IS NOT RGBA inside a U32 ! (ie endianess may change the u32)
	if (index < m_uiVertexCount) {
		u32 alpha = ((u8*)(&color))[3];
		alpha += (alpha & 0x80) >> 7;
		m_pLocalColors[index] = color;
		m_uiStatus |= FLAG_COLORUPDATE;
		if (owner) {
			owner->markUpColor();
		}
	}
}

bool CKLBDynSprite::importXYUV		(CKLBImageAsset* pImage) {
	klb_assertNull(pImage,"Null ptr");
	klb_assertNull(pImage->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT), "Not a standard rectangular image");
	//
	// Import only rectangular shape into 2 triangles.
	// 013 123 Source
	// 012 345 Dest
	//
	for (u32 n=0; n<12; n+=2) {
		u32 src;

		switch (n>>1) {
		case 0: src = 0; break;
		case 1: src = 2; break;
		case 2: src = 6; break;
		case 3: src = 2; break;
		case 4: src = 4; break;
		case 5: src = 6; break;
		default: src = 0;
		}

		// Copy XY
		_internalImg.m_pXYCoord[n  ]	= pImage->m_pXYCoord[src  ];
		_internalImg.m_pXYCoord[n+1]	= pImage->m_pXYCoord[src+1];

		// Copy UV
		_internalImg.m_pUVCoord[n  ]	= pImage->m_pUVCoord[src  ];
		_internalImg.m_pUVCoord[n+1]	= pImage->m_pUVCoord[src+1];

		// Index
		m_pIndex [n>>1] = (u16)(n>>1);
	}

	_internalImg.m_pTextureAsset	= pImage->getTexture();
	this->m_pTexture				= pImage->getTexture()->m_pTextureUsage;

	// May have change in vertex count.
	m_uiStatus |= FLAG_BUFFERSHIFT;

	return true;
}

void CKLBDynSprite::setTexture	(CKLBImageAsset* pImage) {
	if (pImage) {
		_internalImg.m_pTextureAsset	= pImage->getTexture();
		this->m_pTexture				= pImage->getTexture()->m_pTextureUsage;
		_internalImg.m_fileSource	= pImage->m_fileSource;
	} else {
		_internalImg.m_pTextureAsset	= NULL;
		this->m_pTexture				= CKLBRenderingManager::getInstance().m_pWhiteTextureUsage;
		_internalImg.m_fileSource	= NULL;
	}
	
	// May have change in vertex count.
	m_uiStatus |= FLAG_BUFFERSHIFT;
}

void CKLBDynSprite::setTexture	(CTextureUsage* pUsage) {
	_internalImg.m_pTextureAsset	= NULL;
	this->m_pTexture = pUsage ? pUsage : CKLBRenderingManager::getInstance().m_pWhiteTextureUsage;
	m_uiStatus |= FLAG_BUFFERSHIFT;
}

void CKLBDynSprite::setVICount	(u32 vertexCount, u32 indexCount) {
	this->m_uiVertexCount = vertexCount;
	this->m_uiIndexCount  = indexCount;

	// May have change in vertex count.
	m_uiStatus |= FLAG_BUFFERSHIFT;
}

// ------------------------------------------
//   Slice 9 Sprite.
// ------------------------------------------

CKLBSpriteScale9::CKLBSpriteScale9()
:CKLBDynSprite		()
,m_width			(0)
,m_height			(0)
,m_pOriginalImage	(NULL)
,m_deferRecompute	(false)
{
}

CKLBSpriteScale9::~CKLBSpriteScale9() {
}

#define CHANGE_X	(1)
#define CHANGE_Y	(2)

void CKLBSpriteScale9::setWidth(s32 width) {
	if (width != m_width) {
		klb_assertNull((width >= 0) && (width < 32768), "Invalid Width (0..32767)");
		m_width		= (s16)width;
		if (!m_deferRecompute) {
			recomputeVertex(CHANGE_X);
		}
	}
}

void CKLBSpriteScale9::setHeight(s32 height) {
	if (height != m_height) {
		klb_assertNull((height >= 0) && (height < 32768), "Invalid Height (0..32767)");
		m_height	= (s16)height;
		if (!m_deferRecompute) {
			recomputeVertex(CHANGE_Y);
		}
	}
}

void CKLBSpriteScale9::endSizeUpdate() {
	if (m_deferRecompute) {
		m_deferRecompute = false;
		recomputeVertex(CHANGE_X | CHANGE_Y);
	}
}

/*
 * Reloading a texture is deliberately handled at the asset-manager boundary.
 * The loader installs the replacement texture before this routine walks the
 * render-command allocation list.  Render commands can therefore be repaired
 * in place without rebuilding the scene graph or changing command priority.
 *
 * A reload has four distinct participants:
 *
 * - the texture asset, which owns the replacement image definitions;
 * - allocated sprite commands, which may still refer to an old definition;
 * - the root node, which propagates replacement through node-owned assets;
 * - data handlers and tasks, which maintain non-rendering references.
 *
 * The function below keeps those responsibilities separate.  In particular,
 * replacing a sprite image is not a substitute for broadcasting the asset
 * update: scripts and composite resources may hold the same named image while
 * having no render command in the manager's allocation list.
 *
 * Texture validation
 * ------------------
 *
 * Loading can legitimately return no object or an object of another asset
 * type.  Neither case is an error for the reload request, so both return true
 * without entering the texture-specific repair path.
 *
 * The root node is obtained before validation because it is the common owner
 * used by the successful replacement path.  The exact target order also keeps
 * singleton acquisition consistent with the normal asset reload entry point.
 *
 * Image traversal
 * ---------------
 *
 * Every image belonging to the replacement texture is processed separately.
 * Its registered name is the stable identity used to find old sprite images
 * and to notify task, node, and data-handler users.
 *
 * The allocated-sprite list is walked from its retained head for every image.
 * This is intentional: one texture may contain several independently named
 * atlas images, and a render command can reference any one of them.
 *
 * Commands that are not sprites remain untouched.  A sprite is considered a
 * replacement candidate only when it owns an old image, that image exposes a
 * file source, and that source equals the replacement image name.
 *
 * Static sprite replacement
 * -------------------------
 *
 * A static sprite can preserve its image center.  When that mode is enabled,
 * the new image is converted to the equivalent top-left representation before
 * switchImage installs it.  This preserves the sprite's visible placement
 * even when the replacement atlas reports a different center.
 *
 * switchImage is responsible for the static sprite's texture, geometry, and
 * asset-reference transition.  The reload path must not duplicate that work.
 *
 * Dynamic sprite replacement
 * --------------------------
 *
 * Only translated dynamic sprites can be reconstructed from an atlas image.
 * Other dynamic geometry was authored independently and is deliberately left
 * alone even when its old image name happens to match.
 *
 * The replacement image is converted to top-left coordinates and supplied to
 * setTexture.  Source UV and index buffers are then copied using the counts
 * owned by the replacement image.
 *
 * Dynamic positions require an additional translation pass.  Each source
 * vertex contributes one X and one Y coordinate, and the sprite's retained
 * translation is added while copying them into its source position buffer.
 *
 * The loop uses the replacement image's vertex count.  UV, index, and position
 * data consequently remain a coherent geometry set after an atlas reload.
 *
 * Scale-nine replacement
 * ----------------------
 *
 * Scale-nine sprites retain their logical width and height while adopting a
 * replacement image's border attributes.  useImage owns that transition and
 * recomputes geometry unless a size update has explicitly been deferred.
 *
 * The geometry-type switch is exhaustive for the currently allocated sprite
 * implementations.  Unknown future geometry kinds are ignored so that a
 * texture reload cannot corrupt storage whose layout it does not understand.
 *
 * Notification order
 * ------------------
 *
 * Once render commands for one named image have been repaired, tasks receive
 * the update first.  The root node then replaces matching scene assets, and
 * data handlers finally broadcast the same typed image replacement.
 *
 * This ordering lets task-owned state observe a valid render image before node
 * traversal marks scene data dirty.  It also keeps handler callbacks after the
 * engine's direct owners have completed their transitions.
 *
 * Texture-level completion
 * ------------------------
 *
 * After every contained image has been processed, data handlers replace their
 * texture-level references.  The root node's matrix/color and render state are
 * then marked dirty so the next traversal consumes all updated geometry.
 *
 * A freshly loaded texture can finish this path without an external reference.
 * The balanced increment/decrement pair deliberately exercises normal final
 * reference handling in that case; it is not an accidental no-op.
 *
 * Ownership and lifetime invariants
 * ---------------------------------
 *
 * The replacement asset remains owned by the asset manager throughout the
 * command-list walk.  Image pointers borrowed from it therefore stay valid
 * until all sprite, node, task, and data-handler transitions are complete.
 *
 * The allocation list itself is stable during this synchronous operation.
 * Replacement helpers may change image ownership and geometry, but they do
 * not register, release, or reorder the command currently being visited.
 *
 * m_pAllocNext is read after processing each command.  This keeps traversal
 * independent of render-order links, which can change when a scale-nine image
 * adopts a replacement render offset.
 *
 * The function reports successful handling rather than whether a matching
 * sprite was found.  Callers request a reload operation; an absent texture or
 * an image with no live render users still constitutes a completed request.
 *
 * Maintenance notes
 * -----------------
 *
 * Keep geometry-specific repair inside the geometry-type switch.  Moving the
 * shared notifications into individual cases would skip non-rendering users
 * whenever no allocated sprite currently references the replacement image.
 *
 * Do not infer dynamic buffer lengths from the old sprite.  Atlas revisions
 * may change vertex or index counts, and the replacement image is the only
 * authoritative description of the copied source geometry.
 *
 * Do not replace the allocation traversal with scene-node traversal.  Render
 * commands can be retained by engine systems that are not presently attached
 * beneath the draw-resource root, yet their image references still require
 * repair before later reuse.
 *
 * Keep the final dirty marks after texture-level handler replacement.  They
 * form the publication boundary at which the reconstructed render state is
 * ready for the next frame.
 */
bool
CKLBAssetManager::reloadAssetByFileName(const char* fileName)
{
	CKLBAbstractAsset* loadedAsset =
		loadAssetByFileName(fileName, NULL, false, false);
	CKLBNode* rootNode = CKLBDrawResource::getInstance().getRoot();
	if (!loadedAsset || loadedAsset->getAssetType() != ASSET_TEXTURE) {
		return true;
	}

	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	CKLBRenderCommand* renderList = rendering.m_pAllocatedSpriteList;
	CKLBTextureAsset* texture =
		static_cast<CKLBTextureAsset*>(loadedAsset);
	for (s32 imageIndex = 0; imageIndex < texture->m_imageCount; ++imageIndex) {
		CKLBImageAsset* image = texture->m_pImages[imageIndex];
		const char* imageName = image->getName();

		if (renderList) {
			CKLBRenderCommand* command = renderList;
			do {
				if (command->m_commandType & RENDERCOMMAND_SPRITE) {
					CKLBSprite* sprite = static_cast<CKLBSprite*>(command);
					CKLBImageAsset* previousImage = sprite->m_pImageAsset;
					if (previousImage && previousImage->getFileSource()
					 && strcmp(previousImage->getFileSource(), imageName) == 0) {
						switch (sprite->getGeometryType()) {
						case CKLBSprite::GEOMETRY_STATIC:
							if (sprite->m_preserveImageCenter) {
								s32 centerX;
								s32 centerY;
								image->getCenter(centerX, centerY);
								image->getAsTopLeftImage(centerX, centerY);
							}
							sprite->switchImage(image);
							break;

						case CKLBSprite::GEOMETRY_DYNAMIC: {
							CKLBDynSprite* dynamicSprite =
								static_cast<CKLBDynSprite*>(sprite);
							if (!dynamicSprite->m_useTranslation) {
								break;
							}
							s32 centerX;
							s32 centerY;
							image->getCenter(centerX, centerY);
							dynamicSprite->setTexture(
								image->getAsTopLeftImage(centerX, centerY));

							memcpy(dynamicSprite->getSrcUVBuffer(),
								image->getUVBuffer(),
								image->getVertexCount() * 2 * sizeof(float));
							memcpy(dynamicSprite->getSrcIndexBuffer(),
								image->getIndexBuffer(),
								image->getIndexCount() * sizeof(u16));

							float* destinationXY =
								dynamicSprite->getSrcXYBuffer();
							float* sourceXY = image->getXYBuffer();
							const float translationX =
								dynamicSprite->m_translationX;
							const float translationY =
								dynamicSprite->m_translationY;
							for (u32 vertex = 0;
								 vertex < image->getVertexCount();
								 ++vertex) {
								*destinationXY++ =
									*sourceXY++ + translationX;
								*destinationXY++ =
									*sourceXY++ + translationY;
							}
							break;
						}

						case CKLBSprite::GEOMETRY_SCALE9:
							static_cast<CKLBSpriteScale9*>(sprite)->useImage(image);
							break;

						default:
							break;
						}
					}
				}
				command = command->m_pAllocNext;
			} while (command);
		}

		CKLBTaskMgr::getInstance().notifyAssetUpdate(imageName, image);
		rootNode->replaceAsset(imageName, image);
		CKLBDataHandler::broadcastToHandlers(imageName, image);
	}

	CKLBDataHandler::replaceTexture(loadedAsset);
	rootNode->markUpMatrixAndColor();
	rootNode->markUpRender();
	if (!loadedAsset->getRefCount()) {
		loadedAsset->incrementRefCount();
		loadedAsset->decrementRefCount();
	}
	return true;
}

void CKLBSpriteScale9::useImage(CKLBImageAsset* pImage) {
	if (pImage->hasStandardAttribute(CKLBImageAsset::IS_SCALE9)) {
		u32 oldOrder = this->m_uiOrder - m_renderOffset; // Original order

		// Copy XY
		memcpy(_internalImg.m_pXYCoord, pImage->m_pXYCoord, 16*2*sizeof(float));
		// Copy UV
		memcpy(_internalImg.m_pUVCoord, pImage->m_pUVCoord, 16*2*sizeof(float));
		// Copy Index the first time.
		if (m_pOriginalImage == NULL) {
			memcpy(_internalImg.m_pIndex, pImage->m_pIndex, 54*sizeof(u16));
		}

		_internalImg.m_pTextureAsset	= pImage->getTexture();
		this->m_pTexture				= pImage->getTexture()->m_pTextureUsage;
		this->m_pOriginalImage			= pImage;
		this->m_renderOffset			= pImage->m_renderOffset * gUseOffsetSystem;
		
		s32 left,middleX,right;
		s32 top,middleY,bottom;

		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_LEFT,		left);
		m_left	 = (s16)left;
		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_MIDDLEX,	middleX);
		m_middleX= (s16)middleX;
		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_RIGHT,	right);
		m_right	 = (s16)right;
		m_fRight = (float)right;

		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_TOP,		top);
		m_top	 = (s16)top;
		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_MIDDLEY,	middleY);
		m_middleY= (s16)middleY;
		pImage->getAttribute(ASSET_ATTRIB::zK2_S9_BOTTOM,	bottom);
		m_bottom = (s16)bottom;
		m_fBottom = (float)bottom;

		// Adapt to new size.
		if (!m_deferRecompute) {
			recomputeVertex(CHANGE_X | CHANGE_Y);
		}

		changeOrder(CKLBRenderingManager::getInstance(), oldOrder);
	} else {
		klb_assertAlways("Image not usable in SCALE9");
	}
}

void CKLBSpriteScale9::recomputeVertex(u32 mode) {
	if (!m_pOriginalImage) {
		return;
	}

	//	0-1---2-3
	//  | |   | |
	//  4-5---6-7
	//  | |   | |
	//  | |   | |
	//  | |   | |
	//	8-9---A-B
	//  | |   | |
	//  C-D---E-F
	//  this->m_pOriginalImage : all info.
	//  -> Recompute sizes.

	if (mode & CHANGE_X) {
		float middle = (float)(m_width - (m_left + m_right));
		float left = (float)m_left;
		float right = m_fRight;
		if (middle < 0.0f) {
			float border = left + right;
			if (border > 0.0f) {
				float scale = (middle + border) / border;
				left *= scale;
				right *= scale;
			} else {
				left = 0.0f;
				right = 0.0f;
			}
			middle = 0.0f;
		}

		float* pVertex = _internalImg.m_pXYCoord;
		for (int n=0; n<4; n++) {
			pVertex[2] = pVertex[0] + left;
			pVertex += 8;	// Skip 4 vertex XY
		}
		pVertex = &_internalImg.m_pXYCoord[2];
		for (int n=0; n<4; n++) {
			pVertex[2] = pVertex[0] + middle;
			pVertex += 8;	// Skip 4 vertex XY
		}
		pVertex = &_internalImg.m_pXYCoord[4];
		for (int n=0; n<4; n++) {
			pVertex[2] = pVertex[0] + right;
			pVertex += 8;	// Skip 4 vertex XY
		}
	}

	if (mode & CHANGE_Y) {
		float middle = (float)(m_height - (m_top + m_bottom));
		float top = (float)m_top;
		float bottom = m_fBottom;
		if (middle < 0.0f) {
			float border = top + bottom;
			if (border > 0.0f) {
				float scale = (middle + border) / border;
				top *= scale;
				bottom *= scale;
			} else {
				top = 0.0f;
				bottom = 0.0f;
			}
			middle = 0.0f;
		}

		float* pVertex = &_internalImg.m_pXYCoord[1];
		for (int n=0; n<4; n++) {
			pVertex[8] = pVertex[0] + top;
			pVertex += 2;	// Next vertex XY
		}
		pVertex = &_internalImg.m_pXYCoord[9];
		for (int n=0; n<4; n++) {
			pVertex[8] = pVertex[0] + middle;
			pVertex += 2;	// Next vertex XY
		}
		pVertex = &_internalImg.m_pXYCoord[17];
		for (int n=0; n<4; n++) {
			pVertex[8] = pVertex[0] + bottom;
			pVertex += 2;	// Next vertex XY
		}
	}
}

// ------------------------------------------
//   Render State / Command
// ------------------------------------------

CKLBRenderState::CKLBRenderState()
:m_pState			(&internalState)
,pShaderInstance	(NULL)
,m_pRenderTarget	(NULL)
,m_pStateCallback	(NULL)
,m_depthStart		(0.0f)
,m_depthEnd			(1.0f)
{
	m_commandType		= RENDERCOMMAND_CHANGERENDERSTATE;
}

CKLBRenderState::~CKLBRenderState()	{
}

void CKLBRenderState::executeCommand() {
	if (m_commandType & RENDERCOMMAND_CHANGETARGET) {
		CKLBOGLWrapper::getInstance().setRenderFrame(m_pRenderTarget);
	}

	GLbitfield mask = 0;

	if (m_commandType & RENDERCOMMAND_CLEARCOLOR)	{	
		dglClearColor (m_colorClearRed, m_colorClearGreen, m_colorClearBlue, m_colorClearAlpha);
		mask |= GL_COLOR_BUFFER_BIT;	
	}
	if (m_commandType & RENDERCOMMAND_CLEARDEPTH)	{
#ifdef STD_OPENGL
		glClearDepth (m_depthClear);
#else
		dglClearDepthf (m_depthClear);
#endif
		dglDepthMask(GL_TRUE);
		mask |= GL_DEPTH_BUFFER_BIT;
	}
	if (m_commandType & RENDERCOMMAND_CLEARSTENCIL) {
		dglClearStencil (m_stencilClear);
		mask |= GL_STENCIL_BUFFER_BIT;
	}

	if (mask) {
		dglClear (mask);
	}

	if (m_commandType & RENDERCOMMAND_DEPTHRANGE) {
#ifdef STD_OPENGL
		glDepthRange(m_depthStart, m_depthEnd);
#else
		dglDepthRangef(m_depthStart, m_depthEnd);
#endif
	}

	if (m_commandType & RENDERCOMMAND_STATECALLBACK) {
		SRenderState* pState = m_pStateCallback->callback(m_pStateCallback->context);
		if (pState) {
			m_pState = pState;
		}
	}
}

void CKLBRenderState::setRenderTarget(CFrame* pFrame) {
	m_pRenderTarget = pFrame;
	if (pFrame) {
		m_commandType |= RENDERCOMMAND_CHANGETARGET;
	} else {
		m_commandType &= ~RENDERCOMMAND_CHANGETARGET;
	}
}

void CKLBRenderState::setStateCallback(SRenderStateCallback* pCallback) {
	m_pStateCallback = pCallback;
	if (pCallback) {
		m_commandType &= ~RENDERCOMMAND_CHANGERENDERSTATE;
		m_commandType |= RENDERCOMMAND_EXECUTECOMMAND | RENDERCOMMAND_STATECALLBACK;
	} else {
		m_commandType &= ~RENDERCOMMAND_STATECALLBACK;
		m_commandType |= RENDERCOMMAND_CHANGERENDERSTATE;
	}
}

void CKLBRenderState::setDepthRange(float _near, float _far) {
	m_commandType |= RENDERCOMMAND_DEPTHRANGE;
	m_depthStart = _near;
	m_depthEnd	 = _far;
}

void CKLBRenderState::setClearColor(bool active, float r, float g, float b, float alpha) {
	if (active) {
		m_commandType |= RENDERCOMMAND_CLEARCOLOR;
	} else {
		m_commandType &= ~RENDERCOMMAND_CLEARCOLOR;
	}

	m_colorClearRed		= r;
	m_colorClearGreen	= g;
	m_colorClearBlue	= b;
	m_colorClearAlpha	= alpha;
}

void CKLBRenderState::setClearDepth(bool active, float depth) {
	if (active) {
		m_commandType |= RENDERCOMMAND_CLEARDEPTH;
	} else {
		m_commandType &= ~RENDERCOMMAND_CLEARDEPTH;
	}

	m_depthClear		= depth;
}

void CKLBRenderState::setClearStencil(bool active, u32 value) {
	if (active) {
		m_commandType |= RENDERCOMMAND_CLEARSTENCIL;
	} else {
		m_commandType &= ~RENDERCOMMAND_CLEARSTENCIL;
	}

	m_stencilClear = value;
}

void CKLBRenderState::setScissor(bool active, s32 x, s32 y, s32 w, s32 h) {

	internalState.changed		 = true;
	if (active) {
		internalState.bEnableScissor = TRUE_BOOL_U8;
		m_scissor[0] = x;
		m_scissor[1] = y;
		m_scissor[2] = x + w;
		m_scissor[3] = y + h;
	} else {
		internalState.bEnableScissor = FALSE_BOOL_U8;
	}
}

/*virtual*/
void CKLBRenderState::applyNode(CKLBNode* pNode) {
	if (internalState.bEnableScissor == TRUE_BOOL_U8) {
		const SMatrix2D* pMat = &pNode->m_composedMatrix;

		// Apply node transformation to Scissor.
		float tx = pMat->m_matrix[MAT_TX];
		float ty = pMat->m_matrix[MAT_TY];
		float sx = pMat->m_matrix[MAT_A];
		float sy = pMat->m_matrix[MAT_D];

		float* scissorSrc	= &m_scissor[0];
		float* scissorPost	= &m_scissorPost[0]; 

		for (int n=0; n < 2; n++) {
			*scissorPost++ = ((*scissorSrc++) * sx) + tx;
			*scissorPost++ = ((*scissorSrc++) * sy) + ty;
		}

		//
		// Logical Screen Space --> Physical
		//
		CKLBDrawResource& pDRsc = CKLBDrawResource::getInstance();
		float logicalX0 = m_scissorPost[0];
		float logicalY0 = m_scissorPost[1];
		s32 x0 = (s32)pDRsc.toPhisical(logicalX0) + pDRsc.screenBorderX();
		s32 y0 = (s32)pDRsc.toPhisical(logicalY0) + pDRsc.screenBorderY();
		float logicalY1 = m_scissorPost[3];
		s32 x1 = (s32)ceilf(pDRsc.toPhisical(m_scissorPost[2])) + pDRsc.screenBorderX();
		s32 y1 = (s32)ceilf(pDRsc.toPhisical(logicalY1)) + pDRsc.screenBorderY();

		//
		// convert x,y,x,y into x,y,w,h
		//
		m_scissorPost[2] -= logicalX0;
		m_scissorPost[3] = logicalY1 - logicalY0;

		//
		// Float to int for GL (expensive float->int conv)
		//
		// Trick : GL Coordinate system is opposite on Bottom-Left, our system is Top-Left
		s32 h = y1 - y0;
		if (h < 0) { h = 0; }
		s32 w = x1 - x0;
		if (w < 0) { w = 0; }
		internalState.enableScissor	(	x0,
										(pDRsc.phisicalHeight() - y0 - h),
										w,
										h);
	} else {
		internalState.disableScissor();
	}
}

void CKLBRenderState::setUse(bool useRenderState, bool useCommand, CShaderInstance* pShaderInstance) {
	if (useRenderState) {
		m_commandType |= RENDERCOMMAND_CHANGERENDERSTATE;
	} else {
		m_commandType &= ~RENDERCOMMAND_CHANGERENDERSTATE;
	}

	if (useCommand) {
		m_commandType |= RENDERCOMMAND_EXECUTECOMMAND;
	} else {
		m_commandType &= ~RENDERCOMMAND_EXECUTECOMMAND;
	}

	if (pShaderInstance) {
		this->pShaderInstance = pShaderInstance;
		m_commandType |= RENDERCOMMAND_SETSHADER;
	} else {
		this->pShaderInstance = NULL;
		m_commandType &= ~RENDERCOMMAND_SETSHADER;
	}
}

void
CKLBSpriteScale9::beginSizeUpdate()
{
	m_deferRecompute = true;
}

void
CKLBRenderState::getClearColor(float* color)
{
	color[0] = m_colorClearRed;
	color[1] = m_colorClearGreen;
	color[2] = m_colorClearBlue;
	color[3] = m_colorClearAlpha;
}
