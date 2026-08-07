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
#include "CKLBScoreNode.h"
#include "mem.h"

CKLBScoreNode::CKLBScoreNode()
:CKLBNode		()
,m_uiScore		(0xFFFFFFFF)
,m_newAnimated	(false)
,m_oldAnimated	(false)
,m_bScoreChange	(true)
,m_dotSprite	(NULL)
,m_dotNode		(NULL)
,m_widthDot		(0)
,m_heightDot	(0)
,m_widthComma	(0)
,m_heightComma	(0)
,m_commaCount	(0)
,m_dotOffsetX	(0)
,m_dotOffsetY	(0)
,m_commaOffsetX	(0)
,m_commaOffsetY	(0)
,m_DotPosition	(0)
,m_prevDotPosition	(0xFE)
{
	m_deleteRender = false; // Force own management of sprite on destruction.
	memset32(m_scoreOldNode, NULL, __SCORE_LEN_MAX__ * sizeof(void*));
	memset32(m_scoreNewNode, NULL, __SCORE_LEN_MAX__ * sizeof(void*));
	memset32(m_font		   , NULL, 11 * sizeof(void*));
	memset32(m_commaSprite , NULL, __SCORE_COMMA_MAX__ * sizeof(void*));
	memset32(m_commaNode   , NULL, __SCORE_COMMA_MAX__ * sizeof(void*));
	m_animInfo[0].m_hasAnim = false;
	m_animInfo[1].m_hasAnim = false;
}

CKLBScoreNode::~CKLBScoreNode()
{
	CKLBRenderingManager& pMgr = CKLBRenderingManager::getInstance();
	for (int n=0; n < __SCORE_LEN_MAX__; n++) {
		if (m_scoreOldNode[n]) {
			// 1. Back up sprite.
			CKLBRenderCommand* pComm = m_scoreOldNode[n]->getRender();
			m_scoreOldNode[n]->setRender(NULL);
			// 2. Destroy node -> remove from render list if any.
			KLBDELETE(m_scoreOldNode[n]);
			// 3. Destroy sprite if any.
			if (pComm) { pMgr.releaseCommand(pComm); }
		}
		if (m_scoreNewNode[n]) {
			// Order important !
			CKLBRenderCommand* pComm = m_scoreNewNode[n]->getRender();
			m_scoreNewNode[n]->setRender(NULL);
			KLBDELETE(m_scoreNewNode[n]);
			if (pComm) { pMgr.releaseCommand(pComm); }
		}
	}

	if (m_dotNode) {
		m_dotNode->setRender(NULL);
		KLBDELETE(m_dotNode);
	}

	if (m_dotSprite) {
		pMgr.releaseCommand(m_dotSprite);
	}

	for (int n = 0; n < __SCORE_COMMA_MAX__; n++) {
		if (m_commaNode[n]) {
			m_commaNode[n]->setRender(NULL);
			KLBDELETE(m_commaNode[n]);
		}
		if (m_commaSprite[n]) {
			pMgr.releaseCommand(m_commaSprite[n]);
		}
	}
}

void CKLBScoreNode::setDot(	CKLBImageAsset* dotAsset, s32 width, s32 height, s32 offsetX, s32 offsetY ) {
	m_dotSprite->switchImage(dotAsset);

	m_widthDot = (float)width;
	m_heightDot= (float)height;
	m_dotOffsetX = (float)offsetX;
	m_dotOffsetY = (float)offsetY;

	m_dotNode->markUpMatrix();
}

void CKLBScoreNode::setComma(CKLBImageAsset* commaAsset, s32 width, s32 height, s32 offsetX, s32 offsetY) {
	m_widthComma = (float)width;
	m_heightComma = (float)height;
	m_commaOffsetX = (float)offsetX;
	m_commaOffsetY = (float)offsetY;

	for (int n = 0; n < __SCORE_COMMA_MAX__; n++) {
		m_commaSprite[n]->switchImage(commaAsset);
		m_commaNode[n]->markUpMatrix();
	}
}

void CKLBScoreNode::setDotActive(u32 position) {
	m_DotPosition = position;
}

void CKLBScoreNode::setScore(u64 value, bool animFirstTime) {
	setDotActive(255); // Outside of range.
	setScoreInternal(value, animFirstTime);
}

void CKLBScoreNode::setScoreFloat(double value, u32 dotPosition, bool animFirstTime) {
	static double s_multiplier[18] = {
		1.0,
		10.0,
		100.0,
		1000.0,
		10000.0,
		100000.0,
		1000000.0,
		10000000.0,
		100000000.0,
		1000000000.0,
		10000000000.0,
		100000000000.0,
		1000000000000.0,
		10000000000000.0,
		100000000000000.0,
		1000000000000000.0,
		10000000000000000.0,
		100000000000000000.0,
	};

	if (dotPosition >= 17) {
		dotPosition = 17;
	}

	value *= s_multiplier[dotPosition];
	value += 0.5;

	dotPosition = m_scoreLength - dotPosition;

	// Float to int
	u64 score = (u64)value;
	setDotActive(dotPosition);
	setScoreInternal(score, animFirstTime);
}


bool CKLBScoreNode::init(	u32						basePriority,
							s32						oldNumberPriorityOffset,
							CKLBImageAsset*		 	char0_9[10],
							s32 					stepX,
							s32 					stepY,
							u32 					score_length,
							bool					fillWithZero,
							bool					animAll) {
	bool res = true;
	m_animAll		= animAll;
	m_default		= (fillWithZero     ) ? 0 : 10;
	m_scoreLength	= (score_length > 18) ? 18 : (score_length < 1 ? 1 : score_length);
	m_oldNumberPriorityOffset	= oldNumberPriorityOffset;
	m_stepX			= stepX;
	m_stepY			= stepY;

	memcpy32(m_font, char0_9, 10 * sizeof(CKLBImageAsset*));

	CKLBRenderingManager& pMgr = CKLBRenderingManager::getInstance();

	for (int n = 0; n < __SCORE_LEN_MAX__; n++) {
		m_oldScore[n] = 30; // Only occurs the first time.
	}

	for (int n = 0; n < m_scoreLength; n++) {
		float	fStepX			= (float)(n*m_stepX);
		float	fStepY			= (float)(n*m_stepY);

		m_scoreOldNode[n] = KLBNEW(CKLBSplineNode);
		m_scoreNewNode[n] = KLBNEW(CKLBSplineNode);
		CKLBSprite* pSpr0 = pMgr.allocateCommandSprite(4,6);
		CKLBSprite* pSpr1 = pMgr.allocateCommandSprite(4,6);

		if ((!m_scoreNewNode[n]) || (!m_scoreOldNode	[n]) || (!pSpr0) || (!pSpr1)) {
			res = false; break;
		}

		m_scoreOldNode[n]->setRender(pSpr0);
		m_scoreNewNode[n]->setRender(pSpr1);

		m_scoreOldNode[n]->setTranslate(fStepX, fStepY);

		((CKLBSprite*)m_scoreOldNode[n]->getRender())->changeOrder(pMgr, basePriority + n + oldNumberPriorityOffset);
		((CKLBSprite*)m_scoreNewNode[n]->getRender())->changeOrder(pMgr, basePriority + n);

		m_scoreNewNode[n]->setTranslate(fStepX, fStepY);
		this->addNode(m_scoreNewNode[n]);

	}

	m_dotSprite = pMgr.allocateCommandSprite(4,6);
	m_dotNode   = KLBNEW(CKLBNode);
	this->addNode(m_dotNode);

	for (int n = 0; n < __SCORE_COMMA_MAX__; n++) {
		m_commaSprite[n] = pMgr.allocateCommandSprite(4,6);
		m_commaNode[n] = KLBNEW(CKLBNode);
		this->addNode(m_commaNode[n]);
		if ((!m_commaSprite[n]) || (!m_commaNode[n])) {
			res = false;
			break;
		}
	}

	return res && (m_dotSprite != NULL) && (m_dotNode != NULL);
}

void CKLBScoreNode::setPriority(u32 order)
{
	CKLBRenderingManager& pRdr = CKLBRenderingManager::getInstance();

	for (int n = 0; n < m_scoreLength; n++) {
		((CKLBSprite*)m_scoreOldNode[n]->getRender())->changeOrder(pRdr, order + n + m_oldNumberPriorityOffset);
		((CKLBSprite*)m_scoreNewNode[n]->getRender())->changeOrder(pRdr, order + n);
	}

	if (m_DotPosition != 0xFF) {
		m_dotNode->setRender(m_dotSprite, 0);
		m_dotSprite->changeOrder(pRdr, order + m_DotPosition + m_oldNumberPriorityOffset);
	} else {
		m_dotNode->setRender(NULL, 0);
	}

	for (int n = 0; n < __SCORE_COMMA_MAX__; n++) {
		m_commaSprite[n]->changeOrder(pRdr, order + m_oldNumberPriorityOffset);
	}
}

void CKLBScoreNode::setAnimationInternal(	bool	isNew,
											s32		milliSecondsPlayTime,
											s32		timeShift,
											bool	/*onlyChange*/,
											u32		type,
											u32		affected,
											const float* arrayParam) {
	AnimInfo* pAnim = &m_animInfo[isNew ? 1 : 0];
	float* dst = pAnim->m_Anim;
	if (affected & CKLBSplineNode::ANM_X_COORD_0)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_Y_COORD_1)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_SCALE_COORD_2)	{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++;
															*dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_R_COLOR_3)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_G_COLOR_4)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_B_COLOR_5)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }
	if (affected & CKLBSplineNode::ANM_A_COLOR_6)		{   *dst++ = *arrayParam++;  *dst++ = *arrayParam++; }

	pAnim->m_affected				= affected;
	pAnim->m_milliSecondsPlayTime	= milliSecondsPlayTime;
	pAnim->m_type					= type;
	pAnim->m_timeShift				= timeShift;
	pAnim->m_hasAnim				= affected != 0;
	reAssignAnim(isNew ? 1 : 0);
}

void CKLBScoreNode::reAssignAnim(u32 idx) {
	float localFloat[16];

	AnimInfo* pAnim = &m_animInfo[idx];
	if (pAnim->m_hasAnim) {
		CKLBSplineNode** pNodeArr = (idx != 0) ? m_scoreNewNode : m_scoreOldNode;

		const float* backup = pAnim->m_Anim;
		memcpy(localFloat, backup, sizeof(float)*16);
		for (int n=0; n < m_scoreLength; n++) {
			float* backup = pAnim->m_Anim;
			float*	dst             = localFloat;
			int		m				= m_scoreLength - n;
			int		timeShiftLocal	= m*pAnim->m_timeShift;

			float   offsetX			= ((n >= m_DotPosition) ? m_widthDot : 0.0f);
			float   offsetY			= ((n >= m_DotPosition) ? m_heightDot: 0.0f);

			float	fStepX			= ((float)(n*m_stepX)) + offsetX;
			float	fStepY			= ((float)(n*m_stepY)) + offsetY;

			if (pAnim->m_affected & CKLBSplineNode::ANM_X_COORD_0)	{   *dst++ = (*backup++) + fStepX;  *dst++ = (*backup++) + fStepX; }
			if (pAnim->m_affected & CKLBSplineNode::ANM_Y_COORD_1)	{   *dst++ = (*backup++) + fStepY;  *dst++ = (*backup++) + fStepY; }

			pNodeArr[n]->setAnimation(pAnim->m_milliSecondsPlayTime, timeShiftLocal, pAnim->m_type, NULL, 0, pAnim->m_affected, localFloat);
		}
	}
}

/**
	No animation : affected = 0
 */
void CKLBScoreNode::setEnterAnimation	(s32 milliSecondsPlayTime, s32 timeShift, bool onlyChange, u32 type, u32 affected, const float* arrayParam)
{
	if (!arrayParam) { affected = 0; }

	//
	// Enter animation : never remove.
	//
	affected &= ~CKLBSplineNode::ANM_REMOVESCENE;

	setAnimationInternal(true, milliSecondsPlayTime, timeShift, onlyChange, type, affected, arrayParam);

	m_newAnimated = (affected != 0);
}

void CKLBScoreNode::setExitAnimation	(s32 milliSecondsPlayTime, s32 timeShift, bool onlyChange, u32 type, u32 affected, const float* arrayParam)
{
	if (!arrayParam) { affected = 0; }

	setAnimationInternal(false, milliSecondsPlayTime, timeShift, onlyChange, type, affected | CKLBSplineNode::ANM_REMOVESCENE, arrayParam);

	m_oldAnimated = (affected != 0);
}

void CKLBScoreNode::setScoreInternal	(u64 score, bool /*animFirstTime*/)
{
	if (score != m_uiScore) {
		m_uiScore       = score;
		m_bScoreChange  = true;
	}
}

void CKLBScoreNode::update(u32 order) {
	if (m_prevDotPosition != m_DotPosition) {
		m_bScoreChange = true;
		m_prevDotPosition = m_DotPosition;
		if (m_DotPosition >= m_scoreLength) {
			m_dotNode->setRender(NULL,0);

			reAssignAnim(0);
			reAssignAnim(1);

			int commaStep = m_scoreLength % 3;
			int commaCounter = commaStep ? (3 - commaStep) : commaStep;
			m_commaCount = 0;
			if (m_scoreLength) {
				int n = 0;
				do {
					float fStepX = (float)(n*m_stepX) + (m_commaCount * m_widthComma);
					float fStepY = (float)(n*m_stepY);
					int next = n + 1;

					if (commaCounter >= 2) {
						commaCounter = 0;
						if (m_scoreLength > next && m_commaNode[m_commaCount]) {
							m_commaNode[m_commaCount]->setTranslate(fStepX + m_stepX + m_commaOffsetX,
												m_heightComma + m_commaOffsetY);
							m_commaCount++;
						}
					} else {
						commaCounter++;
					}

					m_scoreOldNode[n]->setTranslate(fStepX, fStepY);
					m_scoreNewNode[n]->setTranslate(fStepX, fStepY);
				} while (++n < m_scoreLength);
			}
		} else {
			m_dotNode->setRender(m_dotSprite);

			reAssignAnim(0);
			reAssignAnim(1);

			float dotOffsetX = m_widthDot + m_dotOffsetX;
			float dotOffsetY = m_heightDot + m_dotOffsetY;
			for (int n = 0; n < m_scoreLength; n++) {
				float offsetX = ((n >= m_DotPosition) ? dotOffsetX : 0.0f);
				float offsetY = ((n >= m_DotPosition) ? dotOffsetY : 0.0f);

				float fStepX = ((float)(n*m_stepX)) + offsetX;
				float fStepY = ((float)(n*m_stepY)) + offsetY;

				m_scoreOldNode[n]->setTranslate(fStepX, fStepY);
				m_scoreNewNode[n]->setTranslate(fStepX, fStepY);
			}

			m_dotNode->setTranslate((float)(m_DotPosition * m_stepX),
								(float)(m_DotPosition * m_stepY));
		}
	}

	setPriority(order);

	if (m_bScoreChange) {
		u64 score = m_uiScore;
		u8 newScore[__SCORE_LEN_MAX__];
		m_bScoreChange = false;
		// Reset.
		newScore[0 ] = 0;  //m_default; // 0fill���Ȃ��ꍇ�ł�A1���ڂ�0�͕\������B
		newScore[1 ] = m_default;
		newScore[2 ] = m_default;
		newScore[3 ] = m_default;
		newScore[4 ] = m_default;
		newScore[5 ] = m_default;
		newScore[6 ] = m_default;
		newScore[7 ] = m_default;
		newScore[8 ] = m_default;
		newScore[9 ] = m_default;
		newScore[10] = m_default;
		newScore[11] = m_default;
		newScore[12] = m_default;
		newScore[13] = m_default;
		newScore[14] = m_default;
		newScore[15] = m_default;
		newScore[16] = m_default;
		newScore[17] = m_default;

		// Split the 64-bit value into two nine-digit groups so each digit can
		// be extracted using 32-bit arithmetic.
		u64 scoreGroup[2];
		scoreGroup[1] = score / 1000000000ULL;
		score %= 1000000000ULL;
		scoreGroup[0] = score;
		u8* digits = &newScore[9];
		for (int group = 1; group >= 0; group--, digits -= 9) {
			u32 part = scoreGroup[group];
			if (part) {
				while (part >= 100000000) {	part -= 100000000;	digits[8]++;	}
				while (part >=  10000000) {	part -=  10000000;	digits[7]++;	}
				while (part >=   1000000) {	part -=   1000000;	digits[6]++;	}
				while (part >=    100000) {	part -=    100000;	digits[5]++;	}
				while (part >=     10000) {	part -=     10000;	digits[4]++;	}
				while (part >=      1000) {	part -=      1000;	digits[3]++;	}
				while (part >=       100) {	part -=       100;	digits[2]++;	}
				while (part >=        10) {	part -=        10;	digits[1]++;	}
				while (part >=         1) {	part -=         1;	digits[0]++;	}
			}
		}

		//
		// Manage to handle that fill 0 with empty space works only for the BEGINNING of the score
		// ie : 000010000 -> ____10000
		//
		if (m_default != 0) {
			bool resetFill = false;
			for (int n = m_scoreLength - 1; n>=0; n--) {
				if (newScore[n] != 10) {
					resetFill = true;
					if (newScore[n] > 10) {
						newScore[n] -= 10;
					}
				} else {
					if (n <= m_scoreLength - m_DotPosition) {
						resetFill = true;
					}
					if (resetFill) {
						newScore[n] = 0;
					}
				}
			}
		}

		//
		// Perform display switch.
		//
		int commaIndex = m_commaCount;
		if (commaIndex > 0) {
			commaIndex--;
		}
		int commaCounter = -1;
		for (int n = 0; n < m_scoreLength; n++) {
			// From higher to lower.
			int m = ((m_scoreLength-1) - n);
			if ((m_oldScore[n] != newScore[n]) || m_animAll) {
				// New score graphic setup
				CKLBImageAsset* pAsset = m_font[newScore[n]];

				// ---- Atomic Operation ----
				((CKLBSprite*)m_scoreNewNode[m]->getRender())->switchImage(pAsset);
				// Changed only UV but node recompute use matrix changes.
				m_scoreNewNode[m]->markUpMatrixAndColor();
				// --------------------------

				// Set animation if needed.
				if (m_newAnimated) {
					// Set new font item.
					m_scoreNewNode[m]->play();
				}

				// If old animation is needed.
				if (m_oldAnimated) {
					// Put back in scene graph if animation is already complete.
					if (m_scoreOldNode[m]->getParent() == NULL) {
						this->addNode(m_scoreOldNode[m], 0);
					}

					if (m_oldScore[n] <= 10) {
						// Assign the previous digit only when it names a font image.
						CKLBImageAsset* pAsset = m_font[m_oldScore[n]];
						((CKLBSprite*)m_scoreOldNode[m]->getRender())->switchImage(pAsset);
						m_scoreOldNode[m]->markUpMatrixAndColor();
						m_scoreOldNode[m]->play();
					}
				}
			}

			if (commaCounter >= 2) {
				commaCounter = 0;
				if (commaIndex >= 0 && m_commaNode[commaIndex]) {
					if (m_default && (newScore[n] == m_default)) {
						m_commaNode[commaIndex]->setRender(NULL, 0);
					} else {
						m_commaNode[commaIndex]->setRender(m_commaSprite[commaIndex], 0);
					}
					commaIndex--;
				}
			} else {
				commaCounter++;
			}
			m_oldScore[n] = newScore[n];
		}
	}
}
