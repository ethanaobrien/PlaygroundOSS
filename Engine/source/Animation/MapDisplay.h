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
#ifndef __KLB_MAP_DISPLAY__
#define __KLB_MAP_DISPLAY__

#include "CKLBNode.h"

class CKLBImageAsset;
class CKLBMapAsset;
class CKLBSprite;

class CKLBMapNode : public CKLBNode {
public:
	CKLBMapNode();
	virtual ~CKLBMapNode();

	virtual u32 getClassID();
	virtual void recomputeCustom();
	virtual void setPriority(u32 order);

	void setMargin(s32 marginX, s32 marginY);
	void setDefaultTile(u32 layer, CKLBImageAsset* image, const u32* color);
	bool setSpriteAllocator(u32 spriteCount);
	void resetTiles();
	void setupTiles(u32 width, u32 height, s32 viewportX, s32 viewportY,
		s32 order, s32 layerOrderOffset, s32 rowOrderOffset);
	void setTileID(u32 layer, s32 column, s32 row, u32 tileID);
	void setTileColor(u32 layer, s32 column, s32 row, const u32* color);
	void beginTileUpdate();
	void endTileUpdate();
	void getScreenCoordinate(s32 x, s32 y, s32 isometric, s32 originX,
		s32 originY, s32* screenX, s32* screenY) const;
	bool getLogicalCoordinate(s32 x, s32 y, s32* column, s32* row) const;
	void releaseTiles();
	void unlinkSprite(size_t entryIndex);

protected:
	virtual void addRender();
	virtual void removeRender();

private:
	friend class CKLBMapAsset;

	static const u16 INVALID_SPRITE_INDEX = 0xFFFF;
	static const u32 MAX_MAP_LAYERS = 5;

	struct SpritePoolEntry {
		u16 next;
		u16 previous;
		CKLBRenderCommand* command;
	};

	struct RenderCell {
		u32 color;
		CKLBSprite* sprite;
		u16 tileID;
		u16 spriteIndex;
	};

	void cleanup();
	void updateScroll(s32 x, s32 y, s32 isometric);
	CKLBSprite* popSprite(u16* spriteIndex);

	s32 m_layerStrideX;
	s32 m_layerStrideY;
	s32 m_marginX;
	s32 m_marginY;
	s32 m_centerX;
	s32 m_centerY;
	s32 m_visibleColumns;
	s32 m_visibleRows;
	s32 m_originX;
	s32 m_originY;
	s32 m_firstColumn;
	s32 m_firstRow;
	s32 m_lastColumn;
	s32 m_scrollX;
	s32 m_scrollY;
	s32 m_previousColumn;
	s32 m_previousRow;
	s32 m_previousScroll;
	s32 m_tileWidth;
	s32 m_tileHeight;
	u32 m_layerCount;
	s32 m_mapWidth;
	s32 m_mapHeight;

	u16* m_layerTileData[MAX_MAP_LAYERS];
	u32* m_layerRenderCells[MAX_MAP_LAYERS];
	RenderCell* m_layerWindows[MAX_MAP_LAYERS];
	CKLBImageAsset* m_layerSprites[MAX_MAP_LAYERS];
	u32 m_layerRenderIndex[MAX_MAP_LAYERS];

	SpritePoolEntry* m_spritePool;
	u16 m_spritePoolCount;
	u16 m_freeSpriteHead;
	u16 m_usedSpriteTail;
	float m_projectionX;
	float m_projectionY;
	CKLBMapAsset* m_mapAsset;
	bool m_tilesAttached;
	bool m_scrollDirty;
	bool m_geometryDirty;
	bool m_isometric;
};

#endif // __KLB_MAP_DISPLAY__
