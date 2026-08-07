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
#ifndef CKLBUIClippedMap_h
#define CKLBUIClippedMap_h

#include "CKLBUITask.h"
#include "CKLBModalStack.h"

class CKLBNode;

class CKLBUIClippedMap : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUIClippedMap>;

private:
	struct TouchPoint {
		s32 fingerID;
		s32 startX;
		s32 startY;
		s32 currentX;
		s32 currentY;
	};

	struct ClickZone {
		u32 id;
		float x;
		float y;
		float radius;
	};

	CKLBUIClippedMap();
	virtual ~CKLBUIClippedMap();

	void resetTouches();
	void setLocked(bool locked);
	void releaseTouch(s32 fingerID);
	TouchPoint* getTouch(s32 fingerID, u8 create);
	void screenToMap(float x, float y, float* mapX, float* mapY);
	void mapToScreen(float x, float y, float* screenX, float* screenY);
	void updateZoomBounds();
	void updateFocus();
	void updateView(u32 changes);
	void setCoord(float mapX, float mapY, float screenX, float screenY, float zoom);
	void setFocus(float mapX, float mapY, float duration, bool animate);
	void executeFocusAnimation();
	bool processTouches();
	u32 updateGesture();
	bool setup(s32 baseOrder, s32 maxOrder, s32 x, s32 y,
			   s32 viewWidth, s32 viewHeight, bool vertical);
	bool setupClip(u32 baseOrder, u32 maxOrder,
				   s16 x, s16 y, s16 width, s16 height);

public:
	static CKLBUIClippedMap* create(CKLBUITask* parent, CKLBNode* node,
									s32 baseOrder, s32 maxOrder, s32 x, s32 y,
									s32 viewWidth, s32 viewHeight, bool vertical);

	virtual u32 getClassID();
	virtual void execute(u32 deltaT);

protected:
	virtual bool initUI(CLuaState& lua);
	virtual int commandUI(CLuaState& lua, int argc, int cmd);
	virtual void dieUI();

private:
	bool init(CKLBUITask* parent, CKLBNode* node,
			  s32 baseOrder, s32 maxOrder, s32 x, s32 y,
			  s32 viewWidth, s32 viewHeight, bool vertical);

	bool            m_dragActive;
	bool            m_pinchActive;
	bool            m_focusAnimation;
	bool            m_touchMoved;
	float           m_gestureScale;
	float           m_gestureBaseZoom;
	float           m_dragVelocityX;
	float           m_dragVelocityY;
	s32             m_pinchCenterX;
	s32             m_pinchCenterY;
	float           m_pinchMapX;
	float           m_pinchMapY;
	bool            m_locked;
	const char*     m_clickCallback;
	const char*     m_limitCallback;
	const char*     m_focusCallback;
	TouchPoint      m_touches[2];
	CKLBModalStack  m_modal;
	ClickZone       m_clickZones[100];
	u32             m_clickZoneCount;
	s32             m_basePriority;
	s32             m_endPriority;
	float           m_mapWidth;
	float           m_mapHeight;
	float           m_mapOffsetX;
	float           m_mapOffsetY;
	s32             m_viewWidth;
	s32             m_viewHeight;
	float           m_minZoom;
	float           m_maxZoom;
	float           m_minFocusX;
	float           m_maxFocusX;
	float           m_minFocusY;
	float           m_maxFocusY;
	float*          m_animationTargetX;
	float*          m_animationTargetY;
	float*          m_animationStep;
	float           m_focusX;
	float           m_focusY;
	float           m_zoom;
	void*           m_touchSurface;
	CKLBNode*       m_mapNode;
	u8              m_limitMask;

	static PROP_V2 ms_propItems[];
};

#endif // CKLBUIClippedMap_h
