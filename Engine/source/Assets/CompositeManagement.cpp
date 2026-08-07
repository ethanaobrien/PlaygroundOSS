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
#include <stdlib.h>
#include <string.h>
#include "CompositeManagement.h"
#include "mem.h"
#include "CKLBUISystem.h"
#include "CKLBNodeVirtualDocument.h"
#include "CKLBTextInputNode.h"
#include "CKLBLabelNode.h"
#include "CKLBSplineNode.h"
#include "AudioAsset.h"
#include "CKLBUITextInput.h"
#include "CKLBUIVariableItem.h"
#include "CKLBPropertyBag.h"
#include "CKLBSplineNode.h"
#include "CKLBUIGroup.h"
#include "CKLBLuaLibSOUND.h"
#include "CKLBUILabel.h"
#include "CKLBUIScale9Btn.h"
#include "CKLBDrawTask.h"
#include "CKLBUIShader.h"
#include "CKLBRenderBufferTask.h"

// === Scroll Bar Parameters
#define VAR_MIN						(0)
#define VAR_MAX						(1)
#define VAR_SLIDERSIZE				(2)
#define VAR_MIN_SLIDERSIZE			(3)
#define VAR_SELECTCOLOR				(4)

#define DB_VAR_MIN					(0)
#define DB_VAR_MAX					(1)

// Progress bar / ScrollBar
#define DB_VAL_VALUE				(2)
// =========================================

// === Canvas Parameters
#define VAR_MAXVERTEX				(0)
#define VAR_MAXINDEX				(1)
// =========================================

#define DB_VAR_STARTANGLE			(0)
#define DB_VAR_ENDANGLE				(1)

#define DB_VAR_ENABLE				(0)
#define DB_VAR_CHECKED				(1)
#define DB_VAL_COLOR				(3)

static const u32 LAYOUT_SIZE_MODES[] = { 1, 1, 2, 2 };

struct SCompositeLayoutRecord {
	float x;
	float y;
	float width;
	float height;
	bool isRect;

	void clearCoordinates();
};

void
SCompositeLayoutRecord::clearCoordinates()
{
	x = y = width = height = 0.0f;
}

SCompositeLayoutRecord g_layoutRecords[200];
static SCompositeLayoutRecord s_designRects[20];
static const char* s_designRectNames[20];
static CKLBNode* s_designRectNodes[20];
s32 g_layoutRecordCount = 0;

/*
{
	x
	y
	sx
	sy
	lw
	lh
	layoutInfo
	classID
	sh
	sw
	priority
	
	"class"
	"on*"	
	"default"/"select"/"disable"/"focus"
	
	"sub" : [
	]
}*/

enum {
	NORMAL_ASSET		= 0,
	DISABLE_ASSET		= 1,
	FOCUS_ASSET			= 2,
	PUSH_ASSET			= 3,
	ASSET4				= 4,
	ASSET5				= 5,
	ASSET6				= 6,
	ASSET7				= 7,
	ASSET8				= 8,
	ASSET9				= 9,
	DOT_ASSET			= 10,
	____END_ASSET		= 11,

	DOWNAUDIO_ASSET		= 8,
	UPAUDIO_ASSET		= 9,
};

enum {
	NONE_CLASSID		= 0,
	BUTTON_CLASSID		= 1,
	CHECK_CLASSID		= 2,
	CONTAINER_CLASSID	= 3,
	LABEL_CLASSID		= 4,
	TEXTBOX_CLASSID		= 5,
	ANIMNODE_CLASSID	= 6,
	WEBVIEW_CLASSID		= 7,
	VIRTUALDOC_CLASSID	= 8,
	SCORE_CLASSID		= 9,
	PROGRESSBAR_CLASSID	= 10,
	DRAGICON_CLASSID	= 11,
	LIST_CLASSID		= 12,
	PIECHART_CLASSID	= 13,
	SCROLLBAR_CLASSID	= 14,
	CANVAS_CLASSID		= 15,
	SCALE9_CLASSID		= 16,
	VARITEM_CLASSID		= 17,
	GROUP_CLASSID		= 18,
	TILEDCANVAS_CLASSID	= 19,
	FORMAT_RECT_CLASSID	= 20,
	BUTTON_SCALE9_CLASSID	= 21,
	SHADER_CLASSID		= 22,
	BUFFER_CLASSID		= 23,
};

enum {
	DOCK_NONE	= 0,
	DOCK_LEFT	= 1,
	DOCK_RIGHT	= 2,
	DOCK_TOP	= 3,
	DOCK_BOTTOM	= 4,
	DOCK_FILL	= 5,
};

#include "../../libs/JSonParser/api/yajl_parse.h"
#include "../../libs/JSonParser/api/yajl_gen.h"

CKLBAnimInfo::CKLBAnimInfo()
:mode		(NULL)
,modeID		(0)
,m_next		(NULL)
,data		(NULL)
,spline		(1)
,loop		(0)
,pingpong	(0)
,fromX		(0)
,toX		(0)
,fromY		(0)
,toY		(0)

,fromScaleX	(1.0f)
,toScaleX	(1.0f)
,fromScaleY	(1.0f)
,toScaleY	(1.0f)
,fromRotation	(0.0f)
,toRotation		(0.0f)

,fromAlpha	(255)
,toAlpha	(255)

,fromR		(255)
,toR		(255)

,fromG		(255)
,toG		(255)

,fromB		(255)
,toB		(255)
{
}

CKLBAnimInfo::~CKLBAnimInfo() {
	KLBDELETEA(data);
}

#include "ArrayAllocator.h"

ArrayAllocator<CKLBInnerDef> gInnerDefAlloc;

/*static*/ bool CKLBInnerDefManager::initManager(int size) {
	return gInnerDefAlloc.init(size);
}

/*static*/ void CKLBInnerDefManager::releaseManager() {
	gInnerDefAlloc.release();
}

/*static*/ void* CKLBInnerDef::operator new		(size_t /*size*/)
{
	// Ignore size, we are fixed size type here.
	return gInnerDefAlloc.allocEntry();
}

/*static*/ void CKLBInnerDef::operator delete	(void *p)
{
	gInnerDefAlloc.freeEntry((CKLBInnerDef*)p);
}

CKLBInnerDef::CKLBInnerDef()
:next		(NULL)
,sub		(NULL)
,classID	(NONE_CLASSID)
,radioID	(-1)
,name		(0)
,text		(NULL)
,placeholder(NULL)
,fontName	(NULL)
,color		(0xFFFFFFFF)
,fontSize	(15)
,clipx		(0)
,clipy		(0)
,clipw		(0)
,cliph		(0)
,id			(0)
,anim		(NULL)
,xscale		(1.0f)
,yscale		(1.0f)
,rotation	(0.0f)
,shadowBlur	(0.0f)
,layoutInfo	(0)
,layoutDir	(0)
,value		(0)
,priority	(0)
,spline		(NULL)
,propertyBag(NULL)
,visible	(true)
,volAudioDown	(100)
,volAudioUp		(100)
,shadowDX	(0)
,shadowDY	(0)
,anchor		(0)
{
	for (u32 n = 0; n < MAX_HANDLER; n++) {
		handler[n] = NULL;
	}

	for (u32 n = 0; n < ____END_ASSET ; n++) {
		assets[n] = NULL;
	}

	flag[0] = 0;
	flag[1] = 0;
	flag[2] = 1;
	flag[3] = 1;

	position[LAYOUT_X] = 0.0f;
	position[LAYOUT_Y] = 0.0f;
	width = 0.0f;
	height = 0.0f;
	
	dbField[0] = NULL;
	dbField[1] = NULL;
	dbField[2] = NULL;
	dbField[3] = NULL;
}

CKLBInnerDef::~CKLBInnerDef() {
	//
	// Free assets.
	//
	/* All Entries are stored in the main CKLBComposite "string" list : Free occurs there.
	for (int n=0; n < ____END_ASSET; n++) {
		if (assets[n]) {
			if (assets[n]->assetCache) {
				assets[n]->assetCache->decrementRefCount();
			}
		}
	} */
	
	// Spline info.
	KLBDELETEA(spline);

	// Free all linked definition.
	CKLBAnimInfo* pInfo = this->anim;
	while (pInfo) {
		CKLBAnimInfo* pN = pInfo->m_next;
		KLBDELETE(pInfo);
		pInfo = pN;
	}

	// Free all sub definitions.
	CKLBInnerDef* p = this->sub;
	while (p) {
		CKLBInnerDef* pN = p->next;
		KLBDELETE(p);
		p = pN;
	}
}

// ------------------- Plugin / Loader ------------------

/*virtual*/
CKLBAbstractAsset*	CKLBCompositeAssetPlugin::loadAsset(u8* stream, size_t streamSize) {
	static yajl_callbacks callbacks = {  
		CKLBCompositeAsset::read_null,  
		CKLBCompositeAsset::read_boolean,  
		CKLBCompositeAsset::read_int,  
		CKLBCompositeAsset::read_double,  
		null,  
		CKLBCompositeAsset::read_string,  
		CKLBCompositeAsset::read_start_map,  
		CKLBCompositeAsset::read_map_key,  
		CKLBCompositeAsset::read_end_map,  
		CKLBCompositeAsset::read_start_array,  
		CKLBCompositeAsset::read_end_array
	};

    yajl_handle hand;
    /* generator config */  
//    yajl_gen g;				// My Context. 2012.12.05  
    yajl_status stat;  
  
//    g = yajl_gen_alloc(NULL);	// 2012.12.05  

	CKLBCompositeAsset* pNewAsset = KLBNEW(CKLBCompositeAsset);

	if (pNewAsset && pNewAsset->init()) {
		/* ok.  open file.  let's read and parse */  
		hand = yajl_alloc(&callbacks, NULL, pNewAsset);  
		if (hand) {
			/* and let's allow comments by default */  
			yajl_config(hand, yajl_allow_comments, 1);

			pNewAsset->parserCtx = hand;

			stat = yajl_parse(hand, stream, streamSize);
	
			if (stat == yajl_status_ok) {
				stat = yajl_complete_parse(hand);
				if (stat == yajl_status_ok) {
					yajl_free(hand);

					// Force implementation mistake to make NULL ptr error. This variable should NOT be used.
					pNewAsset->m_pCurrInnerDef = NULL;

					return pNewAsset;
				}
			}
			yajl_free(hand);
		}
	}
	KLBDELETE(pNewAsset);

	return null;
}

CKLBCompositeAssetPlugin::CKLBCompositeAssetPlugin()
: IKLBAssetPlugin()
{
	// Do nothing.
}

CKLBCompositeAssetPlugin::~CKLBCompositeAssetPlugin() {
	// Do nothing.
}

// ------------------- Asset ------------------

/*static*/ int CKLBCompositeAsset::read_start_map	(void * ctx, unsigned int size)
{ return ((CKLBCompositeAsset*)ctx)->readStartMap(size); }
/*static*/ int CKLBCompositeAsset::read_null		(void * ctx)
{ return ((CKLBCompositeAsset*)ctx)->readNull(); }
/*static*/ int CKLBCompositeAsset::read_boolean		(void * ctx, int boolean)
{ return ((CKLBCompositeAsset*)ctx)->readBoolean(boolean); }
/*static*/ int CKLBCompositeAsset::read_int			(void * ctx, long long integerVal)
{ return ((CKLBCompositeAsset*)ctx)->readInt(integerVal); }
/*static*/ int CKLBCompositeAsset::read_double		(void * ctx, double doubleVal)
{ return ((CKLBCompositeAsset*)ctx)->readDouble(doubleVal); }
/*static*/ int CKLBCompositeAsset::read_string		(void * ctx, const unsigned char * stringVal, size_t stringLen, int cte_pool)
{ return ((CKLBCompositeAsset*)ctx)->readString(stringVal, stringLen, cte_pool); }
/*static*/ int CKLBCompositeAsset::read_map_key		(void * ctx, const unsigned char * stringVal, size_t stringLen, int cte_pool)
{ return ((CKLBCompositeAsset*)ctx)->readMapKey(stringVal, stringLen, cte_pool); }
/*static*/ int CKLBCompositeAsset::read_end_map		(void * ctx)
{ return ((CKLBCompositeAsset*)ctx)->readEndMap(); }
/*static*/ int CKLBCompositeAsset::read_start_array	(void * ctx, unsigned int size)
{ return ((CKLBCompositeAsset*)ctx)->readStartArray(size); }
/*static*/ int CKLBCompositeAsset::read_end_array	(void * ctx)
{ return ((CKLBCompositeAsset*)ctx)->readEndArray(); }

#define STANDARD_PARSER_MODE	(0)
#define GENERIC_MODE			(1)

static float s_deviceScale;

float CKLBInnerDef::getLayoutValue(LAYOUT_VALUE value, float parentSize) const {
	u32 mode = (layoutInfo >> (value * 2)) & 3;
	float result;
	switch (value) {
	case LAYOUT_X:
	case LAYOUT_Y:
		result = position[value];
		break;
	case LAYOUT_WIDTH:
		result = (classID == LIST_CLASSID) ? clipw : width;
		break;
	case LAYOUT_HEIGHT:
		result = (classID == LIST_CLASSID) ? cliph : height;
		break;
	case LAYOUT_CLIP_X:
		result = clipx;
		break;
	case LAYOUT_CLIP_Y:
		result = clipy;
		break;
	case LAYOUT_CLIP_WIDTH:
		result = clipw;
		break;
	case LAYOUT_CLIP_HEIGHT:
		result = cliph;
		break;
	}

	switch (mode) {
	case 0:
		return result;
	case 1:
		return result * s_deviceScale;
	case 2:
		return (result * parentSize) / 100.0f;
	default:
		return 0.0f;
	}
}

float CKLBInnerDef::getLayoutValue(LAYOUT_VALUE value) const {
	u32 mode = (layoutInfo >> (value * 2)) & 3;
	float result;
	switch (value) {
	case LAYOUT_X:
	case LAYOUT_Y:
		result = position[value];
		break;
	case LAYOUT_WIDTH:
		result = (classID == LIST_CLASSID) ? clipw : width;
		break;
	case LAYOUT_HEIGHT:
		result = (classID == LIST_CLASSID) ? cliph : height;
		break;
	case LAYOUT_CLIP_X:
		result = clipx;
		break;
	case LAYOUT_CLIP_Y:
		result = clipy;
		break;
	case LAYOUT_CLIP_WIDTH:
		result = clipw;
		break;
	case LAYOUT_CLIP_HEIGHT:
		result = cliph;
		break;
	}

	switch (mode) {
	case 0:
		return result;
	case 1:
		return result * s_deviceScale;
	case 2:
		klb_assertAlways("Should never enter here");
	default:
		return 0.0f;
	}
}

CKLBCompositeAsset::CKLBCompositeAsset()
:parserCtx			(NULL)
,m_allocatedString	(NULL)
,m_groupID			(0)
,m_parserField		(0)
,m_currArraySpline	(NULL)
,m_pSource			(NULL)
,m_pCurrInnerDef	(NULL)
,m_pCurrAnim		(NULL)
,m_pParent			(NULL)
,m_root				(NULL)
,m_rootParent		(NULL)
,m_rectXYWH			(NULL)
,m_rectName			(NULL)
,m_recordID			((u64)-1)
,m_designRectCount	(0)
,m_basePriority		(0)
,m_layoutScale		(1.0f)
,m_width			(-1)
,m_height			(-1)
,m_formWidth			(1)
,m_formHeight		(1)
,m_parent			(0)
,m_recCount			(0)
,mode				(STANDARD_PARSER_MODE)
,m_bTreeMode		(true)
,m_bDirectComposite	(false)
{
	m_parentStack[0] = NULL;
	m_bLowRes = CPFInterface::getInstance().client().getPhysicalScreenHeight() < 480;
	s_deviceScale = CPFInterface::getInstance().platform().getDeviceScale();
}

CKLBCompositeAsset::~CKLBCompositeAsset() {
	if (m_allocatedString) {
		STRINGENTRY* parse = m_allocatedString;
		while (parse) {
			STRINGENTRY* parseNext = parse->next;

			// String is freed by EACH user. We just delete the container list.
			KLBDELETE(parse);

			parse = parseNext;
		}
		m_allocatedString = NULL;
	}

	if (m_rootParent) {
		KLBDELETE(m_rootParent);
	}

	if (m_root) {
		KLBDELETE(m_root);
	}

	// Do not need to free or check.
	// STRINGENTRY*	handler	[MAX_HANDLER];
	// STRINGENTRY*	assets	[MAX_ASSETS];
}

void CKLBCompositeAsset::replaceAsset(const char* name, CKLBAbstractAsset* asset) {
	STRINGENTRY* entry = m_allocatedString;
	while (entry) {
		CKLBAsset* cachedAsset = entry->assetCache;
		if (cachedAsset && cachedAsset->getFileSource() && strcmp(cachedAsset->getFileSource(), name) == 0) {
			if (entry->assetCache->getAssetType() != ASSET_IMAGE) {
				entry->assetCache->decrementRefCount();
			} else {
				((CKLBImageAsset*)entry->assetCache)->getTexture()->decrementRefCount();
			}

			entry->assetCache = (CKLBAsset*)asset;
			if (entry->assetCache) {
				if (entry->assetCache->getAssetType() != ASSET_IMAGE) {
					entry->assetCache->incrementRefCount();
				} else {
					((CKLBImageAsset*)entry->assetCache)->getTexture()->incrementRefCount();
				}
			}
		}
		entry = entry->next;
	}
}

bool CKLBCompositeAsset::init() {
	m_rootParent = KLBNEW(CKLBNode);
	return (m_rootParent != NULL);
}

CKLBCompositeAsset::STRINGENTRY::STRINGENTRY()
:next		(NULL)
,string		(NULL)
,assetCache	(NULL) 
{
}

CKLBCompositeAsset::STRINGENTRY::~STRINGENTRY() {
	if (string)	{ KLBFREE(string); string = NULL; }

	if (assetCache) {
		if (assetCache->getAssetType() != ASSET_IMAGE) {
			assetCache->decrementRefCount();
		} else {
			((CKLBImageAsset*)assetCache)->getTexture()->decrementRefCount();
		}
	}
}

void CKLBCompositeAsset::STRINGENTRY::incrementRefCount() {
	if (assetCache) {
		if (assetCache->getAssetType() != ASSET_IMAGE) {
			assetCache->incrementRefCount();
		} else {
			((CKLBImageAsset*)assetCache)->getTexture()->incrementRefCount();
		}
	}
}

void CKLBCompositeAsset::STRINGENTRY::decrementRefCount() {
	if (assetCache) {
		if (assetCache->getAssetType() != ASSET_IMAGE) {
			assetCache->decrementRefCount();
		} else {
			((CKLBImageAsset*)assetCache)->getTexture()->decrementRefCount();
		}
	}
}

CKLBCompositeAsset::STRINGENTRY* CKLBCompositeAsset::registerString(const char* string, u32 strLen, bool* err) {
	*err = false;
	if (strLen && string) {
		STRINGENTRY* parse = m_allocatedString;
		while (parse) {
			if (strcmp(parse->string, string) == 0) {
				return parse;
			}
			parse = parse->next;
		}

		//
		// Create if not found.
		//
		STRINGENTRY* pNew = KLBNEW(STRINGENTRY);
		char* pNewStr = (char*)KLBMALLOC(strLen+1);

		if (pNew && pNewStr) {
			pNew->next = m_allocatedString;
			m_allocatedString = pNew;
			pNew->string = pNewStr;
			memcpy(pNewStr, string, strLen);
			pNewStr[strLen] = 0; // C String close.
			return pNew;
		}
		if (pNew) { KLBDELETE(pNew); }
		*err = true;
	}

	return NULL;
}

s32 CKLBCompositeAsset::readLayoutInt(s32 value, u32 layoutShift) {
	s32 layoutMode = ((static_cast<u32>(value) >> 24) & 0xF) - 1;
	u32 layoutMask = 0;
	if (static_cast<u32>(layoutMode) < 4) {
		layoutMask = LAYOUT_SIZE_MODES[layoutMode];
	}
	m_pCurrInnerDef->layoutInfo |= layoutMask << layoutShift;
	return (value << 8) >> 8;
}

void CKLBCompositeAsset::appendInnerDef(CKLBInnerDef* definition) {
	if (!m_pParent) {
		m_root = definition;
	} else if (m_pParent->sub) {
		if (m_pCurrInnerDef) {
			m_pCurrInnerDef->next = definition;
		} else {
			definition->next = m_pParent->sub;
			m_pParent->sub = definition;
		}
	} else {
		definition->next = m_pParent->sub;
		m_pParent->sub = definition;
	}
	m_pCurrInnerDef = definition;
}

int CKLBCompositeAsset::readNull() {
    return 1;
}
  
int CKLBCompositeAsset::readBoolean(int boolean) {
	switch (m_parserField) {
	case VISIBLE_FIELD:
		m_pCurrInnerDef->visible = boolean ? true : false;
		break;
	case GENERIC_FIELD:
		m_pCurrInnerDef->propertyBag->setPropertyBool(tmpBuff, boolean ? true:false);
		break;
	}
	return 1;  
}  

int CKLBCompositeAsset::readInt(long long integerVal) 
{
	switch(m_parserField) {
	case ID:
		m_pCurrInnerDef->id		 = (u32)integerVal;
		break;
	case CLIPX:
		m_pCurrInnerDef->clipx	 = (s16)integerVal;
		break;
	case CLIPY:
		m_pCurrInnerDef->clipy	 = (s16)integerVal;
		break;
	case CLIPW:
		m_pCurrInnerDef->clipw	 = (s16)integerVal;
		break;
	case CLIPH:
		m_pCurrInnerDef->cliph	 = (s16)integerVal;
		break;
	case CLIPSTART:
		m_pCurrInnerDef->priorityClipStart = (u32)integerVal;
		break;
	case CLIPEND:
		m_pCurrInnerDef->priorityClipEnd = (u32)integerVal;
		break;
	case RADIO_FIELD:
		m_pCurrInnerDef->radioID = (s32)integerVal;
		break;
	case PRIORITY_FIELD:
		m_pCurrInnerDef->priority = (u32)integerVal;	
		break;
	case X_FIELD:
		m_pCurrInnerDef->position[CKLBInnerDef::LAYOUT_X] = (float)(m_bLowRes ? ((integerVal>>1)<<1) : integerVal);
		break;
	case Y_FIELD:	
		m_pCurrInnerDef->position[CKLBInnerDef::LAYOUT_Y] = (float)(m_bLowRes ? ((integerVal>>1)<<1) : integerVal);
		break;
	case SX_FIELD:	
		m_pCurrInnerDef->sx = (s16)integerVal;
		break;
	case SY_FIELD:	
		m_pCurrInnerDef->sy = (s16)integerVal;
		break;
	case SW_FIELD:	
		m_pCurrInnerDef->sw = (s16)integerVal;
		break;
	case SH_FIELD:	
		m_pCurrInnerDef->sh = (s16)integerVal;
		break;
	case LAYOUT_DIR_FIELD:	
		m_pCurrInnerDef->layoutDir = (s32)integerVal;
		break;
	case LW_FIELD:
		m_pCurrInnerDef->lw = (s16)integerVal;
		break;
	case LH_FIELD:
		m_pCurrInnerDef->lh = (s16)integerVal;
		break;
	case FONT_SIZE_FIELD:
		m_pCurrInnerDef->fontSize = (u16)integerVal;
		break;
	case COLOR_FIELD:
		m_pCurrInnerDef->color = (u32)integerVal;
		break;
	case WIDTH_FIELD:
		if (m_root != m_pCurrInnerDef) {
			s32 layoutMode = ((((u32)integerVal) >> 24) & 0xF) - 1;
			u32 layoutMask = 0;
			if ((u32)layoutMode < 4) {
				layoutMask = LAYOUT_SIZE_MODES[layoutMode] << 4;
			}
			m_pCurrInnerDef->layoutInfo |= layoutMask;
			s32 width = (s32)integerVal;
			width = (width << 8) >> 8;
			m_pCurrInnerDef->width = (float)width;
		} else {
			m_width = (s16)integerVal;
		}
		break;
	case HEIGHT_FIELD:
		if (m_root != m_pCurrInnerDef) {
			s32 layoutMode = ((((u32)integerVal) >> 24) & 0xF) - 1;
			u32 layoutMask = 0;
			if ((u32)layoutMode < 4) {
				layoutMask = LAYOUT_SIZE_MODES[layoutMode] << 6;
			}
			m_pCurrInnerDef->layoutInfo |= layoutMask;
			s32 height = (s32)integerVal;
			height = (height << 8) >> 8;
			m_pCurrInnerDef->height = (float)height;
		} else {
			m_height = (s16)integerVal;
		}
		break;
	case XSCALE_FIELD:
	case STARTANGLE_FIELD:
		m_pCurrInnerDef->xscale = (float)integerVal;
		break;
	case YSCALE_FIELD:
	case ENDANGLE_FIELD:
		m_pCurrInnerDef->yscale = (float)integerVal;
		break;
	case ROTATION_FIELD:
		m_pCurrInnerDef->rotation = (float)integerVal;
		break;
	case ANIM_EASING:
		m_pCurrAnim->spline = (u8)integerVal;
		break;
	case ANIM_LENGTH:
		m_pCurrAnim->length = (u16)integerVal;
		break;
	case ANIM_SHIFT:
		m_pCurrAnim->timeShift = (s16)integerVal;
		break;
	case ANIM_FROMX:
		m_pCurrAnim->fromX	= (s16)integerVal;
		break;
	case ANIM_TOX:
		m_pCurrAnim->toX	= (s16)integerVal;
		break;
	case ANIM_FROMY:
		m_pCurrAnim->fromY	= (s16)integerVal;
		break;
	case ANIM_TOY:
		m_pCurrAnim->toY	= (s16)integerVal;
		break;
	case ANIM_FROMALPHA:
		m_pCurrAnim->fromAlpha = (u8)integerVal;
		break;
	case ANIM_TOALPHA:
		m_pCurrAnim->toAlpha = (u8)integerVal;
		break;
	case ANIM_FROMCOLOR:
		m_pCurrAnim->fromR = (integerVal>>16) & 0xFF;
		m_pCurrAnim->fromG = (integerVal>>8)  & 0xFF;
		m_pCurrAnim->fromB = (integerVal)     & 0xFF;
		break;
	case ANIM_TOCOLOR:
		m_pCurrAnim->toR = (integerVal>>16) & 0xFF;
		m_pCurrAnim->toG = (integerVal>>8)  & 0xFF;
		m_pCurrAnim->toB = (integerVal)     & 0xFF;
		break;
	case ANIM_FROMSCALE:
		m_pCurrAnim->fromScaleX = (float)integerVal;
		m_pCurrAnim->fromScaleY = m_pCurrAnim->fromScaleX;
		break;
	case ANIM_TOSCALE:
		m_pCurrAnim->toScaleX = (float)integerVal;
		m_pCurrAnim->toScaleY = m_pCurrAnim->toScaleX;
		break;
	case ANIM_FROMSCALE_X:
		m_pCurrAnim->fromScaleX = (float)integerVal;
		break;
	case ANIM_FROMSCALE_Y:
		m_pCurrAnim->fromScaleY = (float)integerVal;
		break;
	case ANIM_TOSCALE_X:
		m_pCurrAnim->toScaleX = (float)integerVal;
		break;
	case ANIM_TOSCALE_Y:
		m_pCurrAnim->toScaleY = (float)integerVal;
		break;
	case ANIM_FROM_ROTATION:
		m_pCurrAnim->fromRotation	= (float)integerVal;
		break;
	case ANIM_TO_ROTATION:
		m_pCurrAnim->toRotation		= (float)integerVal;
		break;
	case ANIM_LOOP:
		m_pCurrAnim->loop	= (u8)integerVal;
		break;
	case ANIM_PINGPONG:
		m_pCurrAnim->pingpong = (u8)integerVal;
		break;
	case ANIM_DATA:
		if (m_pCurrAnim->data == NULL) {
			integerVal *= 4; // Key count into integer count
			m_pCurrAnim->data		= KLBNEWA(s32, (u32)integerVal);
			m_pCurrAnim->dataSize	= (u16)integerVal;
			m_pCurrAnim->currSize	= 0;
			m_pCurrAnim->spline		= 0; // Custom
		} else {
			if (m_pCurrAnim->currSize < m_pCurrAnim->dataSize) {
				m_pCurrAnim->data[m_pCurrAnim->currSize++] = (s32)integerVal;
			}
		}
		break;
	case ORIENTATION_FIELD:
		// Reuse SX to reduce memory usage.
		// orientation
		m_pCurrInnerDef->sx = (s16)integerVal;
		break;
	case INT_0_FIELD:
		// Reuse sw to reduce memory usage :
		// - docWidth
		m_pCurrInnerDef->sw = (s16)integerVal;
		break;
	case INT_1_FIELD:
		// Reuse sh to reduce memory usage :
		// - docHeight
		m_pCurrInnerDef->sh = (s16)integerVal;
		break;
	case INT_2_FIELD:
		// Reuse radioID to reduce memory usage :
		// - maxCommand Count
		m_pCurrInnerDef->radioID = (s32)integerVal;
		break;
	case FLAG_0_FIELD:
	case ALIGN_FIELD:
	case BASEINVISIBLE_FIELD:
	case ISPASSWORD_FIELD:
	case VARCLIP_FIELD:
		// allow navigation			(webview)
		// base invisible			(ui drag)
		// align					(button/checkbox)
		m_pCurrInnerDef->flag[0]			= (u8)integerVal;
		break;
	case FILLZERO_FIELD:
		m_pCurrInnerDef->flag[2]			= (u8)integerVal;
		break;
	case ANIMATE_FIELD:
	case FIT_FIELD:
		m_pCurrInnerDef->flag[1]			= (u8)integerVal;
		break;
	case COUNTCLIP_FIELD:
		m_pCurrInnerDef->flag[3]			= 1 - (u8)integerVal;	// default
		break;
	case VALUE_FIELD:
		m_pCurrInnerDef->value				= (s32)integerVal;
		break;
	case PRIO_OFFSET_FIELD:
		m_pCurrInnerDef->priorityClipStart	= (u32)integerVal;		// reuse field
		break;
	case DRAG_ALPHA_FIELD:
		m_pCurrInnerDef->rotation			= (((float)integerVal) / 255.0f);		// reuse field
		break;
	case CENTERX_FIELD:
		m_pCurrInnerDef->clipx				= (s16)integerVal;	// reuse field
		break;
	case CENTERY_FIELD:
		m_pCurrInnerDef->clipy				= (s16)integerVal;	// reuse field
		break;
	case START_PIX_FIELD:
		m_pCurrInnerDef->clipw				= (s16)integerVal;	// reuse field
		break;
	case END_PIX_FIELD:
		m_pCurrInnerDef->cliph				= (s16)integerVal;	// reuse field
		break;
	case ANIM_TIME_FIELD:
	case NUMBERCOUNT_FIELD:
		m_pCurrInnerDef->radioID			= (s32)integerVal;		// reuse field
		break;
	case STEPX_FIELD:
		m_pCurrInnerDef->sx					= (s16)integerVal;
		break;
	case STEPY_FIELD:
		m_pCurrInnerDef->sy					= (s16)integerVal;
		break;
	case ENABLE_FIELD:
		m_pCurrInnerDef->flag[2]			= (u8)integerVal;
		break;

	case MINVALUE:
		m_pCurrInnerDef->variable[VAR_MIN]			= (u32)integerVal;
		break;
	case MAXVALUE:
		m_pCurrInnerDef->variable[VAR_MAX]			= (u32)integerVal;
		break;
	case SLIDERSIZE:
		m_pCurrInnerDef->variable[VAR_SLIDERSIZE]	= (u32)integerVal;
		break;
	case MINSLIDERSIZE:
		m_pCurrInnerDef->variable[VAR_MIN_SLIDERSIZE]	= (u32)integerVal;
		break;
	case SELECTCOLOR:
		m_pCurrInnerDef->variable[VAR_SELECTCOLOR]	= (u32)integerVal;
		break;

	case MAXVERTEX:
		m_pCurrInnerDef->variable[VAR_MAXVERTEX]	= (u32)integerVal;
		break;
	case MAXINDEX:
		m_pCurrInnerDef->variable[VAR_MAXINDEX]		= (u32)integerVal;
		break;

	case SPLINE_COUNT:
		m_pCurrInnerDef->splineCount = (u16)integerVal;
		break;
	case SPLINE_LENGTH:
		m_pCurrInnerDef->splineLength = (u16)integerVal;
		break;
	case SPLINE_MASK:
		m_pCurrInnerDef->splineMask			= ((u16)integerVal) | 3; // Always X&Y
		{
			u8 size = 0;
			for (int n=0; n < CKLBSplineNode::MODIFY_TOTALCOUNT; n++) {
				if ((m_pCurrInnerDef->splineMask) & (1<<n)) {
					size += ((n == (CKLBSplineNode::MODIFY_SCALE)) ? 4 : 2);
				}
			}
			m_pCurrInnerDef->splineVectorSize	= size;
		}
		break;
	case SPLINE_ARRAY:
		if (!m_pCurrInnerDef->spline || !m_currArraySpline ||
			m_currArraySpline >= (m_pCurrInnerDef->spline + (m_pCurrInnerDef->splineVectorSize * m_pCurrInnerDef->splineCount))) {
			return 0;
		}
		*m_currArraySpline++		= (float)integerVal;
		break;
	case GENERIC_FIELD:
		m_pCurrInnerDef->propertyBag->setPropertyInt(tmpBuff, (s32)integerVal);
		break;
	case VISIBLE_FIELD:
		m_pCurrInnerDef->visible = integerVal ? true : false;
		break;
	case MAXLENGTH:
		m_pCurrInnerDef->clipw				= (u32)integerVal;
		break;
	case CHARTYPE:
		m_pCurrInnerDef->cliph				= (u32)integerVal;
		break;
	case VOL_AUDIO_UP:
		m_pCurrInnerDef->volAudioUp			= (u8)integerVal;
		break;
	case VOL_AUDIO_DOWN:
		m_pCurrInnerDef->volAudioDown		= (u8)integerVal;
		m_pCurrInnerDef->shadowDX			= (u8)integerVal;
		break;
	case DOCK_FIELD:
		m_pCurrInnerDef->flag[2]			= (u8)integerVal;
		break;
	case ANCHOR_FIELD:
	case ANCHOR_X_FIELD:
	case ANCHOR_Y_FIELD:
		m_pCurrInnerDef->anchor |= (u8)integerVal;
		break;
	case MARGIN_DOWN_FIELD:
		m_pCurrInnerDef->cliph = (s16)integerVal;
		break;
	case MARGIN_UP_FIELD:
		m_pCurrInnerDef->clipy = (s16)integerVal;
		break;
	case MARGIN_LEFT_FIELD:
		m_pCurrInnerDef->clipx = (s16)integerVal;
		break;
	case MARGIN_RIGHT_FIELD:
		m_pCurrInnerDef->clipw = (s16)integerVal;
		break;
	case SHADOW_DX_FIELD:
		m_pCurrInnerDef->shadowDX			= (u8)integerVal;
		break;
	case SHADOW_DY_FIELD:
		m_pCurrInnerDef->shadowDY = (u8)integerVal;
		break;
	case SHADOW_COLOR_FIELD:
		m_pCurrInnerDef->shadowColor = (u32)integerVal;
		break;
	case SHADOW_BLUR_FIELD:
		m_pCurrInnerDef->shadowBlur = (float)integerVal;
		break;
	}
    return 1;
}  
  
int CKLBCompositeAsset::readDouble(double doubleVal)  
{	
	// do nothing here.
	switch (m_parserField) {
	case ANIM_FROMSCALE:
		m_pCurrAnim->fromScaleX = (float)doubleVal;
		m_pCurrAnim->fromScaleY = m_pCurrAnim->fromScaleX;
		break;
	case ANIM_TOSCALE:
		m_pCurrAnim->toScaleX       = (float)doubleVal;
		m_pCurrAnim->toScaleY       = m_pCurrAnim->toScaleX;
		break;
	case ANIM_FROMSCALE_X:
		m_pCurrAnim->fromScaleX     = (float)doubleVal;
		break;
	case ANIM_FROMSCALE_Y:
		m_pCurrAnim->fromScaleY     = (float)doubleVal;
		break;
	case ANIM_TOSCALE_X:
		m_pCurrAnim->toScaleX       = (float)doubleVal;
		break;
	case ANIM_TOSCALE_Y:
		m_pCurrAnim->toScaleY       = (float)doubleVal;
		break;
	case XSCALE_FIELD:
	case STARTANGLE_FIELD:
		m_pCurrInnerDef->xscale     = (float)doubleVal;
		break;
	case YSCALE_FIELD:
	case ENDANGLE_FIELD:
		m_pCurrInnerDef->yscale     = (float)doubleVal;
		break;
	case ROTATION_FIELD:
		m_pCurrInnerDef->rotation   = (float)doubleVal;
		break;
	case ANIM_FROM_ROTATION:
		m_pCurrAnim->fromRotation	= (float)doubleVal;
		break;
	case ANIM_TO_ROTATION:
		m_pCurrAnim->toRotation		= (float)doubleVal;
		break;
	case DRAG_ALPHA_FIELD:
		m_pCurrInnerDef->rotation	= (float)doubleVal;		// reuse field
		break;
	case SPLINE_ARRAY:
		if (!m_pCurrInnerDef->spline || !m_currArraySpline ||
			m_currArraySpline >= (m_pCurrInnerDef->spline + (m_pCurrInnerDef->splineVectorSize * m_pCurrInnerDef->splineCount))) {
			return 0;
		}
		*m_currArraySpline++		= (float)doubleVal;
		break;
	case GENERIC_FIELD:
		m_pCurrInnerDef->propertyBag->setPropertyFloat(tmpBuff, (float)doubleVal);
		break;
	case SHADOW_BLUR_FIELD:
		m_pCurrInnerDef->shadowBlur = (float)doubleVal;
		break;
	}
	return 1;  
}  
  
int CKLBCompositeAsset::readString(const unsigned char * stringVal, size_t stringLen, int /*cte_pool*/)  
{
	bool err = false;
	switch (m_parserField) {
	case CLASS_FIELD:
		if (strncmp("button", (const char*)stringVal,(sizeof("button")-1))==0) {
			m_pCurrInnerDef->classID = BUTTON_CLASSID;
		} else
		if (strncmp("btnscale9", (const char*)stringVal,(sizeof("btnscale9")-1))==0) {
			m_pCurrInnerDef->classID = BUTTON_SCALE9_CLASSID;
		} else
		if (strncmp("checkbox", (const char*)stringVal,(sizeof("checkbox")-1))==0) {
			m_pCurrInnerDef->classID = CHECK_CLASSID;
		} else
		if (strncmp("container", (const char*)stringVal,(sizeof("container")-1)) == 0) {
			m_pCurrInnerDef->classID = CONTAINER_CLASSID;
		} else 
		if (strncmp("label", (const char*)stringVal, (sizeof("label")-1)) == 0) {
			m_pCurrInnerDef->classID = LABEL_CLASSID;
		} else 
		if (strncmp("animnode", (const char*)stringVal, (sizeof("animnode")-1)) == 0) {
			m_pCurrInnerDef->classID = ANIMNODE_CLASSID;
		} else 
		if (strncmp("textbox", (const char*)stringVal, (sizeof("textbox")-1)) == 0) {
			m_pCurrInnerDef->classID = TEXTBOX_CLASSID;
		} else 
		if (strncmp("task_webview", (const char*)stringVal, (sizeof("task_webview")-1)) == 0) {
			m_pCurrInnerDef->classID = WEBVIEW_CLASSID;
		} else 
		if (strncmp("task_virtualdoc", (const char*)stringVal, (sizeof("task_virtualdoc")-1)) == 0) {
			m_pCurrInnerDef->classID = VIRTUALDOC_CLASSID;
		} else 
		if (strncmp("task_score", (const char*)stringVal, (sizeof("task_score")-1)) == 0) {
			m_pCurrInnerDef->classID = SCORE_CLASSID;
		} else 
		if (strncmp("task_progressbar", (const char*)stringVal, (sizeof("task_progressbar")-1)) == 0) {
			m_pCurrInnerDef->classID = PROGRESSBAR_CLASSID;
		} else 
		if (strncmp("task_group", (const char *)stringVal, (sizeof("task_group")-1)) == 0) {
			m_pCurrInnerDef->classID = GROUP_CLASSID;
		} else
		if (strncmp("task_dragicon", (const char*)stringVal, (sizeof("task_dragicon")-1)) == 0) {
			m_pCurrInnerDef->classID = DRAGICON_CLASSID;
		} else 
		if (strncmp("task_list", (const char*)stringVal, (sizeof("task_list")-1)) == 0) {
			m_pCurrInnerDef->classID = LIST_CLASSID;
		} else
		if (strncmp("task_piechart", (const char*)stringVal, (sizeof("task_piechart")-1)) == 0) {
			m_pCurrInnerDef->classID = PIECHART_CLASSID; 
		} else 
		if (strncmp("task_canvas", (const char*)stringVal, (sizeof("task_canvas")-1)) == 0) {
			m_pCurrInnerDef->classID = CANVAS_CLASSID; 
		} else 
		if (strncmp("task_scrollbar", (const char*)stringVal, (sizeof("task_scrollbar")-1)) == 0) {
			m_pCurrInnerDef->classID = SCROLLBAR_CLASSID; 
		} else 
		if (strncmp("task_scale9", (const char*)stringVal, (sizeof("task_scale9")-1)) == 0) {
			m_pCurrInnerDef->classID = SCALE9_CLASSID; 
		} else
		if (strncmp("task_varitem", (const char *)stringVal, (sizeof("task_varitem")-1)) == 0) {
			m_pCurrInnerDef->classID = VARITEM_CLASSID;
		} else
		if (strncmp("task_tiledcanvas", (const char*)stringVal, (sizeof("task_tiledcanvas")-1)) == 0) {
			m_pCurrInnerDef->classID = TILEDCANVAS_CLASSID; 
		} else
		if (strncmp("format_rect", (const char*)stringVal, (sizeof("format_rect")-1)) == 0) {
			m_pCurrInnerDef->classID = FORMAT_RECT_CLASSID;
		} else
		if (strncmp("task_shader", (const char*)stringVal, (sizeof("task_shader")-1)) == 0) {
			m_pCurrInnerDef->classID = SHADER_CLASSID;
		} else
		if (strncmp("task_buffer", (const char*)stringVal, (sizeof("task_buffer")-1)) == 0) {
			m_pCurrInnerDef->classID = BUFFER_CLASSID;
		}
			
		break;
	case TEXT_FIELD:
		m_pCurrInnerDef->text           = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case PLACEHOLDER_FIELD:
		m_pCurrInnerDef->placeholder    = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ON_CLICK:
		m_pCurrInnerDef->handler[0]     = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ASSET_FIELD:
		m_pCurrInnerDef->assets[NORMAL_ASSET]	= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ASSET_DISABLED_FIELD:
		m_pCurrInnerDef->assets[DISABLE_ASSET]	= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ASSET_FOCUS_FIELD:
		m_pCurrInnerDef->assets[FOCUS_ASSET]	= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ASSET_PUSH_FIELD:
		m_pCurrInnerDef->assets[PUSH_ASSET]		= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ASSET_4:
	case ASSET_5:
	case ASSET_6:
	case ASSET_7:
	case ASSET_8:
	case ASSET_9:
	case ASSET_DOT:
		m_pCurrInnerDef->assets[m_parserField - ASSET_FIELD] = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case NAME_FIELD:
		m_pCurrInnerDef->name					= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case FONT_NAME_FIELD:
		m_pCurrInnerDef->fontName				= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ANIM_TYPE:
		m_pCurrAnim->mode						= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case CALLBACK_FIELD:
		// Use same space as onclick
		m_pCurrInnerDef->handler[0] = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case URL_FIELD:
		m_pCurrInnerDef->handler[1] = this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case SOUNDUP_FIELD:
		m_pCurrInnerDef->assets[UPAUDIO_ASSET]		= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case SOUNDDOWN_FIELD:
		m_pCurrInnerDef->assets[DOWNAUDIO_ASSET]	= this->registerString((const char*)stringVal, stringLen, &err);
		break;

	case STARTANGLE_FIELD:
		m_pCurrInnerDef->dbField[DB_VAR_STARTANGLE]		= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ENDANGLE_FIELD:
		m_pCurrInnerDef->dbField[DB_VAR_ENDANGLE]		= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case ENABLE_FIELD:
		m_pCurrInnerDef->dbField[DB_VAR_ENABLE]			= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case MINVALUE:
		m_pCurrInnerDef->dbField[DB_VAR_MIN]			= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case MAXVALUE:
		m_pCurrInnerDef->dbField[DB_VAR_MAX]			= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case VALUE_FIELD:
		m_pCurrInnerDef->dbField[DB_VAL_VALUE]			= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case COLOR_FIELD:
		m_pCurrInnerDef->dbField[DB_VAL_COLOR]			= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case FLAG_0_FIELD:
		m_pCurrInnerDef->dbField[DB_VAR_CHECKED]		= this->registerString((const char*)stringVal, stringLen, &err);
		break;
	case GENERIC_FIELD:
		size_t prevBuffLen = strlen(tmpBuff) + 2;
		memcpy(&tmpBuff[prevBuffLen], stringVal, stringLen);
		tmpBuff[prevBuffLen + stringLen] = 0; // Close C String.
		m_pCurrInnerDef->propertyBag->setPropertyString(tmpBuff, &tmpBuff[prevBuffLen]);
		break;
	}

	if (err) {
		return 0;
	}
	return 1;
}

#define sizet(a)	((sizeof(a)-1))

struct key_name_value {
  const char *name;
  int size;
  int value;
};

static const key_name_value keywords1[] = {
	// Size 1
	{"x",(sizeof("x")-1),CKLBCompositeAsset::X_FIELD},
	{"y",(sizeof("y")-1),CKLBCompositeAsset::Y_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords2[] = {
	// Size 2
	{"id",(sizeof("id")-1),CKLBCompositeAsset::ID},
	{"lh",(sizeof("lh")-1),CKLBCompositeAsset::LH_FIELD},
	{"lw",(sizeof("lw")-1),CKLBCompositeAsset::LW_FIELD},
	{"sh",(sizeof("sh")-1),CKLBCompositeAsset::SH_FIELD},
	{"sw",(sizeof("sw")-1),CKLBCompositeAsset::SW_FIELD},
	{"sx",(sizeof("sx")-1),CKLBCompositeAsset::SX_FIELD},
	{"sy",(sizeof("sy")-1),CKLBCompositeAsset::SY_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords3[] = {
	// Size 3
	{"fit",(sizeof("fit")-1),CKLBCompositeAsset::FIT_FIELD},
	{"sub",(sizeof("sub")-1),CKLBCompositeAsset::SUB_FIELD},
	{"tox",(sizeof("tox")-1),CKLBCompositeAsset::ANIM_TOX},
	{"toy",(sizeof("toy")-1),CKLBCompositeAsset::ANIM_TOY},
	{"url",(sizeof("url")-1),CKLBCompositeAsset::URL_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords4[] = {
	// Size 4
	{"dock",(sizeof("dock")-1),CKLBCompositeAsset::DOCK_FIELD},
	{"loop",(sizeof("loop")-1),CKLBCompositeAsset::ANIM_LOOP},
	{"name",(sizeof("name")-1),CKLBCompositeAsset::NAME_FIELD},
	{"text",(sizeof("text")-1),CKLBCompositeAsset::TEXT_FIELD},
	{"type",(sizeof("type")-1),CKLBCompositeAsset::ANIM_TYPE}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords5[] = {
	// Size 5
	{"align",(sizeof("align")-1),CKLBCompositeAsset::ALIGN_FIELD},
	{"class",(sizeof("class")-1),CKLBCompositeAsset::CLASS_FIELD},
	{"cliph",(sizeof("cliph")-1),CKLBCompositeAsset::CLIPH},
	{"clipw",(sizeof("clipw")-1),CKLBCompositeAsset::CLIPW},
	{"clipx",(sizeof("clipx")-1),CKLBCompositeAsset::CLIPX},
	{"clipy",(sizeof("clipy")-1),CKLBCompositeAsset::CLIPY},
	{"color",(sizeof("color")-1),CKLBCompositeAsset::COLOR_FIELD},
	{"focus",(sizeof("focus")-1),CKLBCompositeAsset::ASSET_FOCUS_FIELD},
	{"fromx",(sizeof("fromx")-1),CKLBCompositeAsset::ANIM_FROMX},
	{"fromy",(sizeof("fromy")-1),CKLBCompositeAsset::ANIM_FROMY},
	{"radio",(sizeof("radio")-1),CKLBCompositeAsset::RADIO_FIELD},
	{"stepx",(sizeof("stepx")-1),CKLBCompositeAsset::STEPX_FIELD},
	{"stepy",(sizeof("stepy")-1),CKLBCompositeAsset::STEPY_FIELD},
	{"value",(sizeof("value")-1),CKLBCompositeAsset::VALUE_FIELD},
	{"width",(sizeof("width")-1),CKLBCompositeAsset::WIDTH_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords6[] = {
	// Size 6
	{"anchor",(sizeof("anchor")-1),CKLBCompositeAsset::ANCHOR_FIELD},
	{"asset0",(sizeof("asset0")-1),CKLBCompositeAsset::ASSET_FIELD + 0},
	{"asset1",(sizeof("asset1")-1),CKLBCompositeAsset::ASSET_FIELD + 1},
	{"asset2",(sizeof("asset2")-1),CKLBCompositeAsset::ASSET_FIELD + 2},
	{"asset3",(sizeof("asset3")-1),CKLBCompositeAsset::ASSET_FIELD + 3},
	{"asset4",(sizeof("asset4")-1),CKLBCompositeAsset::ASSET_FIELD + 4},
	{"asset5",(sizeof("asset5")-1),CKLBCompositeAsset::ASSET_FIELD + 5},
	{"asset6",(sizeof("asset6")-1),CKLBCompositeAsset::ASSET_FIELD + 6},
	{"asset7",(sizeof("asset7")-1),CKLBCompositeAsset::ASSET_FIELD + 7},
	{"asset8",(sizeof("asset8")-1),CKLBCompositeAsset::ASSET_FIELD + 8},
	{"asset9",(sizeof("asset9")-1),CKLBCompositeAsset::ASSET_FIELD + 9},
	{"easing",(sizeof("easing")-1),CKLBCompositeAsset::ANIM_EASING},
	{"enable",(sizeof("enable")-1),CKLBCompositeAsset::ENABLE_FIELD},
	{"height",(sizeof("height")-1),CKLBCompositeAsset::HEIGHT_FIELD},
/* ???? */
	{"layout",(sizeof("layout")-1),CKLBCompositeAsset::LAYOUT_DIR_FIELD},
	{"length",(sizeof("length")-1),CKLBCompositeAsset::ANIM_LENGTH},
	{"select",(sizeof("select")-1),CKLBCompositeAsset::ASSET_PUSH_FIELD},
	{"states",(sizeof("states")-1),CKLBCompositeAsset::STATES_FIELD},
	{"xscale",(sizeof("xscale")-1),CKLBCompositeAsset::XSCALE_FIELD},
	{"yscale",(sizeof("yscale")-1),CKLBCompositeAsset::YSCALE_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywords7[] = {
	// Size 7
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
	{"anchorX",(sizeof("anchorX")-1),CKLBCompositeAsset::ANCHOR_X_FIELD},
	{"anchorY",(sizeof("anchorY")-1),CKLBCompositeAsset::ANCHOR_Y_FIELD},
	{"animate",(sizeof("animate")-1),CKLBCompositeAsset::ANIMATE_FIELD},
	{"centerx",(sizeof("centerx")-1),CKLBCompositeAsset::CENTERX_FIELD},
	{"centery",(sizeof("centery")-1),CKLBCompositeAsset::CENTERY_FIELD},
	{"checked",(sizeof("checked")-1),CKLBCompositeAsset::FLAG_0_FIELD},
	{"clipend",(sizeof("clipend")-1),CKLBCompositeAsset::CLIPEND},
	{"comment",(sizeof("comment")-1),CKLBCompositeAsset::COMMENT_FIELD},
	{"default",(sizeof("default")-1),CKLBCompositeAsset::ASSET_FIELD},
	{"disable",(sizeof("disable")-1),CKLBCompositeAsset::ASSET_DISABLED_FIELD},
	{"onclick",(sizeof("onclick")-1),CKLBCompositeAsset::ON_CLICK},
	{"soundup",(sizeof("soundup")-1),CKLBCompositeAsset::SOUNDUP_FIELD},
	{"toalpha",(sizeof("toalpha")-1),CKLBCompositeAsset::ANIM_TOALPHA},
	{"tocolor",(sizeof("tocolor")-1),CKLBCompositeAsset::ANIM_TOCOLOR},
	{"toscale",(sizeof("toscale")-1),CKLBCompositeAsset::ANIM_TOSCALE},
	{"varclip",(sizeof("varclip")-1),CKLBCompositeAsset::VARCLIP_FIELD},
	{"visible",(sizeof("visible")-1),CKLBCompositeAsset::VISIBLE_FIELD}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const key_name_value keywordsOther[] = {
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
	{"allownavigation",(sizeof("allownavigation")-1),CKLBCompositeAsset::FLAG_0_FIELD},
	{"animtime",(sizeof("animtime")-1),CKLBCompositeAsset::ANIM_TIME_FIELD},
	{"arrownavigation",(sizeof("arrownavigation")-1),CKLBCompositeAsset::FLAG_0_FIELD},
	{"assetdot",(sizeof("assetdot")-1),CKLBCompositeAsset::ASSET_DOT},
	{"baseinvisible",(sizeof("baseinvisible")-1),CKLBCompositeAsset::BASEINVISIBLE_FIELD},
	{"callback",(sizeof("callback")-1),CKLBCompositeAsset::CALLBACK_FIELD},
	{"chartype",(sizeof("chartype")-1),CKLBCompositeAsset::CHARTYPE},
	{"clipimage",(sizeof("clipimage")-1),CKLBCompositeAsset::ASSET_DISABLED_FIELD},
	{"clipstart",(sizeof("clipstart")-1),CKLBCompositeAsset::CLIPSTART},
	{"countclip",(sizeof("countclip")-1),CKLBCompositeAsset::COUNTCLIP_FIELD},
	{"docheight",(sizeof("docheight")-1),CKLBCompositeAsset::INT_1_FIELD},
	{"docwidth",(sizeof("docwidth")-1),CKLBCompositeAsset::INT_0_FIELD},
	{"dragalpha",(sizeof("dragalpha")-1),CKLBCompositeAsset::DRAG_ALPHA_FIELD},
	{"endangle",(sizeof("endangle")-1),CKLBCompositeAsset::ENDANGLE_FIELD},
	{"endpixel",(sizeof("endpixel")-1),CKLBCompositeAsset::END_PIX_FIELD},
	{"fillzero",(sizeof("fillzero")-1),CKLBCompositeAsset::FILLZERO_FIELD},
	{"fontname",(sizeof("fontname")-1),CKLBCompositeAsset::FONT_NAME_FIELD},
	{"fontsize",(sizeof("fontsize")-1),CKLBCompositeAsset::FONT_SIZE_FIELD},
	{"fromalpha",(sizeof("fromalpha")-1),CKLBCompositeAsset::ANIM_FROMALPHA},
	{"fromcolor",(sizeof("fromcolor")-1),CKLBCompositeAsset::ANIM_FROMCOLOR},
	{"fromrotation",(sizeof("fromrotation")-1),CKLBCompositeAsset::ANIM_FROM_ROTATION},
	{"fromscale",(sizeof("fromscale")-1),CKLBCompositeAsset::ANIM_FROMSCALE},
	{"fromscalex",(sizeof("fromscalex")-1),CKLBCompositeAsset::ANIM_FROMSCALE_X},
	{"fromscaley",(sizeof("fromscaley")-1),CKLBCompositeAsset::ANIM_FROMSCALE_Y},
	{"margindown",(sizeof("margindown")-1),CKLBCompositeAsset::MARGIN_DOWN_FIELD},
	{"marginleft",(sizeof("marginleft")-1),CKLBCompositeAsset::MARGIN_LEFT_FIELD},
	{"marginright",(sizeof("marginright")-1),CKLBCompositeAsset::MARGIN_RIGHT_FIELD},
	{"marginup",(sizeof("marginup")-1),CKLBCompositeAsset::MARGIN_UP_FIELD},
	{"maxcommandcount",(sizeof("maxcommandcount")-1),CKLBCompositeAsset::INT_2_FIELD},
	{"maxindex",(sizeof("maxindex")-1),CKLBCompositeAsset::MAXINDEX},
	{"maxlength",(sizeof("maxlength")-1),CKLBCompositeAsset::MAXLENGTH},
	{"maxvalue",(sizeof("maxvalue")-1),CKLBCompositeAsset::MAXVALUE},
	{"maxvertex",(sizeof("maxvertex")-1),CKLBCompositeAsset::MAXVERTEX},
	{"minslidersize",(sizeof("minslidersize")-1),CKLBCompositeAsset::MINSLIDERSIZE},
	{"minvalue",(sizeof("minvalue")-1),CKLBCompositeAsset::MINVALUE},
	{"numbercount",(sizeof("numbercount")-1),CKLBCompositeAsset::NUMBERCOUNT_FIELD},
	{"orientation",(sizeof("orientation")-1),CKLBCompositeAsset::ORIENTATION_FIELD},
	{"passwordmode",(sizeof("passwordmode")-1),CKLBCompositeAsset::ISPASSWORD_FIELD},
	{"pingpong",(sizeof("pingpong")-1),CKLBCompositeAsset::ANIM_PINGPONG},
	{"placeholder",(sizeof("placeholder")-1),CKLBCompositeAsset::PLACEHOLDER_FIELD},
	{"priority",(sizeof("priority")-1),CKLBCompositeAsset::PRIORITY_FIELD},
	{"priorityoffset",(sizeof("priorityoffset")-1),CKLBCompositeAsset::PRIO_OFFSET_FIELD},
	{"rotation",(sizeof("rotation")-1),CKLBCompositeAsset::ROTATION_FIELD},
	{"scrollbar",(sizeof("scrollbar")-1),CKLBCompositeAsset::GENERIC_FIELD},
	{"selectcolor",(sizeof("selectcolor")-1),CKLBCompositeAsset::SELECTCOLOR},
	{"shadowblur",(sizeof("shadowblur")-1),CKLBCompositeAsset::SHADOW_BLUR_FIELD},
	{"shadowcolor",(sizeof("shadowcolor")-1),CKLBCompositeAsset::SHADOW_COLOR_FIELD},
	{"shadowdx",(sizeof("shadowdx")-1),CKLBCompositeAsset::SHADOW_DX_FIELD},
	{"shadowdy",(sizeof("shadowdy")-1),CKLBCompositeAsset::SHADOW_DY_FIELD},
	{"slidersize",(sizeof("slidersize")-1),CKLBCompositeAsset::SLIDERSIZE},
	{"sounddown",(sizeof("sounddown")-1),CKLBCompositeAsset::SOUNDDOWN_FIELD},
	{"sounddownvolume",(sizeof("sounddownvolume")-1),CKLBCompositeAsset::VOL_AUDIO_DOWN},
	{"soundupvolume",(sizeof("soundupvolume")-1),CKLBCompositeAsset::VOL_AUDIO_UP},
	{"splinecount",(sizeof("splinecount")-1),CKLBCompositeAsset::SPLINE_COUNT},
	{"splinedata",(sizeof("splinedata")-1),CKLBCompositeAsset::SPLINE_ARRAY},
	{"splinelength",(sizeof("splinelength")-1),CKLBCompositeAsset::SPLINE_LENGTH},
	{"splinemask",(sizeof("splinemask")-1),CKLBCompositeAsset::SPLINE_MASK},
	{"startangle",(sizeof("startangle")-1),CKLBCompositeAsset::STARTANGLE_FIELD},
	{"startpixel",(sizeof("startpixel")-1),CKLBCompositeAsset::START_PIX_FIELD},
	{"timeshift",(sizeof("timeshift")-1),CKLBCompositeAsset::ANIM_SHIFT},
	{"torotation",(sizeof("torotation")-1),CKLBCompositeAsset::ANIM_TO_ROTATION},
	{"toscalex",(sizeof("toscalex")-1),CKLBCompositeAsset::ANIM_TOSCALE_X},
	{"toscaley",(sizeof("toscaley")-1),CKLBCompositeAsset::ANIM_TOSCALE_Y}
	//
	// KEEP LIST IN ORDER !!! UPDATE USING EXCEL, NOT MANUALLY !!!!
	// Update using EXCEL 
	//
};

static const int keyWordCount[9] = {
	0, // No array of size 0
	sizeof(keywords1) / sizeof(key_name_value),
	sizeof(keywords2) / sizeof(key_name_value),
	sizeof(keywords3) / sizeof(key_name_value),
	sizeof(keywords4) / sizeof(key_name_value),
	sizeof(keywords5) / sizeof(key_name_value),
	sizeof(keywords6) / sizeof(key_name_value),
	sizeof(keywords7) / sizeof(key_name_value),
	sizeof(keywordsOther) / sizeof(key_name_value),
};

static u32 bin_search(const key_name_value* searchArray, const unsigned char *key, u32 start, u32 finish)
{
	if (start >= finish) {
		klb_assertAlways("Invalid Key");
		return 0xFFFFFFFF;
	} else {
		u32 mid = (start+finish)/2;
		int cmp = strcmp((const char*)key,searchArray[mid].name);
		if (cmp == 0) {
			return searchArray[mid].value;
		} else if (cmp < 0) {
			return bin_search(searchArray, key,start,mid);
		} else {
			return bin_search(searchArray, key,mid+1,finish);
		}
	}
}

static u32 Composite_keySearch(const char* buff, u32 stringLen) {
	int num_keywords;
	const key_name_value* keywords;
	int keyLen = stringLen;

	switch (keyLen) {
	case 1:	keywords = keywords1;   break;
	case 2:	keywords = keywords2;	break;
	case 3:	keywords = keywords3;	break;
	case 4:	keywords = keywords4;	break;
	case 5:	keywords = keywords5;	break;
	case 6:	keywords = keywords6;	break;
	case 7:	keywords = keywords7;	break;
	default: 
		keywords = keywordsOther;	
		keyLen = 8;
		break;
	}

	num_keywords = keyWordCount[keyLen];

	return bin_search(keywords,(const unsigned char*)buff,0,num_keywords);
}

/*static*/
int CKLBCompositeAsset::readMapKey(const unsigned char * stringVal, size_t stringLen, int cte_pool)  
{
	if (mode == GENERIC_MODE) {
		m_parserField	= GENERIC_FIELD;
		if (stringLen <= TMP_COMPOSITE_ASSET_STR_MAXLEN) {
			memcpy(tmpBuff, stringVal, stringLen);
			tmpBuff[stringLen] = 0;// Close C string.
			return 1;
		}
		klb_assertAlways("Map Key name is too long");
	}

	//
	m_parserField = 0xFFFF; // Undefined


	if (cte_pool > -1) {
		int id = bjson_getCPCacheID((yajl_handle)this->parserCtx, cte_pool);
		if (id != -1) {
			m_parserField = (u16)id;
			if (id == STATES_FIELD) {
				m_bTreeMode = false;
			}
			return 1;
		}
	}


	// Tmp C string
	char buff[100];
	memcpy(buff,stringVal,stringLen);
	buff[stringLen] = 0;

	m_parserField = Composite_keySearch(buff, stringLen);
	// printf("Keyword : %s (%i)", buff,m_parserField);

	if (m_parserField == STATES_FIELD) {
		m_bTreeMode = false;
	}
	
	if (cte_pool > -1) {
		bjson_setCPCacheID((yajl_handle)this->parserCtx, cte_pool, m_parserField);
	}

	return (m_parserField != 0xFFFF);
}
  
int CKLBCompositeAsset::readStartMap(unsigned int /*size*/) {
	if (m_bTreeMode) {
		if (m_parserField == GENERIC_FIELD) {
			// A property map can be the first map in a definition. In that case,
			// create the definition before attaching the property bag to it.
			if (!m_pCurrInnerDef) {
				CKLBInnerDef* pNext = KLBNEW(CKLBInnerDef);
				if (pNext) {
					if (!m_pParent) {
						m_root = pNext;
					} else if (m_pParent->sub) {
						if (m_pCurrInnerDef) {
							m_pCurrInnerDef->next = pNext;
						} else {
							pNext->next = m_pParent->sub;
							m_pParent->sub = pNext;
						}
					} else {
						pNext->next = m_pParent->sub;
						m_pParent->sub = pNext;
					}
					m_pCurrInnerDef = pNext;
				}
			}
			if (!m_pCurrInnerDef) {
				return 0;
			}

			m_pCurrInnerDef->propertyBag = CKLBPropertyBag::getPropertyBag();
			if (m_pCurrInnerDef->propertyBag) {
				mode = GENERIC_MODE;
			} else {
				return 0;
			}
		} else {
			CKLBInnerDef* pNext = KLBNEW(CKLBInnerDef);
			if (!pNext) {
				return 0;
			}

			if (!m_pParent) {
				// root
				m_root = pNext;
			} else if (m_pParent->sub) {
				if (m_pCurrInnerDef) {
					// Add at the end of the current child list.
					m_pCurrInnerDef->next = pNext;
				} else {
					pNext->next = m_pParent->sub;
					m_pParent->sub = pNext;
				}
			} else {
				pNext->next		= m_pParent->sub;
				m_pParent->sub	= pNext;
			}

			m_pCurrInnerDef = pNext;
		}
	} else {
		CKLBAnimInfo* pNext = KLBNEW(CKLBAnimInfo);
		if (m_pCurrInnerDef) {
			pNext->m_next = m_pCurrInnerDef->anim;
			m_pCurrInnerDef->anim = pNext;
		}
		m_pCurrAnim = pNext;
	}
	return 1;
}
  
int CKLBCompositeAsset::readEndMap()  
{
	if (m_parserField == GENERIC_FIELD) {
		mode = STANDARD_PARSER_MODE;
		m_parserField = 0xFFFF;	// undefined.
	}
	// Do nothing.
	return 1;  
}

int CKLBCompositeAsset::readStartArray(unsigned int /*size*/)
{	// 0 always null
	// 1 has ptr
	if (m_bTreeMode) {
		if (m_parserField == SPLINE_ARRAY) {
			m_currArraySpline			= KLBNEWA(float, m_pCurrInnerDef->splineVectorSize * m_pCurrInnerDef->splineCount);
			m_pCurrInnerDef->spline		= m_currArraySpline;
			if (!m_currArraySpline) {
				return 0;
			}
		} else {
			if (m_parent <= (MAX_STACK_DEPTH - 1)) {
				m_pParent					= m_pCurrInnerDef;
				m_parentStack[++m_parent]	= m_pCurrInnerDef;
			} else {
				klb_assertAlways("Reached Maximum Stack Depth for UI Form (%i)", MAX_STACK_DEPTH);
			}
		}
	} else {
		// Do nothing
		if (m_parserField == ANIM_EASING) {
			m_parserField = ANIM_DATA;
		}
	}

	return 1;
}
  
/*static*/ 
int CKLBCompositeAsset::readEndArray()  {
	if (!m_bTreeMode) {
		if (m_parserField == ANIM_DATA) {
			// Do nothing
		} else {
			m_bTreeMode = true;
		}
	} else {
		if (m_parserField != SPLINE_ARRAY && m_parent) {
			m_pCurrInnerDef = m_parentStack[m_parent--];
			m_pParent	= m_parentStack[m_parent];
		}
	}
	return 1;
}

/*virtual*/
CKLBNode* CKLBCompositeAsset::createSubTree(u32 /*priorityBase*/) {
	klb_assertAlways("[DEPRECATED] Generate method for tree creation is not available anymore");
	return NULL;
}


CKLBNode* CKLBCompositeAsset::createSubTree(CKLBUITask* pParentTask,u32 priorityBase) {
	m_rectXYWH = s_designRects;
	m_rectName = s_designRectNames;
	m_rectNode = s_designRectNodes;
	m_designRectCount = 0;

	for (s32 i = 0; i < g_layoutRecordCount; i++) {
		g_layoutRecords[i].clearCoordinates();
	}
	g_layoutRecordCount = 0;

	m_slotBase = 1;
	m_slotCurrent = 0;
	g_layoutRecords[0].isRect = true;
	g_layoutRecords[0].width = static_cast<float>((m_formWidth != -1) ? m_formWidth : m_width);
	g_layoutRecords[0].height = static_cast<float>((m_formHeight != -1) ? m_formHeight : m_height);

	// Node have priority set into data stream.
	if ((m_recCount == 0) && m_rootParent && m_root && createSubTreeRecursive(m_groupID, pParentTask, m_rootParent, m_root, priorityBase)) {
		m_recCount = 0;
		return m_rootParent->getChild();			
	} else {
		CKLBNode* pChild = m_rootParent->getChild();

		// Destruct half build tree.
		KLBDELETE(pChild);

		return NULL;
	}
}

CKLBAnimInfo* CKLBInnerDef::findAnimation(const char* animName) {
	CKLBAnimInfo* pInfo = this->anim;
	while (pInfo) {
		if ((pInfo->mode) && (pInfo->mode->string) && CKLBUtility::safe_strcmp(pInfo->mode->string, animName)==0) {
			return pInfo;
		}
		pInfo = pInfo->m_next;
	}
	return NULL;
}

void CKLBSplineNode::setCounter(u32* refCounterPtr) {
	if (m_refCounter) {
		*m_refCounter = (*m_refCounter) - 1;
	}
	m_refCounter = refCounterPtr;
}

void CKLBNode::skipAnimation(const char* animName) {
	if (this->getClassID() == CLS_KLBSPLINENODE) {
		CKLBSplineNode* pAnim = (CKLBSplineNode*)this;
		void* pTag = pAnim->getTag();
		if (pTag) {
			//
			CKLBInnerDef* temp = (CKLBInnerDef*)pTag;
			CKLBAnimInfo* animData = NULL;
			if (animName) {
				animData = temp->findAnimation(animName);
				if (animData) {
					pAnim->endAnimation(animData->data);
				}
			}
		}
	}

	CKLBNode* pParse = this->m_pChild;
	while (pParse) {
		pParse->skipAnimation(animName);
		pParse = pParse->m_pBrother;
	}
}

void CKLBNode::kickAnimation(const char* animName,u32* refCounterPtr, bool doBlend) {
	//
	// Is animated node with given animation name setup ?
	//
	if (this->getClassID() == CLS_KLBSPLINENODE) {
		CKLBSplineNode* pAnim = (CKLBSplineNode*)this;
		void* pTag = pAnim->getTag();
		if (pTag) {
			//
			CKLBInnerDef* temp = (CKLBInnerDef*)pTag;
			CKLBAnimInfo* animData = temp->findAnimation(animName);
			if (animData) {
				u32 mask = 0;
				float params[18];
				float src;
				float dst;
				float* fill = params;

				//
				// Yes : Setup animation here.
				//

				src = (doBlend) ? pAnim->m_matrix.m_matrix[MAT_TX] : animData->fromX;
				dst = animData->toX;

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_X_COORD_0;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? pAnim->m_matrix.m_matrix[MAT_TY] : animData->fromY;
				dst = animData->toY;

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_Y_COORD_1;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? m_localColorMatrix.m_vector[0] : (animData->fromR / 255.0f);
				dst = (animData->toR / 255.0f);

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_R_COLOR_3;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? m_localColorMatrix.m_vector[1] : (animData->fromG / 255.0f);
				dst = (animData->toG / 255.0f);

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_G_COLOR_4;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? m_localColorMatrix.m_vector[2] : (animData->fromB / 255.0f);
				dst = (animData->toB / 255.0f);

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_B_COLOR_5;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? m_localColorMatrix.m_vector[3] : (animData->fromAlpha / 255.0f);
				dst = (animData->toAlpha / 255.0f);

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_A_COLOR_6;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? pAnim->getScaleX() : animData->fromScaleX;
				dst = animData->toScaleX;

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_SCALEX_COORD_7;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? pAnim->getScaleY() : animData->fromScaleY;
				dst = animData->toScaleY;

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_SCALEY_COORD_8;
					*fill++ = src;
					*fill++ = dst;
				}

				src = (doBlend) ? pAnim->getRotation() : animData->fromRotation;
				dst = animData->toRotation;

				if ((src != dst) || (!doBlend)) {
					mask |= CKLBSplineNode::ANM_ROTATION_COORD_9;
					*fill++ = src;
					*fill++ = dst;
				}

				//
				pAnim->setAnimation(	animData->length,
										animData->timeShift,
										animData->spline,
										animData->data,
										animData->dataSize >> 2,
										mask | (animData->loop ? CKLBSplineNode::ANM_LOOPBIT : 0) | (animData->pingpong ? CKLBSplineNode::ANM_PING_PONG : 0), params);
				pAnim->play();

				if (refCounterPtr) {
					// Increment ref counter
					*refCounterPtr = *refCounterPtr + 1;
					pAnim->setCounter(refCounterPtr);
				}
			}
		}
	}

	CKLBNode* pParse = this->m_pChild;
	while (pParse) {
		pParse->kickAnimation(animName, refCounterPtr, doBlend);
		pParse = pParse->m_pBrother;
	}
}

#include "CKLBUIWebArea.h"
#include "CKLBUIScore.h"
#include "CKLBUIProgressBar.h"
#include "CKLBUIDragIcon.h"
#include "CKLBUIVirtualDoc.h"
#include "CKLBUIList.h"
#include "CKLBUIPieChart.h"
#include "CKLBUIScrollBar.h"
#include "CKLBUICanvas.h"
#include "CKLBUIScale9.h"

/*static*/char		CKLBCompositeAsset::tmpBuff[TMP_COMPOSITE_ASSET_STR_BUFFSIZE];
/*static*/char*		CKLBCompositeAsset::ptrBuff;

void CKLBCompositeAsset::resetTmpBuff() {
	ptrBuff = &tmpBuff[0];
}

const char* CKLBCompositeAsset::addAssetPrefix(const char* prefixLessAsset) {
	const char* d = ptrBuff;
	const char* assetPreFix = "asset://"; // 8 char 
	memcpy(ptrBuff,assetPreFix,8);
	ptrBuff += 8;
	size_t len = strlen(prefixLessAsset) + 1; // include 0.
	memcpy(ptrBuff,prefixLessAsset, len);
	ptrBuff += len;

	klb_assertNull((ptrBuff <= &tmpBuff[TMP_COMPOSITE_ASSET_STR_BUFFSIZE]), "Temp composite string buffer overflow");
	return d;
}

const char* CKLBCompositeAsset::formatI(s32 i) {
	const char* pRes = ptrBuff;
	sprintf(ptrBuff,"%i",i);
	size_t len = strlen(ptrBuff);
	ptrBuff += len + 1;
	return pRes;
}

const char* CKLBCompositeAsset::formatLL(s64 i) {
	const char* pRes = ptrBuff;
	sprintf(ptrBuff, "%lld", i);
	size_t len = strlen(ptrBuff);
	ptrBuff += len + 1;
	return pRes;
}

const char* CKLBCompositeAsset::formatB(bool b) {
	if (b) {
		return "true";
	} else {
		return "false";
	}
}

const char* CKLBCompositeAsset::formatF(float f) {
	const char* pRes = ptrBuff;
	sprintf(ptrBuff,"%f",f);
	size_t len = strlen(ptrBuff);
	ptrBuff += len + 1;
	return pRes;
}

const char* CKLBCompositeAsset::addString(const char* source, u32 len) {
	const char* res = ptrBuff;
	memcpy(ptrBuff,source,len);
	ptrBuff[len] = 0;
	ptrBuff += len + 1;
	return res;
}

s32 CKLBCompositeAsset::filterDBInt(CKLBCompositeAsset::STRINGENTRY* input, s32 original) {
	s32 result = original;
	if (input) {
		char prefix = input->string[0];
		if (prefix == '@') {
			return CKLBLuaEnv::getInstance().getGlobalInt(input->string + 1);
		}
		if (prefix == '$') {
			s32 index = m_pSource->getFieldIndex(input->string + 1);
			if (index < 0) {
				klb_assertAlways("DB Field Name %s not found.", input);
			} else if (m_pSource->getFieldType(index) != IDataSource::TYPE_INT) {
				klb_assertAlways("Invalid Record Field Type.");
			} else {
				result = m_pSource->getAsInt(index);
			}
		}
	}
	return result;
}

void
CKLBCompositeAsset::getDesignRect(u32 index, const char*& name, CKLBNode*& node,
	float& x, float& y, float& width, float& height) const
{
	name   = s_designRectNames[index];
	node   = s_designRectNodes[index];
	x      = s_designRects[index].x;
	y      = s_designRects[index].y;
	width  = s_designRects[index].width;
	height = s_designRects[index].height;
}

void
CKLBCompositeAsset::setFormSize(u32 width, u32 height)
{
	m_formWidth  = width;
	m_formHeight = height;
}

u32
CKLBCompositeAsset::getDesignRectCount() const
{
	return m_designRectCount;
}

float CKLBCompositeAsset::filterDBFloat(CKLBCompositeAsset::STRINGENTRY* input, float original) {
	float result = original;
	if (input) {
		char prefix = input->string[0];
		if (prefix == '@') {
			return CKLBLuaEnv::getInstance().getGlobalFloat(input->string + 1);
		}
		if (prefix == '$') {
			s32 index = m_pSource->getFieldIndex(input->string + 1);
			if (index < 0) {
				klb_assertAlways("DB Field Name %s not found.", input);
			} else if (m_pSource->getFieldType(index) != IDataSource::TYPE_FLOAT) {
				klb_assertAlways("Invalid Record Field Type.");
			} else {
				result = m_pSource->getAsFloat(index);
			}
		}
	}
	return result;
}

const char* CKLBCompositeAsset::filterDB(const char* input, bool* filtered) {
	if (!input) {
		return NULL;
	}
	if (m_pSource && (input[0] == '$')) {
		if (filtered) {
			*filtered = true;
		}
		input++;
		s32 index = m_pSource->getFieldIndex(input);
		switch (m_pSource->getFieldType(index)) {
		case IDataSource::TYPE_INT:
			{
				s64 value = m_pSource->getAsInt(index);
				const char* result = ptrBuff;
				sprintf(ptrBuff, "%lld", value);
				ptrBuff += strlen(ptrBuff) + 1;
				return result;
			}
		case IDataSource::TYPE_STR:
			{
				u32 length;
				const char* source = m_pSource->getAsString(index, length);
				return addString(source, length);
			}
		case IDataSource::TYPE_BOOL:
			return formatB(m_pSource->getAsBool(index));
		case IDataSource::TYPE_FLOAT:
			return formatF(static_cast<float>(m_pSource->getAsBool(index)));
		case IDataSource::TYPE_NIL:
		case IDataSource::TYPE_BLOB:
			return NULL;
		default:
			klb_assertAlways("Invalid Record Field Type.");
		}
	}
	if (input[0] == '@') {
		return CKLBLuaEnv::getInstance().getGlobalString(input + 1);
	}

	return input;
}


void CKLBCompositeAsset::setRecord(BaseRealDataProducer* pSource) {
	m_pSource	= pSource;
}

void CKLBCompositeAsset::setupTask(CKLBInnerDef* templateDef, CKLBUITask* tsk) {
	if (tsk) {
		tsk->setVisible(templateDef->visible);
		tsk->setRecordID(m_recordID);
		if (templateDef->name) {
			tsk->getNode()->setName(templateDef->name->string);
		}
	}
}

bool CKLBCompositeAsset::createTaskNode(CKLBInnerDef* /*templateDef*/, CKLBNode* parent,
	CKLBUITask* task, CKLBNode*& node)
{
	CKLBNode* taskNode = NULL;
	if (task) {
		taskNode = KLBNEW(CKLBNode);
		if (taskNode) {
			taskNode->setUITask(task);
			parent->addNode(taskNode, 0);
		} else {
			task->kill();
		}
	}
	node = taskNode;
	return taskNode != NULL;
}

void CKLBCompositeAsset::setupNode(CKLBInnerDef* templateDef, CKLBNode* node) {
	if (node) {
		node->setVisible(templateDef->visible);
		if (templateDef->name) {
			node->setName(templateDef->name->string);
		}
	}
}

bool CKLBCompositeAsset::createSubTreeRecursive(u16 groupID, CKLBUITask* pParentTask, CKLBNode* parent, CKLBInnerDef* templateDef, u32 priorityOffset) {
	bool res					= true; // Success by default.
	CKLBNode* pNode				= NULL;
	CKLBAssetManager& pAssetMgr = CKLBAssetManager::getInstance();
	CKLBAsset* pAsset[____END_ASSET];

	// Flag to avoid composite refering to itself.
	m_recCount = 1;

	for (int n=0; n < ____END_ASSET; n++) {
		pAsset[n] = NULL;
		if (templateDef->assets[n]) {
			bool filtered = false;
			pAsset[n] = templateDef->assets[n]->assetCache;
			if (pAsset[n] == NULL) {
				if ((n < DOWNAUDIO_ASSET) || (templateDef->classID == SCORE_CLASSID)) {
					const char* name = filterDB(templateDef->assets[n]->string,&filtered);
					CKLBAbstractAsset* lAsset = NULL;
					u16 assetID = pAssetMgr.getAssetIDFromName(name, 0, 0, &lAsset);
					if (!lAsset) {
						lAsset = pAssetMgr.getAsset(assetID);
					}
					if (lAsset && (lAsset->getAssetType() == ASSET_TEXTURE)) {
						// Extract Image from texture.
						if (!filtered) {
							lAsset->incrementRefCount();	// Later we increment image but we do not care.
						}
						lAsset = ((CKLBTextureAsset*)lAsset)->getImage(name);
					}

					if (lAsset && (lAsset->getAssetType() & HAS_CREATENODE)) {
						pAsset[n] = (CKLBAsset*)lAsset;
						if ((lAsset->getAssetType() != ASSET_IMAGE) && (!filtered)) {
							pAsset[n]->incrementRefCount();
						}
						if (!filtered) {
							templateDef->assets[n]->assetCache = pAsset[n];
						}
					}
				} else if (n >= DOWNAUDIO_ASSET) {
					resetTmpBuff();
					CKLBAbstractAsset* lAsset = pAssetMgr.loadAssetByFileName(addAssetPrefix(filterDB(templateDef->assets[n]->string,&filtered)), pAssetMgr.getPlugin('A'), true);
					if (lAsset && (lAsset->getAssetType() == ASSET_AUDIO)) {
						CKLBAudioAsset* pAudio = (CKLBAudioAsset*)lAsset;
						if (!pAudio->preLoad()) {
							klb_assertAlways("Failed preloading audio");
						}
						if (!filtered) {
							lAsset->incrementRefCount();	// Later we increment image but we do not care.
							templateDef->assets[n]->assetCache = (CKLBAsset*)lAsset;
						}
						pAsset[n] = (CKLBAsset*)lAsset;
					}
				}
			}
		}
	}

	u32 newPrio = priorityOffset + templateDef->priority;
	resetTmpBuff();

	SCompositeLayoutRecord& layout = g_layoutRecords[m_slotCurrent];
	if (templateDef->classID == FORMAT_RECT_CLASSID) {
		const float marginLeft   = templateDef->getLayoutValue(CKLBInnerDef::LAYOUT_CLIP_X, layout.width);
		const float marginTop    = templateDef->getLayoutValue(CKLBInnerDef::LAYOUT_CLIP_Y, layout.height);
		const float marginRight  = templateDef->getLayoutValue(CKLBInnerDef::LAYOUT_CLIP_WIDTH, layout.width);
		const float marginBottom = templateDef->getLayoutValue(CKLBInnerDef::LAYOUT_CLIP_HEIGHT, layout.height);

		layout.width  -= marginLeft + marginRight;
		layout.x      += marginLeft;
		layout.height -= marginTop + marginBottom;
		layout.y      += marginTop;

		if (templateDef->name) {
			klb_assertNull(m_designRectCount < 20,
				"reached limit of registerable NAMES design rect, please use Toboggan and remove name from rect you do not need.");
			m_rectXYWH->x = layout.x;
			m_rectXYWH->y = layout.y;
			m_rectXYWH->width = layout.width;
			m_rectXYWH->height = layout.height;
			*m_rectName++ = templateDef->name->string;
			*m_rectNode++ = parent;
			m_rectXYWH++;
			m_designRectCount++;
		}
	}

	const u32 childSlotBase = m_slotBase;
	CKLBInnerDef* child = templateDef->sub;
	s32 flowItemCount = 0;
	float dockedWidth = 0.0f;
	float dockedHeight = 0.0f;
	float contentWidth = 0.0f;
	float contentHeight = 0.0f;
	u32 childSlot = childSlotBase;

	while (child) {
		SCompositeLayoutRecord& childLayout = g_layoutRecords[childSlot++];
		childLayout.isRect = child->classID == FORMAT_RECT_CLASSID;

		float childWidth = 0.0f;
		float childHeight = 0.0f;
		bool isFlowItem = false;

		if (childLayout.isRect) {
			switch (child->flag[2]) {
			case DOCK_LEFT:
			case DOCK_RIGHT:
				if (((child->layoutInfo >> 4) & 3) != 2) {
					dockedWidth += child->getLayoutValue(CKLBInnerDef::LAYOUT_WIDTH);
				}
				break;
			case DOCK_TOP:
			case DOCK_BOTTOM:
				if (((child->layoutInfo >> 6) & 3) != 2) {
					dockedHeight += child->getLayoutValue(CKLBInnerDef::LAYOUT_HEIGHT);
				}
				break;
			case DOCK_NONE:
				childWidth = child->getLayoutValue(CKLBInnerDef::LAYOUT_WIDTH, layout.width);
				childHeight = child->getLayoutValue(CKLBInnerDef::LAYOUT_HEIGHT, layout.height);
				childLayout.width = childWidth;
				childLayout.height = childHeight;

				float childX = child->getLayoutValue(CKLBInnerDef::LAYOUT_X);
				float childY = child->getLayoutValue(CKLBInnerDef::LAYOUT_Y);
				if (child->anchor & 1) {
					childX = layout.width - (childX + childWidth);
				}
				if (child->anchor & 2) {
					childY = layout.height - (childY + childHeight);
				}
				childLayout.x = layout.x + childX;
				childLayout.y = layout.y + childY;
				isFlowItem = true;
				break;
			}
		} else {
			childWidth = child->getLayoutValue(CKLBInnerDef::LAYOUT_WIDTH, layout.width);
			childHeight = child->getLayoutValue(CKLBInnerDef::LAYOUT_HEIGHT, layout.height);
			childLayout.width = childWidth;
			childLayout.height = childHeight;
			isFlowItem = true;
		}

		if (isFlowItem) {
			flowItemCount++;
			contentWidth += childWidth;
			contentHeight += childHeight;
		}
		child = child->next;
	}

	float flowAlignment = 0.5f;
	switch (templateDef->layoutDir) {
	case 5:
	case 7:
		flowItemCount++;
		flowAlignment = 1.0f;
		break;
	case 6:
	case 8:
		break;
	default:
		flowItemCount--;
		flowAlignment = 0.0f;
		break;
	}
	const float inverseFlowCount = flowItemCount ? (1.0f / flowItemCount) : 0.0f;

	const float borderX = CKLBDrawResource::getInstance().borderX();
	const float borderY = CKLBDrawResource::getInstance().borderY();
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();

	child = templateDef->sub;
	childSlot = childSlotBase;
	if (child) {
		float runLeft = layout.x;
		float runTop = layout.y;
		float runRight = layout.x + layout.width;
		float runBottom = layout.y + layout.height;
		const float availableWidth = runRight - dockedWidth;
		const float availableHeight = runBottom - dockedHeight;
		const float horizontalGap = (layout.width - contentWidth) * inverseFlowCount;
		const float verticalGap = (layout.height - contentHeight) * inverseFlowCount;
		float flowX = flowAlignment * horizontalGap;
		float flowY = flowAlignment * verticalGap;
		SCompositeLayoutRecord* fillLayout = NULL;

		while (child) {
			SCompositeLayoutRecord& childLayout = g_layoutRecords[childSlot++];
			bool docked = (child->classID == FORMAT_RECT_CLASSID);

			if (docked) {
				switch (child->flag[2]) {
				case DOCK_LEFT:
				case DOCK_RIGHT:
					childLayout.width = child->getLayoutValue(CKLBInnerDef::LAYOUT_WIDTH, availableWidth);
					childLayout.height = runBottom - layout.y;
					childLayout.y = layout.y;
					if (child->flag[2] == DOCK_LEFT) {
						childLayout.x = runLeft;
						runLeft += childLayout.width;
					} else {
						runRight -= childLayout.width;
						childLayout.x = runRight;
					}
					break;

				case DOCK_TOP:
				case DOCK_BOTTOM:
					childLayout.width = runRight - runLeft;
					childLayout.height = child->getLayoutValue(CKLBInnerDef::LAYOUT_HEIGHT, availableHeight);
					childLayout.x = runLeft;
					if (child->flag[2] == DOCK_TOP) {
						childLayout.y = runTop;
						runTop += childLayout.height;
					} else {
						runBottom -= childLayout.height;
						childLayout.y = runBottom;
					}
					break;

				case DOCK_FILL:
					if (fillLayout) {
						klb_assertAlways("Multiple rect with FILL, should be only one");
					}
					fillLayout = &childLayout;
					break;

				case DOCK_NONE:
					docked = false;
					break;
				}
			}

			if (!docked) {
				float baseX = layout.isRect ? layout.x : 0.0f;
				float baseY = layout.isRect ? layout.y : 0.0f;

				if (child->classID == ANIMNODE_CLASSID) {
					childLayout.x = baseX + child->getLayoutValue(CKLBInnerDef::LAYOUT_X);
					childLayout.y = baseY + child->getLayoutValue(CKLBInnerDef::LAYOUT_Y);
					childLayout.width = layout.width;
					childLayout.height = layout.height;
				} else if (child->anchor == 4) {
					childLayout.x = layout.x;
					childLayout.y = layout.y;
					childLayout.width = layout.width;
					childLayout.height = layout.height;
				} else {
					float childX = child->getLayoutValue(CKLBInnerDef::LAYOUT_X);
					float childY = child->getLayoutValue(CKLBInnerDef::LAYOUT_Y);

					float lowInsetX = (child->anchor & 8) ? borderX : 0.0f;
					float highInsetX = lowInsetX;
					if (child->anchor & 0x20) {
						lowInsetX = draw.unsafeArea(0) + borderX;
						highInsetX = draw.unsafeArea(1) + borderX;
					}

					float lowInsetY = (child->anchor & 0x10) ? borderY : 0.0f;
					float highInsetY = lowInsetY;
					if (child->anchor & 0x40) {
						lowInsetY = draw.unsafeArea(2) + borderY;
						highInsetY = draw.unsafeArea(3) + borderY;
					}

					if (child->anchor & 1) {
						childX = highInsetX + layout.width - (childX + childLayout.width);
					} else {
						childX -= lowInsetX;
					}
					if (child->anchor & 2) {
						childY = highInsetY + layout.height - (childY + childLayout.height);
					} else {
						childY -= lowInsetY;
					}
					childLayout.x = baseX + childX;
					childLayout.y = baseY + childY;

					if ((templateDef->classID == FORMAT_RECT_CLASSID) && templateDef->layoutDir) {
						bool horizontalFlow = true;
						switch (templateDef->layoutDir) {
						case 1:
							childLayout.x = baseX + flowX;
							flowX += childLayout.width;
							break;
						case 2:
						case 5:
						case 6:
							childLayout.x = baseX + flowX;
							flowX += childLayout.width + horizontalGap;
							break;
						case 3:
							childLayout.y = baseY + flowY;
							flowY += childLayout.height;
							horizontalFlow = false;
							break;
						case 4:
						case 7:
						case 8:
							childLayout.y = baseY + flowY;
							flowY += childLayout.height + verticalGap;
							horizontalFlow = false;
							break;
						}

						switch (templateDef->flag[0]) {
						case 1:
							if (horizontalFlow) {
								childLayout.y = layout.y + layout.height - childLayout.height;
							} else {
								childLayout.x = layout.x;
							}
							break;
						case 2:
							if (horizontalFlow) {
								childLayout.y = layout.y;
							} else {
								childLayout.x = layout.x + layout.width - childLayout.width;
							}
							break;
						case 3:
							if (horizontalFlow) {
								childLayout.y = layout.y + (layout.height - childLayout.height) * 0.5f;
							} else {
								childLayout.x = layout.x + (layout.width - childLayout.width) * 0.5f;
							}
							break;
						case 4:
							if (horizontalFlow) {
								childLayout.y = layout.y;
								childLayout.height = layout.height;
							} else {
								childLayout.x = layout.x;
								childLayout.width = layout.width;
							}
							break;
						}
					}
				}
			}

			child = child->next;
		}

		if (fillLayout) {
			fillLayout->width = runRight - runLeft;
			fillLayout->height = runBottom - runTop;
			fillLayout->x = runLeft;
			fillLayout->y = runTop;
		}
	}

	klb_assert(
		   ((((s64)priorityOffset + (s64)templateDef->priority)>>32) != 0)
		|| ((((s64)priorityOffset + (s64)templateDef->priority)>>32) != -1), "Overflow or underflow");

	bool execNodeSetup = true;
	if (templateDef->classID != NONE_CLASSID) {

		switch (templateDef->classID) {
		case WEBVIEW_CLASSID:
			{
				const char* url = NULL;
				const char* cb  = NULL;

				if (templateDef->handler[1]) {
					url = filterDB(templateDef->handler[1]->string);
				}

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUIWebArea* tsk = CKLBUIWebArea::create(
					pParentTask,
					parent,
					templateDef->flag[0] ? true : false,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					url,	// URL
					cb		// Call back.
				);

				setupTask(templateDef,tsk);

				// Do not authorize creation of sub node for now.

				pNode = NULL;

				/*	Allow sub system to be created.
					----------------------------------

					pNode = lNode;
					pParentTask = tsk;
				 */
				res = (tsk != NULL);
			}
			break;
		case VIRTUALDOC_CLASSID:
			{
				const char* cb  = NULL;

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUIVirtualDoc* tsk = CKLBUIVirtualDoc::create(
					pParentTask,
					parent,
					newPrio, //templateDef->priority,
					layout.x,
					layout.y,
					templateDef->sw,
					templateDef->sh,
					layout.width,
					layout.height,
					templateDef->radioID,			// MAX COMMAND, memory reuse !
					templateDef->sx ? true : false,	// Vertical/Horizontal, memory reuse !
					cb
				);
				if(tsk) {
					tsk->setFitMode(templateDef->flag[1]);
				}


				//const char * fontName  = (templateDef->fontName) ? templateDef->fontName->string	: 0; // "default";
				//const char * labelText = (templateDef->text)     ? templateDef->text->string		: "";

				/*
				if (templateDef->text != 0) {
					tsk->startDocument();
					tsk->setFont(0, fontName , templateDef->fontSize);
					tsk->drawText(0, 0, filterDB(labelText), filterDBInt(templateDef->dbField[DB_VAL_COLOR], templateDef->color), 0);
					tsk->endDocument();
				} */

				setupTask(templateDef,tsk);

				pNode = NULL;

				res = (tsk != NULL);
			}
			break;
		case SCORE_CLASSID:
			{
				// Do not authorize creation of sub node for now.
				const char *	tex_table[10];
				for (int n = 0; n < 10; n++) {
					tex_table[n] = addAssetPrefix(templateDef->assets[n]->string);
				}

				CKLBUIScore* tsk = CKLBUIScore::create(
					pParentTask,
					parent,
					newPrio, //templateDef->priority,
					templateDef->priorityClipStart,
					layout.x,
					layout.y,
					tex_table,
					templateDef->sx,
					templateDef->sy,
					templateDef->radioID, 
					templateDef->flag[2] ? true : false,
					templateDef->flag[1] ? true : false,
					templateDef->flag[0],	// align
					(1 - templateDef->flag[3]) ? true : false	// countclip (0:true / 1:false)
				);

				tsk->setValue(templateDef->value);
				tsk->setColor(filterDBInt(templateDef->dbField[DB_VAL_COLOR], templateDef->color));

				if (templateDef->assets[DOT_ASSET]) {
					CKLBImageAsset* dotImage =
						(CKLBImageAsset*)templateDef->assets[DOT_ASSET]->assetCache;
					SKLBRect* dotSize = dotImage->getSize();
					s32 dotLeft = dotSize->m_iLeft;
					s32 dotRight = dotSize->m_iRight;
					s32 dotTop = dotSize->m_iTop;
					s32 dotBottom = dotSize->m_iBottom;
					const char* dotAsset =
						addAssetPrefix(templateDef->assets[DOT_ASSET]->string);

					if (templateDef->sx) {
						tsk->setDot(dotAsset, dotRight - dotLeft, 0);
					} else {
						tsk->setDot(dotAsset, 0, dotBottom - dotTop);
					}
				}

				setupTask(templateDef,tsk);

				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case PROGRESSBAR_CLASSID:
			{
				CKLBUIProgressBar* tsk = CKLBUIProgressBar::create(
					pParentTask,
					parent,
					newPrio, // templateDef->priority,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL,
					templateDef->assets[1] ? addAssetPrefix(filterDB(templateDef->assets[1]->string)) : NULL,
					templateDef->clipw,
					templateDef->cliph,
					templateDef->radioID <= 0 ? 0 : templateDef->radioID,
					templateDef->sx ? true : false
				);

				tsk->setValue((filterDBInt(templateDef->dbField[DB_VAL_VALUE], templateDef->value) / 100.0f));

				setupTask(templateDef,tsk);

				// Do not authorize creation of sub node for now.
				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case SCROLLBAR_CLASSID:
			{
				const char* cb  = NULL;

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUIScrollBar* tsk = CKLBUIScrollBar::create(pParentTask,parent,newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					filterDBInt(templateDef->dbField[DB_VAR_MIN],   templateDef->variable[VAR_MIN]),
					filterDBInt(templateDef->dbField[DB_VAR_MAX],   templateDef->variable[VAR_MAX]),
					filterDBInt(templateDef->dbField[DB_VAL_VALUE], templateDef->value),			//pos
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL,
					
					templateDef->variable[VAR_SLIDERSIZE],
					templateDef->variable[VAR_MIN_SLIDERSIZE],
					cb,
					filterDBInt(templateDef->dbField[DB_VAL_COLOR], templateDef->color),
					templateDef->variable[VAR_SELECTCOLOR],
					templateDef->sx ? true : false,					// Vertical/Horizontal
					templateDef->flag[2] ? true : false
				);

				setupTask(templateDef,tsk);

				pNode = NULL;
				res	  = (tsk != NULL);
			}
			break;
		case CANVAS_CLASSID:
		case TILEDCANVAS_CLASSID:
			{
				const char* cb  = NULL;

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUICanvas* tsk = CKLBUICanvas::create(pParentTask,parent,newPrio,
					layout.x,
					layout.y,
					templateDef->variable[VAR_MAXVERTEX],
					templateDef->variable[VAR_MAXINDEX],
					cb
				);

				if (templateDef->classID == TILEDCANVAS_CLASSID) {
					tsk->setTiledRect((u32)layout.width, (u32)layout.height, templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL, (u32)0xFF);
					tsk->freeze(true);
				}

				setupTask(templateDef,tsk);

				pNode = NULL;
				res	  = (tsk != NULL);
			}
			break;
		case SCALE9_CLASSID:
			{
				CKLBUIScale9* tsk = CKLBUIScale9::create(
					pParentTask,parent,newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL
				);


				setupTask(templateDef,tsk);

				pNode = NULL;
				res	  = (tsk != NULL);
			}
			break;
		case DRAGICON_CLASSID:
			{
				const char* cb = NULL;

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUIDragIcon::AREA	tapArea;

				resetTmpBuff();

				tapArea.x		= templateDef->sx;
				tapArea.y		= templateDef->sy;
				tapArea.width	= templateDef->sw;
				tapArea.height	= templateDef->sh;

				CKLBUIDragIcon* tsk = CKLBUIDragIcon::create(
					pParentTask,
					parent,
					newPrio, // templateDef->priority,
					layout.x,
					layout.y,
					&tapArea,
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL,
					templateDef->assets[1] ? addAssetPrefix(filterDB(templateDef->assets[1]->string)) : NULL,
					templateDef->priorityClipStart,
					templateDef->rotation,
					templateDef->clipx,
					templateDef->clipy, 
					cb,
					templateDef->flag[0]
				);

				setupTask(templateDef, tsk);

				// Do not authorize creation of sub node for now.
				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case LIST_CLASSID:
			{
				const char* cb = NULL;

				if (templateDef->handler[0]) {
					cb	= templateDef->handler[0]->string;
				}

				CKLBUIList * tsk = CKLBUIList::create(
					pParentTask,
					parent,
					priorityOffset + templateDef->priority,
					priorityOffset + templateDef->priorityClipEnd,
					layout.x,
					layout.y,
					templateDef->clipw,
					templateDef->cliph,
					((templateDef->sx == 1) ?
						layout.height : layout.width),
					templateDef->sx ? true : false,		// Vertical/Horizontal
					cb,									//  
					templateDef->flag[0] ? CKLBUIList::LIST_FLAG_BOTTOM : 0
				);

				if (templateDef->spline) {
					tsk->setSplineLayout(templateDef->spline, templateDef->splineMask, templateDef->splineLength);
				}

				setupTask(templateDef,tsk);

				if(tsk && templateDef->propertyBag) {
					CKLBPropertyBag * pProp = templateDef->propertyBag;
					tsk->selectScrollMgrByProperty(pProp);

					if(pProp->getPropertyBool("use_scrollBar")) {
						tsk->useScrollBarByProperty(pProp, priorityOffset);
					}
				}

				// Do not authorize creation of sub node for now.
				pNode   = NULL;
				res     = (tsk != NULL);
			}
			break;

		case PIECHART_CLASSID:
			{
				CKLBUIPieChart * tsk = CKLBUIPieChart::create(
					pParentTask,
					parent,
					newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL,
					filterDBFloat(templateDef->dbField[DB_VAR_STARTANGLE], templateDef->xscale), // Start use xscale to save memory.
					filterDBFloat(templateDef->dbField[DB_VAR_ENDANGLE], templateDef->yscale),	 // End use yscale to save memory.
					(templateDef->radioID <= 0) ? 0 : templateDef->radioID,
					filterDBInt(templateDef->dbField[DB_VAL_VALUE], templateDef->value) / 100.0f
				);

				setupTask(templateDef,tsk);

				// Do not authorize creation of sub node for now.
				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case VARITEM_CLASSID:
			{
				CKLBUIVariableItem * tsk = CKLBUIVariableItem::create(
					pParentTask,
					parent,
					newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					templateDef->assets[0] ? addAssetPrefix(filterDB(templateDef->assets[0]->string)) : NULL
					);

				if(tsk && templateDef->flag[0]) {
					tsk->changeUV(	templateDef->clipx,
									templateDef->clipy,
									templateDef->clipw,
									templateDef->cliph);
				}

				setupTask(templateDef,tsk);

				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case GROUP_CLASSID:
			{
				CKLBUIGroup * tsk = CKLBUIGroup::create(
					pParentTask, 
					parent,
					newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height);

				setupTask(templateDef,tsk);

				pNode = NULL;
				res = (tsk != NULL);
			}
			break;

		case ANIMNODE_CLASSID:
			{
				CKLBSplineNode* pNewNode = KLBNEW(CKLBSplineNode);
				if (pNewNode) {
					parent->addNode(pNewNode);
					pNode = pNewNode;
					pNewNode->setTag(templateDef);
					pNewNode->setTranslate(layout.x, layout.y);
					pNewNode->setScaleRotation(templateDef->xscale, templateDef->yscale,templateDef->rotation);
				}
			}
			break;
		case TEXTBOX_CLASSID:
			{
				const char * placeholder = (templateDef->placeholder) ? templateDef->placeholder->string : 0;
				int maxlen = templateDef->clipw;
				int chartype = (templateDef->cliph) ? templateDef->cliph : (IWidget::TXCH_7BIT_ASCII | IWidget::TXCH_UTF8);

				const char* txt = NULL;
				if (templateDef->text) {
					txt = filterDB(templateDef->text->string);
				}

				const char* handler = NULL;
				if(templateDef->handler[0]) {
					handler = templateDef->handler[0]->string;
				}

				CKLBUITextInput* tsk = CKLBUITextInput::create(pParentTask,parent,
					templateDef->flag[0] ? true : false,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					txt,
					handler,
					templateDef->id,
					maxlen,
					chartype
				);

				if (tsk) {
					if(templateDef->name) {
						tsk->getNode()->setName(templateDef->name->string);
					}

					if(placeholder) {
						tsk->setPlaceHolder(placeholder);
					}

					setupTask(templateDef,tsk);
				}

				// Do not authorize creation of sub node for now.
				pNode = NULL;
				res = (tsk != NULL);
			}
			break;
		case LABEL_CLASSID:
			{
				const char * fontname  = (templateDef->fontName) ? templateDef->fontName->string : 0; // "default";
				const char * labelText = (templateDef->text)     ? templateDef->text->string		: "";
				u32 color = filterDBInt(templateDef->dbField[DB_VAL_COLOR], templateDef->color);
				CKLBUILabel* tsk = CKLBUILabel::create(
					pParentTask,
					parent,
					newPrio,
					layout.x,
					layout.y,
					layout.width,
					layout.height,
					color >> 24,
					color & 0xFFFFFF,
					fontname,
					templateDef->fontSize,
					filterDB(labelText),
					templateDef->flag[0],
					templateDef->shadowBlur,
					templateDef->shadowColor,
					static_cast<s8>(templateDef->shadowDX),
					static_cast<s8>(templateDef->shadowDY),
					(templateDef->shadowBlur >= 3.0f) ? 2 : 1,
					templateDef->flag[1]
				);

				if (tsk) {
					setupTask(templateDef, tsk);
					pNode = tsk->getNode();
				} else {
					res = false;
				}
			}
			break;
		case BUTTON_CLASSID:
		case BUTTON_SCALE9_CLASSID:
		case CHECK_CLASSID:
			{
				CKLBUISelectable* pElement = (templateDef->classID == BUTTON_SCALE9_CLASSID)
					? static_cast<CKLBUISelectable*>(KLBNEW(CKLBUIScale9Btn))
					: KLBNEW(CKLBUISelectable);
				if (pElement && pElement->init(newPrio)) {
					pElement->setPriority(newPrio);
					if (templateDef->classID == CHECK_CLASSID) {
						pElement->setStick(true);
						if (templateDef->radioID != -1) {
							pElement->setRadio(templateDef->radioID);
						}
					}

					parent->addNode(pElement, 0);
					pElement->setScriptable(pParentTask);
					pElement->setTranslate(layout.x, layout.y);	// Related to container issue.
					pElement->setScaleRotation(templateDef->xscale, templateDef->yscale,templateDef->rotation);

					pElement->setClickLeft	(templateDef->sx);
					pElement->setClickTop	(templateDef->sy);
					pElement->setClickWidth	(templateDef->sw);
					pElement->setClickHeight(templateDef->sh);

					pElement->setAsset(pAsset[NORMAL_ASSET ],	CKLBUIElement::NORMAL_ASSET		);
					pElement->setAsset(pAsset[DISABLE_ASSET],	CKLBUIElement::DISABLED_ASSET	);
					pElement->setAsset(pAsset[FOCUS_ASSET  ],	CKLBUIElement::FOCUSED_ASSET	);
					pElement->setAsset(pAsset[PUSH_ASSET   ],	CKLBUIElement::PUSHED_ASSET		);
					
					pElement->setAudio((CKLBAudioAsset*)pAsset[DOWNAUDIO_ASSET], 0, (templateDef->volAudioDown / 100.0f));
					pElement->setAudio((CKLBAudioAsset*)pAsset[UPAUDIO_ASSET  ], 1, (templateDef->volAudioUp   / 100.0f));
                    
                    pElement->setMultiplyVolume( 0, CKLBLuaLibSOUND::getFormGlobalVolume() );
                    pElement->setMultiplyVolume( 1, CKLBLuaLibSOUND::getFormGlobalVolume() );

					pElement->setSticked(filterDBInt(templateDef->dbField[DB_VAR_CHECKED], templateDef->flag[0]) ? true : false);

					pElement->setEnabled(filterDBInt(templateDef->dbField[DB_VAR_ENABLE], templateDef->flag[2]) ? true : false);
					pElement->m_groupID = groupID;
					pElement->setRecordID(m_recordID);

					if (templateDef->classID == BUTTON_SCALE9_CLASSID) {
						CKLBUIScale9Btn* scale9Button =
							static_cast<CKLBUIScale9Btn*>(pElement);
						scale9Button->setTextAlign(templateDef->flag[0]);
						scale9Button->setClickLeft(0);
						scale9Button->setClickTop(0);
						scale9Button->setClickWidth((s32)layout.width);
						scale9Button->setClickHeight((s32)layout.height);

						const char* fontName = templateDef->fontName
							? templateDef->fontName->string
							: NULL;
						u32 textColor = filterDBInt(
							templateDef->dbField[DB_VAL_COLOR],
							templateDef->color);
						scale9Button->setFont(
							fontName,
							templateDef->fontSize,
							textColor);

						const char* text = templateDef->text
							? filterDB(templateDef->text->string)
							: NULL;
						scale9Button->setText(text);
						scale9Button->setTextShadow(
							templateDef->shadowColor,
							(s8)templateDef->shadowDX,
							(s8)templateDef->shadowDY,
							templateDef->shadowBlur,
							(templateDef->shadowBlur >= 3.0f) ? 1 : 2);
						scale9Button->rebuildText();
					}

					// Lua handler function
					if(templateDef->handler[0]) {
						pElement->setLuaFunction(templateDef->handler[0]->string);
					}
					pNode = pElement;
				} else {
					KLBDELETE(pElement);
					res = false;
				}
			}
			break;
		case CONTAINER_CLASSID:
			{
				CKLBUIContainer* pCont = KLBNEW(CKLBUIContainer);
				if (pCont && pCont->init()) {
					pCont->setScriptable(pParentTask);
					pCont->setPriority(newPrio);
					pCont->setScaleRotation(templateDef->xscale, templateDef->yscale,templateDef->rotation);
					parent->addNode(pCont, 0);
					pCont->setTranslate(layout.x, layout.y);
					pCont->setAsset(pAsset[NORMAL_ASSET ],	CKLBUIElement::NORMAL_ASSET		);
					
					pNode = pCont->getInnerNode();

					if (templateDef->cliph && templateDef->clipw) {
						if (templateDef->priorityClipStart != templateDef->priorityClipEnd) {
							CKLBRenderingManager& pMgr = CKLBRenderingManager::getInstance();
							CKLBRenderState* pClipStart = pMgr.allocateCommandState();
							CKLBRenderState* pClipEnd = pMgr.allocateCommandState();

							pClipStart->changeOrder(pMgr, templateDef->priorityClipStart + priorityOffset);
							pClipEnd->changeOrder(pMgr, templateDef->priorityClipEnd + priorityOffset);

							if (!templateDef->assets[DISABLE_ASSET]) {
								if (pNode->setRenderSlotCount(2) && pClipStart && pClipEnd) {

									klb_assert(
										   ((((s64)priorityOffset + (s64)templateDef->priorityClipStart)>>32) != 0)
										|| ((((s64)priorityOffset + (s64)templateDef->priorityClipStart)>>32) != -1), "Overflow or underflow");

									klb_assert(
										   ((((s64)priorityOffset + (s64)templateDef->priorityClipEnd)>>32) != 0)
										|| ((((s64)priorityOffset + (s64)templateDef->priorityClipEnd)>>32) != -1), "Overflow or underflow");

									pClipStart->setUse	(true,false,null);
									pClipEnd->setUse	(true,false,null);

									pClipStart->setScissor(true, templateDef->clipx,templateDef->clipy,templateDef->clipw,templateDef->cliph);
									pClipEnd->setScissor(false);
							
									pNode->setRender(pClipStart,0);
									pNode->setRender(pClipEnd  ,1);

									pCont->setClipHandle(CKLBUISystem::registerClip(pClipStart, pClipEnd));
								} else {
									klb_assertAlways("Failed allocation");
									if (pClipEnd)	{ pMgr.releaseCommand(pClipEnd);	}
									if (pClipStart) { pMgr.releaseCommand(pClipStart);	}
								}
							} else {
								// Sprite
								CKLBDynSprite* pRenderMask = pMgr.allocateCommandDynSprite(6,6, templateDef->priorityClipStart + priorityOffset+1);
								CKLBRenderState* pDoMasking  = pMgr.allocateCommandState();

								if (pNode->setRenderSlotCount(3) && pClipStart && pClipEnd && pRenderMask && pDoMasking) {
									// Base + 1 
									// pRenderMask
									// Base + 2
									pDoMasking->changeOrder	(pMgr, templateDef->priorityClipStart + priorityOffset + 2);
								
									//
									// 1. Start Clipping
									//

									// Clear Z Buffer with 1.0
									pClipStart->setUse(true,true,null);	// Assign new state and execute clear command.
									// Setup the clear command.
									pClipStart->setClearDepth(true, 1.0f);
									pClipStart->setDepthRange(0.0f, 1.0f);

									SRenderState* pState = pClipStart->getState();
									// Set Z Write Always
									pState->setDepthState(true,true,SRenderState::ALWAYS);
									// [Opt] Alpha Test
									// pState->enableAlphaTest(value,SRenderState::GEQUAL);

									//
									// 2. Render Mask
									//
									pRenderMask->importXYUV	((CKLBImageAsset*)pAsset[DISABLE_ASSET]);
									//u16* pIdx = pRenderMask->getSrcIndexBuffer();
									pRenderMask->setVertexXY(0, 0.0f              , 0.0f);
								
									pRenderMask->setVertexXY(1, templateDef->clipw, 0.0f);
									pRenderMask->setVertexXY(3, templateDef->clipw, 0.0f);

									pRenderMask->setVertexXY(4, templateDef->clipw, templateDef->cliph);

									pRenderMask->setVertexXY(2, 0.0f              , templateDef->cliph);
									pRenderMask->setVertexXY(5, 0.0f              , templateDef->cliph);

									//
									// 3. Enable Depth Test, Disable Z Write, [Opt Alpha Test Disable]
									//
									pState = pDoMasking->getState();
									pDoMasking->setUse(true,true,null); // Use render state.
									pDoMasking->setDepthRange(0.5f, 1.0f);
									// Draw only pixel <= 0.0f
									pState->setDepthState(false, true, SRenderState::GEQUAL);
									// [Opt] pState->disableAlphaTest();

									//
									// 4. Disable Depth Test
									//
									pClipEnd->setUse(true,false,null);
									pState = pClipEnd->getState();
									pState->setDepthState(false, false, SRenderState::ALWAYS);

									pNode->setRender(pClipStart ,0);
									pCont->setRender(pRenderMask,0); // Transform is upper node.
									pNode->setRender(pDoMasking ,1);
									pNode->setRender(pClipEnd   ,2);
								} else {
									klb_assertAlways("Failed allocation");
									if (pClipEnd)	{ pMgr.releaseCommand(pClipEnd);	}
									if (pClipStart) { pMgr.releaseCommand(pClipStart);	}
									if (pDoMasking) { pMgr.releaseCommand(pDoMasking);	}
									if (pRenderMask){ pMgr.releaseCommand(pRenderMask);	}
								}
							}
						}
					}

					setupNode(templateDef, pCont);
					execNodeSetup  = false;
				} else {
					KLBDELETE(pCont);
					res = false;
				}
			}
			break;
		case FORMAT_RECT_CLASSID:
			// Layout rectangles do not create a node. Their children remain
			// attached to the node which owns the rectangle.
			pNode = parent;
			execNodeSetup = false;
			break;
		case SHADER_CLASSID:
			{
				const char* assetName = "";
				if (templateDef->text) {
					assetName = filterDB(templateDef->text->string);
				}
				templateDef->visible = false;
				if (assetName && !assetName[0]) {
					assetName = NULL;
				}

				if (parent) {
					CKLBShaderTask* task =
						CKLBShaderTask::create(pParentTask, assetName, newPrio);
					createTaskNode(templateDef, parent,
						reinterpret_cast<CKLBUITask*>(task), pNode);
					res = task != NULL;
				} else {
					pNode = NULL;
					res = false;
				}
			}
			break;
		case BUFFER_CLASSID:
			{
				const char* assetName = "";
				if (templateDef->text) {
					assetName = filterDB(templateDef->text->string);
				}
				templateDef->visible = false;
				if (assetName && !assetName[0]) {
					assetName = NULL;
				}

				if (parent) {
					CKLBRenderBufferTask* task = CKLBRenderBufferTask::create(
						pParentTask, assetName, newPrio, templateDef->value);
					createTaskNode(templateDef, parent,
						reinterpret_cast<CKLBUITask*>(task), pNode);
					res = task != NULL;
				} else {
					pNode = NULL;
					res = false;
				}
			}
			break;
		}
	} else {
		//
		// Composite any
		//
		if (pAsset[NORMAL_ASSET]) {
			pNode = pAsset[NORMAL_ASSET]->createSubTree(newPrio);
		} else {
			// No Asset & No Class = simple node
			pNode = KLBNEW(CKLBNode);
		}

		pNode->resetAsInternalNode();

		// Recursively create the composite.
		if (pNode) {
			pNode->setTranslate(layout.x, layout.y);
			pNode->setScaleRotation(templateDef->xscale, templateDef->yscale,templateDef->rotation);
			parent->addNode(pNode);
		} else {
			res = false;
		}
	}

	// Object name
	if (execNodeSetup) {
		setupNode(templateDef, pNode);
	}

	if(templateDef->propertyBag) {
		CKLBPropertyBag::releasePropertyBag(templateDef->propertyBag);
		templateDef->propertyBag = 0;
	}

	if (res && pNode) {
		m_slotBase = childSlot;
		CKLBInnerDef* subList = templateDef->sub;
		u32 currentChildSlot = childSlotBase;
		while (subList) {
			m_slotCurrent = currentChildSlot++;
			res &= createSubTreeRecursive(groupID, pParentTask, pNode, subList, priorityOffset);
			subList = subList->next;
		}
	}
	m_slotBase = childSlotBase;

	/*
	if (!res) {
		// Do nothing : top destruction will destroy all nodes.		
	}*/

	return res;
}
