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
#ifndef __KLB_MAP_MANAGEMENT__
#define __KLB_MAP_MANAGEMENT__

#include "CKLBAsset.h"

class CKLBMapNode;
class CKLBImageAsset;

class CKLBMapAsset : public CKLBAsset {
public:
	CKLBMapAsset();
	virtual ~CKLBMapAsset();

	virtual u32 getClassID();
	virtual ASSET_TYPE getAssetType();
	virtual CKLBNode* createSubTree(u32 priorityBase = 0);

	struct MapLayerInfo {
		s32 x;
		s32 y;
		u32 height;
		u32 width;
		u16* sourceData;
	};

	u32 getLayerCount() const;
	bool getLayerInfo(MapLayerInfo& info, const char* name) const;
	u32 getObjectCount() const;
	const char* getObjectName(u32 index) const;
	bool getObjectGeometry(u32 index, s32& x, s32& y, s32& width, s32& height) const;

private:
	friend class CKLBMapNode;
	friend class CKLBMapAssetPlugin;

	enum ParseState {
		S_NONE = 0,
		S_TOP = 1,
		S_LAYER = 2,
		S_TILESET = 4,
		S_TILESET_PROPERTIES = 0x20,
		S_LAYER_DATA = 0x40,
		S_OBJECT = 0x80,
		S_OBJECT_PROPERTIES = 0x100
	};

	struct MapProperty {
		MapProperty();
		~MapProperty();
		MapProperty* next;
		char* type;
		char* name;
		char* valueType;
		char* value;
	};

	struct MapObject {
		MapObject();
		~MapObject();
		MapObject* next;
		u32 gid;
		u32 id;
		s32 x;
		s32 y;
		char* name;
		char* type;
		MapProperty* currentProperty;
		MapProperty* propertiesTail;
		MapProperty* properties;
	};

	struct MapLayer {
		MapLayer();
		~MapLayer();
		MapLayer* next;
		u16* data;
		u16* sourceData;
		u32 width;
		u32 height;
		char* name;
		float opacity;
		u32 objectGroup;
		s32 x;
		s32 y;
		u32 color;
		bool visible;
		MapProperty* freeProperties;
		MapObject* objects;
	};

	struct MapTileset {
		MapTileset();
		~MapTileset();
		MapTileset* next;
		u32 firstGid;
		char* image;
		CKLBImageAsset* imageAsset;
		u32 imageWidth;
		u32 imageHeight;
		u32 margin;
		u32 tileCount;
		char* name;
		u32 spacing;
		u32 tileWidth;
		u32 tileHeight;
		bool noImage;
		MapProperty* properties;
	};

	struct MapString {
		MapString();
		~MapString();
		MapString* next;
		char* value;
	};

	struct MapData {
		MapData();
		~MapData();

		s32 isoOffsetX;
		u32 isometric;
		s32 height;
		s32 width;
		s32 tileHeight;
		s32 tileWidth;
		MapProperty* mapProperties;
		MapLayer* layers;
		MapTileset* tilesets;
		CKLBAbstractAsset** surfaces;
		u32 firstGid;
		u32 surfaceCount;
		MapProperty* currentProperties;
	};

	static int read_null(void* context);
	static int read_boolean(void* context, int value);
	static int read_int(void* context, long long value);
	static int read_double(void* context, double value);
	static int read_string(void* context, const unsigned char* value, size_t length, int pooled);
	static int read_start_map(void* context, unsigned int size);
	static int read_map_key(void* context, const unsigned char* key, size_t length, int pooled);
	static int read_end_map(void* context);
	static int read_start_array(void* context, unsigned int size);
	static int read_end_array(void* context);

	int readNull();
	int readBoolean(int value);
	int readInt(long long value);
	int readDouble(double value);
	int readString(const unsigned char* value, size_t length);
	int readStartMap();
	int readMapKey(const unsigned char* key);
	int readEndMap();
	int readStartArray();
	int readEndArray();

	char* registerString(const void* value, u32 length);
	u32 parseColor(const char* value, s32 length) const;
	void copyLayerTiles(u16 columnCount, u16 rowCount, const u16* source,
	                    u16 outputStride, u16 outputHeight, u16* output) const;
	bool allocateTileBuffer();
	void scaleObjectsToTiles();
	void sortObjects(MapObject** objects, s32 count) const;
	bool buildSurfaceTable();
	bool buildLayerData();
	bool setupNode(CKLBMapNode* node, bool allocateRenderColors) const;

	MapData m_mapData;
	MapString* m_strings;
	u16 m_state;
	u16 m_field;
	s16 m_mapDepth;
	s16 m_arrayDepth;
	MapLayer* m_currentLayer;
	MapObject* m_currentObject;
	MapTileset* m_currentTileset;
	MapProperty* m_currentProperty;
	MapObject** m_objects;
	u32 m_objectCount;
	u32 m_dataCount;
	u16* m_tileDataBuffer;
	s32 m_sourceWidth;
	s32 m_sourceHeight;
};

class CKLBMapAssetPlugin : public IKLBAssetPlugin {
public:
	CKLBMapAssetPlugin();
	virtual ~CKLBMapAssetPlugin();

	virtual u8 charHeader();
	virtual u32 getChunkID();
	virtual const char* fileExtension();
	virtual CKLBAbstractAsset* loadAsset(u8* stream, size_t streamSize);
};

#endif // __KLB_MAP_MANAGEMENT__
