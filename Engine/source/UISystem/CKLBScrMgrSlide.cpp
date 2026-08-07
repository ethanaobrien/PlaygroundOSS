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
#include "CKLBScrMgrSlide.h"

#include <cmath>

void
CKLBScrMgrSurf::setMargin(int /* top */, int /* bottom */)
{
}

void
CKLBScrMgrSurf::setScrollEnable(bool enable)
{
	m_scrollEnabled = enable;
}

void
CKLBScrMgrSurf::setScrollPhysicsInit(int position)
{
	m_dragging             = false;
	m_dragStartPosition    = position;
	m_previousDragPosition = (float)position;
	m_velocity             = 0.0f;
	m_dragDelta            = 0.0f;
}

void
CKLBScrMgrSurf::setScrollPhysicsTarget(int position)
{
	float currentPosition = (float)position;
	m_dragDelta = currentPosition - m_previousDragPosition;
	if(m_dragging) {
		setScrollPhysicsInit(position);
	}
	m_previousDragPosition = currentPosition;

	m_accumulatedPosition -= m_dragDelta;
	m_accumulatedPosition += m_velocity;
	m_velocity *= m_brake;

	float displayPosition = m_accumulatedPosition;
	if(m_lenLoop > 0) {
		displayPosition = loopRound(displayPosition);
	} else if(displayPosition < 0.0f) {
		m_velocity = 0.0f;
		displayPosition = 0.0f;
	} else if(displayPosition > (float)m_maxPos) {
		m_velocity = 0.0f;
		displayPosition = (float)m_maxPos;
	}
	m_position = displayPosition;
	m_targetPosition = displayPosition;
	m_accumulatedPosition = displayPosition;
	m_callbackPending = true;
}

void
CKLBScrMgrSurf::updatePosition()
{
	m_accumulatedPosition += m_velocity;
	m_velocity *= m_brake;

	float position = m_accumulatedPosition;
	if(m_lenLoop > 0) {
		position = loopRound(position);
	} else if(position < 0.0f) {
		m_velocity = 0.0f;
		position = 0.0f;
	} else if(position > (float)m_maxPos) {
		m_velocity = 0.0f;
		position = (float)m_maxPos;
	}
	m_position = position;
}

void
CKLBScrMgrSurf::onScrollDragEnd(int /* position */)
{
	m_dragging = true;
	if(fabsf(m_dragDelta) < 0.5f) {
		m_dragDelta = 0.0f;
		m_velocity = 0.0f;
	}
	m_velocity -= m_dragDelta * m_speedFactor;
}

void
CKLBScrMgrSurf::setInitial(int position)
{
	setPosition(position, 0);
	m_accumulatedPosition = m_targetPosition;
	m_position = loopRound(m_targetPosition);
	m_velocity = 0.0f;
	m_precallPos = (int)m_position;
}

void
CKLBScrMgrSurf::setPosition(int position, int direction)
{
	if(m_scrollEnabled) {
		m_accumulatedPosition = m_targetPosition;
	} else {
		m_targetPosition = (float)position;
		if(m_lenLoop == 0) {
			if((direction == 1 && m_position >= (float)m_maxPos)
			|| (direction == -1 && m_position <= 0.0f)) {
				m_velocity = 0.0f;
			}
		}
		if(fabsf(m_velocity) < 0.5f) {
			m_velocity = 0.0f;
		}
		m_accumulatedPosition = m_targetPosition;
	}
	m_position = loopRound(m_targetPosition);
}

int
CKLBScrMgrSurf::getPosition()
{
	return (int)m_position;
}

int
CKLBScrMgrSurf::getBarPosition()
{
	return (int)m_position;
}

void
CKLBScrMgrSurf::execute(u32 /* deltaT */)
{
	if(fabsf(m_velocity) < 0.5f) {
		m_velocity = 0.0f;
		execCallback((int)m_position);
		m_callbackPending = false;
		return;
	}

	m_accumulatedPosition += m_velocity;
	m_velocity             *= m_brake;

	float position = m_accumulatedPosition;
	if(m_lenLoop > 0) {
		position = loopRound(position);
	} else {
		if(position < 0.0f) {
			m_velocity = 0.0f;
			position = 0.0f;
		} else if(position > (float)m_maxPos) {
			m_velocity = 0.0f;
			position = (float)m_maxPos;
		}
	}
	m_position = position;
}

bool
CKLBScrMgrSurf::stillScrolling()
{
	return fabsf(m_velocity) > 0.5f;
}
