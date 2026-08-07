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
#ifndef CKLBUILabel_h
#define CKLBUILabel_h

#include "CKLBUITask.h"
#include "CKLBLabelNode.h"

/*!
* \class CKLBUILabel
* \brief Label Task Class
* 
* CKLBUILabel is a basic text label task.
*/
class CKLBUILabel : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUILabel>;
private:
	CKLBUILabel();
	virtual ~CKLBUILabel();

	bool init(CKLBUITask* parent, CKLBNode* pNode, u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text, u32 align, float shadowBlur, u32 shadowColor, s32 shadowOffsetX, s32 shadowOffsetY, u32 shadowEnabled, bool marqueeActive);
	bool initCore(u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text,u32 align);
public:
	static CKLBUILabel* create(CKLBUITask* parent, CKLBNode* pNode, u32 order, float x, float y, u32 width, u32 height, u32 alpha, u32 color, const char* font, u32 size, const char* text, u32 align, float shadowBlur, u32 shadowColor, s32 shadowOffsetX, s32 shadowOffsetY, u32 shadowEnabled, bool marqueeActive);
	virtual u32 getClassID();

	bool initUI (CLuaState& lua);
	void onResume();
	void execute(u32 deltaT);
	void dieUI  ();

	int commandUI(CLuaState& lua, int argc, int cmd);

	inline virtual u32 getOrder()	{ return m_order;	}

	inline virtual void setOrder(u32 order) {
		if (order != m_order) {
			m_order = order;
			REFRESH_C;
		}
	}

	inline u32 getAlign()	{ return m_align;	}

	inline void setAlign(u32 align) {
		if (align != m_align) {
			m_align  = align;
			REFRESH_A;
		}
	}

	inline void setText(const char* txt, const char* ellipsis = NULL) {
		if (txt) {
			if ((!m_text) || (strcmp(txt,m_text)!=0)) {
				setStrC(m_text, txt);
				REFRESH_A;
			}
		} else {
			if (m_text) {
				setStrC(m_text, txt);
				REFRESH_A;
			}
		}

		if (ellipsis) {
			if ((!m_textEllipsis) || (strcmp(ellipsis,m_textEllipsis)!=0)) {
				setStrC(m_textEllipsis, ellipsis);
				REFRESH_A;
			}
		} else {
			if (m_textEllipsis) {
				setStrC(m_textEllipsis, ellipsis);
				REFRESH_A;
			}
		}
	}

	inline const char* getText()		{ return m_text;	}

	inline void setAlpha(u32 alpha)		{ 
		if(alpha != m_alpha) {
			m_alpha = alpha;
			REFRESH_A;
		}
	}
	inline u32 getAlpha()				{ return m_alpha;	}

	inline void setU24Color(u32 color)	{ 
		if(color != m_color) {
			m_color = color;	
			REFRESH_A;
		}
	}
	inline u32 getU24Color()			{ return m_color;	}

	inline void setColor(u32 color) {
		u32 alpha = color>>24;
		u32 col	  = color & 0xFFFFFF;
		if (alpha != m_alpha) {
			m_alpha = alpha;
			REFRESH_A;
		}

		if (col != m_color) {
			m_color = col;
			REFRESH_A;
		}
	}

	inline u32 getColor()	{ return m_color | (m_alpha<<24);	}

	CKLBNodeVirtualDocument* getVirtualDocument() const;
	void setMarqueeActive(u32 active);

	inline void setFont(const char* font) {
		if (font) {
			if ((!m_font) || (strcmp(font,m_font)!=0)) {
				setStrC(m_font, font);
				REFRESH_A;
			}
		} else {
			if (m_font) {
				setStrC(m_font, font);
				REFRESH_A;
			}
		}
	}

	inline const char* getFont()		{ return m_font; }

	inline void setFontSize(u32 size) {
		if (size != m_size) {
			m_size = size;
			REFRESH_A;
		}
	}

	inline u32 getFontSize() { return m_size; }

	inline void setPosition(float x, float y) {
		setX(x);
		setY(y);
		REFRESH_A;
	}

private:
	u32			m_width;
	u32			m_height;
	u32			m_align;
	u32			m_color;
	u32			m_size;
	u32			m_order;
	const char* m_font;
	const char* m_text;
	const char* m_textEllipsis;
	float		m_shadowBlur;
	u32			m_shadowColor;
	u8			m_alpha;
	s8			m_shadowOffsetX;
	s8			m_shadowOffsetY;
	u8			m_shadowEnabled;
	u8			m_marqueeActive;
	bool		m_useNativeFont;
	u32			m_fitMode;

	bool setup_node();
	
	// 現在は VDocで仮実装しておく。
	CKLBLabelNode			*	m_pLabel;
	bool						m_update;
	static	PROP_V2				ms_propItems[];
};


#endif // CKLBUILabel_h

