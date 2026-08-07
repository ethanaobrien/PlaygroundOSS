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
#include "CKLBLabelNode.h"

#include "CPFInterface.h"

#include "CKLBLanguageDatabase.h"

#include <math.h>

namespace {

struct LabelDocumentStyle {
	CKLBNodeVirtualDocument* documents;
	u32 textColor;
	u32 shadowColor;
	u32 documentSize;
	u16 shadowBlurFixed;
	u16 scaleXFixed;
	u16 scaleYFixed;
	u8 fontSize;
	s8 shadowOffsetX;
	s8 shadowOffsetY;
	u8 textLength;
	u8 fontNameLength;
};

static const u32 LABEL_STYLE_CAPACITY = 400;
static const u32 LABEL_STYLE_TEXT_CAPACITY = 150;
static const u32 LABEL_STYLE_FONT_CAPACITY = 50;

static LabelDocumentStyle s_labelStyles[LABEL_STYLE_CAPACITY];
static char s_labelStyleStrings[LABEL_STYLE_CAPACITY]
							   [LABEL_STYLE_FONT_CAPACITY + LABEL_STYLE_TEXT_CAPACITY];
static s32 s_labelStyleCount;

}

void CKLBLabelNode::findLabelDocumentList(
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
) {
	const size_t textLength = text ? strlen(text) : 0;
	const size_t fontNameLength = fontName ? strlen(fontName) : 0;
	const u32 documentSize = documentWidth | (documentHeight << 16);
	const u16 scaleXFixed = (u16)(s64)(scaleX * 1024.0f);
	const u16 scaleYFixed = (u16)(s64)(scaleY * 1024.0f);

	s32 styleIndex;
	if (s_labelStyleCount < 1) {
		styleIndex = s_labelStyleCount++;
	} else {
		s32 reusableIndex = -1;
		for (s32 index = 0; index < s_labelStyleCount; ++index) {
			LabelDocumentStyle& style = s_labelStyles[index];

			if (!style.documents) {
				reusableIndex = index;
			} else if (
				documentSize == style.documentSize
				&& textLength == style.textLength
				&& fontNameLength == style.fontNameLength
				&& textColor == style.textColor
				&& shadowColor == style.shadowColor
				&& fontSize == style.fontSize
				&& shadowOffsetX == style.shadowOffsetX
				&& shadowOffsetY == style.shadowOffsetY
				&& shadowBlurFixed == style.shadowBlurFixed
				&& scaleXFixed == style.scaleXFixed
				&& scaleYFixed == style.scaleYFixed
				&& strcmp(text, s_labelStyleStrings[index] + LABEL_STYLE_FONT_CAPACITY) == 0
				&& (!fontName || strcmp(fontName, s_labelStyleStrings[index]) == 0)
			) {
				*documentList = &style.documents;
				return;
			}
		}

		styleIndex = reusableIndex;
		if (styleIndex == -1) {
			styleIndex = s_labelStyleCount++;
			klb_assert(styleIndex < (s32)LABEL_STYLE_CAPACITY,
					   "Label Entry Cache FULL");
		}
	}

	LabelDocumentStyle& style = s_labelStyles[styleIndex];
	style.textColor = textColor;
	style.shadowColor = shadowColor;
	style.fontSize = fontSize;
	style.documents = NULL;
	style.shadowBlurFixed = shadowBlurFixed;
	style.shadowOffsetX = shadowOffsetX;
	style.shadowOffsetY = shadowOffsetY;
	style.documentSize = documentSize;
	style.textLength = (u8)textLength;
	style.fontNameLength = (u8)fontNameLength;
	style.scaleXFixed = scaleXFixed;
	style.scaleYFixed = scaleYFixed;

	klb_assertNull(textLength < LABEL_STYLE_TEXT_CAPACITY - 1,
				   "String too long");
	klb_assertNull(fontNameLength < LABEL_STYLE_FONT_CAPACITY - 1,
				   "Font name too long");

	if (text) {
		memcpy(s_labelStyleStrings[styleIndex] + LABEL_STYLE_FONT_CAPACITY,
			   text,
			   textLength + 1);
	}
	if (fontName) {
		memcpy(s_labelStyleStrings[styleIndex],
			   fontName,
			   fontNameLength + 1);
	}
	*documentList = &style.documents;
}

const char * CKLBLabelNode::ms_default_font = NULL;

bool
CKLBLabelNode::setDefaultFont(const char * fontname)
{
	char * name = NULL;
	if(fontname) {
		name = KLBNEWA(char, strlen(fontname) + 1);
		strcpy(name, fontname);
		if(!name) return false;
	}
	KLBDELETEA(ms_default_font);
	ms_default_font = (const char *)name;
	return true;
}

void
CKLBLabelNode::release()
{
	KLBDELETEA(ms_default_font);
	ms_default_font = NULL;	// 2012.12.11  Reboot時に値が残ったままになり。↑のsetDefaultFont()で変なアドレスが解放される
}

CKLBLabelNode::CKLBLabelNode(int fontsize, const char * fontname, const char * text)
:m_pLabel			(NULL)
,m_fontname			(NULL)
,m_fontsize			(-1)
,m_width			(0)
,m_height			(0)
,m_textLen			(0)
,m_textBuf			(NULL)
,m_textEllipsis		(NULL)
,m_textEllipsisLen	(0)
,m_align			(0)
,m_alignX			(0.0f)
,m_fitLineHeight	(20)
,m_shadowColor		(0)
,m_ty				(0.0f)
,m_marqueePosition	(0.0f)
,m_shadowBlurFixed	(0)
,m_shadowOffsetX		(0)
,m_shadowOffsetY		(0)
,m_shadowPassCount		(1)
,m_marqueeActive		(false)
,m_lock				(false)
,m_changed			(false)
,m_useTextSize		(true)
,m_fitEnabled		(false)
,m_fitWrap			(false)
,m_parseInlineFormatting(false)
,m_useNativeFont	(false)
,m_fitMode			(3)
{
	lock(true);
	setFont(fontsize, fontname);
	setText((char *)text);
	m_lock = false;
	m_changed = false;
	m_format = TexturePacker::getCurrentModeTexture();
}

CKLBLabelNode::CKLBLabelNode(int fontsize, bool parseInlineFormatting, const char * fontname, const char * text)
:m_pLabel			(NULL)
,m_fontname			(NULL)
,m_fontsize			(-1)
,m_width			(0)
,m_height			(0)
,m_textLen			(0)
,m_textBuf			(NULL)
,m_textEllipsis		(NULL)
,m_textEllipsisLen	(0)
,m_align			(0)
,m_alignX			(0.0f)
,m_fitLineHeight	(20)
,m_shadowColor		(0)
,m_ty				(0.0f)
,m_marqueePosition	(0.0f)
,m_shadowBlurFixed	(0)
,m_shadowOffsetX		(0)
,m_shadowOffsetY		(0)
,m_shadowPassCount		(1)
,m_marqueeActive		(false)
,m_lock				(false)
,m_changed			(false)
,m_useTextSize		(true)	// Must be true by default.
,m_fitEnabled		(false)
,m_fitWrap			(false)
,m_parseInlineFormatting(parseInlineFormatting)
,m_useNativeFont	(false)
,m_fitMode			(3)
{
	// klb_assert(m_pLabel, "could not create label.");
	lock(true);
	setFont	(fontsize, fontname);
	setText	((char *)text);
	// Trick : do not force creation of object here.
	m_lock		= false;
	m_changed	= false;
	m_format	= TexturePacker::getCurrentModeTexture();
}

CKLBLabelNode::~CKLBLabelNode() {
	// 
	KLBDELETE(m_pLabel);

	// Call Interface to release the input box
	KLBDELETEA(m_textBuf);
	KLBDELETEA(m_textEllipsis);

	KLBDELETEA(m_fontname);
}

/*virtual*/	
void CKLBLabelNode::recomputeCustom() {
	CKLBUIElement::recomputeCustom();
}

/*virtual*/
void CKLBLabelNode::setAsset(CKLBAsset* /*pAsset*/, ASSET_TYPE /*mode*/) {
	// Do nothing, no asset display.
}

/*virtual*/
bool CKLBLabelNode::processAction	(CKLBAction* /*pAction*/) {
	// Do nothing for now, may implement call back.
	return false;
}

void CKLBLabelNode::lock		(bool stop) {
	if (stop != m_lock) {
		if (!stop) {
			if (m_changed) {
				updateLabel();
				m_changed = false;
			}
		}
		m_lock = stop;
	}
}

bool CKLBLabelNode::setFont		(int fontsize, const char * fontname) {
	if(!fontname) fontname = ms_default_font;
	bool allow = (fontsize != m_fontsize);
	if (fontname) {
		if (m_fontname) {
			allow |= (strcmp(m_fontname, fontname) != 0);
		}

		if (allow) {
			char * buf = KLBNEWA(char, strlen(fontname) + 1);
			if (buf) {
				KLBDELETEA(m_fontname);
				strcpy(buf, fontname);
				m_fontname = (const char *)buf;
				m_fontsize = fontsize;
				if (!m_lock) { updateLabel(); } else { m_changed = true; }
				return true;
			} else {
				return false;
			}
		} else {
			// No changes
			return true;
		}
	} else {
		if (m_fontname) {
			KLBDELETEA(m_fontname);
			allow = true;
		}

		if (allow) {
			m_fontname = NULL;
			m_fontsize = fontsize;
			if (!m_lock) { updateLabel(); } else { m_changed = true; }
		}
		return true;
	}
}

void CKLBLabelNode::setWidth	(u32 width) {
	if (width != m_width) {
		m_width = width;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::setHeight	(u32 height) {
	if (height != m_height) {
		m_height = height;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::setAlign(u32 align) {
	if (align != m_align) {
		m_align = align;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::setTextColor(u32 color) {
	if (color != m_color) {
		m_color = color;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::setText(const char* text) {
	if (text) {
		text = CKLBLanguageDatabase::getInstance().getString(text);
		size_t len = strlen(text);
		bool allow = len != m_textLen;
		
		if (m_textBuf) {
			allow |= (strcmp(text, m_textBuf) != 0);
		} else {
			allow = true;
		}

		if (allow) {
			KLBDELETEA(m_textBuf);
			m_textBuf = KLBNEWA(char, len + 1);
			m_textLen = len;

			strcpy(m_textBuf, text);
			if (!m_lock) {
				updateLabel();
			} else {
				m_changed = true;
			}
		} else {
			m_textLen = 0;
		}
	} else if (m_textBuf) {
		KLBDELETEA(m_textBuf);
		m_textBuf = NULL;
		m_textLen = 0;
		if (!m_lock) {
			updateLabel();
		} else {
			m_changed = true;
		}
	}
}

void CKLBLabelNode::setTextEllipsis(const char* ellipsis) {
	if (ellipsis) {
		char* previous = m_textEllipsis;
		int len = strlen(ellipsis);
		KLBDELETEA(previous);
		m_textEllipsis = KLBNEWA(char, len + 1);
		m_textEllipsisLen = len;
		strcpy(m_textEllipsis, ellipsis);
	} else {
		char* previous = m_textEllipsis;
		if (!previous) return;
		KLBDELETEA(previous);
		m_textEllipsis = NULL;
		m_textEllipsisLen = 0;
	}

	if (!m_lock) {
		updateLabel();
	} else {
		m_changed = true;
	}
}

const char*	CKLBLabelNode::getText() {
	return m_textBuf;
}

void CKLBLabelNode::setUseTextSize(bool autoSize) {
	m_useTextSize = autoSize;
}

void CKLBLabelNode::setFontPolicy(bool useNativeFont, u32 fitMode) {
	m_changed |= (m_useNativeFont != useNativeFont) || (m_fitMode != fitMode);
	m_useNativeFont = useNativeFont;
	m_fitMode = fitMode;
}

void CKLBLabelNode::setFit(bool enabled, u32 lineHeight, bool wrap) {
	if(enabled != m_fitEnabled) {
		m_fitEnabled = enabled;
		m_useTextSize = false;
		m_fitLineHeight = lineHeight;
		m_fitWrap = wrap;
		KLBDELETE(m_pLabel);
		m_pLabel = NULL;
		updateLabel();
	}
}

void CKLBLabelNode::setMarquee(u32 active, s32 insetLeft, s32 insetRight, s32 insetTop, s32 insetBottom) {
	if(active != m_marqueeActive) {
		m_marqueeActive = active;
		m_marqueeInsetLeft = (float)insetLeft;
		m_marqueeInsetRight = (float)insetRight;
		m_marqueeInsetTop = (float)insetTop;
		m_marqueeInsetBottom = (float)insetBottom;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::setMarquee(s16 startDelay, s16 endDelay, u8 mode, float speed) {
	if(m_pLabel) {
		m_pLabel->setMarquee(startDelay, endDelay, mode, speed);
	}
}

void CKLBLabelNode::setMarqueePosition(s16 position) {
	if(m_pLabel) {
		m_pLabel->setMarqueePos(position);
	}
}

void CKLBLabelNode::updateMarquee(s32 deltaT) {
	if(m_pLabel) {
		m_pLabel->updateMarquee(deltaT);
	}
}

bool CKLBLabelNode::isMarqueeStopped() {
	return m_pLabel ? m_pLabel->isMarqueeStopped() : true;
}

void CKLBLabelNode::setViewPortPosition(s32 x) {
	if(m_pLabel) {
		m_pLabel->setViewPortPos(x, 0);
	}
}

void CKLBLabelNode::setShadow(u32 color, s32 offsetX, s32 offsetY, float blur, u8 /*enabled*/) {
	s64 blurFixed = (s64)((double)fabsf(blur) * 256.0);
	if((offsetX != m_shadowOffsetX) ||
	   (offsetY != m_shadowOffsetY) ||
	   ((s32)blurFixed != m_shadowBlurFixed) ||
	   (color != m_shadowColor)) {
		m_shadowOffsetX = offsetX;
		m_shadowOffsetY = offsetY;
		m_shadowBlurFixed = blurFixed;
		m_shadowColor = color;
		if (!m_lock) { updateLabel(); } else { m_changed = true; }
	}
}

void CKLBLabelNode::updateLabel()
{
	if(!m_textBuf || ((!m_width || !m_height) && !m_useTextSize)) {
		KLBDELETE(m_pLabel);
		m_pLabel = NULL;
		return;
	}

	// 本来VDocは動作中にプロパティを変更するような作りになっていないため、
	// プロパティ変更が生じたときは改めて VDOCを作り直す。
	if (!m_pLabel) {
		CKLBNodeVirtualDocument * pNewNode = KLBNEW(CKLBNodeVirtualDocument);
		if(!pNewNode) return;

		if(m_pLabel) KLBDELETE(m_pLabel);
		m_pLabel = pNewNode;

		this->addNode(m_pLabel);
	}

	IPlatformRequest& pForm = CPFInterface::getInstance().platform();

	m_pLabel->setUseNativeFont(m_useNativeFont, m_fitMode);
	pForm.setNativeFont(m_useNativeFont);
	void* pFont = pForm.getFont(m_fontsize, m_fontname, m_fitMode);

	s32 characterEndX26_6[500];
	u32 characterEndByteOffsets[500];
	STextInfo txinfo;
	txinfo.parseInlineFormatting = true;
	txinfo.characterEndX26_6 = characterEndX26_6;
	txinfo.characterEndByteOffsets = characterEndByteOffsets;
	txinfo.characterCount = 500;
	pForm.setNativeFont(m_useNativeFont);
	pForm.getTextInfo(m_textBuf ? m_textBuf : " ", pFont, &txinfo, 1.0f, 1.0f);

	float textContentHeight = txinfo.height - txinfo.outlineExtraHeight;
	s32 marqueeMode = m_useTextSize ? 0 : m_marqueeActive;
	bool fitEnabled = m_fitEnabled;
	bool standardLayout = marqueeMode == 0;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	float x;
	float y;

	if (!standardLayout) {
		float availableWidth = (float)m_width - (m_marqueeInsetLeft + m_marqueeInsetRight);
		x = m_marqueeInsetLeft;
		y = m_marqueeInsetTop;

		if (availableWidth < txinfo.width) {
			x = 0.0f;
			availableWidth = (float)m_width;
			if ((float)m_width < txinfo.width) {
				scaleX = (float)m_width / txinfo.width;
				if (scaleX < 0.0f) {
					scaleX = 0.0f;
				}
			}
		}

		float scaledVerticalOffset = 0.0f;
		if (marqueeMode == 2) {
			scaleY = scaleX;
			scaledVerticalOffset =
				truncf((1.0f - scaleX) * txinfo.height * -0.5f);
		}

		if (!m_parseInlineFormatting) {
			const float displayedWidth =
				txinfo.width < (float)m_width ? txinfo.width : (float)m_width;
			switch (m_align & 3) {
			case 1:
				x = displayedWidth * -0.5f;
				break;
			case 2:
				x = -displayedWidth;
				break;
			default:
				x = 0.0f;
				break;
			}

			const float displayedHeight = (float)m_height < txinfo.height
				? (float)m_height
				: textContentHeight;
			switch (m_align >> 2) {
			case 1:
				y = scaledVerticalOffset + displayedHeight * -0.5f;
				break;
			case 2:
				y = scaledVerticalOffset - displayedHeight;
				break;
			default:
				y = scaledVerticalOffset;
				break;
			}
		} else {
			const float horizontalRemainder = availableWidth - txinfo.width;
			float alignedX = x;
			switch (m_align & 3) {
			case 1:
				alignedX += horizontalRemainder * 0.5f;
				break;
			case 2:
				alignedX += horizontalRemainder;
				break;
			default:
				break;
			}
			x = scaleX < 1.0f ? x : alignedX;

			const float availableHeight =
				(float)m_height - (m_marqueeInsetTop + m_marqueeInsetBottom);
			switch (m_align >> 2) {
			case 1:
				y += (availableHeight - textContentHeight) * 0.5f;
				break;
			case 2:
				y += availableHeight - txinfo.height;
				break;
			default:
				break;
			}
		}
	} else {
		if (m_textEllipsis && !m_useTextSize) {
			STextInfo ellipsisInfo;
			pForm.setNativeFont(m_useNativeFont);
			pForm.getTextInfo(m_textEllipsis, pFont, &ellipsisInfo, 1.0f, 1.0f);
			const s32 ellipsisWidth = (s32)ellipsisInfo.width;

			s32 remainingCharacters = (s32)txinfo.characterCount;
			s32* characterEndX =
				txinfo.characterEndX26_6 + remainingCharacters - 1;
			s32 fittingCharacterCount;
			do {
				fittingCharacterCount = remainingCharacters;
				if (fittingCharacterCount < 1) {
					break;
				}
				const u32 endX = (u32)*characterEndX--;
				--remainingCharacters;
				if (((u32)m_width << 6) > endX) {
					break;
				}
			} while (true);

			if ((s32)fittingCharacterCount != (s32)txinfo.characterCount) {
				s32 retainedBytes = 0;
				if (fittingCharacterCount > 0) {
					s32 index = fittingCharacterCount - 1;
					const u32 availableTextWidth =
						(u32)((s32)m_width - ellipsisWidth) << 6;
					do {
						if ((u32)txinfo.characterEndX26_6[index] < availableTextWidth) {
							retainedBytes = txinfo.characterEndByteOffsets[index] + 1;
							break;
						}
					} while (--index >= 0);
				}

				char* replacement = KLBNEWA(
					char,
					retainedBytes + m_textEllipsisLen + 1
				);
				if (retainedBytes > 0) {
					memcpy(replacement, m_textBuf, retainedBytes);
				}
				memcpy(replacement + retainedBytes, m_textEllipsis, m_textEllipsisLen);
				replacement[retainedBytes + m_textEllipsisLen] = 0;
				KLBDELETEA(m_textBuf);
				m_textBuf = replacement;

				txinfo.characterCount = 500;
				pForm.setNativeFont(m_useNativeFont);
				pForm.getTextInfo(m_textBuf, pFont, &txinfo, 1.0f, 1.0f);
			}
		}

		const float displayedWidth =
			txinfo.width < (float)m_width ? txinfo.width : (float)m_width;
		switch (m_align & 3) {
		case 1:
			x = displayedWidth * -0.5f;
			break;
		case 2:
			x = -displayedWidth;
			break;
		default:
			x = 0.0f;
			break;
		}

		const float displayedHeight = txinfo.height > (float)m_height
			? (float)m_height
			: textContentHeight;
		switch (m_align >> 2) {
		case 1:
			y = displayedHeight * -0.5f;
			break;
		case 2:
			y = -displayedHeight;
			break;
		default:
			y = 0.0f;
			break;
		}
	}

	const s32 blurRadius = (m_shadowBlurFixed + 63) >> 6;
	if (m_shadowOffsetX < 0) {
		x -= (float)(blurRadius - m_shadowOffsetX);
	} else if (m_shadowOffsetX < blurRadius) {
		x -= (float)(blurRadius - m_shadowOffsetX);
	}
	if (m_shadowOffsetY < 0) {
		y -= (float)(blurRadius - m_shadowOffsetY);
	} else if (m_shadowOffsetY < blurRadius) {
		y -= (float)(blurRadius - m_shadowOffsetY);
	}

	standardLayout = standardLayout && fitEnabled;
	float documentWidth = m_useTextSize ? txinfo.width : (float)m_width;
	if (standardLayout) {
		documentWidth = txinfo.width < (float)m_width
			? (float)m_width
			: txinfo.width + 2.0f;
	} else {
		documentWidth += 0.99f;
	}

	float documentHeight = m_useTextSize ? txinfo.height : (float)m_height;
	s32 outwardShadowX = 0;
	s32 outwardShadowY = 0;
	if (m_useTextSize) {
		outwardShadowX = m_shadowOffsetX < 0 ? -m_shadowOffsetX : m_shadowOffsetX;
		outwardShadowY = m_shadowOffsetY < 0 ? -m_shadowOffsetY : m_shadowOffsetY;
	}
	const s32 descentCompensation = (s32)-txinfo.descent;
	const u32 width = (u32)documentWidth + blurRadius * 2 + outwardShadowX;
	const u32 height =
		(u32)documentHeight + blurRadius * 2 + outwardShadowY + descentCompensation;

	m_pLabel->setDocumentSize(width, height, false);
	x = roundf(x);
	y = roundf(y) - (double)descentCompensation;
	float viewPortX = x;
	float viewPortY = y;
	if (marqueeMode) {
		setTranslate(x, y);
		viewPortX = 0.0f;
		viewPortY = 0.0f;
	}

	CKLBNodeVirtualDocument** documentList = NULL;
	findLabelDocumentList(
		&documentList,
		m_fontname,
		(u8)m_fontsize,
		m_color,
		m_shadowColor,
		m_textBuf,
		m_shadowOffsetX,
		m_shadowOffsetY,
		(u16)m_shadowBlurFixed,
		width,
		height,
		scaleX,
		scaleY
	);
	if (!standardLayout) {
		m_pLabel->setDocumentList(documentList);
	}

	u32 inlineCommandCount = 0;
	if (m_textBuf) {
		u32 lineCount = 1;
		const char* cursor = m_textBuf;
		while (true) {
			const s32 code = *cursor;
			if (!code) {
				break;
			}
			++cursor;
			if (code == '\n' || code == '\r' || code == '\\') {
				++lineCount;
			} else if (code == '{') {
				++inlineCommandCount;
			}
		}
		inlineCommandCount *= lineCount;
	}

	const u32 baseCommandCount = standardLayout ? (m_fitWrap | 2) : 2;
	m_pLabel->createDocument((u16)(baseCommandCount + inlineCommandCount), m_format);
	m_pLabel->setDocumentSize(width, height, false);
	m_pLabel->setViewPortSize(
		width,
		height,
		viewPortX,
		viewPortY,
		m_renderPrio,
		false
	);
	m_pLabel->setFont(0, m_fontname, (u16)m_fontsize);
	m_pLabel->clear(m_color & 0x00FFFFFF);

	m_pLabel->lockDocument();
	if (m_textBuf) {
		m_pLabel->setFontScale(scaleX, scaleY);
		const s16 drawY = (s16)(s32)(txinfo.top + (float)descentCompensation);
		m_pLabel->drawText(
			0,
			drawY,
			m_textBuf,
			m_color,
			0,
			0,
			0,
			0,
			m_shadowColor,
			m_shadowOffsetX,
			m_shadowOffsetY,
			m_shadowBlurFixed,
			m_shadowPassCount
		);
		if (standardLayout && m_fitWrap) {
			m_pLabel->drawText(
				(s16)(s32)((float)m_fitLineHeight + txinfo.width),
				drawY,
				m_textBuf,
				m_color,
				0,
				0,
				0,
				0,
				m_shadowColor,
				m_shadowOffsetX,
				m_shadowOffsetY,
				m_shadowBlurFixed,
				m_shadowPassCount
			);
		}
	}
	m_pLabel->unlockDocument();

	pForm.setNativeFont(m_useNativeFont);
	pForm.deleteFontResource(pFont);

	m_status |= MATRIX_CHANGE;
	m_pLabel->setViewPortPos(0, 0);
	m_pLabel->setPriority(m_renderPrio);
	markUpTree();
	m_pLabel->setMarqueePos((s16)(s32)txinfo.width);
	m_pLabel->setMarqueeActive(standardLayout, m_fitLineHeight, m_fitWrap);
}

void	CKLBLabelNode::setPriority(u32 renderPriority)
{
	m_renderPrio = renderPriority;
	if(m_pLabel) m_pLabel->setPriority(renderPriority);
}

void CKLBLabelNode::freeFont()
{
	m_pLabel->freeFont();
}

/*
void CKLBLabelNode::setTranslateVirtual	(float x, float y)  {
	// --> Not possible anymore : if ((x != m_matrix.m_matrix[MAT_TX]) || (y != m_matrix.m_matrix[MAT_TY])) {

	m_status |= MATRIX_CHANGE;
	markUpTree();
	m_tx = x;
	m_ty = y;
	m_matrix.m_matrix[MAT_TX] = m_tx + m_alignX;
	m_matrix.m_matrix[MAT_TY] = m_ty + m_alignY;
	if (m_matrix.m_type == MATRIX_ID) {
		m_matrix.m_type = MATRIX_T;
	}

	// }
}
*/
