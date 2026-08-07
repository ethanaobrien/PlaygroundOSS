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
#ifndef __TEXTURE_MGT__
#define __TEXTURE_MGT__

#include "CKLBAsset.h"

class KLBTextureAssetPlugin;

class CTexture;
class CTextureUsage;
class CKLBImageAsset;
class CKLBGridTextureObject;

extern bool g_enableTextureBorderPatch;

/*!
* \class CKLBTextureAsset
* \brief Texture Asset Class
* 
* Texture asset is a texture that describes a 2D texture uploaded to the GPU.
* Inside a texture asset file, we also store a list of 2D models.
* A 2D model is a list vertices and related indexes for rendering triangles. 
*/
class CKLBTextureAsset : public CKLBAbstractAsset {
	friend class KLBTextureAssetPlugin;
	friend class CKLBGridTextureObject;
public:

	CKLBTextureAsset();
	~CKLBTextureAsset();

	virtual	ASSET_TYPE	getAssetType()	{ return ASSET_TEXTURE;		}
	virtual u32			getClassID	()	{ return CLS_ASSETTEXTURE;  }

	/* NO DICO
	virtual bool	include				(const char* name);*/
	virtual void unloadRessource();

	virtual void onRegisterSubAsset		();
	virtual void onUnregisterSubAsset	();
	virtual CKLBTextureAsset* clone		();
	virtual void incrementRefCount		();
	virtual bool decrementRefCount		();
	virtual void onFirstReference		();
	virtual void onLastReference			();
	virtual void setDefaultImageName	(const char* name);

	CKLBImageAsset*		getImage		(const char* fileName);
	CKLBImageAsset*		createImageAlias(CKLBImageAsset* image, const char* aliasName);
	static CKLBTextureAsset* createMovieTexture(const char* name);
	void updateMovieTexture(
		u32 textureTarget,
		u32 textureName,
		s32 width,
		s32 height,
		const float* uv
	);

	u16					m_width;
	u16					m_height;
	u16					m_type;
	u32					m_totalVertexCount;
	u32					m_totalIndexCount;
	u16					m_imageCount;
	void*				m_bitmap;
	u16*				m_indexBufferTotal;
	float*				m_floatBufferTotal;
	CTexture*			m_pTexture;
	CKLBGridTextureObject* m_pTextureState;	// Owning dynamic-grid state; alias clones do not inherit it.
	CTextureUsage*		m_pTextureUsage;
	CKLBImageAsset**	m_pImages;
	u8*					m_softTexture;
	u8					m_bytePerPix;
	const char*			m_pDefaultImageName;
	CKLBTextureAsset*	m_pParentTexture;
	CKLBTextureAsset*	m_pChildTexture;
	CKLBTextureAsset*	m_pNextTexture;
	CKLBImageAsset*		m_pAliasList;
};

struct SKLBRect {
public:
	s16	m_iLeft;
	s16 m_iRight;
	s16 m_iTop;
	s16 m_iBottom;

	inline
	s32 getWidth() {	return m_iRight - m_iLeft; }

	inline
	s32 getHeight() {	return m_iBottom - m_iTop; }
};

/*!
* \class CKLBImageAsset
* \brief Image Asset Class
* 
* CKLBImageAsset is a basic asset for image rendering.
* It uses a CKLBTextureAsset.
* There are also various attributes that are stored within an image asset.
*/
class CKLBImageAsset : public CKLBAsset {
	friend class KLBTextureAssetPlugin;
	friend class CKLBGridTextureObject;
public:
	static const u8		IS_STANDARD_RECT	= 0x1;
	static const u8		IS_SCALE9			= 0x2;
	static const u8		IS_SCROLLBARTYPE	= 0x4;
	static const u8		IS_3DMODEL			= 0x8;
	static const u8		IS_STANDARD_USAGE_5	= 0x10;
	static const u8		IS_STANDARD_USAGE_6	= 0x20;

	static CKLBNode*	createSprite	(	u32 textureHandle,
											const char* imageName,
											CKLBNode* pParentNode,
											u32 renderPriority);

	CKLBImageAsset();
	~CKLBImageAsset();

	virtual CKLBNode*	createSubTree	(u32 priorityBase = 0);
	virtual u32			getClassID		()		{ return CLS_ASSETIMAGE;	}
	virtual	ASSET_TYPE	getAssetType	()		{ return ASSET_IMAGE;		}
	virtual CKLBImageAsset* clone		();

	inline
	u32					getVertexCount	()		{ return m_uiVertexCount;	}

	inline
	u32					getIndexCount	()		{ return m_uiIndexCount;	}

	CKLBTextureAsset*	getTexture		()		{ return m_pTextureAsset;	}	

	inline
	SKLBRect*			getSize			()		{ return &m_imageSize;		}

	void				setSubImage		(u32 width, u32 height, u32 offX, u32 offY);
	CKLBImageAsset*		findSub			(u32 index);

	void				addSubImage		(CKLBImageAsset* pImage);

	/** Warning : the asset pointer is not managed and becomes the responsability of the owner. */
	CKLBImageAsset*		getSubImage		(u32 index, CKLBImageAsset* replaceAsset = NULL);

	void				getCenter		(s32& cx, s32& cy);
	CKLBImageAsset*		getAsTopLeftImage(s32 offX, s32 offY);

	inline
	u16*				getIndexBuffer	()	{ return m_pIndex;		}

	inline
	float*				getUVBuffer		()	{ return m_pUVCoord;	}

	inline
	float*				getXYBuffer		()	{ return m_pXYCoord;	}

	void				getXY(u32 vertexIndex, float* pX, float* pY);
	void				getUV(u32 vertexIndex, float* pU, float* pV);

	bool				getAttribute(u8 attribID, s32& attribValue);
	bool				getAttribute(u8 attribID, float& attribValue);
	bool				getAttribute(u8 attribID, const char*& attribValue);

	inline u8			hasStandardAttribute(u8 mask)	{ return m_usageType & mask; }
public:
	ASSET_ATTRIB*		m_attribList;

	u16*				m_pIndex;
	float*				m_pUVCoord;
	float*				m_pXYCoord;

	CKLBTextureAsset*	m_pTextureAsset;

	CKLBImageAsset*		m_subTiles;
	CKLBImageAsset*		m_nextSubTile;

	SKLBRect			m_imageSize;	// Original image size
	float				m_boundWidth;
	float				m_boundHeight;
	s32					m_renderOffset;

	s16					m_subIndex;

	s16					m_iCenterX;
	s16					m_iCenterY;

	u16					m_uiVertexCount;
	u16					m_uiIndexCount;


	u16					m_attribMask;
	u16					m_attribCount;
	u16					m_tileWidth;
	u16					m_tileHeight;
	u16					m_tileOffX;
	u16					m_tileOffY;
	u16					m_tileCount;
	u16					m_uiSubTileCount;
	u8					m_usageType;
	bool				m_bAllocatedOutsideTexture;

	// Link and owner metadata for image aliases created by a texture fallback.
	// The owning texture manages the alias chain; image assets do not own it.
	CKLBImageAsset*		m_pNextAlias;
	CKLBTextureAsset*	m_pOwnerTexture;
private:
	CKLBImageAsset*		m_topLeftImage;
};

/*!
 * \class CKLBGridTextureObject
 * \brief Runtime texture atlas divided into reusable image cells.
 *
 * Each cell owns a regular image/texture-asset pair.  The grid keeps three
 * intrusive index lists so cells can move between free, live and reclaimable
 * states without allocating list nodes while images are being streamed.
 */
struct AssetGridSource;

class CKLBGridTextureObject : public CKLBTextureAsset {
	friend class CKLBTextureAsset;
	friend class KLBTextureAssetPlugin;
public:
	union CellStatus {
		u16 value;
		struct {
			u8 lifecycle;
			u8 loading;
		} bytes;
	};

	struct CellState {
		u16 previous;
		u16 next;
		u32 uploadOffset;
		void* source;
		u32 sourceSize;
		CellStatus status;
		u16 format;
	};

	CKLBGridTextureObject();
	~CKLBGridTextureObject();

	virtual u32 getClassID() { return CLS_ASSETGRIDTEXTURE; }
	virtual void destroy();

	static CKLBGridTextureObject* create(
		s32 cellWidth,
		s32 cellHeight,
		s32 cellCount,
		bool rgba,
		bool border
	);

	void lock();
	bool unlock();
	u16 findImage(const char* name) const;
	u16 loadImage(AssetGridSource* source, const char* name);
	u16 reloadImage(AssetGridSource* source, const char* name);

private:
	enum CellList {
		LIST_LIVE,
		LIST_FREE,
		LIST_RECLAIM,
		LIST_COUNT
	};

	u16 popFront(CellList list);
	u16 popBack(CellList list);
	void pushFront(CellList list, u16 cellIndex);
	void unlinkCell(CellList list, u16 cellIndex);
	void markCellReferenced(u16 cellIndex);
	bool updateCell(
		u32 column,
		u32 row,
		AssetGridSource* source
	);
	void activateCell(CKLBTextureAsset* texture);
	void releaseCell(CKLBTextureAsset* texture);
	bool init(
		s32 cellWidth,
		s32 cellHeight,
		s32 cellCount,
		bool rgba,
		bool border
	);
	static u16 registerTexture(CKLBGridTextureObject* texture);
	static void releaseTextures();

	u16 m_listHeads[LIST_COUNT];
	u16 m_listTails[LIST_COUNT];

	CKLBImageAsset*       m_gridImages;
	CKLBImageAsset**      m_imagePointers;
	CKLBTextureAsset*     m_cellTextures;
	CellState*            m_cellStates;
	float*                m_cellGeometry;
	u8*                   m_uploadBuffer;
	u8*                   m_decodeBuffer;
	u16                   m_quadIndices[6];
	u16                   m_columnCount;
	u16                   m_rowCount;
	u16                   m_textureHeight;
	u16                   m_textureWidth;
	u16                   m_cellWidth;
	u16                   m_cellHeight;
	u16                   m_channelCount;
	u16                   m_gridID;
	bool                  m_hasBorder;
	bool                  m_locked;
};

struct TextureDecodeTarget {
	typedef s32 (*AllocateCallback)(TextureDecodeTarget* target);

	void*             context;
	AllocateCallback  allocate;
	u8*               pixels;
	u32               byteCount;
	u32               width;
	u32               height;
	u32               channelCount;
};

struct AssetGridSource {
	u8* data;
	u32 length;
	u8 option;
};

bool gridRequestCache(
	void* grid,
	AssetGridSource* source,
	const char* name
);
u32 gridLoadImage(
	void* grid,
	AssetGridSource* source,
	const char* name,
	bool mipmap
);

bool decodeTextureImage(
	u8* source,
	u32 sourceSize,
	u32 expectedWidth,
	u32 expectedHeight,
	u8* pixels,
	u32 byteCount,
	u32* channelCount,
	TextureDecodeTarget* target
);

enum E_TEXTURELOADINGMODE {
	TEX_LOAD_GPU,
	TEX_LOAD_CPU,
	TEX_LOAD_GPUCPU
};

/*!
* \class KLBTextureAssetPlugin
* \brief Texture Asset Plugin Class
* 
* Plugin responsible for loading texture and images inside texture.
* See CKLBTextureAsset and CKLBImageAsset.
*/
class KLBTextureAssetPlugin : public IKLBAssetPlugin {
public:
	typedef void (*TextureLoadCallback)(u8* decodedData, u32 length, bool checksumValid);

	KLBTextureAssetPlugin(TextureLoadCallback loadCallback);
	~KLBTextureAssetPlugin();

	void	setLoadingMode(E_TEXTURELOADINGMODE mode);
	u8*		createSoftTexture	(	s32 width, 
									s32 height, 
									u32 pixelFormat, 
									u8 channelCount, 
									void* data );

	virtual u32					getChunkID		()			{ return CHUNK_TAG('T','E','X','B'); }
	virtual	u8					charHeader		()			{ return 'T';			}
	virtual const char*			fileExtension	()			{ return ".texb"; }

	virtual CKLBAbstractAsset*	loadAsset(u8* stream, size_t streamSize);
	virtual void				setCurrentFileName(const char* currentFileName) {
		m_currentFile = currentFileName;
	}

	void				setBuffers		(CKLBTextureAsset*	pTextureAsset, float* uvBuffer, float* xyBuffer, u16* indexBuffer);
	CKLBImageAsset*		loadImage		(u8* stream, u32 streamSize, CKLBImageAsset* pReload);
	void				setQuarterTexture(bool activate) {
		m_useQuarterTexture = activate;
	}
	void				setMipmapOnce() {
		m_mipmapOnce = true;
	}
	void				setDisableImageSizeOptimization(bool disable) {
		m_disableImageSizeOptimization = disable;
	}
private:
	TextureLoadCallback	m_loadCallback;
	float*				m_pUVBuffer;
	float*				m_pXYBuffer;
	float*				m_pLastLoadedUV;
	u16*				m_pLastLoadedIndex;
	u16*				m_pIndexBuffer;
	CKLBTextureAsset*	m_pTextureAsset;
	const char*			m_currentFile;
	bool				m_loadHardware;
	bool				m_loadSoftware;
	bool				m_useQuarterTexture;
	bool				m_mipmapOnce;
	bool				m_disableImageSizeOptimization;
};

bool createScreenAsset	(const char* name, u32 orgWidthI, u32 orgHeightI);
void setupTextureBackgroundFilter(
	float offsetX,
	float offsetY,
	float extent,
	float opacity,
	u32 red,
	u32 green,
	u32 blue);
CKLBAbstractAsset* createTexture(u32 orgWidthI, u32 orgHeightI, const char* name,
	s32 pixelFormat, u8 roundToPowerOfTwo, CTexture* sourceTexture);
bool doScreenShot		(const char* name, u32 srcx, u32 srcy, u32 width, u32 height, u32 dstx, u32 dsty);
void freeScreenAsset	(const char* name);

#endif
