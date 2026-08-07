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
#include "CKLBUIScale9Btn.h"
#include "CKLBLabelNode.h"
#include "CKLBUtility.h"

CKLBUIScale9Btn::CKLBUIScale9Btn()
: CKLBUISelectable()
, m_scale9Render(NULL)
, m_scale9Tree(NULL)
, m_labelNode(NULL)
, m_text(NULL)
, m_fontName(NULL)
, m_shadowColor(0)
, m_shadowBlur(0.0f)
, m_textAlign(0)
, m_fontSize(24)
, m_textColor(0x00FFFFFF)
, m_textAlpha(0xFF)
, m_shadowEnabled(1)
, m_shadowOffsetX(0)
, m_shadowOffsetY(0)
, m_marqueeActive(1)
{
	m_renderPrio = ~0U;
}

CKLBUIScale9Btn::~CKLBUIScale9Btn()
{
	KLBDELETEA(m_text);
	KLBDELETEA(m_fontName);
}

u32
CKLBUIScale9Btn::getClassID()
{
	return CLS_KLBUISCALE9BTN;
}

void
CKLBUIScale9Btn::setPriority(u32 renderPriority)
{
	if(m_renderPrio != renderPriority) {
		m_renderPrio = renderPriority;
	}
}

void
CKLBUIScale9Btn::replaceAsset(const char* sourceName, CKLBAsset* replacement)
{
	CKLBUISelectable::replaceAsset(sourceName, replacement);
}

bool
CKLBUIScale9Btn::init(u32 priority)
{
	m_renderPrio = priority;
	return CKLBUISelectable::init(priority);
}

CKLBNode*
CKLBUIScale9Btn::createSubTree(CKLBAsset* pAsset, u32 priority)
{
	// The button builds its own visual instead of letting the asset do it: a
	// 9-slice image becomes a scale9 command stretched to the touch surface,
	// any other image becomes a four vertex quad carrying the asset geometry.
	CKLBNode* pRoot = KLBNEW(CKLBNode);
	m_scale9Render = NULL;

	if (pAsset && pRoot && (pAsset->getAssetType() == ASSET_IMAGE)) {
		CKLBImageAsset* pImage = (CKLBImageAsset*)pAsset;
		bool isScale9 = (pImage->hasStandardAttribute(CKLBImageAsset::IS_SCALE9) != 0);
		CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();

		CKLBRenderCommand* pSprite;
		if (isScale9) {
			CKLBSpriteScale9* pScale9 = (CKLBSpriteScale9*)pRdrMgr.allocateCommandSprite(pImage, priority, true);
			if (pScale9) {
				pScale9->setWidth ((s32)m_touchSurface.beforeTransform[2]);
				pScale9->setHeight((s32)m_touchSurface.beforeTransform[3]);
				pScale9->endSizeUpdate();
			}
			pSprite = pScale9;
		} else {
			CKLBDynSprite* pDynSprite = pRdrMgr.allocateCommandDynSprite(4, 6, priority);
			if (pDynSprite && (pImage->getIndexCount() == 6) && (pImage->getVertexCount() == 4)) {
				memcpy(pDynSprite->getSrcUVBuffer(),
					   pImage->getUVBuffer(),
					   sizeof(float) * 8);
				memcpy(pDynSprite->getSrcIndexBuffer(),
					   pImage->getIndexBuffer(),
					   sizeof(u16) * 6);

				float width		= m_touchSurface.beforeTransform[2];
				float height	= m_touchSurface.beforeTransform[3];
				pDynSprite->setVertexXY(0, 0.0f,	0.0f);
				pDynSprite->setVertexXY(1, width,	0.0f);
				pDynSprite->setVertexXY(2, width,	height);
				pDynSprite->setVertexXY(3, 0.0f,	height);
				pDynSprite->setTexture(pImage);
				pDynSprite->mark(FLAG_BUFFERSHIFT
								 | CKLBDynSprite::MARK_CHANGE_XY
								 | CKLBDynSprite::MARK_CHANGE_UV);
			}
			pSprite = pDynSprite;
		}
		m_scale9Render = pSprite;

		pRoot->setTranslate(0.0f, 0.0f);
		pRoot->setRender(m_scale9Render);
		pRoot->setRenderOnDestroy(true);
	}
	return pRoot;
}

void
CKLBUIScale9Btn::setText(const char* text)
{
	KLBDELETEA(m_text);
	char* newText = NULL;
	if (text) {
		u32 textLength = strlen(text);
		if (textLength) {
			newText = (char*)CKLBUtility::copyString(text);
		}
	}
	m_text = newText;
}

void
CKLBUIScale9Btn::setTextAlign(u16 align)
{
	m_textAlign = align;
}

void
CKLBUIScale9Btn::setFont(const char* fontName, u16 fontSize, u32 color)
{
	KLBDELETEA(m_fontName);
	m_fontName = fontName
		? (char*)CKLBUtility::copyString(fontName)
		: NULL;
	m_fontSize = fontSize;
	m_textColor = color;
}

void
CKLBUIScale9Btn::setTextColor(u32 color)
{
	m_textColor = color;
}

void
CKLBUIScale9Btn::setTextShadow(u32 color, s8 offsetX, s8 offsetY, float blur, u8 shadowEnabled)
{
	m_shadowColor = color;
	m_shadowOffsetX = offsetX;
	m_shadowOffsetY = offsetY;
	m_shadowBlur = blur;
	m_shadowEnabled = shadowEnabled;
}

bool
CKLBUIScale9Btn::rebuildText()
{
	return rebuildTextInternal();
}

bool
CKLBUIScale9Btn::rebuildTextInternal()
{
	if (!m_text) {
		return true;
	}

	bool labelCreated = false;
	if (!m_labelNode) {
		m_labelNode = KLBNEWC(CKLBLabelNode, (0, true, m_fontName, NULL));
		if (m_labelNode) {
			m_labelNode->lock(true);
			m_labelNode->setUseTextSize(true);
			addNode(m_labelNode);
			m_labelNode->setPriority(m_renderPrio + 1);
			m_labelNode->setWidth((u32)m_touchSurface.beforeTransform[2]);
			m_labelNode->setHeight((u32)m_touchSurface.beforeTransform[3]);
			labelCreated = true;
		}
	}
	if (!m_labelNode) {
		return false;
	}
	if (!labelCreated) {
		m_labelNode->lock(true);
	}

	m_labelNode->setFont(m_fontSize, m_fontName);
	m_labelNode->setAlign(m_textAlign);
	m_labelNode->setText(m_text);

	// The marquee window is inset by the 9-slice margins of the button's
	// normal image, so scrolling text stays inside the stretchable center.
	s32 insetLeft	= 0;
	s32 insetTop	= 0;
	s32 insetBottom	= 0;
	s32 insetRight	= 0;
	if (m_pNormal && (m_pNormal->getAssetType() == ASSET_IMAGE)) {
		CKLBImageAsset* image = (CKLBImageAsset*)m_pNormal;
		if (image->hasStandardAttribute(CKLBImageAsset::IS_SCALE9)) {
			image->getAttribute(ASSET_ATTRIB::zK2_S9_LEFT,	insetLeft);
			image->getAttribute(ASSET_ATTRIB::zK2_S9_RIGHT,	insetRight);
			image->getAttribute(ASSET_ATTRIB::zK2_S9_TOP,	insetTop);
			image->getAttribute(ASSET_ATTRIB::zK2_S9_BOTTOM,insetBottom);
		}
	}
	m_labelNode->setMarquee(
		m_marqueeActive,
		insetLeft,
		insetRight,
		insetTop,
		insetBottom);

	u32 textAlpha = m_textAlpha;
	m_labelNode->setTextColor((textAlpha << 24) | m_textColor);
	m_labelNode->setShadow(
		m_shadowColor,
		m_shadowOffsetX,
		m_shadowOffsetY,
		m_shadowBlur,
		m_shadowEnabled);
	m_labelNode->lock(false);
	return true;
}
