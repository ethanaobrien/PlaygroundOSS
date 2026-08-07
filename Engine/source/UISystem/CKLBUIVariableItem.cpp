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
#include "CKLBUIVariableItem.h"

#include "CKLBDataHandler.h"
#include "CKLBUtility.h"
#include "CKLBLuaEnv.h"

// ƒRƒ}ƒ“ƒh’l
enum {
	UI_VARITEM_CHANGE_ASSET = 0,	// 頂点位置を指定
	UI_VARITEM_SET_UV,				// UV値を画像内ピクセル位置とピクセルサイズで指定する
	UI_VARITEM_RESET_UV,			// UV値をデフォルトに戻す
	UI_VARITEM_SET_MASK,			// Assign a Mask to multivar item.
	UI_VARITEM_SETCENTER,
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_VARITEM_CHANGE_ASSET",	UI_VARITEM_CHANGE_ASSET },
	{ "UI_VARITEM_SET_UV",			UI_VARITEM_SET_UV       },
	{ "UI_VARITEM_RESET_UV",		UI_VARITEM_RESET_UV     },
	{ "UI_VARITEM_SET_MASK",		UI_VARITEM_SET_MASK     },
	{ "UI_VARITEM_SETCENTER",		UI_VARITEM_SETCENTER    },
	{ 0, 0}
};
static CKLBTaskFactory<CKLBUIVariableItem> factory("UI_VariableItem", CLS_KLBUIVARIABLEITEM,cmd);

CKLBUIVariableItem::CKLBUIVariableItem()
: CKLBUITask	(P_UIAFTER)
, m_handle		(0)
, m_pTex		(NULL)
, m_asset		(NULL)
, m_pMaskTex	(NULL)
, m_pDynSprite	(NULL)
, m_Maskhandle	(0)
, m_org_height	(0.0f)
, m_org_width	(0.0f)
, m_bSetUV		(false)
, m_useOrigSize	(false)
, m_setCenter	(false)
{
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUIVariableItem::~CKLBUIVariableItem() 
{
}

u32 CKLBUIVariableItem::getClassID() 
{
	return CLS_KLBUIVARIABLEITEM;
}

// 持たせるプロパティキー
CKLBLuaPropTask::PROP_V2 CKLBUIVariableItem::ms_propItems[] = {
	UI_BASE_PROP,
	{	"order",	UINTEGER,	(setBoolT)&CKLBUIVariableItem::setOrder,	(getBoolT)&CKLBUIVariableItem::getOrder,	0 },
	{	"asset",	STRING,		(setBoolT)&CKLBUIVariableItem::changeAsset,	(getBoolT)&CKLBUIVariableItem::getAsset,	0 },
	{	"width",	NUMERIC,	(setBoolT)&CKLBUIVariableItem::setWidth,	(getBoolT)&CKLBUIVariableItem::getWidth,	0 },
	{	"height",	NUMERIC,	(setBoolT)&CKLBUIVariableItem::setHeight,	(getBoolT)&CKLBUIVariableItem::getHeight,	0 }
};

enum PROP_ID{
	PR_ORDER,
	PR_ASSET,
	PR_WIDTH,
	PR_HEIGHT,
};

// 引数のインデックス定義
enum {
	ARG_PARENT = 1,

	ARG_ORDER,
	ARG_X,
	ARG_Y,
	ARG_WIDTH,
	ARG_HEIGHT,
	ARG_ASSET,

	ARG_REQUIRE = ARG_ASSET,
};


CKLBUIVariableItem *
CKLBUIVariableItem::create(CKLBUITask * pParent, CKLBNode * pNode,
                           u32 order, float x, float y, float width, float height,
                           const char * asset)
{
	CKLBUIVariableItem * pTask = KLBNEW(CKLBUIVariableItem);
    if(!pTask) { return NULL; }
	if(!pTask->init(pParent, pNode,
					order, x, y, width, height,
					asset)) {
		KLBDELETE(pTask);
		return NULL;
	}

	return pTask;
}

bool
CKLBUIVariableItem::init(CKLBUITask * pParent, CKLBNode * pNode,
						 u32 order, float x, float y, float width, float height,
						 const char * asset)
{
    if(!setupNode()) { return false; }

	// ユーザ定義初期化を呼び、初期化に失敗したら終了。
	bool bResult = initCore(order, x, y, width, height, asset);

	// 初期化処理終了後の登録。失敗時の処理も適切に行う。
	bResult = registUI(pParent, bResult);
	if(pNode) {
		pParent->getNode()->removeNode(getNode());
		pNode->addNode(getNode());
	}

	return bResult;
}

bool
CKLBUIVariableItem::initCore(u32 order, float x, float y, float width, float height, const char * asset)
{
	// 対応するプロパティリストを規定する。
	if(!setupPropertyList((const char**)ms_propItems,SizeOfArray(ms_propItems))) {
		return false;
	}

	// 読み込んだ頂点データと、座標値、orderで表示物を用意する。
	CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();

	// 基本プロパティの取り込み
	setInitPos(x, y);

	klb_assert((((s32)order) >= 0), "Order Problem");

	m_order     = order;
	m_width     = width;
	m_height    = height;

	m_pDynSprite = pRdrMgr.allocateCommandDynSprite(8, 12);

	if(!m_pDynSprite) {
		return false;
	}
	
	m_pNode = KLBNEW(CKLBNode);

	// 初期assetを設定する
	setAsset(asset);

	m_pNode->setRender(m_pDynSprite);

	m_pNode->setRenderOnDestroy(true);
	getNode()->addNode(m_pNode);

	m_pNode->setTranslate(0, 0);
	m_pNode->setPriority(order);

	return true;
}

bool
CKLBUIVariableItem::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < ARG_REQUIRE) return false;

	// 基本プロパティの取り込み
	u32 order       = lua.getInt(ARG_ORDER);
	float x         = lua.getFloat(ARG_X);
	float y         = lua.getFloat(ARG_Y);
	float width     = lua.getFloat(ARG_WIDTH);
	float height    = lua.getFloat(ARG_HEIGHT);
	const char * asset = (lua.isNil(ARG_ASSET)) ? NULL : lua.getString(ARG_ASSET);

	return initCore(order, x, y, width, height, asset);
}

bool
CKLBUIVariableItem::setAsset(const char * asset)
{
	u32 handle = 0;
	CKLBImageAsset * pTex = NULL;
	if(asset) {
		pTex = (CKLBImageAsset *)CKLBUtility::loadAssetScript(asset, &handle);
	}
	getUV(pTex);
	getSize(pTex);
	if(m_useOrigSize) {
		m_width = m_org_width;
		m_height = m_org_height;
		m_useOrigSize = false;
	}
	bool ret;

	if(!pTex) {
		m_pDynSprite->setVICount(4,6); // Default no display.
		u16* pIdxBuff = m_pDynSprite->getSrcIndexBuffer();

		pIdxBuff[0] = 0;
		pIdxBuff[1] = 1;
		pIdxBuff[2] = 3;
		pIdxBuff[3] = 1;
		pIdxBuff[4] = 2;
		pIdxBuff[5] = 3;
		m_pDynSprite->setTexture((CTextureUsage*)NULL);

		ret = false;
	} else {
		m_pDynSprite->setVICount(pTex->getVertexCount(),pTex->getIndexCount());
		u16* pIdxBuff = m_pDynSprite->getSrcIndexBuffer();
		u16* pSrcIdx  = pTex->getIndexBuffer();
		for(u32 n = 0; n < pTex->getIndexCount(); n++) {
			pIdxBuff[n] = pSrcIdx[n];
		}

		m_pDynSprite->setTexture(pTex);

		m_pNode->markUpMatrixAndColor();
		// 以前にロードしたassetは破棄する
		if(m_pTex && m_handle) {
			CKLBDataHandler::releaseHandle(m_handle);
			KLBDELETEA(m_asset);
		}
		// 今回ロードしたassetを保持する
		m_pTex   = pTex;
		m_handle = handle;
		m_asset  = CKLBUtility::copyString(asset);
		setStrC(m_asset, (asset) ? asset : "");

		SKLBRect* pRect = pTex->getSize();
		m_org_width = pRect->getWidth();
		m_org_height = pRect->getHeight();
		ret = true;
	}

	// 指定サイズから頂点データを生成する
	createArrVert(m_width, m_height);
	m_pDynSprite->mark(CKLBDynSprite::MARK_CHANGE_UV | CKLBDynSprite::MARK_CHANGE_XY | FLAG_BUFFERSHIFT);

	return ret;
}

void
CKLBUIVariableItem::notifyAssetUpdate(const char* assetName, CKLBAsset* /*asset*/)
{
	// The item owns the asset it displays, so a reload is handled by resolving
	// the updated source through the asset path again and rebuilding the sprite.
	const char* source = m_pTex ? m_pTex->getFileSource() : NULL;
	if(source && !strcmp(source, assetName)) {
		char assetPath[1000];
		sprintf(assetPath, "asset://%s", assetName);
		m_bSetUV = false;
		setAsset(assetPath);
		m_pNode->markUpMatrix();
	}
}

bool
CKLBUIVariableItem::setMaskAsset(const char* asset) {
	// 以前にロードしたassetは破棄する
	if(m_pMaskTex && m_Maskhandle) {
		CKLBDataHandler::releaseHandle(m_Maskhandle);
	}

	u32 handle = 0;
	CKLBImageAsset * pTex = NULL;
	if(asset) {
		pTex = (CKLBImageAsset *)CKLBUtility::loadAssetScript(asset, &handle);
		if(!pTex) {
			return false;
		}
	} else {
		pTex = NULL;
	}

	if(m_pDynSprite) {
		m_pDynSprite->setMask(pTex);
	}

	// 今回ロードしたassetを保持する
	m_pMaskTex   = pTex;
	m_Maskhandle = handle;

	return true;
}

void
CKLBUIVariableItem::getUV(CKLBImageAsset * pTex)
{
	// klb_assert(pTex->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT), "Must use a standard rectangular image in VariableItem task.");
	if(pTex) {
		float* pBuffUV = pTex->getUVBuffer();
		for(u32 i = 0; i < pTex->getVertexCount(); i++) {
			m_pDynSprite->setVertexUV(i, pBuffUV[i*2], pBuffUV[i*2+1]);
		}
	} else {
		m_pDynSprite->setVertexUV(0, 0.0f, 0.0f);
		m_pDynSprite->setVertexUV(1, 1.0f, 0.0f);
		m_pDynSprite->setVertexUV(0, 1.0f, 1.0f);
		m_pDynSprite->setVertexUV(0, 0.0f, 1.0f);
	}
}

void
CKLBUIVariableItem::getSize(CKLBImageAsset * pTex)
{
	// klb_assert(pTex->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT), "Must use a standard rectangular image in VariableItem task.");
	if(pTex) {
		SKLBRect * pRect = pTex->getSize();
		m_org_width = pRect->getWidth();
		m_org_height = pRect->getHeight();
	} else {
		m_org_width = 1.0f;
		m_org_height = 1.0f;
	}
}

#include "CKLBDrawTask.h"

void
CKLBUIVariableItem::createArrVert(float width, float height)
{
	bool originalSize = (width == -1 && height == -1);
	
	float ratioX;
	float ratioY;
	if(originalSize) {
		ratioX = 1.0f;
		ratioY = 1.0f;
	} else {
		ratioX = width / m_org_width;
		ratioY = height/ m_org_height;
	}

	if(!m_pTex) {
		m_pDynSprite->setVertexXY(0, 0.0f,  0.0f);
		m_pDynSprite->setVertexXY(1, width, 0.0f);
		m_pDynSprite->setVertexXY(2, width, height);
		m_pDynSprite->setVertexXY(3, 0.0f,  height);
		m_pDynSprite->setVertexColor(m_pNode,0,0xffffffff);
		m_pDynSprite->setVertexColor(m_pNode,1,0xffffffff);
		m_pDynSprite->setVertexColor(m_pNode,2,0xffffffff);
		m_pDynSprite->setVertexColor(m_pNode,3,0xffffffff);
		return;
	}

	float centerX = m_setCenter ? width  * 0.5f : 0.0f;
	float centerY = m_setCenter ? height * 0.5f : 0.0f;

	float* vertex = m_pTex->getXYBuffer();
	float  cx	  = m_pTex->m_iCenterX;
	float  cy	  = m_pTex->m_iCenterY;

	// Keep original vertex information alive.
	for(u32 n = 0; n < m_pTex->getVertexCount(); n++) {
		float x = (*vertex++ + cx) * ratioX - centerX;
		float y = (*vertex++ + cy) * ratioY - centerY;
		m_pDynSprite->setVertexXY(n, x , y);
		m_pDynSprite->setVertexColor(m_pNode,n,0xffffffff);
	}
}

// 画像内におけるピクセル単位の位置とサイズの指定でUV値を設定する。
void
CKLBUIVariableItem::changeUV(float x, float y, float width, float height)
{
	m_bSetUV       = true;
	m_setUV_x      = x;
	m_setUV_y      = y;
	m_setUV_width  = width;
	m_setUV_height = height;

	// それぞれを「ピクセル単位による画像内の位置およびサイズ」として解釈する。
	if(m_pTex) {
		klb_assertNull(m_pTex->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT), "Must use a standard rectangular image in VariableItem task.");

		float* arrOriginalUV = m_pTex->getUVBuffer();
		SKLBRect* imageSize = m_pTex->getSize();
		s32 imageWidth = imageSize->getWidth();
		s32 imageHeight = imageSize->getHeight();

		if(imageWidth && imageHeight) {
			float imageWidthF = imageWidth;
			float imageHeightF = imageHeight;
			float rightEdgeU = arrOriginalUV[2];
			float baseU = arrOriginalUV[0];
			float baseV = arrOriginalUV[1];
			float widthU  = (rightEdgeU - baseU) / imageWidthF;
			float widthV  = (arrOriginalUV[3] - baseV) / imageWidthF;
			float heightU = (arrOriginalUV[6] - baseU) / imageHeightF;
			float heightV = (arrOriginalUV[7] - baseV) / imageHeightF;

			float startU = baseU + x * widthU + y * heightU;
			float startV = baseV + x * widthV + y * heightV;

			m_pDynSprite->setVertexUV(0, startU, startV);

			float rightU = startU + width * widthU;
			float rightV = startV + width * widthV;
			float bottomOffsetU = height * heightU;
			float bottomOffsetV = height * heightV;
			float bottomU = startU + bottomOffsetU;
			float bottomV = startV + bottomOffsetV;
			m_pDynSprite->setVertexUV(1, rightU,                 rightV);
			m_pDynSprite->setVertexUV(2, rightU + bottomOffsetU, rightV + bottomOffsetV);
			m_pDynSprite->setVertexUV(3, bottomU,                bottomV);
		} else {
			m_pDynSprite->setVertexUV(0, arrOriginalUV[0], arrOriginalUV[1]);
			m_pDynSprite->setVertexUV(1, arrOriginalUV[2], arrOriginalUV[3]);
			m_pDynSprite->setVertexUV(2, arrOriginalUV[4], arrOriginalUV[5]);
			m_pDynSprite->setVertexUV(3, arrOriginalUV[6], arrOriginalUV[7]);
		}
	} else {
		// fill white texture in openGL
		m_pDynSprite->setVertexUV(0, 0.0f,0.0f);
		m_pDynSprite->setVertexUV(1, 0.0f,1.0f);
		m_pDynSprite->setVertexUV(2, 1.0f,1.0f);
		m_pDynSprite->setVertexUV(3, 1.0f,0.0f);
	}

	m_pNode->markUpMatrix();
	m_pDynSprite->mark(CKLBDynSprite::MARK_CHANGE_UV | CKLBDynSprite::MARK_CHANGE_XY | FLAG_BUFFERSHIFT);
}

void
CKLBUIVariableItem::dieUI()
{
	KLBDELETEA(m_asset);
	CKLBUtility::deleteNode(m_pNode, m_handle);

	if(m_pMaskTex && m_Maskhandle) {
		CKLBDataHandler::releaseHandle(m_Maskhandle);
	}
}

void
CKLBUIVariableItem::execute(u32 /* deltaT */)
{
	createArrVert(m_width, m_height);
	m_pNode->markUpMatrix();
	m_pDynSprite->mark(CKLBDynSprite::MARK_CHANGE_UV | CKLBDynSprite::MARK_CHANGE_XY | FLAG_BUFFERSHIFT);
	RESET_A;
}

int
CKLBUIVariableItem::commandUI(CLuaState& lua, int argc, int cmd)
{
	int ret = 1;

	switch(cmd)
	{
	default:
		{
			lua.retBoolean(false);
			ret = 1;
		}
		break;
	case UI_VARITEM_CHANGE_ASSET:
		{
			bool bResult = false;
			if(argc <= 4) {
				const char * asset = (lua.isNil(3)) ? NULL : lua.getString(3);
				m_useOrigSize = (argc == 4 && !lua.isNil(4)) ? lua.getBool(4) : false;
				bResult = changeAsset(asset);
			}
			lua.retBoolean(bResult);
			ret = 1;
		}
		break;
	case UI_VARITEM_SET_UV:
		{
			bool bResult = false;
			if(argc == 6) {
				float x      = lua.getFloat(3);
				float y      = lua.getFloat(4);
				float width  = lua.getFloat(5);
				float height = lua.getFloat(6);

				changeUV(x, y, width, height);

				bResult = true;
			}
			lua.retBool(bResult);
			ret = 1;
		}
		break;
	case UI_VARITEM_RESET_UV:
		{
			bool bResult = false;
			if(argc == 2) {
				getUV(this->m_pTex);
				m_pNode->markUpMatrix();
				m_pDynSprite->mark(CKLBDynSprite::MARK_CHANGE_UV | CKLBDynSprite::MARK_CHANGE_XY | FLAG_BUFFERSHIFT);
				bResult = true;
			}
			lua.retBool(bResult);
			ret = 1;
		}
		break;
	case UI_VARITEM_SET_MASK:
		{
			bool bResult = false;
			if(argc == 3) {
				const char * asset = (lua.isNil(3)) ? 0 : lua.getString(3);
				bResult = setMaskAsset(asset);
			}
			lua.retBool(bResult);
			ret = 1;
		}
		break;
	case UI_VARITEM_SETCENTER:
		{
			int center = lua.getInt(3);
			switch(center) {
			case 0:
				m_setCenter = false;
				break;
			case 1:
				m_setCenter = true;
				break;
			}
			REFRESH_A;
		}
	}
	return ret;
}
