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
#include "CKLBUIClippedMap.h"
#include "CKLBScriptEnv.h"
#include "CKLBUtility.h"
#include "CKLBUISystem.h"
#include "CKLBRendering.h"
#include "CKLBTouchPad.h"

enum {
	UI_CLMP_SET_MAX,
	UI_CLMP_SET_COORD,
	UI_CLMP_LOAD_BG,
	UI_CLMP_GET_MAPCOORD,
	UI_CLMP_GET_SCREENCOORD,
	UI_CLMP_ADD_CLICKZONE,
	UI_CLMP_UPDATE_CLICKZONE,
	UI_CLMP_REMOVE_CLICKZONE,
	UI_CLMP_SET_LUACALL,
	UI_CLMP_SET_LOCK,
	UI_CLMP_SET_ZOOM,
	UI_CLMP_SET_FOCUS
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_CLMP_SET_MAX",          0 },
	{ "UI_CLMP_SET_COORD",        1 },
	{ "UI_CLMP_LOAD_BG",          2 },
	{ "UI_CLMP_GET_MAPCOORD",     3 },
	{ "UI_CLMP_GET_SCREENCOORD",  4 },
	{ "UI_CLMP_ADD_CLICKZONE",    5 },
	{ "UI_CLMP_REMOVE_CLICKZONE", 7 },
	{ "UI_CLMP_SET_LUACALL",      8 },
	{ "UI_CLMP_SET_LOCK",         9 },
	{ "UI_CLMP_SET_ZOOM",        10 },
	{ "UI_CLMP_SET_FOCUS",       11 },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUIClippedMap> factory("UI_ClippedMap", 0x0008005a, cmd);

CKLBLuaPropTask::PROP_V2 CKLBUIClippedMap::ms_propItems[] = {
	UI_BASE_PROP
};

CKLBUIClippedMap::CKLBUIClippedMap()
: CKLBUITask(P_MENU)
, m_dragActive(false)
, m_pinchActive(false)
, m_focusAnimation(false)
, m_touchMoved(false)
, m_modal()
, m_clickZoneCount(0)
, m_animationTargetX(NULL)
, m_animationTargetY(NULL)
, m_animationStep(NULL)
, m_touchSurface(NULL)
, m_mapNode(NULL)
{
	m_newScriptModel = true;
	m_clickCallback = NULL;
	m_limitCallback = NULL;
	m_focusCallback = NULL;
}

CKLBUIClippedMap::~CKLBUIClippedMap()
{
	KLBDELETEA(m_clickCallback);
	m_clickCallback = NULL;
	KLBDELETEA(m_limitCallback);
	m_limitCallback = NULL;
	KLBDELETEA(m_focusCallback);
	m_focusCallback = NULL;
}

CKLBUIClippedMap*
CKLBUIClippedMap::create(CKLBUITask* parent, CKLBNode* node,
						 s32 baseOrder, s32 maxOrder, s32 x, s32 y,
						 s32 viewWidth, s32 viewHeight, bool vertical)
{
	CKLBUIClippedMap* task = KLBNEW(CKLBUIClippedMap);
	if(task && task->init(parent, node, baseOrder, maxOrder, x, y,
						 viewWidth, viewHeight, vertical)) {
		return task;
	}
	KLBDELETE(task);
	return NULL;
}

bool
CKLBUIClippedMap::init(CKLBUITask* parent, CKLBNode* node,
					   s32 baseOrder, s32 maxOrder, s32 x, s32 y,
					   s32 viewWidth, s32 viewHeight, bool vertical)
{
	if(!setupNode()) {
		return false;
	}
	bool result = setup(baseOrder, maxOrder, x, y, viewWidth, viewHeight, vertical);
	result = registUI(parent, result);
	if(result && m_mapNode) {
		parent->getNode()->removeNode(getNode());
		node->addNode(m_mapNode);
	}
	return result;
}

/*
 * Clipped-map setup establishes the view before touch processing is enabled.
 *
 * The base and maximum priorities delimit the pair of render-state commands
 * used to enter and leave the scissor range. They must remain ordered and
 * non-negative because other UI commands can occupy priorities around them.
 *
 * Requested view dimensions are clamped to one pixel. This keeps coordinate
 * conversion and zoom-bound calculations defined even while a layout is being
 * assembled from incomplete script data.
 *
 * Focus starts at the map origin with a unit zoom and unrestricted bounds.
 * The four-bit limit mask enables every edge until content dimensions provide
 * tighter limits. Existing touch slots are reset before the node is published.
 *
 * m_mapNode is the transform root for map content. The task's UI node owns it,
 * while m_pRootNode exposes the same typed node to inherited UI operations.
 *
 * setupClip installs two render-state commands on the UI node. The first
 * enables scissoring at the base priority and the second disables it at the
 * maximum priority, so descendants remain clipped without leaking state to
 * later rendering commands.
 *
 * The modal stack orientation follows the requested scrolling direction.
 * Pushing it only after clip setup ensures input cannot target a half-created
 * view. A failed clip range is reported through both the script environment
 * and the engine's fatal assertion contract.
 *
 * This helper is shared by native construction and initUI. Keeping all default
 * focus, zoom, touch, node, clip, and modal state here guarantees both entry
 * points publish the same initialized object.
 *
 * setup belongs beside init because it completes construction before any of
 * the gesture and coordinate helpers can observe the task.
 * Successful return therefore means rendering and input state are both live.
 */
bool
CKLBUIClippedMap::setup(s32 baseOrder, s32 maxOrder, s32 x, s32 y,
						s32 viewWidth, s32 viewHeight, bool vertical)
{
	if(!setupPropertyList((const char**)ms_propItems, SizeOfArray(ms_propItems))) {
		return false;
	}
	klb_assertNull(baseOrder >= 0, "Order Problem");
	klb_assertNull(maxOrder >= 0, "Order Problem");

	m_basePriority = baseOrder;
	m_endPriority = maxOrder;
	m_mapOffsetX = static_cast<float>(x);
	m_mapOffsetY = static_cast<float>(y);
	if(viewWidth <= 0) {
		viewWidth = 1;
	}
	if(viewHeight <= 0) {
		viewHeight = 1;
	}
	m_viewWidth = viewWidth;
	m_viewHeight = viewHeight;
	m_minZoom = 1.0f;
	m_maxZoom = 1.0f;
	m_focusX = 0.0f;
	m_focusY = 0.0f;
	m_minFocusX = 0.0f;
	m_maxFocusX = 0.0f;
	m_minFocusY = 0.0f;
	m_maxFocusY = 0.0f;
	m_limitMask = 15;
	m_zoom = 1.0f;
	m_locked = false;
	resetTouches();

	m_mapNode = KLBNEW(CKLBNode);
	if(!m_mapNode) {
		return false;
	}
	m_pUINode->addNode(m_mapNode);
	m_pRootNode = m_mapNode;

	bool result = setupClip(
		m_basePriority,
		m_endPriority,
		static_cast<s16>(m_mapOffsetX),
		static_cast<s16>(m_mapOffsetY),
		static_cast<s16>(viewWidth),
		static_cast<s16>(viewHeight)
	);
	m_modal.setModal(vertical);
	m_modal.push();
	if(!result) {
		const char* message = "Overlapping clipping range or Reached max UI clipping stack.";
		CKLBScriptEnv::getInstance().error(message);
		klb_assertAlways(message);
	}
	return true;
}

void
CKLBUIClippedMap::resetTouches()
{
	m_touches[0].fingerID = -1;
	m_touches[1].fingerID = -1;
	m_dragActive = false;
	m_pinchActive = false;
}

void
CKLBUIClippedMap::setLocked(bool locked)
{
	if(m_locked != locked) {
		if(locked) {
			resetTouches();
		}
		m_locked = locked;
	}
}

void
CKLBUIClippedMap::releaseTouch(s32 fingerID)
{
	if(m_touches[0].fingerID == fingerID) {
		m_touches[0].fingerID = -1;
	}
	if(m_touches[1].fingerID == fingerID) {
		m_touches[1].fingerID = -1;
	}
}

CKLBUIClippedMap::TouchPoint*
CKLBUIClippedMap::getTouch(s32 fingerID, u8 create)
{
	create ^= true;
	for(u32 i = 0; i < 2; ++i) {
		TouchPoint* touch = &m_touches[i];
		if(touch->fingerID == fingerID) {
			return touch;
		}
		u8 unavailable = create;
		unavailable |= touch->fingerID != -1;
		if(unavailable) {
			continue;
		}
		touch->fingerID = fingerID;
		return touch;
	}
	return NULL;
}

void
CKLBUIClippedMap::screenToMap(float x, float y, float* mapX, float* mapY)
{
	*mapX = (x - m_focusX) / m_zoom;
	*mapY = (y - m_focusY) / m_zoom;
}

void
CKLBUIClippedMap::mapToScreen(float x, float y, float* screenX, float* screenY)
{
	*screenX = (x * m_zoom) + m_focusX;
	*screenY = (y * m_zoom) + m_focusY;
}

void
CKLBUIClippedMap::updateZoomBounds()
{
	float maxX = ceilf(-((m_mapWidth * m_zoom) - m_viewWidth));
	m_minFocusX = maxX;
	if(maxX > 0.0f) {
		m_minFocusX = 0.0f;
		m_zoom = m_minZoom;
	}

	float maxY = ceilf(-((m_mapHeight * m_zoom) - m_viewHeight));
	m_minFocusY = maxY;
	if(maxY > 0.0f) {
		m_minFocusY = 0.0f;
		m_zoom = m_minZoom;
	}
}

void
CKLBUIClippedMap::updateFocus()
{
	if(m_zoom < m_minZoom) {
		m_zoom = m_minZoom;
	}
	if(m_zoom > m_maxZoom) {
		m_zoom = m_maxZoom;
	}

	u8 oldLimitMask = m_limitMask;
	u8 limitMask = oldLimitMask;
	if(m_focusX < m_minFocusX) {
		m_focusX = m_minFocusX;
	} else if(m_focusX > m_maxFocusX) {
		m_focusX = m_maxFocusX;
	}
	if(m_focusX <= m_minFocusX) {
		limitMask &= ~4;
	} else {
		limitMask |= 4;
	}
	if(m_focusX >= m_maxFocusX) {
		limitMask &= ~8;
	} else {
		limitMask |= 8;
	}

	if(m_focusY < m_minFocusY) {
		m_focusY = m_minFocusY;
	} else if(m_focusY > m_maxFocusY) {
		m_focusY = m_maxFocusY;
	}
	if(m_focusY <= m_minFocusY) {
		limitMask &= ~1;
	} else {
		limitMask |= 1;
	}
	if(m_focusY >= m_maxFocusY) {
		limitMask &= ~2;
	} else {
		limitMask |= 2;
	}

	bool limitChanged = limitMask != oldLimitMask;
	m_limitMask = limitMask;
	if(limitChanged && m_limitCallback) {
		CKLBScriptEnv::getInstance().call_eventClippedMap(
			m_limitCallback, this, m_limitMask
		);
	}
}

void
CKLBUIClippedMap::updateView(u32 changes)
{
	updateFocus();
	if(changes & 2) {
		m_mapNode->setScale(m_zoom, m_zoom);
	}
	if(changes & 1) {
		m_mapNode->setTranslate(
			m_focusX + m_mapOffsetX,
			m_focusY + m_mapOffsetY
		);
	}
}

void
CKLBUIClippedMap::setCoord(
	float mapX, float mapY,
	float screenX, float screenY,
	float zoom)
{
	m_focusX = screenX - (mapX * zoom);
	m_focusY = screenY - (mapY * zoom);

	u32 changes = 1;
	if(m_zoom != zoom) {
		m_zoom = zoom;
		updateZoomBounds();
		changes |= 2;
	}
	updateView(changes);
}

void
CKLBUIClippedMap::setFocus(
	float mapX, float mapY,
	float duration, bool animate)
{
	float focusX = ((float)m_viewWidth * 0.5f) - (mapX * m_zoom);
	float focusY = ((float)m_viewHeight * 0.5f) - (mapY * m_zoom);
	if(focusX > m_maxFocusX) {
		focusX = m_maxFocusX;
	} else if(focusX < m_minFocusX) {
		focusX = m_minFocusX;
	}
	if(focusY > m_maxFocusY) {
		focusY = m_maxFocusY;
	} else if(focusY < m_minFocusY) {
		focusY = m_minFocusY;
	}

	if(animate) {
		m_animationTargetX = KLBNEWC(float, (focusX));
		m_animationTargetY = KLBNEWC(float, (focusY));
		m_animationStep = KLBNEWC(float, (duration));
		m_focusAnimation = true;
		setLocked(true);
	} else {
		m_focusX = focusX;
		m_focusY = focusY;
		updateView(1);
	}
}

void
CKLBUIClippedMap::execute(u32)
{
	if(m_focusAnimation) {
		executeFocusAnimation();
	} else if(processTouches()) {
		u32 changes = updateGesture();
		if(changes) {
			updateView(changes);
		}
	}
}

void
CKLBUIClippedMap::executeFocusAnimation()
{
	float deltaX = *m_animationTargetX - m_focusX;
	float deltaY = *m_animationTargetY - m_focusY;
	float distanceSquared = (deltaX * deltaX) + (deltaY * deltaY);
	if((*m_animationStep * *m_animationStep) >= distanceSquared) {
		m_focusX = *m_animationTargetX;
		m_focusY = *m_animationTargetY;
		m_focusAnimation = false;
		setLocked(false);

		KLBDELETE(m_animationTargetX);
		KLBDELETE(m_animationTargetY);
		KLBDELETE(m_animationStep);
		m_animationTargetX = NULL;
		m_animationTargetY = NULL;
		m_animationStep = NULL;

		if(m_focusCallback) {
			CKLBScriptEnv::getInstance().call_eventClippedMap(m_focusCallback, this);
			KLBDELETE(m_focusCallback);
			m_focusCallback = NULL;
		}
	} else {
		float angle = atan2f(deltaY, deltaX);
		m_focusY += sinf(angle) * *m_animationStep;
		m_focusX += cosf(angle) * *m_animationStep;
	}

	updateFocus();
	m_mapNode->setTranslate(
		m_focusX + m_mapOffsetX,
		m_focusY + m_mapOffsetY
	);
}

bool
CKLBUIClippedMap::processTouches()
{
	if(!m_modal.isEnable()) {
		resetTouches();
		return false;
	}
	if(m_locked) {
		return false;
	}

	CKLBTouchPadQueue& queue = CKLBTouchPadQueue::getInstance();
	queue.startItem();

	bool changed = false;
	const PAD_ITEM* item;
	while((item = queue.getItem(true)) != NULL) {
		switch(item->type) {
		case PAD_ITEM::TAP:
			if(item->x >= m_mapOffsetX &&
			   item->y >= m_mapOffsetY &&
			   item->x < (m_mapOffsetX + m_viewWidth) &&
			   item->y < (m_mapOffsetY + m_viewHeight)) {
				TouchPoint* touch = getTouch(item->id, true);
				if(touch) {
					touch->startX = touch->currentX = item->x;
					touch->startY = touch->currentY = item->y;
				}
			}
			break;

		case PAD_ITEM::DRAG:
			for(u32 i = 0; i < 2; ++i) {
				if(m_touches[i].fingerID == item->id) {
					m_touches[i].currentX = item->x;
					m_touches[i].currentY = item->y;
					changed = true;
					break;
				}
			}
			break;

		case PAD_ITEM::RELEASE: {
			s32 firstFingerID = m_touches[0].fingerID;
			s32 secondFingerID = m_touches[1].fingerID;
			bool hadSingleTouch =
				(firstFingerID != -1) ^ (secondFingerID != -1);
			s32 releasedFingerID = item->id;
			if(firstFingerID == releasedFingerID) {
				m_touches[0].fingerID = -1;
				firstFingerID = -1;
			}
			if(secondFingerID == releasedFingerID) {
				m_touches[1].fingerID = -1;
				secondFingerID = -1;
			}
			changed = true;

			bool hasSingleTouch =
				(firstFingerID != -1) ^ (secondFingerID != -1);
			if(!hasSingleTouch && hadSingleTouch) {

				float mapX;
				float mapY;
				screenToMap(
					item->x - m_mapOffsetX,
					item->y - m_mapOffsetY,
					&mapX,
					&mapY
				);

				s32 focusIndex = -1;
				for(u32 i = 0; i < m_clickZoneCount; ++i) {
					float deltaX = mapX - m_clickZones[i].x;
					float deltaY = mapY - m_clickZones[i].y;
					float distance = sqrtf(
						(deltaX * deltaX) + (deltaY * deltaY)
					);
					if(distance < m_clickZones[i].radius) {
						focusIndex = static_cast<s32>(i);
						break;
					}
				}

				if(m_clickCallback) {
					CKLBScriptEnv::getInstance().call_eventClippedMapTouch(
						m_clickCallback,
						this,
						static_cast<s32>(mapX),
						static_cast<s32>(mapY),
						focusIndex
					);
				}
			}
			break;
		}

		default:
			break;
		}
	}
	return changed;
}

u32
CKLBUIClippedMap::updateGesture()
{
	s32 firstFingerID = m_touches[0].fingerID;
	s32 secondFingerID = m_touches[1].fingerID;
	bool hasSecondTouch = secondFingerID != -1;
	if(firstFingerID == -1) {
		if(hasSecondTouch) {
			m_pinchActive = false;
		} else {
			m_pinchActive = false;
			m_dragActive = false;
			m_touchMoved = false;
			return 0;
		}
	} else if(!hasSecondTouch) {
		m_pinchActive = false;
	} else {
		m_touchMoved = true;
		m_dragActive = false;
		if(m_pinchActive) {
			s32 baseDeltaX = m_touches[0].startX - m_touches[1].startX;
			s32 baseDeltaY = m_touches[0].startY - m_touches[1].startY;
			s32 currentDeltaX = m_touches[0].currentX - m_touches[1].currentX;
			s32 currentDeltaY = m_touches[0].currentY - m_touches[1].currentY;
			float baseDistance = sqrtf(
				static_cast<float>(
					(baseDeltaX * baseDeltaX) + (baseDeltaY * baseDeltaY)
				)
			);
			float currentDistance = sqrtf(
				static_cast<float>(
					(currentDeltaX * currentDeltaX) +
					(currentDeltaY * currentDeltaY)
				)
			);

			float zoom = (
				currentDistance / (baseDistance < 1.0f ? 1.0f : baseDistance)
			) * m_gestureScale;
			zoom = zoom < m_minZoom ? m_minZoom : zoom;
			zoom = zoom > m_maxZoom ? m_maxZoom : zoom;
			setCoord(
				m_pinchMapX,
				m_pinchMapY,
				static_cast<float>(m_pinchCenterX),
				static_cast<float>(m_pinchCenterY),
				zoom
			);
			return 0;
		}

		m_pinchActive = true;
		m_gestureScale = m_zoom;
		m_gestureBaseZoom = 1.0f;
		m_pinchCenterX = static_cast<s32>(
			static_cast<float>(
				(m_touches[0].startX + m_touches[1].startX) >> 1
			) - m_mapOffsetX
		);
		m_pinchCenterY = static_cast<s32>(
			static_cast<float>(
				(m_touches[0].startY + m_touches[1].startY) >> 1
			) - m_mapOffsetY
		);
		screenToMap(
			static_cast<float>(m_pinchCenterX),
			static_cast<float>(m_pinchCenterY),
			&m_pinchMapX,
			&m_pinchMapY
		);
		return 0;
	}

	if(m_touchMoved) {
		return 0;
	}

	if(!m_dragActive) {
		m_dragActive = true;
		m_dragVelocityX = m_focusX;
		m_dragVelocityY = m_focusY;
		return 0;
	}

	TouchPoint& touch = m_touches[firstFingerID == -1 ? 1 : 0];
	s32 deltaX = touch.currentX - touch.startX;
	s32 deltaY = touch.currentY - touch.startY;
	if(abs(deltaX) <= 4 && abs(deltaY) <= 4) {
		return 1;
	}
	m_focusX = m_dragVelocityX + static_cast<float>(deltaX);
	m_focusY = m_dragVelocityY + static_cast<float>(deltaY);
	return 1;
}

bool
CKLBUIClippedMap::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 7 || argc > 8) {
		return false;
	}

	s32 baseOrder = lua.getInt(2);
	s32 maxOrder = lua.getInt(3);
	s32 x = static_cast<s32>(lua.getFloat(4));
	s32 y = static_cast<s32>(lua.getFloat(5));
	s32 viewWidth = static_cast<s32>(lua.getFloat(6));
	s32 viewHeight = static_cast<s32>(lua.getFloat(7));
	bool vertical = argc >= 8 ? lua.getBoolean(8) : false;
	return setup(baseOrder, maxOrder, x, y, viewWidth, viewHeight, vertical);
}

bool
CKLBUIClippedMap::setupClip(u32 baseOrder, u32 maxOrder,
						   s16 x, s16 y, s16 width, s16 height)
{
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	CKLBRenderState* clipStart = renderingManager.allocateCommandState();
	CKLBRenderState* clipEnd = renderingManager.allocateCommandState();
	CKLBNode* node = m_pUINode;
	if(node->setRenderSlotCount(2) && clipStart && clipEnd) {
		clipStart->changeOrder(renderingManager, baseOrder);
		clipEnd->changeOrder(renderingManager, maxOrder);
		clipStart->setUse(true, false, NULL);
		clipEnd->setUse(true, false, NULL);
		clipStart->setScissor(true, x, y, width, height);
		clipEnd->setScissor(false);
		node->setRender(clipStart, 0);
		node->setRender(clipEnd, 1);
		m_touchSurface = CKLBUISystem::registerClip(clipStart, clipEnd);
		return m_touchSurface != NULL;
	}

	if(clipEnd) {
		renderingManager.releaseCommand(clipEnd);
	}
	if(clipStart) {
		renderingManager.releaseCommand(clipStart);
	}
	return false;
}

int
CKLBUIClippedMap::commandUI(CLuaState& lua, int argc, int command)
{
	switch(command) {
	case UI_CLMP_SET_MAX: {
		if(argc != 8) {
			lua.retBoolean(false);
			return 1;
		}
		m_minZoom   = lua.getFloat(3);
		m_maxZoom   = lua.getFloat(4);
		m_minFocusX = (float)-lua.getInt(6);
		m_maxFocusX = (float)-lua.getInt(5);
		m_minFocusY = (float)-lua.getInt(8);
		m_maxFocusY = (float)-lua.getInt(7);

		if(m_maxFocusX <= m_minFocusX) {
			m_maxFocusX = m_minFocusX + 1.0f;
		}
		if(m_maxFocusY <= m_minFocusY) {
			m_maxFocusY = m_minFocusY + 1.0f;
		}

		float viewWidth  = static_cast<float>(m_viewWidth);
		float viewHeight = static_cast<float>(m_viewHeight);
		m_mapWidth  = (m_maxFocusX - m_minFocusX) + viewWidth;
		m_mapHeight = (m_maxFocusY - m_minFocusY) + viewHeight;
		float fitZoom = fmaxf(
			viewWidth  / m_mapWidth,
			viewHeight / m_mapHeight
		);
		if(fitZoom > m_minZoom) {
			m_minZoom = fitZoom;
		}
		updateFocus();
		lua.retBoolean(true);
		return 1;
	}

	case UI_CLMP_SET_COORD: {
		if(argc < 6) {
			lua.retBoolean(false);
			return 1;
		}
		float mapX    = (float)lua.getInt(3);
		float mapY    = (float)lua.getInt(4);
		float screenX = (float)lua.getInt(5);
		float screenY = (float)lua.getInt(6);
		float zoom = 0.0f;
		if(argc >= 7) {
			zoom = lua.getFloat(7);
		}
		if(zoom <= 0.0f) {
			zoom = m_zoom;
		}
		zoom = zoom < m_minZoom ? m_minZoom : zoom;
		zoom = zoom > m_maxZoom ? m_maxZoom : zoom;
		setCoord(mapX, mapY, screenX, screenY, zoom);
		lua.retBoolean(true);
		return 1;
	}

	case UI_CLMP_LOAD_BG: {
		if(argc != 3) {
			lua.retBoolean(false);
			return 1;
		}
		(void)lua.getString(3);
		lua.retBoolean(false);
		return 1;
	}

	case UI_CLMP_GET_MAPCOORD: {
		if(argc != 4) {
			lua.retBoolean(false);
			return 1;
		}
		s32 screenX = (s32)((float)lua.getInt(3) - m_mapOffsetX);
		s32 screenY = (s32)((float)lua.getInt(4) - m_mapOffsetY);
		float mapX;
		float mapY;
		screenToMap((float)screenX, (float)screenY, &mapX, &mapY);
		lua.retInt((int)mapX);
		lua.retInt((int)mapY);
		return 2;
	}

	case UI_CLMP_GET_SCREENCOORD: {
		if(argc != 4) {
			lua.retBoolean(false);
			return 1;
		}
		float mapX = (float)lua.getInt(3);
		float mapY = (float)lua.getInt(4);
		float screenX;
		float screenY;
		mapToScreen(mapX, mapY, &screenX, &screenY);
		lua.retInt((int)(screenX + m_mapOffsetX));
		lua.retInt((int)(screenY + m_mapOffsetY));
		return 2;
	}

	case UI_CLMP_ADD_CLICKZONE: {
		if(argc != 6) {
			lua.retBoolean(false);
			return 1;
		}
		u32 oldCount = m_clickZoneCount;
		u32 id       = (u32)lua.getInt(3);
		float x      = (float)lua.getInt(4);
		float y      = (float)lua.getInt(5);
		float radius = (float)lua.getInt(6);
		if(oldCount < 100) {
			ClickZone& zone = m_clickZones[m_clickZoneCount++];
			zone.id = id;
			zone.x = x;
			zone.y = y;
			zone.radius = radius;
		}
		lua.retBoolean(oldCount < 100);
		return 1;
	}

	case UI_CLMP_UPDATE_CLICKZONE: {
		if(argc != 6) {
			lua.retBoolean(false);
			return 1;
		}
		bool result = false;
		u32 id       = (u32)lua.getInt(3);
		float x      = (float)lua.getInt(4);
		float y      = (float)lua.getInt(5);
		s32 radius   = lua.getInt(6);
		for(u32 i = 0; i < m_clickZoneCount; ++i) {
			if(m_clickZones[i].id == id) {
				m_clickZones[i].x = x;
				m_clickZones[i].y = y;
				if(radius >= 0) {
					m_clickZones[i].radius = (float)radius;
				}
				result = true;
				break;
			}
		}
		lua.retBoolean(result);
		return 1;
	}

	case UI_CLMP_REMOVE_CLICKZONE: {
		if(argc != 3) {
			lua.retBoolean(false);
			return 1;
		}
		bool result = false;
		u32 id = (u32)lua.getInt(3);
		s32 next = 0;
		for(u32 i = 0; i < m_clickZoneCount; ++i) {
			if(m_clickZones[i].id == id) {
				result = true;
			} else {
				m_clickZones[next] = m_clickZones[i];
				++next;
			}
		}
		m_clickZoneCount = next;
		lua.retBoolean(result);
		return 1;
	}

	case UI_CLMP_SET_LUACALL: {
		if(argc != 4) {
			lua.retBoolean(false);
			return 1;
		}
		m_clickCallback = lua.isString(3)
			? CKLBUtility::copyString(lua.getString(3))
			: NULL;
		m_limitCallback = lua.isString(4)
			? CKLBUtility::copyString(lua.getString(4))
			: NULL;
		lua.retBoolean(true);
		return 1;
	}

	case UI_CLMP_SET_LOCK:
		setLocked(lua.getBoolean(3));
		lua.retBoolean(true);
		return 1;

	case UI_CLMP_SET_ZOOM: {
		float zoom = lua.getFloat(3);
		float mapX = lua.getFloat(4);
		float mapY = lua.getFloat(5);
		if(m_zoom != zoom) {
			m_zoom = zoom;
			if(m_zoom < m_minZoom) {
				m_zoom = m_minZoom;
			} else if(m_zoom > m_maxZoom) {
				m_zoom = m_maxZoom;
			}
			updateZoomBounds();
			m_mapNode->setScale(m_zoom, m_zoom);
		}
		setFocus(mapX, mapY, 0.0f, false);
		return 0;
	}

	case UI_CLMP_SET_FOCUS: {
		if(argc != 7) {
			lua.retBoolean(false);
			return 1;
		}
		float mapX = lua.getFloat(3);
		float mapY = lua.getFloat(4);
		float duration = lua.getFloat(5);
		bool animate = lua.getBoolean(6);
		setFocus(mapX, mapY, duration, animate);
		klb_assertNull(
			m_focusCallback == NULL,
			"setFocus has been called, and didn't finish"
		);
		m_focusCallback = (animate && lua.isString(7))
			? CKLBUtility::copyString(lua.getString(7))
			: NULL;
		return 0;
	}
	}

	lua.retBoolean(false);
	return 1;
}

u32
CKLBUIClippedMap::getClassID()
{
	return 0x0008005a;
}

void
CKLBUIClippedMap::dieUI()
{
	if(m_touchSurface) {
		CKLBUISystem::unregisterClip(m_touchSurface);
	}
}
