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
#include "CUICover.h"
#include "CKLBUIPolyline2.h"
#include "CKLBUIPolygon.h"
#include "CKLBRendering.h"
#include "CKLBDataHandler.h"
#include "CKLBUtility.h"
#include "CKLBDrawTask.h"
#include "TextureManagement.h"
#include <limits.h>
#include <math.h>

enum {
	UI_COVER_ADDRECT,
	UI_COVER_REMOVERECT,
	UI_COVER_CLEARRECT,
	UI_COVER_SETCOLOR,
	UI_COVER_BUILD,
	UI_GLBG_SETTEXTURE
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_COVER_ADDRECT",    UI_COVER_ADDRECT    },
	{ "UI_COVER_REMOVERECT", UI_COVER_REMOVERECT },
	{ "UI_COVER_CLEARRECT",  UI_COVER_CLEARRECT  },
	{ "UI_COVER_SETCOLOR",   UI_COVER_SETCOLOR   },
	{ "UI_COVER_BUILD",      UI_COVER_BUILD      },
	{ "UI_GLBG_SETTEXTURE",  UI_GLBG_SETTEXTURE  },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUICover> factory("UI_Cover", CKLBUICover::CLASS_ID, cmd);

enum {
	UI_POLYLINE_CLEAR,
	UI_POLYLINE_ADDPOINT,
	UI_POLYLINE_BUILD
};

static IFactory::DEFCMD polylineCmd[] = {
	{ "UI_POLYLINE_CLEAR",    UI_POLYLINE_CLEAR    },
	{ "UI_POLYLINE_ADDPOINT", UI_POLYLINE_ADDPOINT },
	{ "UI_POLYLINE_BUILD",    UI_POLYLINE_BUILD    },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUIPolyline2> polylineFactory(
	"UI_Polyline2", 0x00080101, polylineCmd);

enum {
	UI_POLYGON_NEWPATH,
	UI_POLYGON_NEWHOLE,
	UI_POLYGON_ADDPOINT,
	UI_POLYGON_ENDHOLE,
	UI_POLYGON_PUSHPATH,
	UI_POLYGON_BUILD,
	UI_POLYGON_SETTEXTURE
};

static IFactory::DEFCMD polygonCmd[] = {
	{ "UI_POLYGON_NEWPATH",    UI_POLYGON_NEWPATH    },
	{ "UI_POLYGON_NEWHOLE",    UI_POLYGON_NEWHOLE    },
	{ "UI_POLYGON_ADDPOINT",   UI_POLYGON_ADDPOINT   },
	{ "UI_POLYGON_ENDHOLE",    UI_POLYGON_ENDHOLE    },
	{ "UI_POLYGON_PUSHPATH",   UI_POLYGON_PUSHPATH   },
	{ "UI_POLYGON_BUILD",      UI_POLYGON_BUILD      },
	{ "UI_POLYGON_SETTEXTURE", UI_POLYGON_SETTEXTURE },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUIPolygon> polygonFactory(
	"UI_Polygon", CKLBUIPolygon::CLASS_ID, polygonCmd);

CKLBLuaPropTask::PROP_V2 CKLBUICover::ms_propItems[] = {
	UI_BASE_PROP,
	{ "order", UINTEGER, (setBoolT)&CKLBUICover::setOrder, (getBoolT)&CKLBUICover::getOrder, 0 }
};

CKLBUICover::CKLBUICover()
: CKLBUITask(P_MENU)
{
	for(s32 i = 0; i < COVER_COUNT; ++i) {
		m_covers[i].left = 0;
		m_covers[i].top = 0;
		m_covers[i].right = 0;
		m_covers[i].bottom = 0;
		m_covers[i].available = true;
	}
	m_coverCount = 0;
	m_dynSprite = NULL;
	m_imageAsset = NULL;
	m_imageLoaded = false;
	m_screenLeft = -1000;
	m_screenRight = 3000;
	m_screenTop = -1000;
	m_screenBottom = 3000;
	m_assetHandle = 0;
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUICover *
CKLBUICover::create(CKLBUITask * parent, CKLBNode * node, u32 order, u32 color)
{
	CKLBUICover * cover = KLBNEW(CKLBUICover);
	if(!cover) { return NULL; }
	if(!cover->init(parent, node, order, color)) {
		KLBDELETE(cover);
		return NULL;
	}
	return cover;
}

bool
CKLBUICover::init(CKLBUITask * parent, CKLBNode *, u32 order, u32 color)
{
	if(!setupNode()) { return false; }
	bool result = initCore(order, color);
	return registUI(parent, result);
}

bool
CKLBUICover::initCore(u32 order, u32 color)
{
	if(!setupPropertyList((const char **)ms_propItems, SizeOfArray(ms_propItems))) {
		return false;
	}
	klb_assertNull(((s32)order >= 0), "Order Problem");
	m_order = order;
	m_usedIndexCount = 0;
	m_usedVertexCount = 0;
	m_dynSprite = CKLBRenderingManager::getInstance().allocateCommandDynSprite(
		VERTEX_COUNT, INDEX_COUNT);
	if(m_dynSprite) {
		getNode()->setRender(m_dynSprite);
		getNode()->setRenderOnDestroy(true);
		getNode()->setPriority(order);
		m_coverCount = 0;
		setCoverColor(color);
		rebuildGeometry();
	}
	return m_dynSprite != NULL;
}

bool
CKLBUICover::initUI(CLuaState& lua)
{
	if(lua.numArgs() != 4) { return false; }
	u32 order = lua.getInt(2);
	u32 alpha = lua.getInt(4);
	if(alpha > 255) { alpha = 255; }
	u32 color = lua.getInt(3) & 0x00ffffff;
	return initCore(order, color | (alpha << 24));
}

int
CKLBUICover::commandScript(CLuaState& lua, int argc, int cmd)
{
	switch(cmd) {
	case UI_COVER_ADDRECT:
		if(argc != 6) {
			lua.retBool(false);
			return 1;
		}
		{
			s32 x      = lua.getInt(3);
			s32 y      = lua.getInt(4);
			s32 width  = lua.getInt(5);
			s32 height = lua.getInt(6);
			lua.retInt(addCover(x, y, width, height));
			return 1;
		}
		break;
	case UI_COVER_REMOVERECT:
		if(argc <= 2) {
			lua.retBool(false);
			return 1;
		}
		{
			s32 index = lua.getInt(3);
			if(index < 0) {
				lua.retBool(true);
				return 1;
			}
			removeCover(index);
		}
		break;
	case UI_COVER_CLEARRECT:
		if(argc <= 1) {
			lua.retBool(false);
			return 1;
		}
		m_coverCount = 0;
		clearCovers();
		rebuildGeometry();
		break;
	case UI_COVER_SETCOLOR:
		if(argc <= 3) {
			lua.retBool(false);
			return 1;
		}
		{
			u32 color = lua.getInt(3) & 0x00ffffff;
			u32 alpha = lua.getInt(4);
			if(alpha > 255) { alpha = 255; }
			setCoverColor(color | (alpha << 24));
		}
		break;
	case UI_COVER_BUILD:
		rebuildGeometry();
		break;
	case UI_GLBG_SETTEXTURE:
		if(argc <= 6) {
			lua.retBool(false);
			return 1;
		}
		{
			const char * asset = lua.getString(3);
			bool repeatX = lua.getBool(4);
			bool repeatY = lua.getBool(5);
			float scaleX = lua.getFloat(6);
			float scaleY = lua.getFloat(7);
			setup(asset, repeatX, repeatY, scaleX, scaleY);
		}
		break;
	default:
		lua.retBool(false);
		return 1;
	}
	lua.retBool(true);
	return 1;
}

void
CKLBUICover::execute(u32)
{
	klb_assert(m_imageLoaded && CKLBDrawResource::getInstance().isInSafeAreaCallback(),
			   "SHOULD NEVER EXECUTE");
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	s32 left = (s32)draw.toLogical((float)-draw.screenBorderX());
	s32 top = (s32)draw.toLogical((float)-draw.screenBorderY());
	s32 right = (s32)draw.toLogical((float)(draw.phisicalWidth() - draw.screenBorderX()));
	s32 bottom = (s32)draw.toLogical((float)(draw.phisicalHeight() - draw.screenBorderY()));
	m_screenLeft = left - 1;
	m_screenTop = top - 1;
	m_screenRight = right + 1;
	m_screenBottom = bottom + 1;
	rebuildGeometry();
}

void
CKLBUICover::updateScreenSize()
{
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	s32 left = (s32)draw.toLogical((float)-draw.screenBorderX());
	s32 top = (s32)draw.toLogical((float)-draw.screenBorderY());
	s32 right = (s32)draw.toLogical((float)(draw.phisicalWidth() - draw.screenBorderX()));
	s32 bottom = (s32)draw.toLogical((float)(draw.phisicalHeight() - draw.screenBorderY()));
	m_screenLeft = left - 1;
	m_screenTop = top - 1;
	m_screenRight = right + 1;
	m_screenBottom = bottom + 1;
}

void
CKLBUICover::dieUI()
{
	CKLBDrawResource::getInstance().registerSafeAreaListener(false, this);
	CKLBDataHandler::releaseHandle((u16)m_assetHandle);
}

void
CKLBUICover::setCoverColor(u32 color)
{
	u32 lowChannel = color >> 16;
	u32 highChannel = color << 16;
	highChannel &= 0x00ff0000;
	lowChannel &= 0x000000ff;
	color &= 0xff00ff00;
	color |= highChannel;
	color |= lowChannel;
	for(s32 i = 0; i < 88; ++i) {
		m_dynSprite->setVertexColor(NULL, i, color);
	}
	getNode()->markUpMatrixAndColor();
}

s32
CKLBUICover::findAvailableCover() const
{
	for(s32 i = 0; i < COVER_COUNT; ++i) {
		if(m_covers[i].available) { return i; }
	}
	return -1;
}

void
CKLBUICover::insertEvent(s32 coordinate, CoverRect * rect, bool opening)
{
	s32 eventCount = m_coverCount * 2 + (opening ? 0 : 1);
	s32 position = 0;
	while(position < eventCount) {
		if(m_events[position].coordinate >= coordinate) {
			for(s32 i = eventCount; i >= position; --i) {
				m_events[i] = m_events[i - 1];
			}
			break;
		}
		++position;
	}
	m_events[position].coordinate = coordinate;
	m_events[position].opening = opening;
	m_events[position].rect = rect;
}

s32
CKLBUICover::addCover(s32 x, s32 y, s32 width, s32 height)
{
	s32 index = -1;
	if(width > 0 && height > 0) {
		index = findAvailableCover();
		if(index >= 0) {
			CoverRect& cover = m_covers[index];
			s32 right = x + width;
			s32 bottom = y + height;
			cover.left = x;
			cover.top = y;
			cover.right = right;
			cover.bottom = bottom;
			cover.available = false;
			insertEvent(y, &cover, true);
			insertEvent(bottom, &cover, false);
			++m_coverCount;
		}
	}
	return index;
}

void
CKLBUICover::removeCover(s32 index)
{
	if(index < 0) { return; }
	CoverRect * removed = &m_covers[index];
	removeEvents(removed);
	removed->available = true;
}

void
CKLBUICover::removeEvents(CoverRect * removed)
{
	CoverEvent * input = m_events;
	CoverEvent * output = m_events;
	bool found = false;
	for(s32 i = 0; i < m_coverCount * 2; ++i) {
		*output = *input;
		if(output->rect == removed) {
			found = true;
		} else {
			++output;
		}
		++input;
	}
	if(found) { --m_coverCount; }
}

void
CKLBUICover::clearCovers()
{
	for(s32 i = 0; i < COVER_COUNT; ++i) {
		m_covers[i].available = true;
	}
}

void
CKLBUICover::setup(const char * asset, bool repeatX, bool repeatY,
				   float scaleX, float scaleY)
{
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	s32 left = (s32)draw.toLogical((float)-draw.screenBorderX());
	s32 top = (s32)draw.toLogical((float)-draw.screenBorderY());
	s32 right = (s32)draw.toLogical((float)(draw.phisicalWidth() - draw.screenBorderX()));
	s32 bottom = (s32)draw.toLogical((float)(draw.phisicalHeight() - draw.screenBorderY()));
	m_screenLeft = left - 1;
	m_screenTop = top - 1;
	m_screenRight = right + 1;
	m_screenBottom = bottom + 1;
	m_repeatX = repeatX;
	m_repeatY = repeatY;
	m_scaleX = scaleX;
	m_scaleY = scaleY;

	CKLBDataHandler::releaseHandle((u16)m_assetHandle);
	m_imageAsset = (CKLBImageAsset *)CKLBUtility::loadAsset(asset, &m_assetHandle);
	if(!m_imageAsset) { return; }
	m_imageLoaded = true;
	m_imageWidth = m_imageAsset->getTexture()->m_width;
	m_imageHeight = m_imageAsset->getTexture()->m_height;
	m_dynSprite->setTexture(m_imageAsset->getTexture()->m_pTextureUsage);
	m_imageAsset->getTexture()->m_pTextureUsage->setWrapping(
		CTextureUsage::REPEAT, CTextureUsage::REPEAT);
	if(m_order == 0) {
		m_dynSprite->setRenderState(CKLBRenderingManager::getInstance().getAlphaState());
	}
	rebuildGeometry();
	CKLBDrawResource::getInstance().registerSafeAreaListener(true, this);
}

void
CKLBUICover::rebuildGeometry()
{
	s32 currentY = m_screenTop;
	s32 bandTop = currentY;
	m_usedIndexCount = 0;
	m_usedVertexCount = 0;
	m_events[m_coverCount * 2].coordinate = 3000;

	bool firstBand = true;
	if(m_coverCount > 0) {
		s32 eventIndex = 0;
		s32 activeCount = 0;
		for(;;) {
			if(currentY != m_screenBottom) {
				if(firstBand) {
					s32 nextY = m_events[eventIndex].coordinate;
					emitQuad(m_screenLeft, currentY, m_screenRight, nextY);
					currentY = nextY;
					firstBand = false;
				}

				while(currentY == m_events[eventIndex].coordinate) {
					CoverEvent& event = m_events[eventIndex];
					if(event.opening) {
						m_activeRects[activeCount++] = event.rect;
					} else {
						CoverRect ** input = m_activeRects;
						CoverRect ** output = m_activeRects;
						for(s32 i = 0; i < activeCount; ++i) {
							*output = *input;
							if(*output != event.rect) { ++output; }
							++input;
						}
						--activeCount;
					}
					++eventIndex;
				}

				bandTop = currentY;
				currentY = m_events[eventIndex].coordinate;
			}
			buildBand(bandTop, currentY, activeCount);
			if(eventIndex >= m_coverCount * 2) { break; }
		}
	}
	if(firstBand) {
		emitQuad(m_screenLeft, currentY, m_screenRight, m_screenBottom);
	}

	m_dynSprite->setVICount(m_usedVertexCount, m_usedIndexCount);
	getNode()->markUpRender();
	getNode()->markUpMatrixAndColor();
}

void
CKLBUICover::buildBand(s32 top, s32 bottom, s32 activeCount)
{
	s32 left[COVER_COUNT];
	s32 right[COVER_COUNT];
	s32 bandLeft = m_screenLeft;
	s32 screenRight = m_screenRight;
	s32 segmentCount = activeCount;
	for(s32 i = 0; i < activeCount; ++i) {
		s32 rectLeft = m_activeRects[i]->left;
		s32 rectRight = m_activeRects[i]->right;
		left[i] = rectLeft;
		right[i] = rectRight;
	}

	s32 mergeIndex = 0;
	while(mergeIndex < activeCount) {
		s32 current = mergeIndex;
		s32 currentLeft = left[current];
		++mergeIndex;
		if(currentLeft == INT_MIN) { continue; }
		for(s32 j = mergeIndex; j < activeCount; ++j) {
			if((right[current] < left[j]) || (currentLeft > right[j]) || (left[j] == INT_MIN)) {
				continue;
			}
			s32 mergedLeft = (s32)fmin((double)currentLeft, (double)left[j]);
			s32 mergedRight = (s32)fmax((double)right[current], (double)right[j]);
			left[current] = mergedLeft;
			right[current] = mergedRight;
			left[j] = INT_MIN;
			mergeIndex = 0;
			break;
		}
	}

	segmentCount = 0;
	for(s32 i = 0; i < activeCount; ++i) {
		if(left[i] != INT_MIN) {
			left[segmentCount] = left[i];
			right[segmentCount] = right[i];
			++segmentCount;
		}
	}

	for(s32 i = 1; i < segmentCount; ++i) {
		s32 insertLeft = left[i];
		s32 insertRight = right[i];
		s32 position = i;
		while(position > 0 && left[position - 1] > insertLeft) {
			left[position] = left[position - 1];
			right[position] = right[position - 1];
			--position;
		}
		left[position] = insertLeft;
		right[position] = insertRight;
	}

	for(s32 i = 0; i < segmentCount; ++i) {
		emitQuad(bandLeft, top, left[i], bottom);
		bandLeft = right[i];
	}
	emitQuad(bandLeft, top, screenRight, bottom);
}

void
CKLBUICover::emitQuad(s32 left, s32 top, s32 right, s32 bottom)
{
	u16 * indices = m_dynSprite->getSrcIndexBuffer();
	float * uv = m_dynSprite->getSrcUVBuffer();
	float * xy = m_dynSprite->getSrcXYBuffer();
	indices += m_usedIndexCount;
	uv += m_usedVertexCount * 2;
	xy += m_usedVertexCount * 2;

	float u0;
	float u1;
	float v0;
	float v1;
	if(!m_imageLoaded) {
		u0 = 0.0f;
		u1 = 1.0f;
		v0 = 0.0f;
		v1 = 1.0f;
	} else {
		if(m_repeatX) {
			float invWidth = 1.0f / (float)(m_screenRight - m_screenLeft);
			u0 = (float)(left - m_screenLeft) * invWidth;
			u1 = (float)(right - m_screenLeft) * invWidth;
		} else {
			float invWidth = 1.0f / ((float)m_imageWidth * m_scaleX);
			CKLBDrawResource& draw = CKLBDrawResource::getInstance();
			float origin = draw.toLogical(
				draw.toPhisical(0.0f) + (float)draw.screenBorderX());
			u0 = ((float)left + origin) * invWidth;
			u1 = u0 + (float)(right - left) * invWidth;
		}
		if(m_repeatY) {
			float invHeight = 1.0f / (float)(m_screenBottom - m_screenTop);
			v0 = (float)(top - m_screenTop) * invHeight;
			v1 = (float)(bottom - m_screenTop) * invHeight;
		} else {
			float invHeight = 1.0f / ((float)m_imageHeight * m_scaleY);
			v0 = (float)top * invHeight;
			v1 = (float)bottom * invHeight;
		}
	}

	uv[0] = u0;
	uv[2] = u1;
	uv[1] = v0;
	uv[3] = v0;
	uv[6] = u0;
	uv[4] = u1;
	uv[7] = v1;
	uv[5] = v1;

	xy[0] = (float)left;
	xy[2] = (float)right;
	xy[1] = (float)top;
	xy[3] = (float)top;
	xy[6] = (float)left;
	xy[4] = (float)right;
	xy[7] = (float)bottom;
	xy[5] = (float)bottom;

	indices[0] = m_usedVertexCount;
	indices[1] = m_usedVertexCount + 1;
	indices[2] = m_usedVertexCount + 2;
	indices[3] = m_usedVertexCount;
	indices[4] = m_usedVertexCount + 2;
	indices[5] = m_usedVertexCount + 3;
	m_usedIndexCount += 6;
	m_usedVertexCount += 4;
}

CKLBUICover::~CKLBUICover()
{
}

u32 CKLBUICover::getClassID()
{
	return CLASS_ID;
}

u32 CKLBUICover::getOrder()
{
	return m_order;
}

void CKLBUICover::setOrder(u32 order)
{
	if (order != m_order) {
		getNode()->setPriority(order);
		m_order = order;
	}
}

// The shipped build compiled the polyline as part of this unit.  Its two
// factory entry points inline the constructor and reach the vtable with a
// direct lea rather than through the GOT, neither of which is possible across
// a translation unit boundary, and the registrations for all three tasks
// already live here - which is why one static initialiser covers them.
#include "CKLBUIPolyline2.cpp"

// The polygon belongs to this unit for the same reason as the polyline: its
// factory entry points inline the constructor and reach the vtable directly.
#include "CKLBUIPolygon.cpp"
