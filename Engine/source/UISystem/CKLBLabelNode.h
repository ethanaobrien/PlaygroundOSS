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
#ifndef __CKLB_LABELNODE__
#define __CKLB_LABELNODE__

#include "CKLBUISystem.h"
#include "CKLBNodeVirtualDocument.h"

/*!
* \class CKLBLabelNode
* \brief Text Specialized Node Class
* 
* CKLBLabelNode is a text specialized Node for Scene Graph.
* It provides methods to modify the apparence of the text label.
*/
class CKLBLabelNode : public CKLBUIElement {
public:
	CKLBLabelNode(int fontsize, const char * fontname = 0, const char * text = 0);
	CKLBLabelNode(int fontsize, bool parseInlineFormatting, const char * fontname, const char * text);
	~CKLBLabelNode();

	// KLBObject::
	virtual
	u32		getClassID			()					{ return CLS_KLBLABEL; }

	// KLBNode::
	virtual		
	void	recomputeCustom		();

	//
	//	Can interact with selection or not.
	//
	// virtual	void setEnabled		(bool isEnabled);

	virtual bool isSelectable   ()  { return true; }
	virtual void setAsset		(CKLBAsset*	pAsset, ASSET_TYPE mode);
	virtual	bool processAction	(CKLBAction* pAction);

	void		lock			(bool stop);
	void		setAlign		(u32 align);
	void		setText			(const char* text);
	void		setTextEllipsis	(const char* ellipsis);
	const char*	getText			();

	void		setTextColor	(u32 color);
	void		setWidth		(u32 width);
	void		setHeight		(u32 heigth);
	u32			getWidth		()	{ return m_width;	}
	u32			getHeight		()	{ return m_height;	}
	bool		setFont			(int fontsize, const char * fontname);   

	void		setUseTextSize	(bool autoSize);
	void		setFontPolicy	(bool useNativeFont, u32 fitMode);
	void		setFit			(bool enabled, u32 lineHeight, bool wrap);
	void		setMarquee		(u32 active, s32 insetLeft, s32 insetRight, s32 insetTop, s32 insetBottom);
	void		setMarquee		(s16 startDelay, s16 endDelay, u8 mode, float speed);
	void		setMarqueePosition(s16 position);
	void		updateMarquee	(s32 deltaT);
	bool		isMarqueeStopped();
	void		setViewPortPosition(s32 x);
	void		setShadow		(u32 color, s32 offsetX, s32 offsetY, float blur, u8 enabled);
	void		setPriority		(u32 renderPriority);
	CKLBNodeVirtualDocument* getVirtualDocument() const { return m_pLabel; }

	void		forceRefresh	()  { m_pLabel->forceRefresh(); }
	void		freeFont		();

	static bool setDefaultFont  (const char * fontname = 0);
	static void release         ();

protected:
	// virtual void setUpperEnabled(bool isEnabled);

	//
	// Visible / Invisible related.
	//
	// virtual void	addRender			();
	// virtual void	removeRender		();

	const char * m_fontname;
	int          m_fontsize;
	CKLBNodeVirtualDocument	*	m_pLabel;

	u32		m_width;
	u32		m_height;

	u32		m_color;

	char	*	m_textBuf;
	size_t		m_textLen;
	char	*	m_textEllipsis;
	u32		m_textEllipsisLen;

	u32		m_align;
	u32		m_shadowColor;
	float	m_ty;
	float	m_alignX;
	s32		m_fitLineHeight;
	float	m_marqueeInsetLeft;
	float	m_marqueeInsetRight;
	float	m_marqueeInsetTop;
	float	m_marqueeInsetBottom;
	float	m_marqueePosition;
	s16		m_shadowBlurFixed;
	s8		m_shadowOffsetX;
	s8		m_shadowOffsetY;
	s8		m_shadowPassCount;
	u8		m_marqueeActive;
	u8		m_format;
	bool	m_lock;
	bool	m_changed;
	bool	m_useTextSize;
	bool	m_fitEnabled;
	bool	m_fitWrap;
	bool	m_parseInlineFormatting;
	bool	m_useNativeFont;
	u32		m_fitMode;

private:

	static void findLabelDocumentList(
		CKLBNodeVirtualDocument*** documentList,
		const char* fontName,
		u8 fontSize,
		u32 textColor,
		u32 shadowColor,
		const char* text,
		s8 shadowOffsetX,
		s8 shadowOffsetY,
		u16 shadowBlurFixed,
		u32 documentWidth,
		u32 documentHeight,
		float scaleX,
		float scaleY
	);
	void updateLabel();

	static const char * ms_default_font;
};

#endif
