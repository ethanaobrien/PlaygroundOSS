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
#include "MapDisplay.h"
#include "CKLBRendering.h"
#include "MapManagement.h"
#include <stdlib.h>

CKLBMapNode::~CKLBMapNode()
{
	cleanup();
	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		if(m_layerRenderCells[layer]) {
			free(m_layerRenderCells[layer]);
			m_layerRenderCells[layer] = NULL;
		}
	}
	m_mapAsset = NULL;
}

void CKLBMapNode::cleanup()
{
	releaseTiles();
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	for(u32 index = 0; index < m_spritePoolCount; ++index) {
		renderingManager.releaseCommand(m_spritePool[index].command);
	}
	KLBDELETEA(m_spritePool);
	m_spritePool = NULL;

	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		KLBDELETE(m_layerWindows[layer]);
	}
}

u32 CKLBMapNode::getClassID() {
	return CLS_KLBNODEMAP;
}

void CKLBMapNode::setMargin(s32 marginX, s32 marginY) {
	m_marginX = m_centerX - marginX;
	m_marginY = m_centerY - marginY;
}

void CKLBMapNode::setDefaultTile(u32 layer, CKLBImageAsset* image,
								const u32* color) {
	if(layer < m_layerCount) {
		m_layerSprites[layer] = image;
		m_layerRenderIndex[layer] = *color;
		m_scrollDirty = true;
		m_geometryDirty = true;
		if(!m_tilesAttached) {
			updateScroll(m_scrollX, m_scrollY, false);
		}
	}
}

bool CKLBMapNode::setSpriteAllocator(u32 spriteCount) {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	m_spritePool = KLBNEWA(SpritePoolEntry, spriteCount);
	m_spritePoolCount = spriteCount;
	for(u32 index = 0; index < m_spritePoolCount; ++index) {
		m_spritePool[index].command =
			renderingManager.allocateCommandSprite(4, 6, 0);
	}
	m_freeSpriteHead = 0;
	m_usedSpriteTail = INVALID_SPRITE_INDEX;
	return m_spritePool != NULL;
}

void CKLBMapNode::resetTiles() {
	releaseTiles();
	for(u32 index = 0; index < m_spritePoolCount; ++index) {
		m_spritePool[index].next = index + 1;
		m_spritePool[index].previous = index - 1;
	}
	m_spritePool[m_spritePoolCount - 1].next = INVALID_SPRITE_INDEX;
	m_freeSpriteHead = 0;
	m_usedSpriteTail = INVALID_SPRITE_INDEX;

	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		KLBDELETE(m_layerWindows[layer]);
	}
}

void CKLBMapNode::setupTiles(u32 width, u32 height,
							s32 viewportX, s32 viewportY,
							s32 order, s32 layerOrderOffset,
							s32 rowOrderOffset) {
	(void)viewportX;
	(void)viewportY;

	CKLBNode* parent = getParent();
	if(parent) {
		parent->removeNode(this);
	}

	resetTiles();
	m_layerStrideX = layerOrderOffset;
	m_layerStrideY = rowOrderOffset;
	m_visibleColumns = width / (u32)m_tileWidth + 1;
	u8 projectionShift = m_isometric;
	m_visibleRows =
		height / (u32)(m_tileHeight >> projectionShift) + 1;
	m_status |= CUSTOM_CHANGE;
	m_originX = 0;
	m_originY = 0;

	if(m_visibleColumns && m_visibleRows) {
		CKLBRenderingManager& renderingManager =
			CKLBRenderingManager::getInstance();
		for(u32 layer = 0; layer < m_layerCount; ++layer) {
			RenderCell* cells =
				KLBNEWA(RenderCell, m_visibleRows * m_visibleColumns);
			m_layerWindows[layer] = cells;

			for(s32 row = 0; row < m_visibleRows; ++row) {
				s32 renderOrder =
					order + layer * layerOrderOffset + row * rowOrderOffset;
				RenderCell* cell = &cells[m_visibleColumns * row];
				for(s64 column = 0;
					column < m_visibleColumns;
					++column, ++cell) {
					cell->sprite = popSprite(&cell->spriteIndex);
					CKLBSprite* sprite = cell->sprite;
					if(sprite) {
						cell->tileID = 0;
						sprite->changeOrder(renderingManager, renderOrder);
					}
				}
			}
		}
		m_scrollDirty = true;
		m_geometryDirty = true;
	}

	if(parent) {
		parent->addNode(this, 0);
	}
}

void CKLBMapNode::releaseTiles() {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	u16 entryIndex = m_usedSpriteTail;
	while(entryIndex != INVALID_SPRITE_INDEX) {
		CKLBRenderCommand* command = m_spritePool[entryIndex].command;
		if(command->isInRenderList()) {
			renderingManager.removeFromRendering(command);
		}
		entryIndex = m_spritePool[entryIndex].next;
	}
}

CKLBSprite* CKLBMapNode::popSprite(u16* spriteIndex) {
	u16 index = m_freeSpriteHead;
	if(index != INVALID_SPRITE_INDEX) {
		SpritePoolEntry& entry = m_spritePool[index];
		m_freeSpriteHead = entry.next;
		entry.previous = INVALID_SPRITE_INDEX;

		u16 usedTail = m_usedSpriteTail;
		u16 previous = INVALID_SPRITE_INDEX;
		if(usedTail != INVALID_SPRITE_INDEX) {
			m_spritePool[usedTail].previous = index;
			previous = usedTail;
		}
		entry.next = previous;
		m_usedSpriteTail = index;
		*spriteIndex = index;
		return static_cast<CKLBSprite*>(entry.command);
	}
	klb_assertAlways(
		"Map Sprite Allocator full, please change setSpriteAllocator() ");
	return NULL;
}

void CKLBMapNode::setTileID(u32 layer, s32 column, s32 row, u32 tileID) {
	m_geometryDirty = true;
	if(m_isometric) {
		s32 projectedColumn = ((column + 1 - row) >> 1);
		projectedColumn += m_mapAsset->m_mapData.isoOffsetX;
		row += column;
		column = projectedColumn;
	}
	if(layer < m_layerCount
	&& (u32)row < (u32)m_mapHeight
	&& (u32)column < (u32)m_mapWidth) {
		u16* tiles = m_layerTileData[layer];
		s32 index = m_mapWidth * row + column;
		m_scrollDirty |= tiles[index] != tileID;
		tiles[index] = tileID;
		if(!m_tilesAttached) {
			updateScroll(m_scrollX - m_marginX, m_scrollY - m_marginY, false);
		}
	}
}

void CKLBMapNode::setTileColor(u32 layer, s32 column, s32 row,
							  const u32* color) {
	m_geometryDirty = true;
	if(m_isometric) {
		s32 projectedColumn = ((column + 1 - row) >> 1);
		projectedColumn += m_mapAsset->m_mapData.isoOffsetX;
		row += column;
		column = projectedColumn;
	}
	if(layer < m_layerCount
	&& (u32)row < (u32)m_mapHeight
	&& (u32)column < (u32)m_mapWidth) {
		u32* colors = m_layerRenderCells[layer];
		s32 index = m_mapWidth * row + column;
		m_scrollDirty |= colors[index] != *color;
		colors[index] = *color;
		if(!m_tilesAttached) {
			updateScroll(m_scrollX, m_scrollY, false);
		}
	}
}

void CKLBMapNode::beginTileUpdate() {
	m_tilesAttached = true;
}

void CKLBMapNode::endTileUpdate() {
	if(m_tilesAttached) {
		m_tilesAttached = false;
		m_scrollDirty |= m_geometryDirty;
		if(m_scrollDirty) {
			updateScroll(m_scrollX, m_scrollY, false);
		}
	}
}

void CKLBMapNode::getScreenCoordinate(s32 x, s32 y, s32 isometric,
									 s32 originX, s32 originY,
									 s32* screenX, s32* screenY) const {
	if(isometric) {
		s32 projectedX = x - y;
		y = (x + y) >> 1;
		x = projectedX;
	}
	s32 centerY = m_centerY;
	if(screenX) {
		*screenX = x - originX + m_centerX - m_scrollX;
	}
	if(screenY) {
		*screenY = y - originY + centerY - m_scrollY;
	}
}

bool CKLBMapNode::getLogicalCoordinate(s32 x, s32 y,
									  s32* column, s32* row) const {
	x += m_scrollX - m_centerX;
	y += m_scrollY - m_centerY;
	float logicalX = (float)x;
	float logicalY = (float)y;
	float tileHeight = (float)m_tileHeight;
	logicalX = (logicalX * tileHeight) / (float)m_tileWidth;
	s32 logicalColumn =
		(s32)(((logicalY + logicalX) * 0.5f) / tileHeight);
	s32 logicalRow =
		(s32)(((logicalY - logicalX) * 0.5f) / tileHeight);
	*column = logicalColumn;
	*row = logicalRow;
	return (logicalColumn | logicalRow) >= 0;
}

void CKLBMapNode::recomputeCustom() {
	if((m_status & (MATRIX_CHANGE | CMATRIX_CHANGE | CUSTOM_CHANGE)) == 0) {
		return;
	}

	u8 projectionShift = m_isometric;
	s32 tileWidthValue = m_tileWidth;
	const float rowHeight =
		static_cast<float>(m_tileHeight >> projectionShift);
	u32 layerCount = m_layerCount;
	if(layerCount) {
		const float tileWidth = (float)tileWidthValue;
		const float halfTileWidth = (float)(tileWidthValue >> 1);
		u32 layer = 0;
		do {
			RenderCell* cells = m_layerWindows[layer];
			if(cells) {
				s32 rowCount = m_visibleRows;
				if(rowCount > 0) {
					float y = m_projectionY;
					s32 row = 0;
					do {
						float x = m_projectionX;
						if(m_isometric && ((m_lastColumn + row) & 1)) {
							if(m_firstColumn == m_firstRow) {
								x -= halfTileWidth;
							} else {
								x += halfTileWidth;
							}
						}
	
						s32 columnCount = m_visibleColumns;
						if(columnCount > 0) {
							RenderCell* cell = cells;
							s32 column = 0;
							do {
								CKLBSprite* sprite = cell->sprite;
								if(sprite && sprite->m_pImageAsset) {
									float spriteY = y + (float)(m_tileHeight
										- sprite->m_pImageAsset->getSize()->getHeight());
									sprite->applyNode(this, x, spriteY);
									sprite->setLocalColor(cell->color);
									applyColor(sprite);
									columnCount = m_visibleColumns;
								}
								x += tileWidth;
								++column;
								++cell;
							} while(column < columnCount);
							rowCount = m_visibleRows;
						}
						cells += columnCount;
						y += rowHeight;
						++row;
					} while(row < rowCount);
					layerCount = m_layerCount;
				}
			}
			++layer;
		} while(layer < layerCount);
	}
	m_scrollDirty = false;
}

void CKLBMapNode::setPriority(u32 order) {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		RenderCell* cells = m_layerWindows[layer];
		if(cells && m_visibleRows > 0) {
			s32 columnCount = m_visibleColumns;
			for(s32 row = 0; row < m_visibleRows; ++row) {
				if(columnCount > 0) {
					u32 renderOrder = m_layerStrideX * layer + order
						+ m_layerStrideY * row;
					RenderCell* cell = &cells[columnCount * row];
					for(s64 column = 0; column < columnCount; ++column, ++cell) {
						if(cell->sprite) {
							cell->sprite->changeOrder(renderingManager, renderOrder);
							columnCount = m_visibleColumns;
						}
					}
				}
			}
		}
	}
}

void CKLBMapNode::addRender() {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		RenderCell* cells = m_layerWindows[layer];
		if(cells && m_visibleRows > 0) {
			s32 columnCount = m_visibleColumns;
			for(s32 row = 0; row < m_visibleRows; ++row) {
				if(columnCount > 0) {
					RenderCell* cell = &cells[columnCount * row];
					for(s64 column = 0; column < columnCount; ++column, ++cell) {
						if(cell->sprite) {
							renderingManager.addToRendering(cell->sprite,
								cell->sprite->getOrder());
							columnCount = m_visibleColumns;
						}
					}
				}
			}
		}
	}
}

void CKLBMapNode::removeRender() {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	for(u32 layer = 0; layer < m_layerCount; ++layer) {
		RenderCell* cells = m_layerWindows[layer];
		if(cells && m_visibleRows > 0) {
			s32 columnCount = m_visibleColumns;
			for(s32 row = 0; row < m_visibleRows; ++row) {
				if(columnCount > 0) {
					RenderCell* cell = &cells[columnCount * row];
					for(s64 column = 0; column < columnCount; ++column, ++cell) {
						if(cell->sprite) {
							renderingManager.removeFromRendering(cell->sprite);
							columnCount = m_visibleColumns;
						}
					}
				}
			}
		}
	}
}

void CKLBMapNode::unlinkSprite(size_t entryIndex) {
	SpritePoolEntry& entry = m_spritePool[entryIndex];
	u16 previous = entry.previous;
	u16 next = entry.next;

	if(previous != INVALID_SPRITE_INDEX) {
		m_spritePool[previous].next = next;
	}
	if(next != INVALID_SPRITE_INDEX) {
		m_spritePool[next].previous = previous;
	}
}

CKLBMapNode::CKLBMapNode()
: CKLBNode()
{
	m_layerCount = 0;
	m_spritePool = NULL;
	m_spritePoolCount = 0;

	m_centerX = m_centerY = 0;
	m_visibleColumns = m_visibleRows = 0;
	m_layerStrideX = m_layerStrideY = 0;
	m_marginX = m_marginY = 0;
	m_scrollX = m_scrollY = 0;
	m_previousColumn = m_previousRow = 0;

	m_usedSpriteTail = INVALID_SPRITE_INDEX;
	m_mapAsset = NULL;
	m_tilesAttached = false;
	m_scrollDirty = true;
	m_geometryDirty = true;

	m_layerRenderIndex[0] = (u32)-1;
	m_layerSprites[0] = NULL;
	m_layerRenderIndex[1] = (u32)-1;
	m_layerRenderCells[0] = m_layerRenderCells[1] = NULL;
	m_layerWindows[0] = m_layerWindows[1] = NULL;
	m_layerRenderIndex[2] = (u32)-1;
	m_layerSprites[1] = m_layerSprites[2] = NULL;
	m_layerRenderIndex[3] = (u32)-1;
	m_layerRenderCells[2] = m_layerRenderCells[3] = NULL;
	m_layerWindows[2] = m_layerWindows[3] = NULL;
	m_layerRenderIndex[4] = (u32)-1;
	m_layerRenderCells[4] = NULL;
	m_layerWindows[4] = NULL;
	m_layerSprites[3] = m_layerSprites[4] = NULL;
}

void CKLBMapNode::updateScroll(s32 x, s32 y, s32 isometric) {
	if(isometric) {
		s32 projectedX = x - y;
		y = (x + y) >> 1;
		x = projectedX;
	}

	s32 scrollX = x + m_marginX;
	s32 scrollY = y + m_marginY;
	if(m_scrollX != scrollX || m_scrollY != scrollY) {
		m_scrollDirty = true;
	}

	if(m_tilesAttached) {
		m_scrollX = x;
		m_scrollY = y;
		return;
	}

	m_scrollX = scrollX;
	m_scrollY = scrollY;
	if(!m_scrollDirty) {
		return;
	}

	m_status |= CUSTOM_CHANGE;
	markUpTree();

	if(scrollX < 0) {
		scrollX -= m_tileWidth;
	}

	if(scrollY < 0) {
		if(m_isometric) {
			scrollY -= m_tileHeight >> 1;
		} else {
			scrollY -= m_tileHeight;
		}
	}

	s32 rowHeight = m_tileHeight >> m_isometric;
	m_firstColumn = scrollX / m_tileWidth;
	s32 columnRemainder =
		scrollX % m_tileWidth + ((scrollX >> 31) & m_tileWidth);
	m_lastColumn = scrollY / rowHeight;
	s32 rowRemainder = scrollY % rowHeight;
	if(scrollY < 0) {
		rowRemainder += m_tileHeight >> m_isometric;
	}

	m_firstRow = m_firstColumn;
	if(m_isometric) {
		m_firstRow = (scrollX + (m_tileWidth >> 1)) / m_tileWidth;
	}

	m_projectionX = (float)(m_originX - columnRemainder);
	m_projectionY = (float)(m_originY - rowRemainder);
	if(m_isometric) {
		m_projectionY -= (float)(m_tileHeight >> 1);
	}

	s32 columnChange = m_firstColumn - m_previousColumn;
	s32 rowChange = m_lastColumn - m_previousRow;
	s32 scrollChange = m_firstRow - m_previousScroll;
	s32 windowChange = columnChange | rowChange | scrollChange;
	if(windowChange || m_geometryDirty) {
		m_previousColumn = m_firstColumn;
		m_previousRow = m_lastColumn;
		m_previousScroll = m_firstRow;

		for(u32 layer = 0; layer < m_layerCount; ++layer) {
			u16* tileData = m_layerTileData[layer];
			u32* renderCells = m_layerRenderCells[layer];
			RenderCell* window = m_layerWindows[layer];
			CKLBImageAsset** defaultImage = &m_layerSprites[layer];
			u32* defaultColor = &m_layerRenderIndex[layer];
			for(s32 windowRow = 0; windowRow < m_visibleRows; ++windowRow) {
				s32 mapRow = m_lastColumn + windowRow;
				s64 mapColumn = (mapRow & 1) ? m_firstRow : m_firstColumn;
				s64 sourceIndex = m_mapWidth * mapRow + mapColumn;
				u16* tileIDs = tileData + sourceIndex;
				u32* colors = renderCells + sourceIndex;
				RenderCell* cells = &window[m_visibleColumns * windowRow];

				for(s64 windowColumn = 0;
					windowColumn < m_visibleColumns;
					++windowColumn, ++cells, ++tileIDs, ++colors) {
					s64 sourceColumn = mapColumn + windowColumn;
					u16 tileID = 0;
					const u32* color = defaultColor;
					if(mapRow >= 0
					&& sourceColumn >= 0
					&& mapRow < m_mapHeight
					&& sourceColumn < m_mapWidth) {
						tileID = *tileIDs;
						color = colors;
					}

					cells->color = *color;
					if(cells->tileID != tileID || m_geometryDirty) {
						cells->tileID = tileID;
						CKLBSprite* sprite = cells->sprite;
						CKLBImageAsset* image;
						if(tileID) {
							u32 surfaceIndex =
								tileID - m_mapAsset->m_mapData.firstGid;
							klb_assertNull(
								surfaceIndex < m_mapAsset->m_mapData.surfaceCount,
								"");
							image = static_cast<CKLBImageAsset*>(
								m_mapAsset->m_mapData.surfaces[surfaceIndex]);
						} else {
							image = *defaultImage;
						}
						sprite->switchImage(image);
					}
				}
			}
		}
		m_geometryDirty = false;
	}
}
