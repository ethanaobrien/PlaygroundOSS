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
#include "CKLBUILabel.h"
;
// Command Values
enum {
	UI_LABEL_SET_TEXT,
	UI_LABEL_SET_COLOR,
	UI_LABEL_SET_FONT,
	UI_LABEL_SET_POSITION,
	UI_LABEL_SET_ALIGNMENT,
	UI_LABEL_MARQUEEACTIVATE,
	UI_LABEL_MARQUEESETUP,
	UI_LABEL_SET_SHADOW,
	UI_LABEL_SET_TEXTELLIPSIS,
	UI_LABEL_SET_FIT,
	UI_LABEL_SET_SIZE,
	UI_LABEL_USENATIVE_FONT,

	MARQUEE_ONCE = 1,
	MARQUEE_LOOP,
	MARQUEE_PINGPONG_ONCE,
	MARQUEE_PINGPONG,

	LABEL_ALIGN_TOP_LEFT = 0,
	LABEL_ALIGN_TOP_CENTER,
	LABEL_ALIGN_TOP_RIGHT,
	LABEL_ALIGN_MIDDLE_LEFT = 4,
	LABEL_ALIGN_MIDDLE_CENTER,
	LABEL_ALIGN_MIDDLE_RIGHT,
	LABEL_ALIGN_BOTTOM_LEFT = 8,
	LABEL_ALIGN_BOTTOM_CENTER,
	LABEL_ALIGN_BOTTOM_RIGHT
};

static IFactory::DEFCMD cmd[] = {
	{"UI_LABEL_SET_TEXT",		UI_LABEL_SET_TEXT },
	{"UI_LABEL_SET_COLOR",		UI_LABEL_SET_COLOR },
	{"UI_LABEL_SET_FONT",		UI_LABEL_SET_FONT },
	{"UI_LABEL_SET_POSITION",	UI_LABEL_SET_POSITION },
	{"UI_LABEL_SET_ALIGNMENT",	UI_LABEL_SET_ALIGNMENT },
	{"UI_LABEL_MARQUEEACTIVATE",	UI_LABEL_MARQUEEACTIVATE },
	{"UI_LABEL_MARQUEESETUP",	UI_LABEL_MARQUEESETUP },
	{"UI_LABEL_SET_SHADOW",		UI_LABEL_SET_SHADOW },
	{"UI_LABEL_SET_TEXTELLIPSIS",	UI_LABEL_SET_TEXTELLIPSIS },
	{"UI_LABEL_SET_FIT",			UI_LABEL_SET_FIT },
	{"UI_LABEL_SET_SIZE",			UI_LABEL_SET_SIZE },
	{"UI_LABEL_USENATIVE_FONT",	UI_LABEL_USENATIVE_FONT },

	{"MARQUEE_ONCE",				MARQUEE_ONCE },
	{"MARQUEE_LOOP",				MARQUEE_LOOP },
	{"MARQUEE_PINGPONG_ONCE",		MARQUEE_PINGPONG_ONCE },
	{"MARQUEE_PINGPONG",			MARQUEE_PINGPONG },

	{"LABEL_ALIGN_TOP_LEFT",		LABEL_ALIGN_TOP_LEFT },
	{"LABEL_ALIGN_TOP_CENTER",		LABEL_ALIGN_TOP_CENTER },
	{"LABEL_ALIGN_TOP_RIGHT",		LABEL_ALIGN_TOP_RIGHT },
	{"LABEL_ALIGN_MIDDLE_LEFT",		LABEL_ALIGN_MIDDLE_LEFT },
	{"LABEL_ALIGN_MIDDLE_CENTER",	LABEL_ALIGN_MIDDLE_CENTER },
	{"LABEL_ALIGN_MIDDLE_RIGHT",	LABEL_ALIGN_MIDDLE_RIGHT },
	{"LABEL_ALIGN_BOTTOM_LEFT",		LABEL_ALIGN_BOTTOM_LEFT },
	{"LABEL_ALIGN_BOTTOM_CENTER",	LABEL_ALIGN_BOTTOM_CENTER },
	{"LABEL_ALIGN_BOTTOM_RIGHT",	LABEL_ALIGN_BOTTOM_RIGHT },
	{0, 0}
};
static CKLBTaskFactory<CKLBUILabel> factory("UI_Label", CLS_KLBUILABEL, cmd);

// Allowed Property Keys
CKLBLuaPropTask::PROP_V2 CKLBUILabel::ms_propItems[] = {
	UI_BASE_PROP,
	{	"order",			UINTEGER,	(setBoolT)&CKLBUILabel::setOrder,		(getBoolT)&CKLBUILabel::getOrder,		0		},
	{	"font",				STRING,		(setBoolT)&CKLBUILabel::setFont,		(getBoolT)&CKLBUILabel::getFont,		0		},
	{	"size",				UINTEGER,	(setBoolT)&CKLBUILabel::setFontSize,	(getBoolT)&CKLBUILabel::getFontSize,	0		},
	{	"txt_alpha",		UINTEGER,	(setBoolT)&CKLBUILabel::setAlpha,		(getBoolT)&CKLBUILabel::getAlpha,		0		},
	{	"txt_color",		UINTEGER,	(setBoolT)&CKLBUILabel::setU24Color,	(getBoolT)&CKLBUILabel::getU24Color,	0		},
	{	"text",				STRING,		(setBoolT)&CKLBUILabel::setText,		(getBoolT)&CKLBUILabel::getText,		0		},
	{	"align",			UINTEGER,	(setBoolT)&CKLBUILabel::setAlign,		(getBoolT)&CKLBUILabel::getAlign,		0		}
};

enum {
	ARG_PARENT = 1,

	ARG_ORDER,
	ARG_X,
	ARG_Y,

	ARG_ALPHA,
	ARG_COLOR,

	ARG_FONT,
	ARG_SIZE,

	ARG_TEXT,

	ARG_ALIGN,

	ARG_REQUIRE = ARG_TEXT,
	ARG_NUMS = ARG_ALIGN
};
CKLBUILabel::CKLBUILabel()
: CKLBUITask(P_UIAFTER)
, m_width	(0)
, m_height	(0)
, m_font	(NULL)
, m_text	(NULL)
, m_textEllipsis(NULL)
, m_shadowBlur(0.0f)
, m_shadowColor(0)
, m_shadowOffsetX(0)
, m_shadowOffsetY(0)
, m_shadowEnabled(true)
, m_marqueeActive(false)
, m_useNativeFont(false)
, m_fitMode(3)
, m_pLabel	(NULL)
{
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUILabel::~CKLBUILabel() 
{
	KLBDELETEA(m_font);
	KLBDELETEA(m_text);
	KLBDELETEA(m_textEllipsis);
}

u32
CKLBUILabel::getClassID()
{
	return CLS_KLBUILABEL;
}

CKLBUILabel* CKLBUILabel::create(CKLBUITask* parent, CKLBNode* pNode, u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text, u32 align, float shadowBlur, u32 shadowColor, s32 shadowOffsetX, s32 shadowOffsetY, u32 shadowEnabled, bool marqueeActive) {
	CKLBUILabel* pTask = KLBNEW(CKLBUILabel);
    if(!pTask) { return NULL; }
	if (!pTask->setupNode()) {
		KLBDELETE(pTask);
		return NULL;
	}
	pTask->m_shadowColor = shadowColor;
	pTask->m_shadowOffsetX = shadowOffsetX;
	pTask->m_shadowOffsetY = shadowOffsetY;
	pTask->m_shadowBlur = shadowBlur;
	pTask->m_shadowEnabled = shadowEnabled;
	pTask->m_marqueeActive = marqueeActive;
	bool bResult = pTask->initCore(order, x, y, width, height, alpha, color, font, size, text, align);
	bResult = pTask->registUI(parent, bResult);
	if (pNode) {
		parent->getNode()->removeNode(pTask->getNode());
		pNode->addNode(pTask->getNode());
	}
	if(!bResult) {
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBUILabel::init(CKLBUITask* pParent, CKLBNode* pNode, u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text, u32 align, float shadowBlur, u32 shadowColor, s32 shadowOffsetX, s32 shadowOffsetY, u32 shadowEnabled, bool marqueeActive) {
	if (!setupNode()) return false;
	m_shadowColor = shadowColor;
	m_shadowOffsetX = shadowOffsetX;
	m_shadowOffsetY = shadowOffsetY;
	m_shadowBlur = shadowBlur;
	m_shadowEnabled = shadowEnabled;
	m_marqueeActive = marqueeActive;
	bool bResult = initCore(order, x, y, width, height, alpha, color, font, size, text,align);
	bResult = registUI(pParent, bResult);
	if (pNode) {
		pParent->getNode()->removeNode(getNode());
		pNode->addNode(getNode());
	}
	return bResult;
}

bool
CKLBUILabel::initCore(u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text, u32 align)
{
	if (!setupPropertyList((const char**)ms_propItems,SizeOfArray(ms_propItems))) {
		return false;
	}

	setInitPos(x, y);

	klb_assertNull((((s32)order) >= 0), "Order Problem");

	m_order = order;
	m_alpha = alpha;
	m_color = color;
	m_width = width;
	m_height = height;
	m_useNativeFont = false;
	setStrC(m_font,font);
	m_size  = size;
	setStrC(m_text,  text);
	m_align = align;
	m_update = false;
	return setup_node();
}

bool
CKLBUILabel::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();

    if(argc < ARG_REQUIRE || argc > ARG_NUMS) { return false; }

	return initCore(lua.getInt(ARG_ORDER),
                    lua.getFloat(ARG_X),
                    lua.getFloat(ARG_Y),
					0,
					0,
                    lua.getInt(ARG_ALPHA),
                    lua.getInt(ARG_COLOR),
                    lua.getString(ARG_FONT),
                    lua.getInt(ARG_SIZE),
                    lua.getString(ARG_TEXT),
                    (argc >= ARG_ALIGN) ? lua.getInt(ARG_ALIGN) : 0
                   );
}

int
CKLBUILabel::commandUI(CLuaState& lua, int argc, int cmd)
{
	int ret = 1;
	switch(cmd)
	{
	case UI_LABEL_SET_TEXT:
	case UI_LABEL_SET_TEXTELLIPSIS:
		{
			if(argc <= 2) {
				lua.retBoolean(false);
				return 1;
			}
			const char * text = lua.getString(3);
			const char * ellipsis = NULL;
			if((argc >= 4) && (cmd == UI_LABEL_SET_TEXTELLIPSIS)) {
				ellipsis = lua.getString(4);
			}

			setText(text, ellipsis);

			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_COLOR:
		{
			if(argc != 4) {
				lua.retBoolean(false);
				return 1;
			}
			u32 alpha = lua.getInt(3);
			u32 col = lua.getInt(4);

			setColor((alpha << 24) | col);

			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_FONT:
		{
			if(argc != 4) {
				lua.retBoolean(false);
				return 1;
			}
			const char * font = lua.getString(3);
			u32 size = lua.getInt(4);

			setFont(font);
			setFontSize(size);

			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_POSITION:
		{
			if(argc != 4) {
				lua.retBoolean(false);
				return 1;
			}
			float x = lua.getFloat(3);
			float y = lua.getFloat(4);

			setPosition(x,y);

			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_ALIGNMENT:
		{
			if(argc != 3) {
				lua.retBoolean(false);
				return 1;
			}
			u32 align = lua.getInt(3);

			setAlign(align);

			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_MARQUEEACTIVATE:
		{
			if(argc <= 4) {
				lua.retBoolean(false);
				return 1;
			}
			bool enabled = lua.getBool(3);
			u32 lineHeight = lua.getInt(4);
			bool wrap = lua.getBool(5);

			if(m_pLabel) {
				m_pLabel->lock(true);
			}

			if(argc >= 7) {
				m_width = lua.getInt(6);
				m_height = lua.getInt(7);
				if(m_pLabel) {
					m_pLabel->setWidth(m_width);
					m_pLabel->setHeight(m_height);
				}
			}

			if(!m_pLabel) {
				lua.retBoolean(false);
				return 1;
			}
			m_pLabel->setFit(enabled, lineHeight, wrap);
			m_pLabel->lock(false);
			if(enabled) {
				REFRESH_B;
			} else {
				RESET_B;
			}
			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_MARQUEESETUP:
		{
			if(argc != 6) {
				lua.retBoolean(false);
				return 1;
			}
			s16 startDelay = (s16)lua.getInt(3);
			s16 endDelay = (s16)lua.getInt(4);
			float speed = (float)lua.getInt(5) / 1000.0f;
			u8 mode = (u8)lua.getInt(6);
			if(m_pLabel) {
				m_pLabel->setMarquee(startDelay, endDelay, mode, speed);
			}
			return 1;
		}
	case UI_LABEL_SET_SHADOW:
		{
			if(argc != 7) {
				lua.retBoolean(false);
				return 1;
			}
			m_shadowColor = lua.getInt(3);
			m_shadowOffsetX = (s8)lua.getInt(4);
			m_shadowOffsetY = (s8)lua.getInt(5);
			m_shadowBlur = lua.getFloat(6);
			u8 enabled = (u8)lua.getInt(7);
			m_shadowEnabled = true;
			if(enabled) {
				m_shadowEnabled = enabled;
			}

			if(m_pLabel) {
				m_pLabel->setShadow(m_shadowColor, m_shadowOffsetX, m_shadowOffsetY, m_shadowBlur, m_shadowEnabled);
			}
			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_FIT:
		{
			if(argc <= 2) {
				lua.retBoolean(false);
				return 1;
			}
			u8 active = (u8)lua.getInt(3);
			if(active != m_marqueeActive) {
				m_marqueeActive = active;
				REFRESH_A;
			}
			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_SET_SIZE:
		{
			if(argc != 4) {
				lua.retBoolean(false);
				return 1;
			}
			s32 width = lua.getInt(3);
			s32 height = lua.getInt(4);
			width = (width < 0) ? 0 : width;
			height = (height < 0) ? 0 : height;
			m_width = width;
			m_height = height;
			REFRESH_B;
			lua.retBoolean(true);
			ret = 1;
		}
		break;
	case UI_LABEL_USENATIVE_FONT:
		{
			m_useNativeFont = lua.getBool(3);
			if(!lua.isNil(4)) {
				m_fitMode = lua.getInt(4);
			}
			REFRESH_A;
			lua.retBoolean(true);
			ret = 1;
		}
		break;
	}

	return ret;
}

CKLBNodeVirtualDocument*
CKLBUILabel::getVirtualDocument() const
{
	return m_pLabel->getVirtualDocument();
}

void
CKLBUILabel::setMarqueeActive(u32 active)
{
	if (active != m_marqueeActive) {
		m_marqueeActive = active;
		REFRESH_A;
	}
}

void
CKLBUILabel::execute(u32 deltaT)
{
	if(CHANGE_A) {
		setup_node();
		RESET_A;
	}

	if(CHANGE_C) {
		if(m_pLabel) {
			m_pLabel->setPriority(m_order);
		}
		RESET_C;
	}

	if(CHANGE_B) {
		if(m_pLabel) {
			m_pLabel->updateMarquee(deltaT);
			if(m_pLabel->isMarqueeStopped()) {
				RESET_B;
			}
		}
	}

	//m_pLabel->setViewPortPos(0, 0);
}

void CKLBUILabel::onResume()
{
	m_pLabel->freeFont();
}

void
CKLBUILabel::dieUI()
{
	KLBDELETE(m_pLabel);
}

bool
CKLBUILabel::setup_node()
{
	bool created = false;
	if(!m_pLabel) {
		m_pLabel = KLBNEWC(CKLBLabelNode,(0,false,m_font,0));	// No text, No font
		if(m_pLabel) { 
			m_pLabel->lock(true);
			getNode()->addNode(m_pLabel, false);
			m_pLabel->setPriority(m_order);
			created = true;
		}
	}

	if(!m_pLabel) {
		return false;
	}

	if(!created) {
		m_pLabel->lock(true);
	}

	bool fixedSize = (m_width != 0) && (m_height != 0);
	m_pLabel->setFontPolicy(m_useNativeFont, m_fitMode);
	m_pLabel->setUseTextSize(!fixedSize);
	if(fixedSize) {
		m_pLabel->setWidth(m_width);
		m_pLabel->setHeight(m_height);
	}
	m_pLabel->setMarquee(m_marqueeActive, 0, 0, 0, 0);
	m_pLabel->setShadow(m_shadowColor, m_shadowOffsetX, m_shadowOffsetY, m_shadowBlur, m_shadowEnabled);

	m_pLabel->setFont   (m_size,m_font);
	m_pLabel->setAlign  (m_align);
	m_pLabel->setTextEllipsis(m_textEllipsis);
	m_pLabel->setText   (m_text);

	u32 alpha = m_alpha;
	u32 col   = m_color;
	m_pLabel->setTextColor(col|(alpha << 24));

	m_pLabel->lock(false);

	//	DEBUG_PRINT("[%p] setup_node(): str = [%s] color=%08x (first)",
	//		this, getStr(PR_TEXT), col|(alpha << 24));
    return true;
}
