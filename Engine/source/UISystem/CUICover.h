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
#ifndef CKLBUICover_h
#define CKLBUICover_h

#include "CKLBUITask.h"

class CKLBDynSprite;
class CKLBImageAsset;

class CKLBUICover : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUICover>;

protected:
	CKLBUICover();
	virtual ~CKLBUICover();

public:
	enum {
		CLASS_ID = 0x00080100
	};

	static CKLBUICover * create(CKLBUITask * parent, CKLBNode * node,
								 u32 order, u32 color);

	virtual u32 getClassID();
	virtual u32 getOrder();
	virtual void setOrder(u32 order);
	virtual bool initUI(CLuaState& lua);
	virtual int commandScript(CLuaState& lua, int argc, int cmd);
	virtual void execute(u32 deltaT);
	virtual void dieUI();

	s32 addCover(s32 x, s32 y, s32 width, s32 height);
	void removeCover(s32 index);
	void clearCovers();
	void setCoverColor(u32 color);
	void setup(const char * asset, bool repeatX, bool repeatY,
			 float scaleX, float scaleY);

private:
	struct CoverRect {
		s32 left;
		s32 top;
		s32 right;
		s32 bottom;
		bool available;
	};

	struct CoverEvent {
		bool opening;
		s32 coordinate;
		CoverRect * rect;
	};

	bool init(CKLBUITask * parent, CKLBNode * node, u32 order, u32 color);
	bool initCore(u32 order, u32 color);
	void updateScreenSize();
	void rebuildGeometry();
	void buildBand(s32 top, s32 bottom, s32 activeCount);
	void emitQuad(s32 left, s32 top, s32 right, s32 bottom);
	void insertEvent(s32 coordinate, CoverRect * rect, bool opening);
	void removeEvents(CoverRect * rect);
	s32 findAvailableCover() const;

	static const s32 COVER_COUNT = 10;
	static const s32 EVENT_COUNT = COVER_COUNT * 2 + 1;
	static const s32 VERTEX_COUNT = 88 * 4;
	static const s32 INDEX_COUNT = 88 * 6;
	static PROP_V2 ms_propItems[];

	u32 m_order;
	CoverRect * m_activeRects[COVER_COUNT];
	CoverEvent m_events[EVENT_COUNT];
	CoverRect m_covers[COVER_COUNT];
	s32 m_coverCount;
	u16 m_usedIndexCount;
	u16 m_usedVertexCount;
	CKLBDynSprite * m_dynSprite;
	CKLBImageAsset * m_imageAsset;
	bool m_imageLoaded;
	s32 m_screenLeft;
	s32 m_screenRight;
	s32 m_screenTop;
	s32 m_screenBottom;
	u32 m_assetHandle;
	bool m_repeatX;
	bool m_repeatY;
	float m_scaleX;
	float m_scaleY;
	u32 m_imageHeight;
	u32 m_imageWidth;
};

#endif // CKLBUICover_h
