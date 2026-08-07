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
#ifndef CKLBScrMgrSlide_h
#define CKLBScrMgrSlide_h

#include "IMgrEntry.h"

class CKLBScrMgrSurf : public CKLBScrollMgr
{
public:
	CKLBScrMgrSurf(float brake, float speedFactor);
	virtual ~CKLBScrMgrSurf();

	virtual void setMargin      (int top, int bottom);
	virtual void setInitial     (int pos);
	virtual void setPosition    (int pos, int dir);
	virtual int  getPosition    ();
	virtual int  getBarPosition ();
	virtual void execute        (u32 deltaT);
	virtual bool stillScrolling ();

	virtual void setScrollEnable        (bool enable);
	virtual void setScrollPhysicsInit   (int position);
	virtual void setScrollPhysicsTarget (int position);
	virtual void onScrollDragEnd        (int position);

private:
	void updatePosition();

	float m_brake;
	float m_velocity;
	float m_speedFactor;
	float m_position;
	float m_targetPosition;
	float m_accumulatedPosition;
	float m_previousDragPosition;
	float m_dragDelta;
	int   m_dragStartPosition;
	bool  m_scrollEnabled;
	bool  m_dragging;
	bool  m_callbackPending;
};

#endif // CKLBScrMgrSlide_h
