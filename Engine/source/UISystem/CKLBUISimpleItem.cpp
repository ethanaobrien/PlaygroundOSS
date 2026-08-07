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
#include "CKLBUISimpleItem.h"
#include "CKLBUtility.h"
#include "CKLBRendering.h"

// Command Values
enum {
	UI_SIMPLE_ = 0,
	UI_SIMPLEITEM_SET_MASK,
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_SIMPLE_", UI_SIMPLE_ },
	{ "UI_SIMPLEITEM_SET_MASK", UI_SIMPLEITEM_SET_MASK },
	{0, 0}
};
static CKLBTaskFactory<CKLBUISimpleItem> factory("UI_SimpleItem", CLS_KLBUISIMPLEITEM, cmd);

// Allowed Property Keys
CKLBLuaPropTask::PROP_V2 CKLBUISimpleItem::ms_propItems[] = {
	UI_BASE_PROP,
	{	"order",	UINTEGER,	(setBoolT)&CKLBUISimpleItem::setOrder,	(getBoolT)&CKLBUISimpleItem::getOrder,	0 },
	{	"asset",	R_STRING,	NULL,									(getBoolT)&CKLBUISimpleItem::getAsset,	0 }
};

enum {
	ARG_PARENT = 1,

	ARG_ORDER,
	ARG_X,
	ARG_Y,

	ARG_ASSET,

	ARG_REQUIRE = ARG_ASSET,
	ARG_NUMS = ARG_ASSET
};

CKLBUISimpleItem::CKLBUISimpleItem() 
: CKLBUITask(P_UIAFTER)
, m_pNode   (NULL)
, m_asset   (NULL) 
, m_maskHandle(0)
, m_pMaskTex(NULL)
{
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUISimpleItem::~CKLBUISimpleItem() 
{
	KLBDELETEA(m_asset);
}

u32 
CKLBUISimpleItem::getClassID() 
{ 
    return CLS_KLBUISIMPLEITEM; 
}

CKLBUISimpleItem* 
CKLBUISimpleItem::create(CKLBUITask* pParent, CKLBNode* pNode, u32 order, float x, float y, const char* asset) 
{
	CKLBUISimpleItem* pTask = KLBNEW(CKLBUISimpleItem);
    if(!pTask) { return NULL; }
	if(!pTask->init(pParent, pNode, order,x,y,asset)) {
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBUISimpleItem::init(CKLBUITask* pParent, CKLBNode* pNode, u32 order, float x, float y, const char* asset) 
{
    if(!setupNode()) { return false; }
	bool bResult = initCore(order,x,y,asset);
	bResult = registUI(pParent, bResult);
	if(pNode) {
		pParent->getNode()->removeNode(getNode());
		pNode->addNode(getNode());
	}
	return bResult;
}

bool
CKLBUISimpleItem::initCore(u32 order, float x, float y, const char* asset)
{
	if(!setupPropertyList((const char**)ms_propItems,SizeOfArray(ms_propItems))) {
		return false;
	}

	setInitPos(x, y);

	klb_assert((((s32)order) >= 0), "Order Problem");

	m_order = order;

	setStrC(m_asset, asset);

	m_pNode = CKLBUtility::createNodeScript( m_asset, m_order, &m_handle);
	if(!m_pNode) {
		return false;
	}
	getNode()->addNode(m_pNode);

	return true;
}

bool
CKLBUISimpleItem::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
    if(argc < ARG_REQUIRE || argc > ARG_NUMS) { return false; }

	float x = lua.getFloat(ARG_X);
	float y = lua.getFloat(ARG_Y);

	return initCore(lua.getInt(ARG_ORDER),x,y,lua.getString(ARG_ASSET));
} 

void
CKLBUISimpleItem::execute(u32 /*deltaT*/)
{
	klb_assertAlways("Should never be executed");
}

void
CKLBUISimpleItem::dieUI()
{
	CKLBUtility::deleteNode(m_pNode, m_handle);
	if(m_pMaskTex && m_maskHandle) {
		CKLBDataHandler::releaseHandle(m_maskHandle);
	}
}

bool
CKLBUISimpleItem::setMaskAsset(const char* asset)
{
	if(m_pMaskTex && m_maskHandle) {
		CKLBDataHandler::releaseHandle(m_maskHandle);
	}

	u32 handle = 0;
	CKLBImageAsset* pTex = NULL;
	if(asset) {
		pTex = (CKLBImageAsset*)CKLBUtility::loadAssetScript(asset, &handle);
		if(!pTex) {
			return false;
		}
	}

	CKLBSprite* pSprite = (CKLBSprite*)m_pNode->getRender(0);
	if(pSprite) {
		pSprite->setMask(pTex);
	}

	m_pMaskTex = pTex;
	m_maskHandle = handle;
	return true;
}

int
CKLBUISimpleItem::commandUI(CLuaState& lua, int argc, int cmd)
{
	if(cmd == UI_SIMPLEITEM_SET_MASK) {
		bool result = false;
		if(argc == 3) {
			const char* asset = lua.isNil(3) ? NULL : lua.getString(3);
			result = setMaskAsset(asset);
		}
		lua.retBool(result);
		return 1;
	}

	lua.retBool(false);
	return 1;
}
