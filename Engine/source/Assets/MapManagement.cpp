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
#include "MapManagement.h"
#include "MapDisplay.h"
#include "TextureManagement.h"
#include "../../libs/JSonParser/api/yajl_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CKLBMapAsset::MapString::MapString()
: next(NULL)
, value(NULL) {
}

CKLBMapAsset::MapString::~MapString() {
}

CKLBMapAsset::MapProperty::MapProperty()
: next(NULL)
, name(NULL)
, value(NULL) {
}

CKLBMapAsset::MapProperty::~MapProperty() {
	if (name) {
		free(name);
		name = NULL;
	}
	if (value) {
		free(value);
		value = NULL;
	}
}

CKLBMapAsset::MapTileset::MapTileset()
: next(NULL)
, image(NULL)
, imageAsset(NULL)
, name(NULL)
, noImage(false)
, properties(NULL) {
}

CKLBMapAsset::MapTileset::~MapTileset() {
	if (name) {
		free(name);
		name = NULL;
	}
	if (image) {
		free(image);
		image = NULL;
	}
	if (imageAsset) {
		imageAsset = NULL;
	}
	MapProperty* property = properties;
	while (property) {
		MapProperty* nextProperty = property->next;
		if (property->name) {
			free(property->name);
			property->name = NULL;
		}
		if (property->value) {
			free(property->value);
		}
		::operator delete(property);
		property = nextProperty;
	}
}

CKLBMapAsset::MapObject::MapObject()
: next(NULL)
, gid(0)
, id(0)
, name(NULL)
, currentProperty(NULL)
, properties(NULL) {
}

CKLBMapAsset::MapObject::~MapObject() {
	if (name) {
		free(name);
		name = NULL;
	}
	if (currentProperty) {
		::operator delete(currentProperty);
		currentProperty = NULL;
	}
	MapProperty* property = properties;
	while (property) {
		MapProperty* nextProperty = property->next;
		if (property->name) {
			free(property->name);
			property->name = NULL;
		}
		if (property->value) {
			free(property->value);
		}
		::operator delete(property);
		property = nextProperty;
	}
}

CKLBMapAsset::MapLayer::MapLayer()
: next(NULL)
, data(NULL)
, sourceData(NULL)
, name(NULL)
, color(0xffffff)
, freeProperties(NULL)
, objects(NULL) {
}

CKLBMapAsset::MapLayer::~MapLayer() {
	if (name) {
		free(name);
		name = NULL;
	}
	if (data) {
		free(data);
		data = NULL;
	}
	if (sourceData) {
		free(sourceData);
		sourceData = NULL;
	}
	MapProperty* property = freeProperties;
	while (property) {
		MapProperty* nextProperty = property->next;
		if (property->name) {
			free(property->name);
			property->name = NULL;
		}
		if (property->value) {
			free(property->value);
		}
		::operator delete(property);
		property = nextProperty;
	}
}

CKLBMapAsset::MapData::MapData()
: layers(NULL)
, tilesets(NULL)
, surfaces(NULL)
, currentProperties(NULL) {
}

CKLBMapAsset::MapData::~MapData() {
	if (surfaces) {
		delete [] surfaces;
		surfaces = NULL;
	}

	MapLayer* layer = layers;
	while (layer) {
		MapLayer* nextLayer = layer->next;
		delete layer;
		layer = nextLayer;
	}

	MapTileset* tileset = tilesets;
	while (tileset) {
		MapTileset* nextTileset = tileset->next;
		if (tileset->imageAsset) {
			tileset->imageAsset->getTexture()->decrementRefCount();
		}
		delete tileset;
		tileset = nextTileset;
	}

	MapProperty* property = currentProperties;
	while (property) {
		MapProperty* nextProperty = property->next;
		delete property;
		property = nextProperty;
	}
}

CKLBMapAssetPlugin::CKLBMapAssetPlugin() {}

CKLBMapAssetPlugin::~CKLBMapAssetPlugin() {}

const char* CKLBMapAssetPlugin::fileExtension() {
	return ".json";
}

CKLBMapAsset::CKLBMapAsset() {
	m_strings = NULL;
	m_state = S_NONE;
	m_mapDepth = 0;
	m_arrayDepth = 0;
	m_objects = NULL;
	m_objectCount = 0;
	m_tileDataBuffer = NULL;
}

CKLBMapAsset::~CKLBMapAsset()
{
	if (m_tileDataBuffer) {
		::operator delete(m_tileDataBuffer);
		m_tileDataBuffer = NULL;
	}

	if (m_strings) {
		MapString* string = m_strings;
		do {
			MapString* nextString = string->next;
			::operator delete(string);
			string = nextString;
		} while (string);
		m_strings = string;
	}

	u32 count = m_objectCount;
	for (u32 index = 0; index < count; ++index) {
		if (m_objects[index]) {
			delete m_objects[index];
			count = m_objectCount;
		}
	}
	delete [] m_objects;
}

char* CKLBMapAsset::registerString(const void* value, u32 length) {
	MapString* entry = new MapString();
	char* copy = static_cast<char*>(malloc(length + 1));
	if (!copy) {
		delete entry;
		return NULL;
	}
	entry->next = m_strings;
	m_strings = entry;
	entry->value = copy;
	memcpy(copy, value, length);
	copy[length] = '\0';
	return copy;
}

u32 CKLBMapAsset::parseColor(const char* value, s32 length) const {
	u32 color = 0;
	const char* digitString = (value[0] == '#') ? value + 1 : value;
	s32 remaining = length + ((value[0] == '#') ? -1 : 0);
	while (remaining) {
		char digit = *digitString;
		if ((u8)(digit - '0') <= 9) {
			color = (color << 4) + digit - '0';
		} else if ((u8)(digit - 'A') <= 5) {
			color = (color << 4) + digit - 'A' + 10;
		} else if ((u8)(digit - 'a') <= 5) {
			color = (color << 4) + digit - 'a' + 10;
		} else {
			return 0;
		}
		++digitString;
		--remaining;
	}
	return color;
}

void CKLBMapAsset::copyLayerTiles(u16 columnCount, u16 rowCount, const u16* source,
                                  u16 outputStride, u16 /*outputHeight*/, u16* output) const {
	// An isometric map is stored as a plain grid but drawn as a diamond: a tile
	// moves to the output row its two coordinates add up to, and to the column
	// their difference selects. An orthogonal map needs no remapping and is
	// taken over as one block.
	if (m_mapData.isometric == 1) {
		// The two diamond diagonals are tracked as running counters: one rises
		// with the source row and gives the output row a tile lands on, the other
		// falls with it and gives the column phase inside that row.
		s32 outputRow = 0;
		s32 columnPhase = 1;
		for (s32 row = 0; row != rowCount; ++row) {
			for (s32 column = 0; column != columnCount; ++column) {
				output[
					(outputRow + column) * outputStride
					+ m_mapData.isoOffsetX
					+ ((column + columnPhase) >> 1)
				] = source[static_cast<size_t>(row) * columnCount + column];
			}
			++outputRow;
			--columnPhase;
		}
	} else {
		memcpy(
			output,
			source,
			static_cast<size_t>(columnCount) * rowCount * sizeof(u16)
		);
	}
}

bool CKLBMapAsset::buildSurfaceTable() {
	MapTileset* tileset = m_mapData.tilesets;
	u32 firstGid = tileset->firstGid;
	u32 lastGid = firstGid;

	if (tileset) {
		lastGid =
			(tileset->imageHeight / tileset->tileHeight)
			* (tileset->imageWidth / tileset->tileWidth)
			+ tileset->firstGid;
		if (lastGid <= firstGid) {
			lastGid = firstGid;
		}

		for (tileset = tileset->next; tileset; tileset = tileset->next) {
			u32 tilesetFirstGid = tileset->firstGid;
			u32 tilesetLastGid =
				(tileset->imageHeight / tileset->tileHeight)
				* (tileset->imageWidth / tileset->tileWidth)
				+ tilesetFirstGid;
			if (tilesetLastGid > lastGid) {
				lastGid = tilesetLastGid;
			}
			if (tilesetFirstGid < firstGid) {
				firstGid = tilesetFirstGid;
			}
		}
	}

	size_t surfaceCount =
		static_cast<u32>(lastGid - firstGid + 1);
	CKLBAbstractAsset** surfaces =
		new CKLBAbstractAsset*[surfaceCount];
	if (surfaceCount) {
		memset(
			surfaces,
			0,
			static_cast<size_t>(lastGid - firstGid)
				* sizeof(*surfaces) + sizeof(*surfaces)
		);
	}

	CKLBAssetManager& assetManager = CKLBAssetManager::getInstance();
	char imageName[512];
	char* const imageNameBuffer = imageName;
	for (
		tileset = m_mapData.tilesets;
		tileset;
		tileset = tileset->next
	) {
		if (tileset->noImage) {
			continue;
		}

		u32 tilesetFirstGid = tileset->firstGid;
		sprintf(imageNameBuffer, "%s.imag", tileset->image);
		u16 assetId =
			assetManager.getAssetIDFromName(imageNameBuffer, 'I');
		CKLBAbstractAsset* asset = assetManager.getAsset(assetId);
		if (!asset) {
			continue;
		}

		klb_assert(asset->getAssetType() == ASSET_TEXTURE,
			"invalid resource type");
		tileset->imageAsset =
			static_cast<CKLBTextureAsset*>(asset)->getImage(imageNameBuffer);
		if (!tileset->imageAsset) {
			continue;
		}

		tileset->imageAsset->getTexture()->incrementRefCount();
		tileset->imageAsset->setSubImage(
			tileset->tileHeight,
			tileset->tileWidth,
			0,
			0
		);

		u32 tileCount =
			(tileset->imageHeight / tileset->tileHeight)
			* (tileset->imageWidth / tileset->tileWidth);
		if (!tileCount) {
			continue;
		}
		if (tileCount == 1) {
			surfaces[tilesetFirstGid - firstGid] = tileset->imageAsset;
		} else {
			for (u32 index = 0; index < tileCount; ++index) {
				surfaces[tilesetFirstGid - firstGid + index] =
					tileset->imageAsset->getSubImage(index, NULL);
			}
		}
	}

	m_mapData.surfaceCount = static_cast<u32>(surfaceCount);
	m_mapData.surfaces = surfaces;
	m_mapData.firstGid = firstGid;
	return true;
}

bool CKLBMapAsset::buildLayerData() {
	m_sourceWidth = m_mapData.width;
	m_sourceHeight = m_mapData.height;
	if (m_tileDataBuffer) {
		::operator delete(m_tileDataBuffer);
	}
	m_tileDataBuffer = NULL;

	s32 outputHeight;
	u32 outputWidth;
	s32 isometricOffset;
	if (m_mapData.isometric == 1) {
		s32 sourceHeight = m_mapData.height;
		outputHeight = sourceHeight + m_mapData.width;
		outputWidth =
			static_cast<u32>(sourceHeight + 1 + m_mapData.width) >> 1;
		isometricOffset =
			static_cast<s32>(static_cast<u32>(sourceHeight - 1) >> 1);
	} else {
		outputHeight = m_mapData.height;
		outputWidth = static_cast<u32>(m_mapData.width);
		isometricOffset = 0;
	}
	m_mapData.isoOffsetX = isometricOffset;

	bool succeeded = true;
	if (m_mapData.layers) {
		size_t outputSize =
			static_cast<size_t>(outputWidth * outputHeight) * sizeof(u16);
		u16 outputStride = static_cast<u16>(outputWidth);
		MapLayer* previous = NULL;
		MapLayer* layer = m_mapData.layers;
		do {
			MapLayer* current = layer;
			layer = layer->next;
			if (static_cast<s32>(current->objectGroup) == 1) {
				if (previous) {
					previous->next = layer;
				} else {
					m_mapData.layers = layer;
				}
				delete current;
			} else {
				u32 sourceWidth = current->height;
				u32 sourceHeight = current->width;
				klb_assertNull(
					sourceWidth == static_cast<u32>(m_mapData.width)
					&& sourceHeight == static_cast<u32>(m_mapData.height),
					"Map size and layer size do not match");

				u16* source = current->data;
				u16* output = static_cast<u16*>(malloc(outputSize));
				if (output) {
					if (outputWidth * outputHeight) {
						memset(output, 0, outputSize);
					}
					copyLayerTiles(
						static_cast<u16>(sourceWidth),
						static_cast<u16>(sourceHeight),
						source,
						outputStride,
						static_cast<u16>(outputHeight),
						output
					);
					current->data = output;
				} else {
					succeeded = false;
				}
				current->sourceData = source;
				previous = current;
			}
		} while (layer);
	}

	m_mapData.height = outputHeight;
	m_mapData.width = static_cast<s32>(outputWidth);
	return succeeded;
}

bool CKLBMapAsset::setupNode(CKLBMapNode* node, bool allocateRenderColors) const {
	u32 layerCount = getLayerCount();

	node->m_layerCount = 0;
	bool allocationFailed = false;
	for (MapLayer* layer = m_mapData.layers; layer; layer = layer->next) {
		u32 tileCount = m_mapData.height * m_mapData.width;
		size_t colorBufferSize = tileCount;
		colorBufferSize <<= 2;
		u32 layerIndex = layerCount - node->m_layerCount - 1;
		node->m_layerTileData[layerIndex] = layer->data;

		if (allocateRenderColors) {
			u32* renderColors =
				static_cast<u32*>(malloc(colorBufferSize));
			node->m_layerRenderCells[layerIndex] = renderColors;
			if (!renderColors) {
				allocationFailed = true;
			} else {
				u32 color =
					(static_cast<u32>(layer->opacity * 255.0f) << 24)
					| layer->color;
				for (u32 index = 0; index < tileCount; ++index) {
					renderColors[index] = color;
				}
			}
		}
		++node->m_layerCount;
	}
	return allocationFailed;
}

CKLBNode* CKLBMapAsset::createSubTree(u32 /*priorityBase*/) {
	CKLBMapNode* node = KLBNEW(CKLBMapNode);
	if (node) {
		node->m_mapAsset = this;
		node->m_tileWidth = m_mapData.tileWidth;
		node->m_tileHeight = m_mapData.tileHeight;
		node->m_layerCount = 0;
		node->m_mapWidth = m_mapData.width;
		node->m_mapHeight = m_mapData.height;
		node->m_isometric = (m_mapData.isometric == 1);
		if (node->m_isometric) {
			node->m_centerX = m_mapData.isoOffsetX * m_mapData.tileWidth
				+ (static_cast<u32>(m_mapData.tileWidth) >> 1);
			node->m_centerY = -m_mapData.tileHeight >> 1;
			node->setMargin(0, 0);
		}

		if (setupNode(node, true)) {
			KLBDELETE(node);
			node = NULL;
		} else {
			node->updateScroll(0, 0, false);
		}
	}
	return node;
}

u32 CKLBMapAsset::getLayerCount() const {
	u32 count = 0;
	for (MapLayer* layer = m_mapData.layers; layer; layer = layer->next) {
		++count;
	}
	return count;
}

bool CKLBMapAsset::getLayerInfo(MapLayerInfo& info, const char* name) const {
	MapLayer* layer = m_mapData.layers;
	while (layer && strcmp(layer->name, name) != 0) {
		layer = layer->next;
	}
	if (!layer) {
		return false;
	}
	info.x = layer->x;
	info.y = layer->y;
	info.height = layer->height;
	info.width = layer->width;
	info.sourceData = layer->sourceData;
	return true;
}

u32 CKLBMapAsset::getObjectCount() const {
	return m_objectCount;
}

const char* CKLBMapAsset::getObjectName(u32 index) const {
	return index < m_objectCount ? m_objects[index]->name : NULL;
}

bool CKLBMapAsset::getObjectGeometry(
	u32 index, s32& x, s32& y, s32& width, s32& height
) const {
	if (index < m_objectCount) {
		MapObject* object = m_objects[index];
		x = object->x;
		y = object->y;
		width = static_cast<s32>(object->id) - object->x;
		height = static_cast<s32>(object->gid) - object->y;
		return true;
	}
	x = 0;
	y = 0;
	width = 0;
	height = 0;
	return false;
}

int CKLBMapAsset::read_null(void* context) {
	return static_cast<CKLBMapAsset*>(context)->readNull();
}

int CKLBMapAsset::read_boolean(void* context, int value) {
	return static_cast<CKLBMapAsset*>(context)->readBoolean(value);
}

int CKLBMapAsset::read_int(void* context, long long value) {
	static_cast<CKLBMapAsset*>(context)->readInt(value);
	return 1;
}

int CKLBMapAsset::read_double(void* context, double value) {
	return static_cast<CKLBMapAsset*>(context)->readDouble(value);
}

int CKLBMapAsset::read_string(void* context, const unsigned char* value, size_t length, int) {
	static_cast<CKLBMapAsset*>(context)->readString(value, length);
	return 1;
}

int CKLBMapAsset::read_start_map(void* context, unsigned int) {
	static_cast<CKLBMapAsset*>(context)->readStartMap();
	return 1;
}

int CKLBMapAsset::read_map_key(void* context, const unsigned char* key, size_t, int) {
	static_cast<CKLBMapAsset*>(context)->readMapKey(key);
	return 1;
}

int CKLBMapAsset::read_end_map(void* context) {
	return static_cast<CKLBMapAsset*>(context)->readEndMap();
}

int CKLBMapAsset::read_start_array(void* context, unsigned int) {
	return static_cast<CKLBMapAsset*>(context)->readStartArray();
}

int CKLBMapAsset::read_end_array(void* context) {
	return static_cast<CKLBMapAsset*>(context)->readEndArray();
}

int CKLBMapAsset::readNull() {
	return 1;
}

int CKLBMapAsset::readBoolean(int value) {
	if (m_field == 9) {
		m_currentLayer->visible = value != 0;
	}
	return 1;
}

int CKLBMapAsset::readInt(long long value) {
	switch (m_field) {
	case 0:
		m_mapData.height = static_cast<s32>(value);
		break;
	case 1:
		m_mapData.width = static_cast<s32>(value);
		break;
	case 2:
		m_currentLayer->width = static_cast<u32>(value);
		break;
	case 3:
		m_currentLayer->height = static_cast<u32>(value);
		break;
	case 4:
		m_mapData.tileHeight = static_cast<s32>(value);
		break;
	case 5:
		m_mapData.tileWidth = static_cast<s32>(value);
		break;
	case 6:
		m_currentTileset->tileWidth = static_cast<u32>(value);
		break;
	case 7:
		m_currentTileset->tileHeight = static_cast<u32>(value);
		break;
	case 8:
		klb_assertNull(m_dataCount < 0x10000,
			"Layer surface exceed loader capacity (65536)");
		m_tileDataBuffer[m_dataCount++] = static_cast<u16>(value);
		break;
	case 10:
		m_currentLayer->x = static_cast<s32>(value);
		break;
	case 0x0b:
		m_currentLayer->y = static_cast<s32>(value);
		break;
	case 0x0c:
		m_currentLayer->opacity = static_cast<float>(value);
		break;
	case 0x0d:
		m_currentTileset->imageWidth = static_cast<u32>(value);
		break;
	case 0x0e:
		m_currentTileset->imageHeight = static_cast<u32>(value);
		break;
	case 0x0f:
		m_currentTileset->margin = static_cast<u32>(value);
		break;
	case 0x10:
		m_currentTileset->spacing = static_cast<u32>(value);
		break;
	case 0x11:
		m_currentTileset->firstGid = static_cast<u32>(value);
		break;
	case 0x1e:
		m_currentObject->id = static_cast<u32>(value);
		break;
	case 0x1f:
		m_currentObject->gid = static_cast<u32>(value);
		break;
	case 0x20:
		m_currentObject->x = static_cast<s32>(value);
		break;
	case 0x21:
		m_currentObject->y = static_cast<s32>(value);
		break;
	}
	return 1;
}

int CKLBMapAsset::readDouble(double value) {
	if (m_field == 12) {
		m_currentLayer->opacity = static_cast<float>(value);
	}
	return 1;
}

int CKLBMapAsset::readString(const unsigned char* value, size_t length) {
	const char* string = reinterpret_cast<const char*>(value);
	u32 stringLength = static_cast<u32>(length);

	switch (m_field) {
	case 0x12:
		m_mapData.isometric = strncmp("isometric", string, 3) == 0;
		break;
	case 0x13:
		m_currentTileset->name =
			registerString(string, stringLength);
		break;
	case 0x14:
		m_currentTileset->image =
			registerString(string, stringLength);
		break;
	case 0x15:
		m_currentLayer->color =
			parseColor(string, static_cast<s32>(stringLength));
		break;
	case 0x16:
		m_currentLayer->objectGroup =
			strncmp("objectgroup", string, 6) == 0;
		break;
	case 0x17:
		m_currentLayer->name =
			registerString(string, stringLength);
		break;
	case 0x1c:
		m_currentTileset->noImage =
			strncmp("true", string, length) == 0;
		break;
	case 0x22:
		m_currentObject->name =
			registerString(string, stringLength);
		break;
	}
	return 1;
}

int CKLBMapAsset::readStartMap() {
	u16 depth = static_cast<u16>(m_mapDepth) + 1;
	m_mapDepth = static_cast<s16>(depth);
	s16 state = static_cast<s16>(m_state);

	if (state <= 3) {
		if (state == S_LAYER) {
			if (depth == 2) {
				MapLayer* layer = new MapLayer();
				m_currentLayer = layer;
				layer->next = m_mapData.layers;
				m_mapData.layers = layer;
			}
		} else if (state == S_NONE) {
			m_state = S_TOP;
		}
	} else if (state == S_OBJECT) {
		if (depth == 3) {
			MapObject* object = new MapObject();
			m_currentObject = object;
			object->next = m_currentLayer->objects;
			m_currentLayer->objects = object;
		}
	} else if (state == S_TILESET) {
		if (depth == 2) {
			MapTileset* tileset = new MapTileset();
			m_currentTileset = tileset;
			tileset->next = m_mapData.tilesets;
			m_mapData.tilesets = tileset;
		}
		if (m_field == 0x1b) {
			m_state = S_TILESET_PROPERTIES;
		}
	}
	return 1;
}

int CKLBMapAsset::readMapKey(const unsigned char* key) {
	const char* name = reinterpret_cast<const char*>(key);
	m_field = 0xffff;

	if (strncmp("properties", name, 10) == 0) {
		if (m_state == S_TILESET) {
			m_field = 0x1b;
		} else if (m_state == S_OBJECT) {
			m_state = S_OBJECT_PROPERTIES;
			m_field = 0x24;
		}
		return 1;
	}
	if (strncmp("height", name, 6) == 0) {
		if (m_state == S_TOP) {
			m_field = 0;
		} else if (m_state == S_LAYER) {
			m_field = 2;
		} else if (m_state == S_OBJECT) {
			m_field = 0x1f;
		}
		return 1;
	}
	if (strncmp("width", name, 5) == 0) {
		if (m_state == S_TOP) {
			m_field = 1;
		} else if (m_state == S_LAYER) {
			m_field = 3;
		} else if (m_state == S_OBJECT) {
			m_field = 0x1e;
		}
		return 1;
	}
	if (strncmp("tileheight", name, 10) == 0) {
		if (m_state == S_TOP) {
			m_field = 4;
		} else if (m_state == S_TILESET) {
			m_field = 6;
		}
		return 1;
	}
	if (strncmp("tilewidth", name, 9) == 0) {
		if (m_state == S_TOP) {
			m_field = 5;
		} else if (m_state == S_TILESET) {
			m_field = 7;
		}
		return 1;
	}

	if (m_state == S_TOP) {
		if (strncmp("layers", name, 6) == 0) {
			m_field = 0x19;
		} else if (strncmp("orientation", name, 11) == 0) {
			m_field = 0x12;
		} else if (strncmp("tilesets", name, 8) == 0) {
			m_field = 0x1a;
		} else if (strncmp("version", name, 7) == 0) {
			m_field = 0x18;
		}
		return 1;
	}
	if (m_state == S_LAYER) {
		if (strncmp("data", name, 4) == 0) {
			m_state = S_LAYER_DATA;
			m_field = 8;
		} else if (strncmp("name", name, 4) == 0) {
			m_field = 0x17;
		} else if (strncmp("opacity", name, 7) == 0) {
			m_field = 0x0c;
		} else if (strncmp("type", name, 4) == 0) {
			m_field = 0x16;
		} else if (*name == 'x') {
			m_field = 10;
		} else if (*name == 'y') {
			m_field = 0x0b;
		} else if (strncmp("visible", name, 7) == 0) {
			m_field = 9;
		} else if (strncmp("color", name, 5) == 0) {
			m_field = 0x15;
		} else if (strncmp("objects", name, 7) == 0) {
			m_state = S_OBJECT;
			m_field = 0x1d;
		}
		return 1;
	}
	if (m_state == S_TILESET) {
		if (strncmp("firstgid", name, 8) == 0) {
			m_field = 0x11;
		} else if (strncmp("imageheight", name, 11) == 0) {
			m_field = 0x0d;
		} else if (strncmp("imagewidth", name, 10) == 0) {
			m_field = 0x0e;
		} else if (*name == 'm') {
			m_field = 0x0f;
		} else if (strncmp("name", name, 4) == 0) {
			m_field = 0x13;
		} else if (strncmp("image", name, 5) == 0) {
			m_field = 0x14;
		} else if (strncmp("spacing", name, 7) == 0) {
			m_field = 0x10;
		}
		return 1;
	}
	if (m_state == S_TILESET_PROPERTIES) {
		if (strncmp("noimage", name, 7) == 0) {
			m_field = 0x1c;
		}
		return 1;
	}
	if (m_state == S_OBJECT) {
		if (strncmp("name", name, 4) == 0) {
			m_field = 0x22;
		} else if (strncmp("type", name, 4) == 0) {
			m_field = 0x23;
		} else if (*name == 'x') {
			m_field = 0x20;
		} else if (*name == 'y') {
			m_field = 0x21;
		}
	}
	return 1;
}

int CKLBMapAsset::readEndMap() {
	if (m_state == S_TOP && m_mapDepth == 1) {
		m_state = S_NONE;
	} else if (m_state == S_TILESET_PROPERTIES) {
		m_state = S_TILESET;
	} else if (m_state == S_OBJECT_PROPERTIES) {
		m_state = S_OBJECT;
	}
	--m_mapDepth;
	return 1;
}

int CKLBMapAsset::readStartArray() {
	s16 previousDepth = m_arrayDepth++;
	u16 state = m_state;
	if (previousDepth == 0 && state == S_LAYER) {
		m_mapData.layers = NULL;
	}
	if (state == S_OBJECT) {
		if (m_field == 0x1d && m_arrayDepth == 2) {
			m_currentLayer->objects = NULL;
		}
	} else if (state == S_TOP) {
		if (m_field == 0x19) {
			m_state = S_LAYER;
		} else if (m_field == 0x1a) {
			m_state = S_TILESET;
		}
	}
	m_dataCount = 0;
	return 1;
}

int CKLBMapAsset::readEndArray() {
	s16 depth = m_arrayDepth;
	s16 state = static_cast<s16>(m_state);

	if (depth == 1 && state < S_LAYER_DATA) {
		if (state == S_LAYER || state == S_TILESET) {
			m_state = S_TOP;
		}
	} else if (state == S_OBJECT) {
		if (depth == 2) {
			m_state = S_LAYER;
			MapLayer* layer = m_currentLayer;
			s32 objectCapacity = 0;
			for (MapObject* object = layer->objects;
				object;
				object = object->next) {
				if (object->gid && object->id) {
					++objectCapacity;
				}
			}

			klb_assert(m_objects == NULL, "Only one object layer per map.");
			m_objects = new MapObject*[objectCapacity];

			s32 objectCount = 0;
			for (MapObject* object = layer->objects;
				object;
				object = object->next) {
				if (object->gid && object->id) {
					m_objects[objectCount++] = object;
				}
			}

			sortObjects(m_objects, objectCount);
			m_objectCount = static_cast<u32>(objectCount);
		}
	} else if (state == S_LAYER_DATA) {
		if (m_field == 8) {
			if (depth == 2) {
				if (m_dataCount) {
					size_t byteCount =
						static_cast<size_t>(m_dataCount) * sizeof(u16);
					u16* layerData = static_cast<u16*>(malloc(byteCount));
					m_currentLayer->data = layerData;
					if (!layerData) {
						return 0;
					}
					memcpy(layerData, m_tileDataBuffer, byteCount);
				}
				m_state = S_LAYER;
			}
		}
	}

	m_arrayDepth = depth - 1;
	return 1;
}

void CKLBMapAsset::sortObjects(MapObject** objects, s32 count) const {
	s32 span = count;
	while (span > 1) {
		span /= 2;
		for (s32 index = span; index < count; ++index) {
			s32 previous = index;
			while ((previous -= span) >= 0) {
				MapObject* first = objects[previous];
				MapObject* second = objects[previous + span];
				if (second->x > first->x
				|| (second->x == first->x && second->y >= first->y)) {
					break;
				}
				objects[previous] = second;
				objects[previous + span] = first;
			}
		}
	}
}

bool CKLBMapAsset::allocateTileBuffer() {
	m_tileDataBuffer = new u16[0x10000];
	return true;
}

void CKLBMapAsset::scaleObjectsToTiles() {
	MapObject** objects = m_objects;
	if (!objects) {
		return;
	}
	s32 tileHeight = m_mapData.tileHeight;
	u32 index = 0;
	while (index < m_objectCount) {
		MapObject* object = objects[index++];
		s32 x = object->x;
		s32 y = object->y;
		s32 right = static_cast<s32>(object->id) + x;
		s32 bottom = static_cast<s32>(object->gid) + y;
		object->x = x / tileHeight;
		object->y = y / tileHeight;
		object->id = static_cast<u32>(right / tileHeight);
		object->gid = static_cast<u32>(bottom / tileHeight);
	}
}

u8 CKLBMapAssetPlugin::charHeader() {
	return 'M';
}

CKLBAbstractAsset* CKLBMapAssetPlugin::loadAsset(
	u8* stream,
	size_t streamSize
) {
	static yajl_callbacks callbacks = {
		CKLBMapAsset::read_null,
		CKLBMapAsset::read_boolean,
		CKLBMapAsset::read_int,
		CKLBMapAsset::read_double,
		NULL,
		CKLBMapAsset::read_string,
		CKLBMapAsset::read_start_map,
		CKLBMapAsset::read_map_key,
		CKLBMapAsset::read_end_map,
		CKLBMapAsset::read_start_array,
		CKLBMapAsset::read_end_array
	};

	CKLBMapAsset* asset = KLBNEW(CKLBMapAsset);
	asset->allocateTileBuffer();

	yajl_handle parser = yajl_alloc(&callbacks, NULL, asset);
	yajl_config(parser, yajl_allow_comments, 1);

	yajl_status status = yajl_parse(parser, stream, streamSize);
	if (status != yajl_status_ok
	|| yajl_complete_parse(parser) != yajl_status_ok) {
		yajl_free(parser);
		return NULL;
	}

	asset->buildSurfaceTable();
	asset->buildLayerData();

	asset->scaleObjectsToTiles();

	yajl_free(parser);
	return asset;
}

u32 CKLBMapAssetPlugin::getChunkID() {
	return CHUNK_TAG('M', 'A', 'P', 'B');
}

u32 CKLBMapAsset::getClassID() {
	return CLS_ASSETMAP;
}

ASSET_TYPE CKLBMapAsset::getAssetType() {
	return ASSET_MAP;
}
