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
#ifndef CKLBUIScale9Btn_h
#define CKLBUIScale9Btn_h

#include "CKLBUISystem.h"

class CKLBLabelNode;

class CKLBUIScale9Btn : public CKLBUISelectable
{
	friend class CKLBCompositeAsset;
	friend class CKLBFormIF;

protected:
	CKLBUIScale9Btn();
	virtual ~CKLBUIScale9Btn();

public:
	virtual u32 getClassID();
	virtual void setPriority(u32 renderPriority);
	virtual void replaceAsset(const char* sourceName, CKLBAsset* replacement);
	virtual bool init(u32 priority);

protected:
	virtual CKLBNode* createSubTree(CKLBAsset* asset, u32 priority);

private:
	void setText(const char* text);
	void setTextAlign(u16 align);
	void setFont(const char* fontName, u16 fontSize, u32 color);
	void setTextColor(u32 color);
	void setTextShadow(u32 color, s8 offsetX, s8 offsetY, float blur, u8 shadowEnabled);
	bool rebuildText();
	bool rebuildTextInternal();

	CKLBRenderCommand*	m_scale9Render;
	// Reserved slot kept by the shipped layout between the render command and
	// the label node. Like m_unknown240 below, the offsets around it are
	// target-proven but no body in the binary reads or writes it.
	CKLBNode*	m_scale9Tree;
	CKLBLabelNode*	m_labelNode;
	char*		m_text;
	char*		m_fontName;
	u32			m_shadowColor;
	float		m_shadowBlur;
	u16			m_textAlign;
	u16			m_fontSize;
	u32			m_textColor;
	// Four bytes at +0x240 that the shipped layout requires but which no
	// body in the binary reads or writes. The surrounding offsets and the
	// 0x250 object size are target-proven; the purpose of these is not.
	u32		m_unknown240;
	u8			m_textAlpha;
	u8			m_shadowEnabled;
	s8			m_shadowOffsetX;
	s8			m_shadowOffsetY;
	u8			m_marqueeActive;
};

#endif // CKLBUIScale9Btn_h
