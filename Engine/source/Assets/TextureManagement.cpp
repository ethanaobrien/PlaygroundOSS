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
#include "TextureManagement.h"
#include "RenderingFramework.h"
#include "mem.h"
#include "zlib.h"
#include "CKLBUtility.h"
#include "CKLBDrawTask.h"
#include "KLBPlatformMetrics.h"
#include "CKLBGameApplication.h"
#include "CKLBLuaLibASSET.h"
#include "CKLBTask.h"
#include "CKLBScriptEnv.h"

/*
 * Here is the header of the ETC1 decoder part taken out from the 
 * rg_etc1 library, we took only the decoder part out.
 *
 * The original project code can be found at :
 * http://code.google.com/p/rg-etc1/
 * The implementation and license detail is at the end of the file.
 *
// Fast, high quality ETC1 block packer/unpacker - Rich Geldreich <richgel99@gmail.com>
// Please see ZLIB license at the end of this file.
 */

namespace rg_etc1
{
   // Unpacks an 8-byte ETC1 compressed block to a block of 4x4 32bpp RGBA pixels.
   // Returns false if the block is invalid. Invalid blocks will still be unpacked with clamping.
   // This function is thread safe, and does not dynamically allocate any memory.
   // If preserve_alpha is true, the alpha channel of the destination pixels will not be overwritten. Otherwise, alpha will be set to 255.
   bool unpack_etc1_block(const void *pETC1_block, unsigned int* pDst_pixels_rgba, bool preserve_alpha = false);
} // namespace rg_etc1

u32 gTextureAllocSW = 0;
u32 gTextureAllocHW = 0;

static void releaseGridTexturesOnPluginShutdown();
void processImage8888(u32 /*pixelCount*/, u32 lineWidth, u32 height, u8* buffer);
void processImage565(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer);
void processImage4444(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer);
void processImage5551(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer);
enum TextureAssetPixelFormat {
	TEXTURE_ASSET_DEPTH16 = 1,
	TEXTURE_ASSET_RESERVED = 2,
	TEXTURE_ASSET_RGB888  = 3,
	TEXTURE_ASSET_RGBA8888 = 4
};

static const int s_textureBytesPerPixel[4] = { 2, 0, 3, 4 };
static const int s_textureChannels[4] = {
	CKLBOGLWrapper::DEPTH,
	CKLBOGLWrapper::LUMINANCE,
	CKLBOGLWrapper::RGB,
	CKLBOGLWrapper::RGBA
};
static const int s_texturePixelFormats[4] = {
	GL_UNSIGNED_SHORT,
	0,
	GL_UNSIGNED_BYTE,
	GL_UNSIGNED_BYTE
};

CKLBAbstractAsset* createTexture(u32 orgWidthI, u32 orgHeightI, const char* name,
	s32 pixelFormat, u8 roundToPowerOfTwo, CTexture* sourceTexture);

CKLBTextureAsset::CKLBTextureAsset()
: CKLBAbstractAsset ()
, m_indexBufferTotal(NULL)
, m_floatBufferTotal(NULL)
, m_pTexture        (NULL)
, m_pTextureState   (NULL)
, m_pTextureUsage   (NULL)
, m_pImages         (NULL)
, m_softTexture     (NULL)
, m_imageCount      (0)
, m_bytePerPix		(0)
, m_width			(0)
, m_height			(0)
, m_pDefaultImageName(NULL)
, m_pParentTexture	(NULL)
, m_pChildTexture	(NULL)
, m_pNextTexture	(NULL)
, m_pAliasList		(NULL)
{
}

CKLBTextureAsset::~CKLBTextureAsset()
{
	if (!m_refCountControlsResource) {
		if (m_pAliasList) {
			CKLBImageAsset* alias = m_pAliasList;
			while (alias) {
				CKLBImageAsset* nextAlias = alias->m_pNextAlias;
				KLBDELETE(alias);
				alias = nextAlias;
			}
			m_pAliasList = NULL;
		}

		if (!m_pParentTexture) {
			if (m_pImages) {
				for (u32 n = 0; n < m_imageCount; n++) {
					if (m_pImages[n]) {
						KLBDELETE(m_pImages[n]);
					}
				}
				KLBDELETEA(m_pImages);
			}

			if (m_indexBufferTotal)	{ KLBDELETE(m_indexBufferTotal); }
			if (m_floatBufferTotal)	{ KLBDELETE(m_floatBufferTotal); }

			CKLBOGLWrapper& pMgr = CKLBOGLWrapper::getInstance();

			if (m_pTexture) {
				gTextureAllocHW -= this->m_width * this->m_height * m_bytePerPix;
				if (m_pTextureUsage) {
					m_pTexture->releaseUsage(m_pTextureUsage);
				}
				pMgr.releaseTexture(m_pTexture);
				m_pTexture = NULL;
			}

			if (m_pChildTexture) {
				CKLBTextureAsset* child = m_pChildTexture;
				while (child) {
					CKLBTextureAsset* nextChild = child->m_pNextTexture;
					KLBDELETE(child);
					child = nextChild;
				}
				m_pChildTexture = NULL;
			}

		} else {
			CKLBTextureAsset* previous = NULL;
			CKLBTextureAsset* child = m_pParentTexture->m_pChildTexture;
			while (child) {
				if (child == this) {
					if (previous) {
						previous->m_pNextTexture = m_pNextTexture;
					} else {
						m_pParentTexture->m_pChildTexture = m_pNextTexture;
					}
					break;
				}
				previous = child;
				child = child->m_pNextTexture;
			}
			m_pParentTexture = NULL;
		}

		KLBDELETEA(m_pDefaultImageName);
		m_pDefaultImageName = NULL;
	}

	if (m_softTexture) {
		KLBDELETEA(m_softTexture);
		gTextureAllocSW -= this->m_width * this->m_height * 4;
		m_softTexture = NULL;
	}
}

CKLBTextureAsset* CKLBTextureAsset::clone()
{
	CKLBTextureAsset* asset = KLBNEW(CKLBTextureAsset);
	if (asset) {
		asset->m_pParentTexture     = this;
		asset->m_width              = m_width;
		asset->m_height             = m_height;
		asset->m_type               = m_type;
		asset->m_totalVertexCount   = m_totalVertexCount;
		asset->m_totalIndexCount    = m_totalIndexCount;
		asset->m_imageCount         = m_imageCount;
		asset->m_bitmap             = m_bitmap;
		asset->m_indexBufferTotal   = m_indexBufferTotal;
		asset->m_floatBufferTotal   = m_floatBufferTotal;
		asset->m_pTexture           = m_pTexture;
		asset->m_pTextureUsage      = m_pTextureUsage;
		asset->m_pImages            = m_pImages;
		asset->m_softTexture        = m_softTexture;
		asset->m_bytePerPix         = m_bytePerPix;
		asset->m_pNextTexture       = m_pChildTexture;
		m_pChildTexture             = asset;
	}
	return asset;
}

void CKLBTextureAsset::incrementRefCount()
{
	if (m_pParentTexture) {
		m_pParentTexture->incrementRefCount();
	}
	CKLBAbstractAsset::incrementRefCount();
}

bool CKLBTextureAsset::decrementRefCount()
{
	CKLBTextureAsset* parent = m_pParentTexture;
	bool released = CKLBAbstractAsset::decrementRefCount();
	if (parent) {
		parent->decrementRefCount();
	}
	return released;
}

void CKLBTextureAsset::setDefaultImageName(const char* name)
{
	KLBDELETEA(m_pDefaultImageName);
	m_pDefaultImageName = CKLBUtility::copyString(name);
}

void
CKLBTextureAsset::unloadRessource() {
	if (m_pTexture) {
		m_pTexture->makeEmptyShell();
	}

	gTextureAllocHW -= this->m_width * this->m_height * m_bytePerPix;

	// Release software texture.
	if (m_softTexture) {
		KLBDELETEA(m_softTexture);
		gTextureAllocSW -= this->m_width * this->m_height * 4;
		m_softTexture = NULL;
	}
}

CKLBImageAsset*
CKLBTextureAsset::getImage(const char* fileName)
{
	const char* assetDirPrefix = static_cast<CKLBGameApplication&>(CPFInterface::getInstance().client()).getAssetDirPrefix();
	size_t fileNameLength = strlen(fileName);
	if (!CKLBUtility::hasAssetDirPrefix(fileName, assetDirPrefix, fileNameLength)) {
		char prefixedName[1000];
		sprintf(prefixedName, "%s/%s", assetDirPrefix, fileName);
		CKLBImageAsset* prefixedImage = getImage(prefixedName);
		if (prefixedImage) return prefixedImage;
	}

	// TODO OPTIMIZE : binary search instead of stupid search.
	// Tool will garantee that image are ordered correctly.
	CKLBImageAsset* image = NULL;
	if (fileName) {
		for (u32 n = 0; n < m_imageCount; n++) {
			if (strcmp(fileName,m_pImages[n]->getName()) == 0) {
				image = m_pImages[n];
			}
		}
	}
	if (image) return image;

	if (m_pDefaultImageName) {
		CKLBImageAsset* defaultImage = NULL;
		for (u32 n = 0; n < m_imageCount; n++) {
			if (strcmp(m_pDefaultImageName, m_pImages[n]->getName()) == 0) {
				defaultImage = m_pImages[n];
				break;
			}
		}

		if (defaultImage) {
			const char* aliasName = CKLBUtility::copyString(fileName);
			image = defaultImage->clone();
			if (aliasName && image) {
				image->m_fileSource = aliasName;
				image->m_pOwnerTexture = this;
				image->m_pNextAlias = m_pAliasList;
				m_pAliasList = image;
			} else {
				KLBDELETE(image);
				KLBDELETEA(aliasName);
			}
		}
	}
	return image;
}

void 
CKLBTextureAsset::onRegisterSubAsset() 
{ 
	//
	// Register all sub images as asset search.
	//
	CKLBAssetManager& pAMgr = CKLBAssetManager::getInstance();
	for (u32 n = 0; n < m_imageCount; n++) {
		pAMgr.addSearchSubEntry(this, m_pImages[n]->getName());
	}
}

void 
CKLBAssetManager::dump() 
{
	if (m_assetRecord) {
		FILE* pFile = CPFInterface::getInstance().client().getShellOutput();

		int totalSize = 0;
		for (u32 n = 0; n < this->m_maxAssetEntry; n++) {
			
			if (!m_assetRecord[n].m_isFree) {
				CKLBAbstractAsset* pAsset = m_assetRecord[n].m_pAsset;

				// Display self name
				fprintf(pFile, "[%4i] %s",n, pAsset->getName());
				
				// display type
				switch (pAsset->getClassID()) {
				case CLS_ASSETBASE:
					fprintf(pFile, " Base Asset?");
					break;
				case CLS_ASSETTEXTURE:
					fprintf(pFile, " Texture Asset");
					break;
				case CLS_ASSETIMAGE:
					fprintf(pFile, " Image Asset");
					break;
				case CLS_ASSETAUDIO:
					fprintf(pFile, " Audio Asset");
					break;
				case CLS_ASSETFLASH:
					fprintf(pFile, " Flash Asset");
					break;
				case CLS_ASSETANIMSPLINE:
					fprintf(pFile, " Spline Asset");
					break;
				case CLS_ASSETMAP:
					fprintf(pFile, " Map Asset");
					break;
				case CLS_ASSETCELLANIM:
					fprintf(pFile, " Cell Anim Asset");
					break;
				case CLS_ASSETCOMPOSITE:
					fprintf(pFile, " UI Form");
					break;
				case CLS_ASSETNODEANIM:
					fprintf(pFile, " Node Anim");
					break;
				default:
					fprintf(pFile, " Unknown");
					break;
				}

				if (pAsset->getClassID() == CLS_ASSETTEXTURE) {
					CKLBTextureAsset* p = (CKLBTextureAsset*)pAsset;

					int size = (p->m_bytePerPix * p->m_width * p->m_height) >> 10;
					totalSize += size;
					fprintf(pFile, " @%p W:%4i H:%4i MEM:%4iKB\n",pAsset, p->m_width, p->m_height, size);
					for (int m = 0; m < p->m_imageCount; m++) {
						fprintf(pFile, "\t%s\n", p->m_pImages[m]->getName());
					}
				} else {
					fprintf(pFile, " @%p\n",pAsset);
				}
			}
		}
		fprintf(pFile, "TOTAL TEXTURE SIZE : %08i KB\n", totalSize);
	}
}

void 
CKLBTextureAsset::onUnregisterSubAsset() 
{
	//
	// Register all sub images as asset search.
	//
	CKLBAssetManager& pAMgr = CKLBAssetManager::getInstance();
	for (u32 n = 0; n < m_imageCount; n++) {
		pAMgr.removeSearchEntry(m_pImages[n]->getName());
	}
}

/* NO DICO
bool CKLBTextureAsset::include(const char* name) {
	if (!CKLBAbstractAsset::include(name)) {
		return (getImage(name) != NULL);
	}
	return true;
}*/

CKLBImageAsset::CKLBImageAsset()
: CKLBAsset         ()
, m_pUVCoord        (NULL)
, m_pXYCoord        (NULL)
, m_pIndex          (NULL)
, m_pTextureAsset   (NULL)
, m_subIndex        (-1)
, m_uiVertexCount   (0)
, m_uiIndexCount    (0)
, m_subTiles        (NULL)
, m_nextSubTile     (NULL)
, m_attribList      (NULL)
, m_attribCount     (0)
, m_usageType       (0)
, m_pNextAlias      (NULL)
, m_pOwnerTexture   (NULL)
, m_topLeftImage    (NULL)
, m_renderOffset    (0)
, m_bAllocatedOutsideTexture(false)
{
}

CKLBImageAsset::~CKLBImageAsset() 
{
	if (m_subTiles) {
		CKLBImageAsset* pImgParse = m_subTiles;
		while (pImgParse) {
			CKLBImageAsset* pImgParseNext = pImgParse->m_nextSubTile;
			KLBDELETE(pImgParse);
			pImgParse = pImgParseNext;
		}
	}

	if (m_attribCount) {
		for (int n = 0; n < m_attribCount;n++) {
			if (m_attribList[n].type == ASSET_ATTRIB::zATTRIB_STRING) {
				KLBDELETEA(m_attribList[n].v.str);
			}
		}
		m_attribCount = 0;
	}

	KLBDELETEA(m_attribList);

	KLBDELETE(m_topLeftImage);

	if (m_bAllocatedOutsideTexture) {
		KLBDELETEA(m_pXYCoord);
		// Use the same array with offset from beginning, NEVER DELETE
		// KLBDELETEA(m_pUVCoord);
		// KLBDELETEA(m_pIndex	);
	}

	if (m_fileSource) {
		if (m_pOwnerTexture) {
			CKLBImageAsset* pPrevious = NULL;
			CKLBImageAsset* pImage = m_pOwnerTexture->m_pAliasList;
			while (pImage) {
				if (pImage == this) {
					if (pPrevious) {
						pPrevious->m_pNextAlias = m_pNextAlias;
					} else {
						m_pOwnerTexture->m_pAliasList = m_pNextAlias;
					}
					break;
				}
				pPrevious = pImage;
				pImage = pImage->m_pNextAlias;
			}
		} else {
			m_fileSource = NULL;
		}
	}
}

CKLBImageAsset* CKLBImageAsset::clone()
{
	CKLBImageAsset* asset = KLBNEW(CKLBImageAsset);
	if (asset) {
		asset->m_subTiles                 = NULL;
		asset->m_attribCount              = 0;
		asset->m_attribList               = NULL;
		asset->m_topLeftImage             = NULL;
		asset->m_iCenterX                 = 0;
		asset->m_iCenterY                 = 0;
		asset->m_bAllocatedOutsideTexture = false;
		asset->m_pXYCoord       = m_pXYCoord;
		asset->m_pIndex         = m_pIndex;
		asset->m_pUVCoord       = m_pUVCoord;
		asset->m_subIndex       = m_subIndex;
		asset->m_pTextureAsset  = m_pTextureAsset;
		asset->m_subTiles       = m_subTiles;
		asset->m_nextSubTile    = m_nextSubTile;
		asset->m_uiVertexCount  = m_uiVertexCount;
		asset->m_uiIndexCount   = m_uiIndexCount;
		asset->m_imageSize      = m_imageSize;
		asset->m_boundWidth     = m_boundWidth;
		asset->m_boundHeight    = m_boundHeight;
		asset->m_usageType      = m_usageType;
	}
	return asset;
}

void 
CKLBImageAsset::getCenter(s32& cx, s32& cy) 
{
	cx = this->m_iCenterX;
	cy = this->m_iCenterY;
}

CKLBImageAsset* 
CKLBImageAsset::getAsTopLeftImage(s32 offX, s32 offY) 
{
	if ((offX == 0) && (offY == 0)) {
		return this;
	}
	if (m_fileSource) {
		return this;
	}
	if (!m_topLeftImage) {
		m_topLeftImage = clone();
		float* pArrayXY = KLBNEWA(float, this->getVertexCount() * 2);
		if (m_topLeftImage && pArrayXY) {
			m_topLeftImage->m_bAllocatedOutsideTexture = true;
			m_topLeftImage->m_pXYCoord = pArrayXY;

			for (u32 n = 0; n < this->getVertexCount() * 2; n += 2) {
				pArrayXY[n    ] = this->m_pXYCoord[n    ] + offX;
				pArrayXY[n + 1] = this->m_pXYCoord[n + 1] + offY;
			}
		} else {
			KLBDELETE(m_topLeftImage);
			m_topLeftImage = NULL;
			KLBDELETEA(pArrayXY);
		}
	}
	return m_topLeftImage;
}

bool 
CKLBImageAsset::getAttribute(u8 attribID, s32& attribValue) 
{
	for (int n = 0; n < m_attribCount; n++) {
		if (this->m_attribList[n].attribID == attribID) {
			if (this->m_attribList[n].type == ASSET_ATTRIB::zATTRIB_INT) {
				attribValue = m_attribList[n].v.value;
				return true;
			}
		}
	}
	return false;
}

bool 
CKLBImageAsset::getAttribute(u8 attribID, float& attribValue) 
{
	for (int n = 0; n < m_attribCount; n++) {
		if (this->m_attribList[n].attribID == attribID) {
			if (this->m_attribList[n].type == ASSET_ATTRIB::zATTRIB_FLOAT) {
				attribValue = m_attribList[n].v.fvalue;
				return true;
			}
		}
	}
	return false;
}

bool 
CKLBImageAsset::getAttribute(u8 attribID, const char*& attribValue) 
{
	for (int n = 0; n < m_attribCount; n++) {
		if (this->m_attribList[n].attribID == attribID) {
			if (this->m_attribList[n].type == ASSET_ATTRIB::zATTRIB_STRING) {
				attribValue = m_attribList[n].v.str;
				return true;
			}
		}
	}
	return false;
}

void 
CKLBImageAsset::getXY(u32 vertexIndex, float* pX, float* pY) 
{
	klb_assertNull(pX && pY, "null pointer");
	klb_assertNull(vertexIndex < m_uiVertexCount , "invalid index");
	if (vertexIndex < m_uiVertexCount) {
		vertexIndex *= 2;
		*pX = m_pXYCoord[vertexIndex++];
		*pY = m_pXYCoord[vertexIndex  ];
	}
}

void 
CKLBImageAsset::getUV(u32 vertexIndex, float* pU, float* pV) 
{
	klb_assertNull(pU && pV, "null pointer");
	klb_assertNull(vertexIndex < m_uiVertexCount , "invalid index");
	if (vertexIndex < m_uiVertexCount) {
		vertexIndex *= 2;
		*pU = m_pUVCoord[vertexIndex++];
		*pV = m_pUVCoord[vertexIndex  ];
	}
}

void 
CKLBImageAsset::setSubImage(u32 width, u32 height, u32 offX, u32 offY) 
{
	this->m_tileWidth	= width;
	this->m_tileHeight	= height;
	this->m_tileOffX	= offX;
	this->m_tileOffY	= offY;
	this->m_tileCount	= this->m_imageSize.getWidth() / m_tileWidth;
	if (m_tileCount == 0) {
		m_tileCount = 1;
	}
}

CKLBImageAsset* 
CKLBImageAsset::findSub(u32 index) 
{
	CKLBImageAsset* pAsset = this->m_subTiles;
	while (pAsset) {
		if (pAsset->m_subIndex == index) {
			return pAsset;
		}
		pAsset = pAsset->m_nextSubTile;
	}
	return NULL;
}

void 
CKLBImageAsset::addSubImage(CKLBImageAsset* pImage) 
{
	// add to link list.
	pImage->m_nextSubTile = this->m_subTiles;
	this->m_subTiles	= pImage;
}

CKLBImageAsset*	CKLBImageAsset::getSubImage(u32 index, CKLBImageAsset* pAsset) {
	if (m_uiSubTileCount != 1) {
		klb_assertAlways("TODO implement");
	} else {
		if (m_uiIndexCount == 6 && m_uiVertexCount == 4) {
			bool doAlloc = (!pAsset);
			if (doAlloc) {
				pAsset = findSub(index); 
				if (!pAsset) {
					pAsset = KLBNEW(CKLBImageAsset);
				} else {
					return pAsset;
				}
			} else {
				klb_assertAlways("FORBIDDEN FOR NOW");
				return NULL;
			}

			if (pAsset) {
				this->addSubImage(pAsset);
				// pAsset->m_assetID		= CKLBAssetManager::getInstance()->allocateAssetSlot(pAsset);
				pAsset->m_pTextureAsset	= this->m_pTextureAsset;
				pAsset->m_pName			= this->m_pName;
				pAsset->m_subIndex		= index;


				pAsset->m_bAllocatedOutsideTexture	= true;

				if (doAlloc) {
					pAsset->m_pXYCoord	= KLBNEWA(float,(8*2) + 3);
					pAsset->m_pUVCoord	= &pAsset->m_pXYCoord[8]; 
					pAsset->m_pIndex	= (u16*)(&pAsset->m_pUVCoord[8]);
				}

				if (pAsset->m_pXYCoord) {
					u32 tY = index / m_tileCount;
					u32 tX = index - (tY * m_tileCount);
					float fWidth	= this->m_pXYCoord[2] - this->m_pXYCoord[0];
					float fHeight	= this->m_pXYCoord[5] - this->m_pXYCoord[1];

					//
					// Index
					//
					pAsset->m_pIndex[0]	= 0;
					pAsset->m_pIndex[1]	= 1;
					pAsset->m_pIndex[2]	= 3;
					pAsset->m_pIndex[3]	= 1;
					pAsset->m_pIndex[4]	= 2;
					pAsset->m_pIndex[5]	= 3;

					//
					// Coordinate subtile.
					//
					pAsset->m_pXYCoord[0] = (float)m_tileOffX;
					pAsset->m_pXYCoord[1] = (float)m_tileOffY;

					pAsset->m_pXYCoord[2] = (float)(m_tileOffX + m_tileWidth);
					pAsset->m_pXYCoord[3] = (float)m_tileOffY;

					pAsset->m_pXYCoord[4] = pAsset->m_pXYCoord[2];
					pAsset->m_pXYCoord[5] = (float)(m_tileOffY + m_tileHeight);

					pAsset->m_pXYCoord[6] = (float)m_tileOffX;
					pAsset->m_pXYCoord[7] = pAsset->m_pXYCoord[5];

					//
					// UV SubTile
					//
					float UPixStep	    = (this->m_pUVCoord[2] - this->m_pUVCoord[0]) / fWidth;
					float VPixStep	    = (this->m_pUVCoord[5] - this->m_pUVCoord[1]) / fHeight;
					float UStep		    = UPixStep * m_tileWidth ;
					float VStep		    = VPixStep * m_tileHeight;
					float UStepOffset   = UStep * m_tileOffX;
					float VStepOffset   = VStep * m_tileOffY;
					float U0            = this->m_pUVCoord[0] + (UStep * tX) + UStepOffset;
					float V0            = this->m_pUVCoord[1] + (VStep * tY) + VStepOffset;
					float U1            = U0 + UStep;
					float V1            = V0 + VStep;

					pAsset->m_pUVCoord[0] = U0;
					pAsset->m_pUVCoord[1] = V0;
					pAsset->m_pUVCoord[2] = U1;
					pAsset->m_pUVCoord[3] = V0;
					pAsset->m_pUVCoord[4] = U1;
					pAsset->m_pUVCoord[5] = V1;
					pAsset->m_pUVCoord[6] = U0;
					pAsset->m_pUVCoord[7] = V1;

					pAsset->m_iCenterX = 0;
					pAsset->m_iCenterY = 0;
					pAsset->m_uiVertexCount	= 4;
					pAsset->m_uiIndexCount  = 6;

					pAsset->m_imageSize.m_iTop		= 0;
					pAsset->m_imageSize.m_iBottom	= m_tileHeight;
					pAsset->m_imageSize.m_iLeft		= 0;
					pAsset->m_imageSize.m_iRight	= m_tileWidth;
					pAsset->m_boundWidth			= m_tileWidth;
					pAsset->m_boundHeight			= m_tileHeight;
				} else {
					// Delete all sub systems too.
					KLBDELETE(pAsset);
					pAsset = NULL;
				}
			}
			return pAsset;
		}
	}
	return NULL;
}

CKLBImageAsset* 
KLBTextureAssetPlugin::loadImage(u8* stream, u32 /*streamSize*/, CKLBImageAsset* pReload) 
{
	//
	// u8  Vertex Count
	// u8  Index  Count
	// u16	center X
	// u16	center Y
	//
	// Vertex Count	--> Convert into UV and XY
	// u32 X		
	// u32 Y
	// Index Count
	// u8  Index
	CKLBImageAsset* pNewAssetI = m_pReloadAsset ? pReload : KLBNEW(CKLBImageAsset);
	if (pNewAssetI) {
		// Str Length including zero and padding.
		u16 length = (stream[0] << 8) | stream[1];
		stream += 2;
	
		// Name
		// + [pad]
		if (!m_pReloadAsset)
		pNewAssetI->m_pName	= pNewAssetI->allocateName(stream, length);

		if (!pNewAssetI->m_pName) {
			return NULL;
		}
		stream += length;

		pNewAssetI->m_uiSubTileCount	= (stream[0] << 8) | stream[1];
		stream += 2;

		bool is3dModel		= false;
		bool loadUVIndex	= true;
		bool compactUV		= false;
		if (pNewAssetI->m_uiSubTileCount == 0xFFFE) {
			compactUV = true;
		}

		u32 patchCoordinateModeFromAttribute = 0;
		if (pNewAssetI->m_uiSubTileCount == 0xFFFF) {
			// Extension with attribute.

			// Attribute count.
			u32 attribCount = (stream[0]<<8) | stream[1]; stream += 2;
			pNewAssetI->m_attribCount = attribCount;
			
			if (!m_pReloadAsset)
			pNewAssetI->m_attribList = KLBNEWA(ASSET_ATTRIB,attribCount);

			if (pNewAssetI->m_attribList) {
				ASSET_ATTRIB* pAtt = pNewAssetI->m_attribList;
				for (u32 n=0; n<attribCount; n++) {
					pAtt[n].type		= ASSET_ATTRIB::zATTRIB_INT;	// Default in case of error.
				}

				for (u32 n=0; n<attribCount; n++) {
					// Key ID + Type + Value
					u8 key	= *stream++;
					u8 type = *stream++;
					pAtt[n].attribID	= key;
					pAtt[n].type		= type;

					if (key == ASSET_ATTRIB::zK0_RECT) {
						// Standard Rect
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_STANDARD_RECT;
					}
					if (key == ASSET_ATTRIB::zK1_SC_LEFT) {
						// Scroll Bar
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_SCROLLBARTYPE;
					}
					if (key == ASSET_ATTRIB::zK2_S9_LEFT) {
						// Slice 9
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_SCALE9;
					}
					if (key == ASSET_ATTRIB::z3DMODEL) {
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_3DMODEL;
						is3dModel = true;
					}
					if (key == ASSET_ATTRIB::zK5_STANDARD_USAGE) {
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_STANDARD_USAGE_5;
					}
					if (key == ASSET_ATTRIB::zK6_STANDARD_USAGE) {
						pNewAssetI->m_usageType |= CKLBImageAsset::IS_STANDARD_USAGE_6;
					}

					switch (type) {
					case ASSET_ATTRIB::zATTRIB_INT:
					case ASSET_ATTRIB::zATTRIB_FLOAT:
						pAtt[n].v.value	= (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | (stream[3]);
						if (is3dModel) {
							loadUVIndex = (pAtt[n].v.value & 0x80000000) ? true : false;
						}
						if (key == ASSET_ATTRIB::zK4_STRETCH_OR_SCALE) {
							patchCoordinateModeFromAttribute = pAtt[n].v.value;
						}
						stream += 4;
						break;
					case ASSET_ATTRIB::zATTRIB_STRING:
						{
							u8 argLen = (stream[0]<<8) | stream[1];
							stream += 2;
							if (argLen) {
								char* p	= KLBNEWA(char,argLen);
								if (p) {
									pAtt[n].v.str = p;
									memcpy(p, stream, argLen);
									stream += argLen;
								} else {
									klb_assertAlways("Memory alloc fail.");
									return NULL;
								}
							} else {
								pAtt[n].v.str	= NULL;
							}
						}
						break;
					default:
						klb_assertAlways("Invalid Image Attribute Type");
						break;
					}

					if (key == ASSET_ATTRIB::zK3_OFFSET) {
						pNewAssetI->m_renderOffset = pAtt[n].v.value;					
					}
				}
			} else {
				return NULL;
			}

			pNewAssetI->m_uiSubTileCount = (stream[0] << 8) | stream[1];
			stream += 2;
		}

		for (u32 n=0; n < pNewAssetI->m_uiSubTileCount; n++) {
			CKLBImageAsset* pNewAsset;
			if (pNewAssetI->m_uiSubTileCount == 1) {
				pNewAsset = pNewAssetI;
			} else {
				pNewAsset = KLBNEW(CKLBImageAsset);
			}

			if (!pNewAsset) {
				return NULL;
			}

			if (is3dModel) {
				pNewAsset->m_uiVertexCount  = (stream[0] << 8) | stream[1]; stream += 2;
				pNewAsset->m_uiIndexCount	= (stream[0] << 8) | stream[1]; stream += 2;
			} else {
				pNewAsset->m_uiVertexCount	= *stream++;
				pNewAsset->m_uiIndexCount	= *stream++;
			}

			pNewAsset->m_imageSize.m_iRight	 = (stream[0] << 8) | stream[1]; stream += 2;
			pNewAsset->m_imageSize.m_iBottom = (stream[0] << 8) | stream[1]; stream += 2;

			// Decide to use information from image info (tool) when standard rect AND borderless mode.
			u32 patchCoordinateMode = 0;
			if (pNewAssetI->m_usageType & CKLBImageAsset::IS_STANDARD_RECT) {
				if (CKLBDrawResource::getInstance().hasBorder() == false) {
					patchCoordinateMode = patchCoordinateModeFromAttribute;
				}
			}

			pNewAsset->m_imageSize.m_iLeft	= 0;
			pNewAsset->m_imageSize.m_iTop	= 0;

			pNewAsset->m_iCenterX	= (s16)((stream[0] << 8) | stream[1]); stream += 2;
			pNewAsset->m_iCenterY	= (s16)((stream[0] << 8) | stream[1]); stream += 2;

			float cx = pNewAsset->m_iCenterX;
			float cy = pNewAsset->m_iCenterY;

			if (m_pUVBuffer && m_pXYBuffer && m_pIndexBuffer) {
				pNewAsset->m_pUVCoord	= this->m_pUVBuffer;
				pNewAsset->m_pXYCoord	= this->m_pXYBuffer;
				pNewAsset->m_pIndex		= this->m_pIndexBuffer;
			} else {
				// find a way to get this->m_pTextureAsset if to implement.
				klb_assertAlways( "stand alone loading not implemented : should create array and implement asset destructor.");
			}

			if (pNewAsset->m_pUVCoord	&&
				pNewAsset->m_pXYCoord	&&
				pNewAsset->m_pIndex	 	&&
				this->m_pTextureAsset) {

				pNewAsset->m_pTextureAsset	= this->m_pTextureAsset;	// For Rendering.

				float minX = 9999.0f;
				float minY = 9999.0f;
				float maxX = -9999.0f;
				float maxY = -9999.0f;

				int idxVert = 0;
				int stepVert;
				if (is3dModel) {
					stepVert = 3;
				} else {
					stepVert = 2;
				}

				for (int n=0; n < pNewAsset->m_uiVertexCount; n++) {
					// XY Coordinates in screen space.
					u32 val	= (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
					float x = (val / 65536.0f) - cx;
					pNewAsset->m_pXYCoord[idxVert  ] = x;
					stream	+= 4;

					val		= (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
					float y = (val / 65536.0f) - cy;
					pNewAsset->m_pXYCoord[idxVert+1] = y;
					stream	+= 4;

					if (is3dModel) {
						val		= (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
						float z = (val / 65536.0f);
						pNewAsset->m_pXYCoord[idxVert+2] = z;
						stream	+= 4;
					} else {
						if (x < minX) { minX = x; }
						if (x > maxX) { maxX = x; }

						if (y < minY) { minY = y; }
						if (y > maxY) { maxY = y; }
					}

					idxVert	+= stepVert;

					if (loadUVIndex) {
						if (compactUV) {
							// UV Coordinates in texture
							val		= (stream[0] << 8) | (stream[1]);
							pNewAsset->m_pUVCoord[(n * 2)  ] = (val / 32768.0f);
							stream	+= 2;

							val		= (stream[0] << 8) | (stream[1]);
							pNewAsset->m_pUVCoord[(n * 2)+1] = (val / 32768.0f);
							stream	+= 2;
						} else {
							// UV Coordinates in texture
							val		= (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
							pNewAsset->m_pUVCoord[(n * 2)  ] = (val / 65536.0f);
							stream	+= 4;

							val		= (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
							pNewAsset->m_pUVCoord[(n * 2)+1] = (val / 65536.0f);
							stream	+= 4;
						}
					}
				}

				if (patchCoordinateMode == 1) {
					float deltaX = CKLBDrawResource::getInstance().borderX();
					float deltaY = CKLBDrawResource::getInstance().borderY();

					pNewAsset->m_pXYCoord[0] -= deltaX;
					pNewAsset->m_pXYCoord[1] -= deltaY;

					pNewAsset->m_pXYCoord[2] += deltaX;
					pNewAsset->m_pXYCoord[3] -= deltaY;

					pNewAsset->m_pXYCoord[4] += deltaX;
					pNewAsset->m_pXYCoord[5] += deltaY;

					pNewAsset->m_pXYCoord[6] -= deltaX;
					pNewAsset->m_pXYCoord[7] += deltaY;
				} else if (patchCoordinateMode == 2) {
					int deltaX = CKLBDrawResource::getInstance().borderX();
					int deltaY = CKLBDrawResource::getInstance().borderY();
					if (deltaX) {
						float purcX = deltaX / (float)CKLBDrawResource::getInstance().width();
						deltaY = purcX * CKLBDrawResource::getInstance().height();
					} else {
						float purcY = deltaY / (float)CKLBDrawResource::getInstance().height();
						deltaX = purcY * CKLBDrawResource::getInstance().width();
					}

					pNewAsset->m_pXYCoord[0] -= deltaX;
					pNewAsset->m_pXYCoord[1] -= deltaY;

					pNewAsset->m_pXYCoord[2] += deltaX;
					pNewAsset->m_pXYCoord[3] -= deltaY;

					pNewAsset->m_pXYCoord[4] += deltaX;
					pNewAsset->m_pXYCoord[5] += deltaY;

					pNewAsset->m_pXYCoord[6] -= deltaX;
					pNewAsset->m_pXYCoord[7] += deltaY;
				}

				if (loadUVIndex) {
					for (int n = 0; n < pNewAsset->m_uiIndexCount; n++) {
						pNewAsset->m_pIndex[n] = *stream++;
					}

					this->m_pLastLoadedUV		= pNewAsset->m_pUVCoord;
					this->m_pLastLoadedIndex	= pNewAsset->m_pIndex;
					this->m_pUVBuffer			+= (pNewAsset->m_uiVertexCount * 2);
					this->m_pIndexBuffer		+= pNewAsset->m_uiIndexCount;
				} else {
					this->m_pUVBuffer			= this->m_pLastLoadedUV;
					this->m_pIndexBuffer		= this->m_pLastLoadedIndex;
				}
				this->m_pXYBuffer			+= (pNewAsset->m_uiVertexCount * stepVert);

				pNewAsset->m_boundWidth		= maxX - minX;
				pNewAsset->m_boundHeight	= maxY - minY;
				return pNewAsset;
			}
		}
	}

	klb_assertAlways("allocation failure.");
	return NULL;
}

s32 g_gridTextureError = 0;
s32 g_gridTextureFirstError = 0;
const char* g_gridTextureDieCallback = NULL;
/**
	Trick function to allow sharing buffer for all images into texture
	and not using multiple allocation.
 */
void 
KLBTextureAssetPlugin::setBuffers(CKLBTextureAsset*	pTextureAsset, float* uvBuffer, float* xyBuffer, u16* indexBuffer) 
{
	m_pUVBuffer		= uvBuffer;
	m_pXYBuffer		= xyBuffer;
	m_pIndexBuffer	= indexBuffer;
	m_pTextureAsset	= pTextureAsset;
}

// ---------------------------------------------------------------------------------------------
//   Texture.
// ---------------------------------------------------------------------------------------------

KLBTextureAssetPlugin::KLBTextureAssetPlugin(TextureLoadCallback loadCallback)
: IKLBAssetPlugin   ()
, m_loadCallback    (loadCallback)
, m_pUVBuffer       (NULL)
, m_pXYBuffer       (NULL)
, m_pIndexBuffer    (NULL)
, m_pTextureAsset   (NULL)
, m_loadHardware    (true)
, m_loadSoftware    (false)
, m_useQuarterTexture(false)
, m_mipmapOnce(false)
, m_disableImageSizeOptimization(false)
{
}

KLBTextureAssetPlugin::~KLBTextureAssetPlugin() {
	releaseGridTexturesOnPluginShutdown();
}

void
KLBTextureAssetPlugin::setLoadingMode(E_TEXTURELOADINGMODE mode) 
{
	switch (mode) {
	case TEX_LOAD_GPU:
		m_loadHardware = true;
		m_loadSoftware = false;
		break;
	case TEX_LOAD_CPU:
		m_loadHardware = false;
		m_loadSoftware = true;
		break;
	case TEX_LOAD_GPUCPU:
		m_loadHardware = true;
		m_loadSoftware = true;
		break;
	}
}

u8* 
KLBTextureAssetPlugin::createSoftTexture(s32 width, s32 height, u32 pixelFormat, u8 channelCount, void* data) 
{
	u8* buffer = KLBNEWA(u8, width * height * 4);
	u8* retBuf = buffer;

	if (buffer) {
		// Setup default alpha
		u8 alpha = 255;

		switch (pixelFormat) {
		case GL_UNSIGNED_SHORT_5_6_5:
			{
				// GL_RGB
				u16* src = (u16*)data;
				for (int y = 0 ; y < height; y++) {
					for (int x = 0 ; x < width ; x++) {
						// RGBA
						u8 tmp = (*src & 0xF800)>>8;
						*buffer++ = tmp | (tmp >> 5);	// Full 5->8 Bit conv
						tmp = (*src & 0x07E0) >> 3;
						*buffer++ = tmp | (tmp >> 6);	// Full 6->8 Bit conv
						tmp = (*src & 0x001F) << 3;
						*buffer++ = tmp | (tmp >> 5);	// Full 5->8 Bit conv
						*buffer++ = alpha;
						src++;
					}
				}
			}
			break;
		case GL_UNSIGNED_SHORT_5_5_5_1:
			{
				u16* src = (u16*)data;
				for (int y = 0 ; y < height; y++) {
					for (int x = 0 ; x < width ; x++) {
						// RGBA
						u8 tmp = (*src & 0xF800)>>8;
						*buffer++ = tmp | (tmp >> 5);	// Full 5->8 Bit conv
						tmp = (*src & 0x07C0) >> 3;
						*buffer++ = tmp | (tmp >> 5);	// Full 5->8 Bit conv
						tmp = (*src & 0x003E) << 2;
						*buffer++ = tmp | (tmp >> 5);	// Full 5->8 Bit conv
						*buffer++ = ((s8)((*src & 1)<<7))>>7;	// Full 1->8 Bit conv
						src++;
					}
				}
			}
			break;
		case GL_UNSIGNED_SHORT_4_4_4_4:
			{
				u16* src = (u16*)data;
				for (int y = 0 ; y < height; y++) {
					for (int x = 0 ; x < width ; x++) {
						// RGBA
						u8 tmp = (*src & 0xF000)>>8;
						*buffer++ = tmp | (tmp >> 4);	// Full 4->8 Bit conv
						tmp = (*src & 0x0F00) >> 4;
						*buffer++ = tmp | (tmp >> 4);	// Full 4->8 Bit conv
						tmp = (*src & 0x00F0);
						*buffer++ = tmp | (tmp >> 4);	// Full 4->8 Bit conv
						tmp = (*src & 0x000F);
						*buffer++ = tmp | (tmp << 4);	// Full 4->8 Bit conv
						src++;
					}
				}
			}
			break;
		default:	// Byte
			switch (channelCount) {
			case 1:
			case 2:
				klb_assertAlways("Those texture mode are not supported");
				break;
			case 3:
				{
					u8* src = (u8*)data;
					for (int y = 0 ; y < height; y++) {
						for (int x = 0 ; x < width ; x++) {
							*buffer++ = *src++;
							*buffer++ = *src++;
							*buffer++ = *src++;
							*buffer++ = 255;
						}
					}
				}
				break;
			case 4:
				{
					memcpy(buffer, data, width * height * 4);
				}
				break;
			}
			break;
		}
	}

	return retBuf;
}

class TexturePayloadReader {
public:
	TexturePayloadReader() {
		m_checksumModulus = 65521;
		m_output = m_header;
		m_payload = NULL;
		m_bitPosition = 0;
		m_suppressMissingPayloadCallback = true;
		memset(m_header, 0, sizeof(m_header));
	}

	~TexturePayloadReader() {
		if (m_payload) {
			KLBDELETEA(m_payload);
		}
	}

	u8* readHeader(
		u8* bitmap,
		CKLBImageAsset* image,
		CKLBTextureAsset* texture,
		u8* channel);
	void readPayload(u8* pixel, u8 channel);

	u8* payload() const {
		return m_payload;
	}
	s32 payloadLength() const {
		return m_payloadLength;
	}
	u32 checksumModulus() const {
		return m_checksumModulus;
	}
	const u8* header() const {
		return m_header;
	}
	bool suppressMissingPayloadCallback() const {
		return m_suppressMissingPayloadCallback;
	}

private:
	void consumeBit(u8* pixel) {
		const u8 encoded = *pixel;
		const u32 bit = encoded & 1;
		m_output[m_bitPosition >> 3] |= bit << (m_bitPosition & 7);
		m_bitPosition++;
		*pixel = (encoded & 0xFE) | (encoded >> 7);
	}

	u32 m_checksumModulus;
	s32 m_bitPosition;
	s32 m_payloadLength;
	s32 m_pixelColumn;
	s32 m_imageWidth;
	s32 m_rowAdvance;
	bool m_suppressMissingPayloadCallback;
	u8* m_payload;
	u8* m_output;
	u8  m_header[10];
};

bool decodeTexturePayload(
	u8* bitmap,
	CKLBImageAsset* image,
	CKLBTextureAsset* texture,
	KLBTextureAssetPlugin::TextureLoadCallback callback);

void
processImage8888(u32 /*pixelCount*/, u32 lineWidth, u32 height, u8* buffer)
{
	u32* line1 = (u32*)buffer;
	u32* line2 = &(((u32*)buffer)[lineWidth]);
	u8* dst = buffer;
	for (u32 y = 0; y < (height >> 1); y++) {
		u8* pix1 = (u8*)line1;
		u8* pix2 = (u8*)line2;
		for (u32 x=0; x < (lineWidth >> 1); x++) {
			u32 sum;
			sum = (pix1[0] + pix1[4] + pix2[0] + pix2[4])>>2;
			*dst++ = sum;
			sum = (pix1[1] + pix1[5] + pix2[1] + pix2[5])>>2;
			*dst++ = sum;
			sum = (pix1[2] + pix1[6] + pix2[2] + pix2[6])>>2;
			*dst++ = sum;
			sum = (pix1[3] + pix1[7] + pix2[3] + pix2[7])>>2;
			*dst++ = sum;

			pix1 += 8;
			pix2 += 8;
		}
		// Skip 2 lines.
		line1 += lineWidth << 1;
		line2 += lineWidth << 1;
	}	
}

void 
processImage565(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer) 
{
	u16* pSrc	= (u16*)buffer;
	u32* pDst	= (u32*)pSrc;
	u16* pDst16	= pSrc;

	//
	// Horizontal pass.
	//
#define R_B		(0xF81F)
#define _G_		(0x07E0)
#define SHIFTNEXT	(16)
	for (u32 n = 0; n < (pixelCount>>1); n++) {
		u32 pix1	= *pSrc++;
		u32 pix2	= *pSrc++;
		*pDst++ = (((pix1 & _G_) << SHIFTNEXT) | (pix1 & R_B)) + (((pix2 & _G_) << SHIFTNEXT) | (pix2 & R_B));
	}

	//
	// Vertical Pass + Mixing into correct.
	//
	u32* line1 = (u32*)buffer;
	u32* line2 = &(((u32*)buffer)[lineWidth>>1]);
	pDst16 = (u16*)buffer;
	for (u32 y = 0; y < (height >> 1); y++) {
		for (u32 x=0; x < (lineWidth >> 1); x++) {
			u32 pix		= (((*line1++) + (*line2++))) >> 2;
			*pDst16++	= ((pix & (_G_ << SHIFTNEXT)) >> SHIFTNEXT) | (pix & R_B);
		}
		// Skip 2 lines.
		line1 += lineWidth>>1;
		line2 += lineWidth>>1;
	}
#undef	R_B
#undef  _G_
#undef SHIFTNEXT
}

void 
processImage4444(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer) 
{
	// 8 Bit
	u16* pSrc	= (u16*)buffer;
	u32* pDst	= (u32*)pSrc;
	u16* pDst16	= pSrc;

	//
	// Horizontal pass.
	//
#define _R_B	(0x0F0F)
#define A_G_	(0xF0F0)
#define SHIFTNEXT	(16 - 2)
	for (u32 n = 0; n < (pixelCount>>1); n++) {
		u32 pix1	= *pSrc++;
		u32 pix2	= *pSrc++;
		*pDst++ = (((pix1 & A_G_) << SHIFTNEXT) | (pix1 & _R_B)) + (((pix2 & A_G_) << SHIFTNEXT) | (pix2 & _R_B));
	}

	//
	// Vertical Pass + Mixing into correct.
	//
	u32* line1 = (u32*)buffer;
	u32* line2 = &(((u32*)buffer)[lineWidth>>1]);
	pDst16 = (u16*)buffer;
	for (u32 y = 0; y < (height >> 1); y++) {
		for (u32 x=0; x < (lineWidth >> 1); x++) {
			u32 pix		= (((*line1++) + (*line2++))) >> 2;
			*pDst16++	= ((pix & (A_G_ << SHIFTNEXT)) >> SHIFTNEXT) | (pix & _R_B);
		}
		// Skip 2 lines.
		line1 += lineWidth>>1;
		line2 += lineWidth>>1;
	}
#undef	_R_B
#undef  A_G_
#undef  SHIFTNEXT
}

void 
processImage5551(u32 pixelCount, u32 lineWidth, u32 height, u8* buffer) 
{
	// 8 Bit
	u16* pSrc	= (u16*)buffer;
	u32* pDst	= (u32*)pSrc;
	u16* pDst16	= pSrc;

	//
	// Horizontal pass.
	//
	// rrrrr ggggg bbbbb a
	// 00000 11111 00000 1
	// 0000.0111.1100.0001	0x07C1

	// 11111 00000 11111 0
	// 1111.1000.0011.1110

	//	1111.1000.0011.1110|0000.0111.1100.0001

#define _R_B	(0x07C1)
#define A_G_	(0xF83E)
#define SHIFTNEXT	(16 - 2)
	for (u32 n = 0; n < (pixelCount>>1); n++) {
		u32 pix1	= *pSrc++;
		u32 pix2	= *pSrc++;
		*pDst++ = (((pix1 & A_G_) << SHIFTNEXT) | (pix1 & _R_B)) + (((pix2 & A_G_) << SHIFTNEXT) | (pix2 & _R_B));
	}

	//
	// Vertical Pass + Mixing into correct.
	//
	u32* line1 = (u32*)buffer;
	u32* line2 = &(((u32*)buffer)[lineWidth>>1]);
	pDst16 = (u16*)buffer;
	for (u32 y = 0; y < (height >> 1); y++) {
		for (u32 x=0; x < (lineWidth >> 1); x++) {
			u32 pix		= (((*line1++) + (*line2++))) >> 2;
			*pDst16++	= ((pix & (A_G_ << SHIFTNEXT)) >> SHIFTNEXT) | (pix & _R_B);
		}
		// Skip 2 lines.
		line1 += lineWidth>>1;
		line2 += lineWidth>>1;
	}
#undef	_R_B
#undef  A_G_
#undef  SHIFTNEXT
}

// Background-filter parameters are quantized once at script setup so texture
// loading can apply them without repeating float conversions.
s32 g_backgroundFilterOffsetX = 0;
s32 g_backgroundFilterOffsetY = 0;
s32 g_backgroundFilterExtent = 0;
s32 g_backgroundFilterOpacity = 0;
u32 g_backgroundFilterRed = 0;
u32 g_backgroundFilterGreen = 0;
u32 g_backgroundFilterBlue = 0;

namespace TextureBackgroundFilter {
	static const u32 BACKGROUND_FILTER_WIDTH = 640;
	static const u32 BACKGROUND_FILTER_SAMPLES = BACKGROUND_FILTER_WIDTH + 2;

	enum BackgroundFilterChannel {
		FILTER_RED,
		FILTER_GREEN,
		FILTER_BLUE,
		FILTER_CHANNEL_COUNT
	};

	struct BackgroundFilterPixel {
		u32 channel[FILTER_CHANNEL_COUNT];
	};

	struct BackgroundFilterLine {
		BackgroundFilterPixel samples[2][BACKGROUND_FILTER_SAMPLES];
		u32 pass;
		u32 opacity;
	};

	void filterLine(BackgroundFilterLine* line)
	{
		BackgroundFilterPixel* first = line->samples[0];
		BackgroundFilterPixel* second = line->samples[1];
		BackgroundFilterPixel* source = (line->pass & 1) ? second : first;
		BackgroundFilterPixel* destination = (line->pass & 1) ? first : second;
		const u32 centerDivisor = line->opacity * 2 + 0x100;
		const u32 edgeDivisor = line->opacity + 0x100;

		destination[0].channel[FILTER_RED] =
			(source[0].channel[FILTER_RED] * 0x100 +
			 source[1].channel[FILTER_RED] * line->opacity) / edgeDivisor;
		destination[0].channel[FILTER_GREEN] =
			(source[0].channel[FILTER_GREEN] * 0x100 +
			 source[1].channel[FILTER_GREEN] * line->opacity) / edgeDivisor;
		destination[0].channel[FILTER_BLUE] =
			(source[0].channel[FILTER_BLUE] * 0x100 +
			 source[1].channel[FILTER_BLUE] * line->opacity) / edgeDivisor;

		u32 weightedRed = source[1].channel[FILTER_RED] * 0x100;
		for (u32 x = 0; x < BACKGROUND_FILTER_WIDTH; x++) {
			u32 filtered = source[x + 2].channel[FILTER_RED];
			filtered += source[x].channel[FILTER_RED];
			filtered *= line->opacity;
			filtered += weightedRed;
			destination[x + 1].channel[FILTER_RED] =
				filtered / centerDivisor;

			filtered = source[x + 2].channel[FILTER_GREEN];
			const u32 weightedGreen =
				source[x + 1].channel[FILTER_GREEN] * 0x100;
			filtered += source[x].channel[FILTER_GREEN];
			filtered *= line->opacity;
			filtered += weightedGreen;
			destination[x + 1].channel[FILTER_GREEN] =
				filtered / centerDivisor;

			filtered = source[x + 2].channel[FILTER_BLUE];
			const u32 weightedBlue =
				source[x + 1].channel[FILTER_BLUE] * 0x100;
			filtered += source[x].channel[FILTER_BLUE];
			filtered *= line->opacity;
			filtered += weightedBlue;
			destination[x + 1].channel[FILTER_BLUE] =
				filtered / centerDivisor;
			weightedRed = source[x + 2].channel[FILTER_RED] * 0x100;
		}

		destination[BACKGROUND_FILTER_WIDTH + 1].channel[FILTER_RED] =
			(weightedRed +
			 source[BACKGROUND_FILTER_WIDTH].channel[FILTER_RED] * line->opacity) / edgeDivisor;
		destination[BACKGROUND_FILTER_WIDTH + 1].channel[FILTER_GREEN] =
			(source[BACKGROUND_FILTER_WIDTH + 1].channel[FILTER_GREEN] * 0x100 +
			 source[BACKGROUND_FILTER_WIDTH].channel[FILTER_GREEN] * line->opacity) / edgeDivisor;
		destination[BACKGROUND_FILTER_WIDTH + 1].channel[FILTER_BLUE] =
			(source[BACKGROUND_FILTER_WIDTH + 1].channel[FILTER_BLUE] * 0x100 +
			 source[BACKGROUND_FILTER_WIDTH].channel[FILTER_BLUE] * line->opacity) / edgeDivisor;

		line->pass++;
	}

	inline void expandImageBounds(float* xy, float expandX, float expandY)
	{
		xy[0] -= expandX;
		xy[1] -= expandY;
		xy[2] += expandX;
		xy[3] -= expandY;
		xy[4] += expandX;
		xy[5] += expandY;
		xy[6] -= expandX;
		xy[7] += expandY;
	}

	void apply(CKLBTextureAsset* texture, u32 pixelFormat)
	{
		CKLBImageAsset* image = texture->m_pImages[0];

		if (g_backgroundFilterOffsetX) {
			const float offset = (float)g_backgroundFilterOffsetX / 1000.0f;
			float expandX = CKLBDrawResource::getInstance().borderX() * offset;
			float expandY = CKLBDrawResource::getInstance().borderY() * offset;
			if (expandX > 0.0f) {
				const float ratio =
					expandX / (float)CKLBDrawResource::getInstance().width();
				expandY = ratio * CKLBDrawResource::getInstance().height();
			} else {
				const float ratio =
					expandY / (float)CKLBDrawResource::getInstance().height();
				expandX = ratio * CKLBDrawResource::getInstance().width();
			}
			expandImageBounds(image->m_pXYCoord, expandX, expandY);
		}

		if (g_backgroundFilterOffsetY) {
			const float offset = (float)g_backgroundFilterOffsetY / 1000.0f;
			const float expandX = CKLBDrawResource::getInstance().borderX() * offset;
			const float expandY = CKLBDrawResource::getInstance().borderY() * offset;
			expandImageBounds(image->m_pXYCoord, expandX, expandY);
		}

		if (!g_backgroundFilterExtent) {
			return;
		}

		BackgroundFilterLine* line = KLBNEW(BackgroundFilterLine);
		memset(line, 0, sizeof(BackgroundFilterLine));

		image->m_usageType &= ~CKLBImageAsset::IS_STANDARD_RECT;
		image->m_uiIndexCount += 12;
		image->m_uiVertexCount += 8;

		const float inverseWidth = 1.0f / texture->m_width;
		const float inverseHeight = 1.0f / texture->m_height;
		const float extent =
			CKLBDrawResource::getInstance().borderX()
			* ((float)g_backgroundFilterExtent / 1000.0f);
		float* uv = image->m_pUVCoord;

		const float firstLeft = 3.0f * inverseWidth;
		const float firstTop = 908.0f * inverseHeight;
		const float firstRight = 642.0f * inverseWidth;
		const float firstBottom = 811.0f * inverseHeight;
		uv[8] = firstLeft;
		uv[9] = firstTop;
		uv[10] = firstLeft;
		uv[11] = firstBottom;
		uv[12] = firstRight;
		uv[13] = firstTop;
		uv[14] = firstRight;
		uv[15] = firstBottom;

		float* xy = image->m_pXYCoord;
		xy[10] = xy[0];
		xy[11] = xy[1];
		xy[8] = xy[0] - extent;
		xy[9] = xy[1];
		xy[14] = xy[6];
		xy[15] = xy[7];
		xy[12] = xy[6] - extent;
		xy[13] = xy[7];

		// The second strip samples the lower source band and reuses each edge.
		u16* indices = image->m_pIndex;
		uv[16] = firstLeft;
		uv[17] = 921.0f * inverseHeight;
		uv[18] = firstLeft;
		uv[19] = 1018.0f * inverseHeight;
		uv[20] = firstRight;
		uv[21] = uv[17];
		uv[22] = uv[20];
		uv[23] = uv[19];

		xy[16] = xy[2];
		xy[17] = xy[3];
		xy[18] = xy[2] + extent;
		xy[19] = xy[3];
		xy[20] = xy[4];
		xy[21] = xy[5];
		xy[22] = xy[4] + extent;
		xy[23] = xy[5];

		indices[6]  = 4; indices[7]  = 5; indices[8]  = 6;
		indices[9]  = 5; indices[10] = 7; indices[11] = 6;
		indices[12] = 8; indices[13] = 9; indices[14] = 10;
		indices[15] = 9; indices[16] = 11; indices[17] = 10;

		line->opacity = g_backgroundFilterOpacity;
		if (pixelFormat != GL_UNSIGNED_SHORT_4_4_4_4) {
			if (pixelFormat == GL_UNSIGNED_SHORT_5_5_5_1) {
				klb_assertAlways("NOT IMPLEMENTED");
			}
		} else {
			klb_assertAlways("NOT IMPLEMENTED");
		}

		const u32 rowStride = texture->m_width * texture->m_bytePerPix;
		const s32 sourceX = (s32)(texture->m_width * uv[0]);
		const s32 sourceY = (s32)(texture->m_height * uv[1]);
		u32 red, green, blue;

		for (s32 side = 0; side < 2; side++) {
			const s32 edgeX = sourceX + (side ? 0 : 959);
			u8* source = (u8*)texture->m_bitmap +
				sourceY * rowStride + edgeX * texture->m_bytePerPix + 2;

			BackgroundFilterPixel* pixel = &line->samples[0][1];
			for (s32 sampleY = sourceY; sampleY < sourceY + (s32)BACKGROUND_FILTER_WIDTH; sampleY++) {
				if (pixelFormat != GL_UNSIGNED_BYTE) {
					if (pixelFormat == GL_UNSIGNED_SHORT_5_6_5) {
						const u16 packed = *(u16*)(source - 2);
						red =
							((packed >> 8) & 0xf8) | (packed >> 13);
						green =
							((packed >> 3) & 0xfc) | ((packed >> 9) & 3);
						blue =
							((packed & 0x1f) << 3) | ((packed >> 2) & 7);
					}
				} else {
					red = source[-2];
					green = source[-1];
					blue = source[0];
				}
				pixel->channel[FILTER_RED] = red; pixel->channel[FILTER_GREEN] = green;
				pixel->channel[FILTER_BLUE] = blue; pixel++;
				source += rowStride;
			}

			for (u32 channel = 0; channel < FILTER_CHANNEL_COUNT; channel++) {
				line->samples[0][0].channel[channel] = line->samples[0][1].channel[channel];
				line->samples[0][BACKGROUND_FILTER_WIDTH + 1].channel[channel] = line->samples[0][BACKGROUND_FILTER_WIDTH].channel[channel];
			}

			const u32 destinationY = side ? 810 : 920;
			for (s32 row = 0; row < 100; row++) {
				const s32 destinationOffset = (destinationY + row) * rowStride + 2 * texture->m_bytePerPix;
				u8* destination = (u8*)texture->m_bitmap + destinationOffset;
				filterLine(line);
				BackgroundFilterPixel* filtered =
					(line->pass & 1) ? line->samples[1] : line->samples[0];
				const s32 blend = (row << 8) / 100;
				const s32 retained = 0x100 - blend;
				const s32 blendRed = g_backgroundFilterRed * blend;
				const s32 blendGreen = g_backgroundFilterGreen * blend;
				const s32 blendBlue = g_backgroundFilterBlue * blend;

				for (s32 x = 0; x < (s32)BACKGROUND_FILTER_SAMPLES; x++, filtered++) {
					s32 red = ((s32)filtered->channel[FILTER_RED] * retained + blendRed) >> 8;
					s32 green = ((s32)filtered->channel[FILTER_GREEN] * retained + blendGreen) >> 8;
					s32 blue =
						((s32)filtered->channel[FILTER_BLUE] * retained + blendBlue) >> 8;
					if (red > 255) red = 255;
					if (green > 255) green = 255;
					if (blue > 255) blue = 255;
					if (red < 0) red = 0;
					if (green < 0) green = 0;
					if (blue < 0) blue = 0;

					if (pixelFormat != GL_UNSIGNED_BYTE) {
						if (pixelFormat == GL_UNSIGNED_SHORT_5_6_5) {
							const u16 packedRed = (u16)((red & 0xf8) << 8), packedGreen = (u16)((green & 0x1ffc) << 3), packedBlue = (u16)(blue >> 3);
							*(u16*)destination = (u16)(packedRed | packedGreen | packedBlue);
							destination += 2;
						}
					} else {
						destination[-2] = (u8)red;
						destination[-1] = (u8)green;
						destination[0] = (u8)blue;
						if (texture->m_bytePerPix == 4) {
							destination[1] = 0xff;
						}
						destination += texture->m_bytePerPix;
					}
				}
			}
		}

		KLBDELETE(line);
	}
}

void
setupTextureBackgroundFilter(
	float offsetX,
	float offsetY,
	float extent,
	float opacity,
	u32 red,
	u32 green,
	u32 blue)
{
	g_backgroundFilterOffsetX = (s32)(offsetX * 1000.0f);
	g_backgroundFilterOffsetY = (s32)(offsetY * 1000.0f);
	g_backgroundFilterExtent = (s32)(extent * 1000.0f);
	g_backgroundFilterOpacity = (s32)(opacity * 255.0f);
	g_backgroundFilterRed = red;
	g_backgroundFilterGreen = green;
	g_backgroundFilterBlue = blue;
}

/*virtual*/
CKLBAbstractAsset* 
KLBTextureAssetPlugin::loadAsset(u8* stream, size_t streamSize) 
{
	CKLBTextureAsset* pNewAsset = m_pReloadAsset ? ((CKLBTextureAsset*)m_pReloadAsset) : KLBNEW(CKLBTextureAsset);

	if (pNewAsset) {
		u8* streamStart = stream;

		// Str Length including zero and padding.
		u16 length = (stream[0] << 8) | stream[1];
		stream += 2;
	
		if (!m_pReloadAsset)
		pNewAsset->m_fileSource = CKLBUtility::copyString(this->m_currentFile);

		// + [pad]
		if (!m_pReloadAsset)
		pNewAsset->m_pName		= pNewAsset->allocateName(stream, length);
		stream += length;

		//
		// Width		2 byte
		// Height		2 byte
		// Type			2 byte
		// totalVertexCount 2/6 byte
		// totalIndexCount	2/6 byte
		// ImageCount	2 byte
		//

		pNewAsset->m_width				= (stream[0]<<8) | stream[1]; stream += 2;
		pNewAsset->m_height				= (stream[0]<<8) | stream[1]; stream += 2;
		pNewAsset->m_type				= (stream[0]<<8) | stream[1]; stream += 2;
		pNewAsset->m_totalVertexCount	= (stream[0]<<8) | stream[1]; stream += 2;

		if (pNewAsset->m_totalVertexCount == 0xFFFF) {
			pNewAsset->m_totalVertexCount	= (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3]; stream += 4;
		}
		pNewAsset->m_totalIndexCount	= (stream[0]<<8) | stream[1]; stream += 2;
		if (pNewAsset->m_totalIndexCount == 0xFFFF) {
			pNewAsset->m_totalIndexCount	= (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3]; stream += 4;
		}
		pNewAsset->m_imageCount			= (stream[0]<<8) | stream[1]; stream += 2;

		int uvOffset;
		if (pNewAsset->m_type & 0x8000) {
			uvOffset = (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3]; stream += 4;
		} else {
			uvOffset = pNewAsset->m_totalVertexCount * 2;
			pNewAsset->m_totalVertexCount *= 4;
		}

		// The canonical single-quad layout can be expanded into two additional
		// screen variants after the image record has been decoded.
		const bool hasScreenVariants =
			(pNewAsset->m_totalVertexCount == 16) &&
			(pNewAsset->m_totalIndexCount == 6) &&
			(pNewAsset->m_imageCount == 1);

		if (!m_pReloadAsset) {
		pNewAsset->m_floatBufferTotal	= KLBNEWA(float				,pNewAsset->m_totalVertexCount + (hasScreenVariants ? 32 : 0)	);
		pNewAsset->m_indexBufferTotal	= KLBNEWA(u16				,pNewAsset->m_totalIndexCount + (hasScreenVariants ? 12 : 0)	);	// UV and X,Y
		pNewAsset->m_pImages			= KLBNEWA(CKLBImageAsset*	,pNewAsset->m_imageCount + (hasScreenVariants ? 2 : 0)		);
		}

		if (hasScreenVariants) {
			uvOffset += 16;
		}

		if (pNewAsset->m_floatBufferTotal && pNewAsset->m_indexBufferTotal && pNewAsset->m_pImages && pNewAsset->m_pName) {
			//
			// Trick here : texture loader directly embed the image loading process
			// and allocate the buffers for the image objects.
			// --> Special API are added to the Image plugin to do so.
			//
			
			setBuffers(
				pNewAsset,
				pNewAsset->m_floatBufferTotal,										// UV Storage
				&pNewAsset->m_floatBufferTotal[uvOffset],	// XY Storage
				pNewAsset->m_indexBufferTotal);

			for (u16 n = 0; n < pNewAsset->m_imageCount; n++) {
				//
				// Stream size
				//
				stream += 4;	// Skip [TIMG]/[3DM_]
				u32 size = (stream[0]<<8) | stream[1]; stream += 2;
				if (size == 0xFFFF) {
					size = (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3]; stream += 4;
				}
		
				CKLBImageAsset* pImg = loadImage(stream, size, pNewAsset->m_pImages[n]);
				pNewAsset->m_pImages[n] = pImg;

				if (pNewAsset->m_pImages[n] == NULL) {
					return NULL;
				}
				stream += size;
			}

			// + [pad] : 4 byte aligned texture.
			// Default for uncompressed stream.
			pNewAsset->m_bitmap				= &stream[0];
			
			// Texture size is all the data remaining (FullDataSize - AlreadyReadDataSize )
			u32 textureSize = streamSize - (stream - streamStart);
			bool hardCompression = false;
			bool ownsBitmap = false;

			GLenum pixelFormat;
			CKLBOGLWrapper::TEX_CHANNEL channelCount;

			// 0 ALPHA | 1 LUMA | 2 LUMALPHA | 3 RGB | 4 RGBA	// Bit 0..2
			// COMPRESS (3) | DOUBLE_BUFF (5) | MIPMAP (4)		// Bit 3,4,5
			// 0 565 | 1 5551 | 2 4444 | 3 8					// Bit 6..7

			switch (pNewAsset->m_type & 0x7) {
			case 0:
				channelCount = CKLBOGLWrapper::ALPHA;
				break;
			case 1:
				channelCount = CKLBOGLWrapper::LUMINANCE;
				break;
			case 2:
				channelCount = CKLBOGLWrapper::LUMINANCE_ALPHA;
				break;
			case 3:
				channelCount = CKLBOGLWrapper::RGB;
				break;
			case 4:
			default:
				channelCount = CKLBOGLWrapper::RGBA;
				break;
			}

			int bytePerPix;

			switch ((pNewAsset->m_type>>6) & 0x3) {
			case 0:
				pixelFormat = GL_UNSIGNED_SHORT_5_6_5;
				bytePerPix	= 2;
				break;
			case 1:
				pixelFormat = GL_UNSIGNED_SHORT_5_5_5_1;
				bytePerPix	= 2;
				break;
			case 2:
				pixelFormat = GL_UNSIGNED_SHORT_4_4_4_4;
				bytePerPix	= 2;
				break;
			case 3:
			default:	// Avoid warning.
				pixelFormat = GL_UNSIGNED_BYTE;
				bytePerPix	= 1 * (channelCount ? channelCount : 1); // Luminance is 1 byte.
				break;
			}

			u32 opt = CKLBOGLWrapper::TEX_NONE;
			u32 compressType = 0;
			if (pNewAsset->m_type & (1<<3)) {
				compressType = (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3]; stream += 4;
				const bool zlibWrapped = (compressType & 0x80000000) != 0;
				u32 outputSize = bytePerPix * pNewAsset->m_width * pNewAsset->m_height;
				if (compressType) {
					if (zlibWrapped) {
						outputSize = (stream[0]<<24) | (stream[1]<<16) | (stream[2]<<8) | stream[3];
					}
					stream += 4;
					textureSize -= 8;
				} else {
					textureSize -= 4;
				}
				pNewAsset->m_bitmap = &stream[0];

				// Extension to be supported, read 4 more byte to find out.
				if ((compressType == 0) || zlibWrapped) {
					/* textureSize = zlib stream */
					pNewAsset->m_bitmap				= KLBNEWA(u8, outputSize);

					if (pNewAsset->m_bitmap) {
						u8* in = &stream[0];
						// textureSize = compressed stream Size.
						int ret;
						z_stream strm;

						/* allocate deflate state */
						strm.zalloc     = Z_NULL;
						strm.zfree      = Z_NULL;
						strm.opaque     = Z_NULL;
						strm.avail_in   = 0;
						strm.next_in    = Z_NULL;
						ret = inflateInit(&strm);
						if (ret != Z_OK) {
							KLBDELETEA((u8*)pNewAsset->m_bitmap);
							pNewAsset->m_bitmap = NULL;
						} else { ownsBitmap = true; }

						// Number of byte available in the stream for decompression.
						strm.avail_in = textureSize; 
						// Input stream setup
						strm.next_in  = in;

						// Number of byte available in the output buffer for decompression.
						strm.avail_out = outputSize;
						// Target Buffer for decompression.
						strm.next_out  = (u8*)pNewAsset->m_bitmap;

						//---------------------------------
						// Decompress me !
						ret = inflate(&strm, Z_NO_FLUSH);
						if ((ret != Z_OK) && (ret != Z_STREAM_END)) {
							KLBDELETEA((u8*)pNewAsset->m_bitmap);
							pNewAsset->m_bitmap = NULL;
							ownsBitmap = false;
						}
						//---------------------------------
					
						// Job complete, end.
						// do not care about end result 
						// because all the free possible have been made.
						ret = inflateEnd(&strm);
					}
				}

				compressType &= 0x7FFFFFFF;
				if (compressType != 0) {
					//
					// Support for later extension PVRTC, ETC1, ETC2, ...
					//
					hardCompression = true;
					opt |= CKLBOGLWrapper::TEX_OPT_COMPRESSED_BIT;
					// pixelFormat	setup
					// channelCount	setup
					// Stream shifted by 4 byte (compress type)

					char *exts = (char *)glGetString(GL_EXTENSIONS);

					// Prefer to put the strcmp AFTER the equality check to avoid useless string compare.
					//
					// === GL_IMG_texture_compression_pvrtc group ===
					//
					#ifdef GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
					if ((compressType == GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc")) {
						pixelFormat = GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					#ifdef GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
					if ((compressType == GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc")) {
						pixelFormat = GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					#ifdef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
					if ((compressType == GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc")) {
						pixelFormat = GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					#ifdef GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG
					if ((compressType == GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc")) {
						pixelFormat = GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					//
					// === GL_IMG_texture_compression_pvrtc2 group ===
					//
					#ifdef GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG
					if ((compressType == GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc2")) {
						pixelFormat = GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					#ifdef GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG
					if ((compressType == GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG) && strstr(exts, "GL_IMG_texture_compression_pvrtc2")) {
						pixelFormat = GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					//
					// === GL_OES_compressed_ETC1_RGB8_texture group ===
					//
					#ifdef GL_ETC1_RGB8_OES
					if ((compressType == GL_ETC1_RGB8_OES) && strstr(exts, "GL_OES_compressed_ETC1_RGB8_texture")) {
						pixelFormat = GL_ETC1_RGB8_OES;
						channelCount = CKLBOGLWrapper::RGBA;
					} else
					#endif
					//
					// === ETC2 is mandatory and there is not EXTENSIONS ===
					//
					#ifdef GL_COMPRESSED_RGB8_ETC2
					if (compressType == GL_COMPRESSED_RGB8_ETC2) {
						// OK.
					} else
					#endif
					#ifdef GL_COMPRESSED_RGBA8_ETC2_EAC
					if (compressType == GL_COMPRESSED_RGBA8_ETC2_EAC) {
						// OK.
					} else
					#endif

					if (true) {
						if (compressType == 0x8D64 /*GL_ETC1_RGB8_OES*/) {
							//
							// SW decoder called.
							//

							// Reset HW compression bits
							hardCompression = false;
							compressType = 0;
							
							pixelFormat = GL_UNSIGNED_BYTE;
							bytePerPix  = 4;

							opt &= ~CKLBOGLWrapper::TEX_OPT_COMPRESSED_BIT;
							u32 outputSize = bytePerPix * pNewAsset->m_width * pNewAsset->m_height;
							u8* compressedBitmap = (u8*)pNewAsset->m_bitmap;
							u8* decodedBitmap = KLBNEWA(u8, outputSize);

							if (decodedBitmap) {

								/* From Khronos Specs
									First block in mem  Second block in mem
									 ---- ---- ---- ---- .... .... .... ....  --> u direction
									|a1  |e1  |i1  |m1  |a2  :e2  :i2  :m2  :
									|    |    |    |    |    :    :    :    : 
									 ---- ---- ---- ---- .... .... .... ....
									|b1  |f1  |j1  |n1  |b2  :f2  :j2  :n2  : 
									|    |    |    |    |    :    :    :    : 
									 ---- ---- ---- ---- .... .... .... ....
									|c1  |g1  |k1  |o1  |c2  :g2  :k2  :o2  : 
									|    |    |    |    |    :    :    :    : 
									 ---- ---- ---- ---- .... .... .... ....
									|d1  |h1  |l1  |p1  |d2  :h2  :l2  :p2  : 
									|    |    |    |    |    :    :    :    : 
									 ---- ---- ---- ---- ---- ---- ---- ---- 
									:a3  :e3  :i3  :m3  |a4  |e4  |i4  |m4  |
									:    :    :    :    |    |    |    |    |
									 .... .... .... .... ---- ---- ---- ---- 
									:b3  :f3  :j3  :n3  |b4  |f4  |j4  |n4  |
									:    :    :    :    |    |    |    |    |
									 .... .... .... .... ---- ---- ---- ---- 
									:c3  :g3  :k3  :o3  |c4  |g4  |k4  |o4  |
									:    :    :    :    |    |    |    |    |
									 .... .... .... .... ---- ---- ---- ---- 
									:d3  :h3  :l3  :p3  |d4  |h4  |l4  |p4  |
									:    :    :    :    |    |    |    |    |
									 .... .... .... .... ---- ---- ---- ---- 
									| Third block in mem  Fourth block in mem
									v
									v direction

									Add figure 3.9.1: Pixel layout for a ETC1 compressed block:

									 ---- ---- ---- ---- 
									|a   |e   |i   |m   |
									|    |    |    |    |
									 ---- ---- ---- ---- 
									|b   |f   |j   |n   |
									|    |    |    |    |
									 ---- ---- ---- ---- 
									|c   |g   |k   |o   |
									|    |    |    |    |
									 ---- ---- ---- ---- 
									|d   |h   |l   |p   |
									|    |    |    |    |
									 ---- ---- ---- ---- 
								*/

								// Horizontal block first then vertical lines
								u8* pSrcStream = compressedBitmap;
								u8* pDstStream = decodedBitmap;
								u32 rgbaOut[16];
								for (int y=0; y < pNewAsset->m_height>>2; y++) {
									u32* writePix = (u32*)pDstStream; 
									for (int x=0; x < pNewAsset->m_width>>2; x++) {
										rg_etc1::unpack_etc1_block(pSrcStream, (u32*)rgbaOut,false);
										pSrcStream += 8; // Next 64 bit chunk.

										writePix[0] = rgbaOut[0];
										writePix[1] = rgbaOut[1];
										writePix[2] = rgbaOut[2];
										writePix[3] = rgbaOut[3];

										writePix += pNewAsset->m_width;

										writePix[0] = rgbaOut[4];
										writePix[1] = rgbaOut[5];
										writePix[2] = rgbaOut[6];
										writePix[3] = rgbaOut[7];

										writePix += pNewAsset->m_width;

										writePix[0] = rgbaOut[8];
										writePix[1] = rgbaOut[9];
										writePix[2] = rgbaOut[10];
										writePix[3] = rgbaOut[11];
										writePix += pNewAsset->m_width;

										writePix[0] = rgbaOut[12];
										writePix[1] = rgbaOut[13];
										writePix[2] = rgbaOut[14];
										writePix[3] = rgbaOut[15];

										writePix -= pNewAsset->m_width * 3; // Rollback at top
										writePix += 4;						// Next block on the line
									}
									pDstStream += 4 * 4 * pNewAsset->m_width; // RGBA * Width * 4 pixel height
								}
							}
							if (ownsBitmap) {
								KLBDELETEA(compressedBitmap);
							}
							pNewAsset->m_bitmap = decodedBitmap;
							ownsBitmap = true;
						} else {
							klb_assertAlways("COMPRESSED TEXTURE FORMAT %8X NOT SUPPORTED ON THIS PLATFORM",compressType);
						}
					}

					if (compressType != 0) {
						pixelFormat = compressType;
						pNewAsset->m_bitmap				= &stream[0];
					}
				}
			}

			if (pNewAsset->m_type & (1<<4)) {
				opt |= CKLBOGLWrapper::TEX_OPT_MIPMAP_BIT;
			}
			// m_mipmapOnce is consulted (and cleared) separately, at texture-creation time.

			if (pNewAsset->m_type & (1<<5)) {
				opt |= CKLBOGLWrapper::TEX_OPT_DOUBLEBUFFERED_BIT;
			}

			CKLBOGLWrapper& pMgr = CKLBOGLWrapper::getInstance();

			if (pNewAsset->m_bitmap) {
				//
				// Texture creation may fail, but asset is considered as loaded
				//
				if ((bytePerPix == 4) && (pNewAsset->m_imageCount == 1)) {
					decodeTexturePayload(
						(u8*)pNewAsset->m_bitmap,
						pNewAsset->m_pImages[0],
						pNewAsset,
						m_loadCallback);
				}

				bool lowRes = ((CPFInterface::getInstance().client().getPhysicalScreenHeight() < 480) || m_useQuarterTexture)
					&& !hardCompression;
				if (lowRes) {
					switch (pixelFormat) {
					case GL_UNSIGNED_SHORT_5_6_5:
						processImage565(
							pNewAsset->m_width * pNewAsset->m_height,
							pNewAsset->m_width,
							pNewAsset->m_height,
							(u8*)pNewAsset->m_bitmap);
						break;
					case GL_UNSIGNED_SHORT_5_5_5_1:
						processImage5551(
							pNewAsset->m_width * pNewAsset->m_height,
							pNewAsset->m_width,
							pNewAsset->m_height,
							(u8*)pNewAsset->m_bitmap);
						break;
					case GL_UNSIGNED_SHORT_4_4_4_4:
						processImage4444(
							pNewAsset->m_width * pNewAsset->m_height,
							pNewAsset->m_width,
							pNewAsset->m_height,
							(u8*)pNewAsset->m_bitmap);
						break;
					case GL_UNSIGNED_BYTE:
						processImage8888(
							pNewAsset->m_width * pNewAsset->m_height,
							pNewAsset->m_width,
							pNewAsset->m_height,
							(u8*)pNewAsset->m_bitmap);
						break;
					}

					pNewAsset->m_width  >>= 1;
					pNewAsset->m_height >>= 1;
					textureSize >>= 2;
				}
				if (hasScreenVariants) {
					CKLBImageAsset* screenImage = pNewAsset->m_pImages[0];
					SKLBRect* imageRect = screenImage->getSize();
					if ((imageRect->getWidth() == 960) &&
						(imageRect->getHeight() == 640)) {
						pNewAsset->m_bytePerPix = bytePerPix;
						TextureBackgroundFilter::apply(pNewAsset, pixelFormat);
					}
				}

				//
				// Standard rectangular images may occupy only a portion of a
				// standalone texture.  Duplicate their outermost texels into a
				// one-pixel border so bilinear sampling cannot pull transparent
				// or unrelated pixels across the UV boundary.
				//
				// The shipped path is deliberately limited to medium-sized,
				// uncompressed, non-mipmapped single-image textures.  Screen
				// images configured for the background filter use a separate
				// geometry expansion and must retain their original bitmap.
				//
				if ((pNewAsset->m_imageCount == 1) &&
					(g_backgroundFilterExtent == 0) &&
					g_enableTextureBorderPatch) {
					CKLBImageAsset* borderImage = pNewAsset->m_pImages[0];
					if (borderImage->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT) &&
					(pNewAsset->m_width > 0x100) && (pNewAsset->m_width <= 0x400) &&
					(pNewAsset->m_height > 0x100) && (pNewAsset->m_height <= 0x400) &&
					!m_mipmapOnce && !m_disableImageSizeOptimization) {
					const u16 sourceWidth = pNewAsset->m_width;
					const u16 sourceHeight = pNewAsset->m_height;
					const float sourceWidthF = (float)sourceWidth;
					const float sourceHeightF = (float)sourceHeight;
					float* uv = borderImage->m_pUVCoord;

					s32 minX = 9999;
					s32 maxX = -1;
					s32 minY = 9999;
					s32 maxY = -1;
					bool fractionalUV = false;

					{
						const float x = uv[0] * sourceWidthF;
						const float y = uv[1] * sourceHeightF;
						const s32 pixelX = (s32)truncf(x);
						const s32 pixelY = (s32)truncf(y);
						if (((s32)truncf(x + 0.99f) != pixelX) ||
							((s32)truncf(y + 0.99f) != pixelY)) {
							fractionalUV = true;
						}
					if (pixelX <= minX) minX = pixelX; if (pixelX >= maxX) maxX = pixelX;
						if (pixelY <= minY) minY = pixelY; if (pixelY >= maxY) maxY = pixelY;
					}

					{
						const float x = uv[2] * sourceWidthF;
						const float y = uv[3] * sourceHeightF;
						const s32 pixelX = (s32)truncf(x);
						const s32 pixelY = (s32)truncf(y);
						if (((s32)truncf(x + 0.99f) != pixelX) ||
							((s32)truncf(y + 0.99f) != pixelY)) {
							fractionalUV = true;
						}
					if (pixelX <= minX) minX = pixelX; if (pixelX >= maxX) maxX = pixelX;
						if (pixelY <= minY) minY = pixelY; if (pixelY >= maxY) maxY = pixelY;
					}

					{
						const float x = uv[4] * sourceWidthF;
						const float y = uv[5] * sourceHeightF;
						const s32 pixelX = (s32)truncf(x);
						const s32 pixelY = (s32)truncf(y);
						if (((s32)truncf(x + 0.99f) != pixelX) ||
							((s32)truncf(y + 0.99f) != pixelY)) {
							fractionalUV = true;
						}
						if (pixelX <= minX) minX = pixelX;
						if (pixelX >= maxX) maxX = pixelX;
						if (pixelY <= minY) minY = pixelY;
						if (pixelY >= maxY) maxY = pixelY;
					}

					{
						const float x = uv[6] * sourceWidthF;
						const float y = uv[7] * sourceHeightF;
						const s32 pixelX = (s32)truncf(x);
						const s32 pixelY = (s32)truncf(y);
						if (((s32)truncf(x + 0.99f) != pixelX) ||
							((s32)truncf(y + 0.99f) != pixelY)) {
							fractionalUV = true;
						}
						if (pixelX <= minX) minX = pixelX;
						if (pixelX >= maxX) maxX = pixelX;
						if (pixelY <= minY) minY = pixelY;
						if (pixelY >= maxY) maxY = pixelY;
					}

					if (fractionalUV) {
						if (maxX < sourceWidth)  maxX++;
						if (maxY < sourceHeight) maxY++;
					}

					const s32 usedWidth = maxX - minX;
					const s32 usedHeight = maxY - minY;
						u8* sourceBitmap = (u8*)pNewAsset->m_bitmap;
						u8* patchedBitmap = sourceBitmap;
						u16 patchedWidth = sourceWidth;
						u16 patchedHeight = sourceHeight;
						s32 contentLeft = minX;
						s32 contentTop = minY;

						// A rectangle that consumes a complete dimension has no
						// spare texel for its border.  Grow that dimension and
						// place the original pixels one texel inside it.
						const bool growWidth = (usedWidth == sourceWidth);
						const bool growHeight = (usedHeight == sourceHeight);
						const bool repack =
							growWidth || growHeight ||
							(minX == 0) || (maxX == sourceWidth) ||
							(minY == 0) || (maxY == sourceHeight);
						if (repack) {
							contentLeft = 1;
							contentTop = 1;
						}
						if (growWidth || growHeight) {
							patchedWidth = growWidth ? (sourceWidth << 1) : sourceWidth;
							patchedHeight = growHeight ? (sourceHeight << 1) : sourceHeight;
							const size_t patchedSize =
								(size_t)patchedWidth * patchedHeight * bytePerPix;
							patchedBitmap = KLBNEWA(u8, patchedSize);
							memset(patchedBitmap, 0, patchedSize);
						}
						pNewAsset->m_width = patchedWidth;
						pNewAsset->m_height = patchedHeight;

						const size_t copyBytes = (size_t)usedWidth * bytePerPix;
						if (repack && (contentTop > minY)) {
							for (s32 row = usedHeight; row-- > 0; ) {
								memmove(
									patchedBitmap +
										((contentTop + row) * patchedWidth + contentLeft) * bytePerPix,
									sourceBitmap +
										((minY + row) * sourceWidth + minX) * bytePerPix,
									copyBytes);
							}
						} else if (repack) {
							for (s32 row = 0; row < usedHeight; row++) {
								memmove(
									patchedBitmap +
										((contentTop + row) * patchedWidth + contentLeft) * bytePerPix,
									sourceBitmap +
										((minY + row) * sourceWidth + minX) * bytePerPix,
									copyBytes);
							}
						}

						const size_t rowStride = (size_t)patchedWidth * bytePerPix;
						if ((contentLeft > 0) && (contentLeft + usedWidth <= patchedWidth)) {
							for (s32 row = 0; row < usedHeight; row++) {
								u8* rowPixels = patchedBitmap +
									(size_t)(contentTop + row) * rowStride;
								u8* border = rowPixels + (contentLeft - 1) * bytePerPix;
								u8* edge = rowPixels + contentLeft * bytePerPix;
								switch (bytePerPix) {
								case 4: border[3] = edge[3];
								case 3: border[2] = edge[2];
								case 2: border[1] = edge[1];
								case 1: border[0] = edge[0];
								}
							}
						}
						if (contentLeft + usedWidth < patchedWidth) {
							for (s32 row = 0; row < usedHeight; row++) {
								u8* rowPixels = patchedBitmap +
									(size_t)(contentTop + row) * rowStride;
								u8* border = rowPixels + (contentLeft + usedWidth) * bytePerPix;
								u8* edge = border - bytePerPix;
								switch (bytePerPix) {
								case 4: border[3] = edge[3];
								case 3: border[2] = edge[2];
								case 2: border[1] = edge[1];
								case 1: border[0] = edge[0];
								}
							}
						}

						const size_t borderedRowBytes = (size_t)usedWidth * bytePerPix;
						if (contentTop > 0) {
							memcpy(
								patchedBitmap +
									((contentTop - 1) * patchedWidth + contentLeft) * bytePerPix,
								patchedBitmap +
									(contentTop * patchedWidth + contentLeft) * bytePerPix,
								borderedRowBytes);
						}
						if (contentTop + usedHeight < patchedHeight) {
							memcpy(
								patchedBitmap +
									((contentTop + usedHeight) * patchedWidth + contentLeft) * bytePerPix,
								patchedBitmap +
									((contentTop + usedHeight - 1) * patchedWidth + contentLeft) * bytePerPix,
								borderedRowBytes);
						}

						if (patchedBitmap != sourceBitmap) {
							if (ownsBitmap) {
								KLBDELETEA(sourceBitmap);
							}
							ownsBitmap = true;
						}
						pNewAsset->m_bitmap = patchedBitmap;

						const float scaleU = growWidth ? 0.5f : 1.0f;
						const float scaleV = growHeight ? 0.5f : 1.0f;
						const float sourceU = (float)minX / sourceWidthF;
						const float sourceV = (float)minY / sourceHeightF;
						const float targetU = (float)contentLeft / patchedWidth;
						const float targetV = (float)contentTop / patchedHeight;
						uv[0] = (uv[0] - sourceU) * scaleU + targetU;
						uv[1] = (uv[1] - sourceV) * scaleV + targetV;
						uv[2] = (uv[2] - sourceU) * scaleU + targetU;
						uv[3] = (uv[3] - sourceV) * scaleV + targetV;
						uv[4] = (uv[4] - sourceU) * scaleU + targetU;
						uv[5] = (uv[5] - sourceV) * scaleV + targetV;
						uv[6] = (uv[6] - sourceU) * scaleU + targetU;
						uv[7] = (uv[7] - sourceV) * scaleV + targetV;
					}
					}
				// m_mipmapOnce is cleared below, at the point its effect is applied.

				if (this->m_loadHardware) {
					if (CKLBAssetManager::getInstance().isAsyncLoading() == false) {
						pNewAsset->m_bytePerPix	= bytePerPix;
						CKLBOGLWrapper::TextureCreateInfo info;
						info.width = pNewAsset->m_width;
						info.height = pNewAsset->m_height;
						info.pixelFormat = pixelFormat;
						info.channelCount = channelCount;
						info.data = pNewAsset->m_bitmap;
						info.dataLength = textureSize;
						info.option = (CKLBOGLWrapper::TEX_OPTION)opt;
						if ((pNewAsset->m_type & (1<<4)) || m_mipmapOnce) { info.mipmapCount = 2; }
						m_mipmapOnce = false;
						pNewAsset->m_pTexture	= pMgr.createTexture(info,
																	 (!m_pReloadAsset) ? NULL : pNewAsset->m_pTexture);
						// Sync loading.
					} else {
						// Async loading, but main thread need to perform the openGL call.
						CKLBAssetManager::getInstance().setMainThreadTexture(pNewAsset, pixelFormat, channelCount, opt, textureSize);
					}
					if (pNewAsset->m_pTexture) {
						gTextureAllocHW += pNewAsset->m_width * pNewAsset->m_height * bytePerPix;
					}
				} else {
					pNewAsset->m_pTexture = NULL;
				}

				if (this->m_loadSoftware) {
					pNewAsset->m_softTexture = createSoftTexture(
						pNewAsset->m_width,
						pNewAsset->m_height,
						pixelFormat,
						channelCount,
						pNewAsset->m_bitmap);
					if (pNewAsset->m_softTexture) {
						gTextureAllocSW += pNewAsset->m_width * pNewAsset->m_height * 4;
					}
				} else {
					pNewAsset->m_softTexture = NULL;
				}

				if (pNewAsset->m_pTexture) {
					pNewAsset->m_pTextureUsage = pNewAsset->m_pTexture->createUsage();				
					// pNewAsset->m_pTextureUsage->setSampling(
				} else {
					pNewAsset->m_pTextureUsage = NULL;
				}
			}

			if (ownsBitmap) {
				if (pNewAsset->m_bitmap) {
					KLBDELETEA((u8*)pNewAsset->m_bitmap);
					pNewAsset->m_bitmap = NULL;
				} else {
					// Allocation error, failure.
					KLBDELETE(pNewAsset);
					pNewAsset = NULL;
				}
			} else {
				pNewAsset->m_bitmap = NULL;
			}
			return pNewAsset;
		}

		klb_assertNull(0, "Allocation issue");
		KLBDELETE(pNewAsset);
	}
	return NULL;
}

#include "CKLBNode.h"

/*virtual*/
CKLBNode* 
CKLBImageAsset::createSubTree(u32 priorityBase) 
{
	CKLBNode*	pNode	= KLBNEW(CKLBNode);
	CKLBSprite*	pRender	= CKLBRenderingManager::getInstance().allocateCommandSprite(this,priorityBase);
	if (pNode && pRender) {
		pNode->setRender(pRender);
		return pNode;
	}
	if (pNode) { KLBDELETE(pNode); }
	return NULL;
}

#include "CKLBDataHandler.h"

/*static*/ 
CKLBNode* 
CKLBImageAsset::createSprite(	u32 textureHandle,
								const char* imageName,
								CKLBNode* pParentNode,
								u32 renderPriority) 
{
	klb_assertNull(textureHandle,	"Valid Handle is required");
	klb_assertNull(pParentNode,		"Requires valid parent node");

	CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();
	CKLBNode* pNode = KLBNEW(CKLBNode);

	if (pNode) {
		CKLBImageAsset* pImg = ((CKLBTextureAsset*)CKLBDataHandler::getPointer(textureHandle))->getImage(imageName);
		if (pImg) {
			CKLBSprite*	pRender	= pRdrMgr.allocateCommandSprite(pImg);
			if (pRender) {
				pRender->changeOrder(pRdrMgr, renderPriority);
				pNode->setRender(pRender);
				pNode->setRenderOnDestroy(true);
				pParentNode->addNode(pNode, 0);
				pParentNode->markUpMatrix();
				return pNode;
			}
		}
		KLBDELETE(pNode);
	}
	return NULL;
}

u8* TexturePayloadReader::readHeader(
	u8* bitmap,
	CKLBImageAsset* image,
	CKLBTextureAsset* texture,
	u8* channel)
{
	const float* uv = image->m_pUVCoord;
	const s32 textureWidth = texture->m_width;
	const s32 startX = (s32)(uv[0] * textureWidth);
	const s32 startY = (s32)(uv[1] * texture->m_height);

	m_imageWidth = image->getSize()->getWidth();
	m_rowAdvance = (textureWidth - m_imageWidth) * 4;
	m_pixelColumn = 0;

	u8* pixel = bitmap + ((startY * textureWidth + startX) * 4);
	do {
		consumeBit(pixel);
		if (m_bitPosition >= 80) {
			pixel++;
			*channel = 0;
			break;
		}

		consumeBit(pixel + 1);
		if (m_bitPosition >= 80) {
			pixel += 2;
			*channel = 1;
			break;
		}

		consumeBit(pixel + 2);
		pixel += 4;
		m_pixelColumn++;
		if (m_pixelColumn == m_imageWidth) {
			m_pixelColumn = 0;
			pixel += m_rowAdvance;
		}
		if (m_bitPosition >= 80) {
			*channel = 3;
			break;
		}
	} while (m_bitPosition < 80);

	if ((m_header[0] == '@') && (m_header[1] == '%') &&
		(m_header[2] == '1') && (m_header[3] == ',')) {
		m_payloadLength = ((u32)m_header[5] << 8) | m_header[8];
		m_payload = KLBNEWA(u8, m_payloadLength + 1);
		m_output = m_payload;
		memset(m_payload, 0, m_payloadLength + 1);
	}
	return pixel;
}

void TexturePayloadReader::readPayload(u8* pixel, u8 channel)
{
	m_bitPosition = 0;
	while ((m_bitPosition >> 3) != m_payloadLength) {
		const s32 payloadBitLength = m_payloadLength << 3;
		switch (channel) {
		case 0:
			consumeBit(pixel);
			pixel++;
			channel++;
			break;
		case 3:
			consumeBit(pixel);
			pixel++;
			channel -= 3;
			break;
		case 1:
			consumeBit(pixel);
			pixel += 2;
			channel += 2;
			m_pixelColumn++;
			if (m_pixelColumn == m_imageWidth) {
				m_pixelColumn = 0;
				pixel += m_rowAdvance;
			}
			break;
		default:
			m_bitPosition = payloadBitLength;
			break;
		}
	}
}

bool decodeTexturePayload(
	u8* bitmap,
	CKLBImageAsset* image,
	CKLBTextureAsset* texture,
	KLBTextureAssetPlugin::TextureLoadCallback callback)
{
	if (!callback) {
		return false;
	}

	TexturePayloadReader reader;
	u8 channel;
	u8* pixel = reader.readHeader(bitmap, image, texture, &channel);
	if (!reader.payload()) {
		if (!reader.suppressMissingPayloadCallback()) {
			callback(NULL, 1, false);
		}
		return false;
	}

	reader.readPayload(pixel, channel);

	const u32 length = reader.payloadLength();
	u8* decodedData = reader.payload();
	u64 randomState = 0xDEADBEEF;
	for (s32 n = 0; n < reader.payloadLength(); n++) {
		randomState = (randomState * 16807) % 2147483647;
		decodedData[(u32)n] ^= (u8)randomState;
	}

	u32 firstSum = 1;
	u32 secondSum = 0;
	for (u32 n = 0; n < length; n++) {
		firstSum = (firstSum + decodedData[n]) % reader.checksumModulus();
		secondSum = (secondSum + firstSum) % reader.checksumModulus();
	}
	const u32 checksum = (secondSum << 16) | firstSum;
	const u8* header = reader.header();
	const bool checksumValid =
		((u8)(checksum >> 24) == header[4]) &&
		((u8)(checksum >> 16) == header[6]) &&
		((u8)(checksum >> 8) == header[7]) &&
		((u8)checksum == header[9]);

	callback(decodedData, length, checksumValid);
	return true;
}

namespace {
	CKLBGridTextureObject* s_gridTextures[16] = { NULL };
	u32 s_gridTextureCount = 0;
}

// Shared with the scripting façade and the asynchronous grid-loading path.
CKLBGridTextureObject::CKLBGridTextureObject()
: CKLBTextureAsset()
, m_gridImages(NULL)
, m_imagePointers(NULL)
, m_cellTextures(NULL)
, m_cellStates(NULL)
, m_cellGeometry(NULL)
, m_uploadBuffer(NULL)
, m_decodeBuffer(NULL)
, m_columnCount(0)
, m_rowCount(0)
, m_gridID(NULL_IDX)
, m_hasBorder(false)
, m_locked(false)
{
}

CKLBGridTextureObject::~CKLBGridTextureObject()
{
	CKLBOGLWrapper& textureManager = CKLBOGLWrapper::getInstance();
	if (m_pTexture) {
		if (m_pTextureUsage) {
			m_pTexture->releaseUsage(m_pTextureUsage);
			m_pTextureUsage = NULL;
		}
		textureManager.releaseTexture(m_pTexture);
		m_pTexture = NULL;
	}

	const u32 rowCount = m_rowCount;
	const u32 columnCount = m_columnCount;
	CKLBAssetManager& assetManager = CKLBAssetManager::getInstance();
	const u32 cellCount = columnCount * rowCount;
	for (u32 index = 0; index < cellCount; index++) {
		const u16 assetID = m_cellTextures[index].getAssetID();
		if (assetID != NULL_IDX) {
			assetManager.freeAssetSlot(assetID);
		}
	}

	KLBDELETEA(m_gridImages);
	KLBDELETEA(m_imagePointers);
	KLBDELETEA(m_cellTextures);
	KLBDELETEA(m_cellStates);
	KLBDELETEA(m_cellGeometry);
	KLBDELETEA(m_uploadBuffer);
	KLBDELETEA(m_decodeBuffer);
	m_gridImages = NULL;
	m_imagePointers = NULL;
	m_cellTextures = NULL;
	m_cellStates = NULL;
	m_cellGeometry = NULL;
	m_uploadBuffer = NULL;
	m_decodeBuffer = NULL;

	if (m_gridID != NULL_IDX) {
		s_gridTextures[m_gridID] = NULL;
	}
}

void CKLBGridTextureObject::destroy()
{
	if (!m_locked) {
		KLBDELETE(this);
		if (!CKLBTaskMgr::getInstance().isClearingTaskList() && g_gridTextureDieCallback) {
			CKLBScriptEnv::getInstance().call_gridTextureDie(g_gridTextureDieCallback, this);
		}
	}
}

void CKLBGridTextureObject::lock()
{
	m_locked = true;
}

u16 CKLBGridTextureObject::popFront(CellList list)
{
	u16 cellIndex = m_listHeads[list];
	if (cellIndex != NULL_IDX) {
		u16 next = m_cellStates[cellIndex].next;
		m_listHeads[list] = next;
		if (next != NULL_IDX) {
			m_cellStates[next].previous = NULL_IDX;
		} else {
			m_listTails[list] = NULL_IDX;
		}
}
	return cellIndex;
}

u16 CKLBGridTextureObject::popBack(CellList list)
{
	u16 cellIndex = m_listTails[list];
	if (cellIndex != NULL_IDX) {
		u16 previous = m_cellStates[cellIndex].previous;
		m_listTails[list] = previous;
		if (previous != NULL_IDX) {
			m_cellStates[previous].next = NULL_IDX;
		} else {
			m_listHeads[list] = NULL_IDX;
		}
	}
	return cellIndex;
}

void CKLBGridTextureObject::pushFront(CellList list, u16 cellIndex)
{
	u16 oldHead = m_listHeads[list];
	CellState& cell = m_cellStates[cellIndex];
	cell.next = oldHead;
	cell.previous = NULL_IDX;
	cell.status.value = (cell.status.value & 0xff0f) | (list << 4);
	m_listHeads[list] = cellIndex;
	if (oldHead != NULL_IDX) {
		m_cellStates[oldHead].previous = cellIndex;
	} else {
		m_listTails[list] = cellIndex;
	}
}

void CKLBGridTextureObject::unlinkCell(CellList list, u16 cellIndex)
{
	CellState& cell = m_cellStates[cellIndex];
	u16 next = cell.next;
	u16 previous = cell.previous;
	if (next != NULL_IDX) {
		m_cellStates[next].previous = previous;
	} else {
		m_listTails[list] = NULL_IDX;
	}
	if (previous != NULL_IDX) {
		m_cellStates[previous].next = next;
	} else {
		m_listHeads[list] = NULL_IDX;
	}
}

void CKLBGridTextureObject::markCellReferenced(u16 cellIndex)
{
	m_cellStates[cellIndex].status.value |= 1;
}

void CKLBGridTextureObject::releaseCell(CKLBTextureAsset* texture)
{
	u16 cellIndex = texture - m_cellTextures;
	CellState& cell = m_cellStates[cellIndex];
	unlinkCell(LIST_LIVE, cellIndex);

	u16 state = cell.status.value;
	u16 unregister = state & 1;
	cell.status.value = state & ~1;

	CellList destination = (CellList)(unregister + 1);
	u16 oldHead = m_listHeads[destination];
	cell.next = oldHead;
	cell.previous = NULL_IDX;
	cell.status.value = (state & 0xff0e) | (destination << 4);
	m_listHeads[destination] = cellIndex;
	if (oldHead != NULL_IDX) {
		m_cellStates[oldHead].previous = cellIndex;
	} else {
		m_listTails[destination] = cellIndex;
	}

	if (unregister) {
		CKLBAssetManager::getInstance().freeAssetSlot(texture->getAssetID());
	}
	texture->m_pTextureState->decrementRefCount();
}

bool CKLBGridTextureObject::unlock()
{
	m_locked = false;
	if (getRefCount() == 0) {
		KLBDELETE(this);
		return false;
	}
	return true;
}

bool CKLBLuaLibASSET::setGridLocked(const void* grid, bool locked)
{
	g_gridTextureError = 0;
	bool result = false;
	if (grid) {
		CKLBGridTextureObject* texture =
			const_cast<CKLBGridTextureObject*>(
				static_cast<const CKLBGridTextureObject*>(grid));
		if (locked) {
			texture->lock();
			result = true;
		} else {
			result = texture->unlock();
		}
	} else {
		g_gridTextureError = 1;
	}

	if (g_gridTextureError && !g_gridTextureFirstError) {
		g_gridTextureFirstError = g_gridTextureError;
	}
	return result;
}

s32 CKLBLuaLibASSET::getGridError(bool clear)
{
	if (clear) {
		s32 error = g_gridTextureFirstError;
		g_gridTextureFirstError = 0;
		return error;
	}
	s32 error = g_gridTextureError;
	g_gridTextureError = 0;
	return error;
}

void CKLBLuaLibASSET::setGridDieCallback(const char* callback)
{
	if (g_gridTextureDieCallback) {
		KLBDELETEA(g_gridTextureDieCallback);
		g_gridTextureDieCallback = NULL;
	}
	if (callback) {
		g_gridTextureDieCallback = CKLBUtility::copyString(callback);
	}
}

// 1. createTexture.
// 2. Register Asset. (Name in dictionnary, slot, etc...)
// 3. Do screenshot feature
CKLBAbstractAsset*
createTexture(u32 orgWidthI, u32 orgHeightI, const char* name,
	s32 pixelFormatID, u8 roundToPowerOfTwo, CTexture* sourceTexture)
{
	CKLBTextureAsset* pNewAsset = KLBNEW(CKLBTextureAsset);

	if (name && pNewAsset) {
		u32 heightI;
		u32 widthI;
		if (roundToPowerOfTwo) {
			widthI	= CKLBUtility::nearest2Pow(orgWidthI );
			heightI = CKLBUtility::nearest2Pow(orgHeightI);
		} else {
			widthI  = orgWidthI;
			heightI = orgHeightI;
		}

		// Name
		// + [pad]
		if (pNewAsset) {
			pNewAsset->setNameDirect(name);

			pNewAsset->m_width				= orgWidthI;
			pNewAsset->m_height				= orgHeightI;
			pNewAsset->m_type				= 3;
			pNewAsset->m_totalVertexCount	= 4;
			pNewAsset->m_totalIndexCount	= 6;
			pNewAsset->m_imageCount			= 1;

			pNewAsset->m_floatBufferTotal	= KLBNEWA(float				, pNewAsset->m_totalVertexCount*4	);  // XYUV
			pNewAsset->m_indexBufferTotal	= KLBNEWA(u16				, pNewAsset->m_totalIndexCount		);
			pNewAsset->m_pImages			= KLBNEWA(CKLBImageAsset*	, pNewAsset->m_imageCount			);

			int textureSize;
			if (sourceTexture) {
				pNewAsset->m_pTexture = sourceTexture;
				pNewAsset->m_bytePerPix = 4;
				orgWidthI = sourceTexture->getWidth();
				orgHeightI = sourceTexture->getHeight();
				textureSize = orgWidthI * orgHeightI * 4;
			} else {
				GLenum pixelFormat;
				CKLBOGLWrapper::TEX_CHANNEL channelCount;
				int bytePerPix;
				int formatIndex = pixelFormatID - 1;
				if (formatIndex >= 0 && formatIndex < 4) {
					bytePerPix = s_textureBytesPerPixel[formatIndex];
					channelCount = (CKLBOGLWrapper::TEX_CHANNEL)s_textureChannels[formatIndex];
					pixelFormat = (GLenum)s_texturePixelFormats[formatIndex];
				}

				CKLBOGLWrapper& pMgr = CKLBOGLWrapper::getInstance();
				textureSize = widthI * heightI * bytePerPix;

				//
				// Texture creation may fail, but asset is considered as loaded
				//
				pNewAsset->m_bytePerPix = bytePerPix;
				pNewAsset->m_pTexture = pMgr.createTexture(	pNewAsset->m_width,
													pNewAsset->m_height,
													pixelFormat,
													channelCount,
													NULL,
													textureSize,
													(CKLBOGLWrapper::TEX_OPTION)0);
			}

			if (pNewAsset->m_pTexture) {
				gTextureAllocHW += textureSize;
			}

			if (pNewAsset->m_floatBufferTotal && pNewAsset->m_indexBufferTotal && pNewAsset->m_pImages && pNewAsset->getName()) {
				CKLBImageAsset* pNewAssetI = KLBNEW(CKLBImageAsset);
				pNewAsset->m_pImages[0] = pNewAssetI;
				if (pNewAssetI) {
					klb_assertNull(strlen(name) < 250, "Name for screenshot name is too long");

					// + [pad]
					char buff[256];
					sprintf(buff,"%s.imag",name);

					pNewAssetI->setNameDirect(buff);
					pNewAssetI->m_uiSubTileCount		= 1;
					pNewAssetI->m_usageType             |= CKLBImageAsset::IS_STANDARD_RECT;

					pNewAssetI->m_uiVertexCount			= 4;
					pNewAssetI->m_uiIndexCount			= 6;

					pNewAssetI->m_imageSize.m_iRight	= orgWidthI;
					pNewAssetI->m_imageSize.m_iBottom	= orgHeightI;
					pNewAssetI->m_imageSize.m_iLeft		= 0;
					pNewAssetI->m_imageSize.m_iTop		= 0;

					pNewAssetI->m_iCenterX				= 0;
					pNewAssetI->m_iCenterY				= 0;

					pNewAssetI->m_pUVCoord				= pNewAsset->m_floatBufferTotal;
					pNewAssetI->m_pXYCoord				= &pNewAsset->m_floatBufferTotal[8];
					pNewAssetI->m_pIndex				= pNewAsset->m_indexBufferTotal;

					pNewAssetI->m_pTextureAsset			= pNewAsset;

					float orgWidth	= orgWidthI;
					float orgHeight	= orgHeightI;
					/*
					float ratioU	= orgWidth  / widthI;
					float ratioV	= orgHeight / heightI;
					*/
					float startV	= 1.0f;
					float endV		= 0.0f;

					float startU	= 0.0f;
					float endU		= 1.0f;

					pNewAssetI->m_pXYCoord[0]			= 0.0f;
					pNewAssetI->m_pXYCoord[1]			= 0.0f;

					pNewAssetI->m_pXYCoord[2]			= orgWidth;
					pNewAssetI->m_pXYCoord[3]			= 0.0f;

					pNewAssetI->m_pXYCoord[4]			= orgWidth;
					pNewAssetI->m_pXYCoord[5]			= orgHeight;

					pNewAssetI->m_pXYCoord[6]			= 0.0f;
					pNewAssetI->m_pXYCoord[7]			= orgHeight;

					pNewAssetI->m_pUVCoord[0]			= startU;
					pNewAssetI->m_pUVCoord[1]			= startV;

					pNewAssetI->m_pUVCoord[2]			= endU;
					pNewAssetI->m_pUVCoord[3]			= startV;

					pNewAssetI->m_pUVCoord[4]			= endU;
					pNewAssetI->m_pUVCoord[5]			= endV;

					pNewAssetI->m_pUVCoord[6]			= startU;
					pNewAssetI->m_pUVCoord[7]			= endV;


					pNewAssetI->m_pIndex[0]				= 0;
					pNewAssetI->m_pIndex[1]				= 1;
					pNewAssetI->m_pIndex[2]				= 3;

					pNewAssetI->m_pIndex[3]				= 1;
					pNewAssetI->m_pIndex[4]				= 2;
					pNewAssetI->m_pIndex[5]				= 3;

					pNewAssetI->m_boundWidth			= orgWidthI;
					pNewAssetI->m_boundHeight			= orgHeightI;

					pNewAsset->m_pTextureUsage			= pNewAsset->m_pTexture->createUsage();

					return pNewAsset;
				}
			}
		}
		KLBDELETE(pNewAsset);
	}

	klb_assertAlways("allocation failure.");
	return NULL;
}

bool 
createScreenAsset(const char* name, u32 orgWidthI, u32 orgHeightI) 
{
	CKLBAbstractAsset* pAsset = createTexture(orgWidthI, orgHeightI, name,
		TEXTURE_ASSET_RGB888, true, NULL);
	if (pAsset) {
		CKLBAssetManager::getInstance().registerAsset(pAsset);
		if (pAsset->getAssetID() != NULL_IDX) {
			pAsset->incrementRefCount();
			return true;
		}
		KLBDELETE(pAsset);
	}
	return false;
}

void CKLBTextureAsset::updateMovieTexture(u32 textureTarget, u32 textureName,
										 s32 width, s32 height, const float* uv) {
	m_width = (u16)width; m_height = (u16)height;
	m_pTexture->bindExternalTexture(textureTarget, textureName);

	CKLBImageAsset* image = m_pImages[0];
	image->m_imageSize.m_iRight = (s16)width; image->m_imageSize.m_iBottom = (s16)height;
	image->m_imageSize.m_iLeft = 0; image->m_imageSize.m_iTop = 0;

	const float floatWidth = (float)width;
	const float floatHeight = (float)height;
	const float startU = uv[0];
	const float startV = uv[1];
	const float endV = uv[3];
	const float endU = uv[2];
	float* xy = image->m_pXYCoord;
	xy[0] = 0.0f;      xy[1] = 0.0f;
	xy[2] = floatWidth; xy[3] = 0.0f;
	xy[4] = floatWidth; xy[5] = floatHeight;
	xy[6] = 0.0f;      xy[7] = floatHeight;

	float* imageUV = image->m_pUVCoord;
	imageUV[0] = startU; imageUV[1] = startV;
	imageUV[2] = endU;   imageUV[3] = startV;
	imageUV[4] = endU;   imageUV[5] = endV;
	imageUV[6] = startU; imageUV[7] = endV;
	image->m_boundWidth = floatWidth; image->m_boundHeight = floatHeight;
}

CKLBTextureAsset* CKLBTextureAsset::createMovieTexture(const char* name) {
	CKLBTextureAsset* texture = KLBNEW(CKLBTextureAsset);
	if (name && texture) {
		texture->setNameDirect(name);
		texture->m_type = 3;
		texture->m_totalVertexCount = 4;
		texture->m_totalIndexCount = 6;
		texture->m_imageCount = 1;

		texture->m_floatBufferTotal =
			KLBNEWA(float, texture->m_totalVertexCount * 4);
		texture->m_indexBufferTotal =
			KLBNEWA(u16, texture->m_totalIndexCount);
		texture->m_pImages =
			KLBNEWA(CKLBImageAsset*, texture->m_imageCount);

		CKLBOGLWrapper& rendering = CKLBOGLWrapper::getInstance();
		texture->m_pTexture = rendering.createTexture(0, 0, 0, CKLBOGLWrapper::RGBA,
			NULL, 0, CKLBOGLWrapper::TEX_OPT_SHELL_BIT);
		if (texture->m_floatBufferTotal && texture->m_indexBufferTotal &&
			texture->m_pImages && texture->getName()) {
			CKLBImageAsset* image = KLBNEW(CKLBImageAsset);
			texture->m_pImages[0] = image;
			if (image) {
				klb_assertNull(strlen(name) < 250, "Name for screenshot name is too long");
				image->setNameDirect(name);
				image->m_uiSubTileCount = 1; image->m_usageType |= CKLBImageAsset::IS_STANDARD_RECT;
				image->m_uiVertexCount = 4; image->m_uiIndexCount = 6;
				image->m_iCenterX = 0; image->m_iCenterY = 0;
				image->m_pUVCoord = texture->m_floatBufferTotal;
				image->m_pXYCoord = &texture->m_floatBufferTotal[8];
				image->m_pIndex = texture->m_indexBufferTotal;
				image->m_pTextureAsset = texture;
				image->m_pIndex[0] = 0; image->m_pIndex[1] = 1; image->m_pIndex[2] = 3;
				image->m_pIndex[3] = 1; image->m_pIndex[4] = 2; image->m_pIndex[5] = 3;
				texture->m_pTextureUsage = texture->m_pTexture->createUsage();
				return texture;
			}
		}
		KLBDELETE(texture);
	}

	klb_assertAlways("allocation failure.");
	return NULL;
}

bool 
doScreenShot(const char* name, u32 srcx, u32 srcy, u32 width, u32 height, u32 dstx, u32 dsty) 
{
	CKLBAssetManager& mgr = CKLBAssetManager::getInstance();
	u16 idx = mgr.getAssetIDFromName(name,(char)NULL,1);
	if (idx != NULL_IDX) {
		CKLBTextureAsset* pNewAsset	= (CKLBTextureAsset*)mgr.getAsset(idx);
		u8* buffer					= NULL;
		u32 bufferSize				= width * height * 4;	// RGBA8888 per pixel
		CKLBOGLWrapper& pMgr		= CKLBOGLWrapper::getInstance();

		buffer = KLBNEWA(u8, bufferSize);
		if (buffer) {
			if (pMgr.copyScreenRGB888(srcx,srcy,width,height,buffer)) {
				if (pNewAsset->m_pTexture->updateTexture(dstx,dsty,width,height,buffer,bufferSize)) {
					KLBDELETEA(buffer);
					return true;
				}
			}
			KLBDELETEA(buffer);
		}
	}
	return false;
}

void 
freeScreenAsset(const char* name) 
{
	CKLBAssetManager& mgr = CKLBAssetManager::getInstance();
	u16 idx = mgr.getAssetIDFromName(name,(char)NULL,1);
	if (idx != NULL_IDX) {
		CKLBTextureAsset* pNewAsset	= (CKLBTextureAsset*)mgr.getAsset(idx);
		pNewAsset->decrementRefCount();
	}
}

/*
 * Here is the implementation of the ETC1 decoder part taken out from the 
 * rg_etc1 library, we took only the decoder part out.
 *
 * The original project code can be found at :
 * http://code.google.com/p/rg-etc1/
 */
//------------------------------------------------------------------------------
//
// rg_etc1 uses the ZLIB license:
// http://opensource.org/licenses/Zlib
//
// Copyright (c) 2012 Rich Geldreich
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
//------------------------------------------------------------------------------

// File: rg_etc1.cpp - Fast, high quality ETC1 block packer/unpacker - Rich Geldreich <richgel99@gmail.com>
// Please see ZLIB license at the end of rg_etc1.h.
//
// For more information Ericsson Texture Compression (ETC/ETC1), see:
// http://www.khronos.org/registry/gles/extensions/OES/OES_compressed_ETC1_RGB8_texture.txt
//
// v1.04 - 5/15/14 - Fix signed vs. unsigned subtraction problem (noticed when compiled with gcc) in pack_etc1_block_init(). 
//         This issue would cause an assert when this func. was called in debug. (Note this module was developed/testing with MSVC, 
//         I still need to test it throughly when compiled with gcc.)
//
// v1.03 - 5/12/13 - Initial public release
#include <stdlib.h>
#include <memory.h>
#include <assert.h>
//#include <stdio.h>
#include <math.h>

#pragma warning (disable: 4201) //  nonstandard extension used : nameless struct/union

#if defined(_DEBUG) || defined(DEBUG)
#define RG_ETC1_BUILD_DEBUG
#endif

// The decoder is fed validated texture blocks; keep its internal diagnostics
// independent from the engine's debug assertion configuration.
#define RG_ETC1_ASSERT(expression) ((void)0)

namespace rg_etc1
{
   typedef unsigned char uint8;
   typedef unsigned short uint16;
   typedef unsigned int uint;
   typedef unsigned int uint32;
   typedef long long int64;
   typedef unsigned long long uint64;

   const uint32 cUINT32_MAX = 0xFFFFFFFFU;
   const uint64 cUINT64_MAX = 0xFFFFFFFFFFFFFFFFULL; //0xFFFFFFFFFFFFFFFFui64;
   
   template<typename T> inline T minimum(T a, T b) { return (a < b) ? a : b; }
   template<typename T> inline T clamp(T value, T low, T high) { return (value < low) ? low : ((value > high) ? high : value); }

   enum eNoClamp { cNoClamp };

   struct color_quad_u8
   {
      static inline int clamp(int v) { if (v & 0xFFFFFF00U) v = (~(static_cast<int>(v) >> 31)) & 0xFF; return v; }

      struct component_traits { enum { cSigned = false, cFloat = false, cMin = 0U, cMax = 255U }; };

   public:
      typedef unsigned char component_t;
      typedef int parameter_t;

      enum { cNumComps = 4 };

      union
      {
         struct
         {
            component_t r;
            component_t g;
            component_t b;
            component_t a;
         };

         component_t c[cNumComps];

         uint32 m_u32;
      };

      inline color_quad_u8()
      {
      }

      inline color_quad_u8(const color_quad_u8& other) : m_u32(other.m_u32)
      {
      }

      explicit inline color_quad_u8(parameter_t y, parameter_t alpha = component_traits::cMax)
      {
         set(y, alpha);
      }

      inline color_quad_u8(parameter_t red, parameter_t green, parameter_t blue, parameter_t alpha = component_traits::cMax)
      {
         set(red, green, blue, alpha);
      }

      explicit inline color_quad_u8(eNoClamp, parameter_t y, parameter_t alpha = component_traits::cMax)
      {
         set_noclamp_y_alpha(y, alpha);
      }

      inline color_quad_u8(eNoClamp, parameter_t red, parameter_t green, parameter_t blue, parameter_t alpha = component_traits::cMax)
      {
         set_noclamp_rgba(red, green, blue, alpha);
      }

      inline void clear()
      {
         m_u32 = 0;
      }

      inline color_quad_u8& operator= (const color_quad_u8& other)
      {
         m_u32 = other.m_u32;
         return *this;
      }

      inline color_quad_u8& set_rgb(const color_quad_u8& other)
      {
         r = other.r;
         g = other.g;
         b = other.b;
         return *this;
      }

      inline color_quad_u8& operator= (parameter_t y)
      {
         set(y, component_traits::cMax);
         return *this;
      }

      inline color_quad_u8& set(parameter_t y, parameter_t alpha = component_traits::cMax)
      {
         y = clamp(y);
         alpha = clamp(alpha);
         r = static_cast<component_t>(y);
         g = static_cast<component_t>(y);
         b = static_cast<component_t>(y);
         a = static_cast<component_t>(alpha);
         return *this;
      }

      inline color_quad_u8& set_noclamp_y_alpha(parameter_t y, parameter_t alpha = component_traits::cMax)
      {
         RG_ETC1_ASSERT( (y >= component_traits::cMin) && (y <= component_traits::cMax) );
         RG_ETC1_ASSERT( (alpha >= component_traits::cMin) && (alpha <= component_traits::cMax) );

         r = static_cast<component_t>(y);
         g = static_cast<component_t>(y);
         b = static_cast<component_t>(y);
         a = static_cast<component_t>(alpha);
         return *this;
      }

      inline color_quad_u8& set(parameter_t red, parameter_t green, parameter_t blue, parameter_t alpha = component_traits::cMax)
      {
         r = static_cast<component_t>(clamp(red));
         g = static_cast<component_t>(clamp(green));
         b = static_cast<component_t>(clamp(blue));
         a = static_cast<component_t>(clamp(alpha));
         return *this;
      }

      inline color_quad_u8& set_noclamp_rgba(parameter_t red, parameter_t green, parameter_t blue, parameter_t alpha)
      {
         RG_ETC1_ASSERT( (red >= component_traits::cMin) && (red <= component_traits::cMax) );
         RG_ETC1_ASSERT( (green >= component_traits::cMin) && (green <= component_traits::cMax) );
         RG_ETC1_ASSERT( (blue >= component_traits::cMin) && (blue <= component_traits::cMax) );
         RG_ETC1_ASSERT( (alpha >= component_traits::cMin) && (alpha <= component_traits::cMax) );

         r = static_cast<component_t>(red);
         g = static_cast<component_t>(green);
         b = static_cast<component_t>(blue);
         a = static_cast<component_t>(alpha);
         return *this;
      }

      inline color_quad_u8& set_noclamp_rgb(parameter_t red, parameter_t green, parameter_t blue)
      {
         RG_ETC1_ASSERT( (red >= component_traits::cMin) && (red <= component_traits::cMax) );
         RG_ETC1_ASSERT( (green >= component_traits::cMin) && (green <= component_traits::cMax) );
         RG_ETC1_ASSERT( (blue >= component_traits::cMin) && (blue <= component_traits::cMax) );

         r = static_cast<component_t>(red);
         g = static_cast<component_t>(green);
         b = static_cast<component_t>(blue);
         return *this;
      }

      static inline parameter_t get_min_comp() { return component_traits::cMin; }
      static inline parameter_t get_max_comp() { return component_traits::cMax; }
      static inline bool get_comps_are_signed() { return component_traits::cSigned; }

      inline component_t operator[] (uint i) const { RG_ETC1_ASSERT(i < cNumComps); return c[i]; }
      inline component_t& operator[] (uint i) { RG_ETC1_ASSERT(i < cNumComps); return c[i]; }

      inline color_quad_u8& set_component(uint i, parameter_t f)
      {
         RG_ETC1_ASSERT(i < cNumComps);

         c[i] = static_cast<component_t>(clamp(f));

         return *this;
      }

      inline color_quad_u8& clamp(const color_quad_u8& l, const color_quad_u8& h)
      {
         for (uint i = 0; i < cNumComps; i++)
            c[i] = static_cast<component_t>(rg_etc1::clamp<parameter_t>(c[i], l[i], h[i]));
         return *this;
      }

      inline color_quad_u8& clamp(parameter_t l, parameter_t h)
      {
         for (uint i = 0; i < cNumComps; i++)
            c[i] = static_cast<component_t>(rg_etc1::clamp<parameter_t>(c[i], l, h));
         return *this;
      }

   }; // class color_quad_u8

   enum etc_constants
   {
      cETC1BytesPerBlock = 8U,

      cETC1SelectorBits = 2U,
      cETC1SelectorValues = 1U << cETC1SelectorBits,
      cETC1SelectorMask = cETC1SelectorValues - 1U,

      cETC1BlockShift = 2U,
      cETC1BlockSize = 1U << cETC1BlockShift,

      cETC1LSBSelectorIndicesBitOffset = 0,
      cETC1MSBSelectorIndicesBitOffset = 16,

      cETC1FlipBitOffset = 32,
      cETC1DiffBitOffset = 33,

      cETC1IntenModifierNumBits = 3,
      cETC1IntenModifierValues = 1 << cETC1IntenModifierNumBits,
      cETC1RightIntenModifierTableBitOffset = 34,
      cETC1LeftIntenModifierTableBitOffset = 37,

      // Base+Delta encoding (5 bit bases, 3 bit delta)
      cETC1BaseColorCompNumBits = 5,
      cETC1BaseColorCompMax = 1 << cETC1BaseColorCompNumBits,

      cETC1DeltaColorCompNumBits = 3,
      cETC1DeltaColorComp = 1 << cETC1DeltaColorCompNumBits,
      cETC1DeltaColorCompMax = 1 << cETC1DeltaColorCompNumBits,

      cETC1BaseColor5RBitOffset = 59,
      cETC1BaseColor5GBitOffset = 51,
      cETC1BaseColor5BBitOffset = 43,

      cETC1DeltaColor3RBitOffset = 56,
      cETC1DeltaColor3GBitOffset = 48,
      cETC1DeltaColor3BBitOffset = 40,

      cETC1AbsColor4R1BitOffset = 60,
      cETC1AbsColor4G1BitOffset = 52,
      cETC1AbsColor4B1BitOffset = 44,

      cETC1AbsColor4R2BitOffset = 56,
      cETC1AbsColor4G2BitOffset = 48,
      cETC1AbsColor4B2BitOffset = 40,

      // Delta3:
      // 0   1   2   3   4   5   6   7
      // 000 001 010 011 100 101 110 111
      // 0   1   2   3   -4  -3  -2  -1
   };
   
   static uint8 g_quant5_tab[256+16];

   static const int g_etc1_inten_tables[cETC1IntenModifierValues][cETC1SelectorValues] = 
   { 
      { -8,  -2,   2,   8 }, { -17,  -5,  5,  17 }, { -29,  -9,   9,  29 }, {  -42, -13, 13,  42 }, 
      { -60, -18, 18,  60 }, { -80, -24, 24,  80 }, { -106, -33, 33, 106 }, { -183, -47, 47, 183 } 
   };

   static const uint8 g_etc1_to_selector_index[cETC1SelectorValues] = { 2, 3, 1, 0 };
      
   struct etc1_block
   {
      // big endian uint64:
      // bit ofs:  56  48  40  32  24  16   8   0
      // byte ofs: b0, b1, b2, b3, b4, b5, b6, b7 
      union 
      {
         uint64 m_uint64;
         uint8 m_bytes[8];
      };

      uint8 m_low_color[2];
      uint8 m_high_color[2];

      enum { cNumSelectorBytes = 4 };
      uint8 m_selectors[cNumSelectorBytes];

      inline uint get_byte_bits(uint ofs, uint num) const
      {
         RG_ETC1_ASSERT((ofs + num) <= 64U);
         RG_ETC1_ASSERT(num && (num <= 8U));
         RG_ETC1_ASSERT((ofs >> 3) == ((ofs + num - 1) >> 3));
         const uint byte_ofs = 7 - (ofs >> 3);
         const uint byte_bit_ofs = ofs & 7;
         return (m_bytes[byte_ofs] >> byte_bit_ofs) & ((1 << num) - 1);
      }

      // false = left/right subblocks
      // true = upper/lower subblocks
      inline bool get_flip_bit() const 
      {
         return (m_bytes[3] & 1) != 0;
      }   

      inline bool get_diff_bit() const
      {
         return (m_bytes[3] & 2) != 0;
      }

      // Returns intensity modifier table (0-7) used by subblock subblock_id.
      // subblock_id=0 left/top (CW 1), 1=right/bottom (CW 2)
      inline uint get_inten_table(uint subblock_id) const
      {
         RG_ETC1_ASSERT(subblock_id < 2);
         const uint ofs = subblock_id ? 2 : 5;
         return (m_bytes[3] >> ofs) & 7;
      }

      // Returned selector value ranges from 0-3 and is a direct index into g_etc1_inten_tables.
      inline uint get_selector(uint x, uint y) const
      {
         RG_ETC1_ASSERT((x | y) < 4);

         const uint bit_index = x * 4 + y;
         const uint byte_bit_ofs = bit_index & 7;
         const uint8 *p = &m_bytes[7 - (bit_index >> 3)];
         const uint lsb = (p[0] >> byte_bit_ofs) & 1;
         const uint msb = (p[-2] >> byte_bit_ofs) & 1;
         const uint val = lsb | (msb << 1);

         return g_etc1_to_selector_index[val];
      }

      inline uint16 get_base4_color(uint idx) const
      {
         uint r, g, b;
         if (idx)
         {
            r = get_byte_bits(cETC1AbsColor4R2BitOffset, 4);
            g = get_byte_bits(cETC1AbsColor4G2BitOffset, 4);
            b = get_byte_bits(cETC1AbsColor4B2BitOffset, 4);
         }
         else
         {
            r = get_byte_bits(cETC1AbsColor4R1BitOffset, 4);
            g = get_byte_bits(cETC1AbsColor4G1BitOffset, 4);
            b = get_byte_bits(cETC1AbsColor4B1BitOffset, 4);
         }
         return static_cast<uint16>(b | (g << 4U) | (r << 8U));
      }

      inline uint16 get_base5_color() const
      {
         const uint r = get_byte_bits(cETC1BaseColor5RBitOffset, 5);
         const uint g = get_byte_bits(cETC1BaseColor5GBitOffset, 5);
         const uint b = get_byte_bits(cETC1BaseColor5BBitOffset, 5);
         return static_cast<uint16>(b | (g << 5U) | (r << 10U));
      }

      inline uint16 get_delta3_color() const
      {
         const uint r = get_byte_bits(cETC1DeltaColor3RBitOffset, 3);
         const uint g = get_byte_bits(cETC1DeltaColor3GBitOffset, 3);
         const uint b = get_byte_bits(cETC1DeltaColor3BBitOffset, 3);
         return static_cast<uint16>(b | (g << 3U) | (r << 6U));
      }

      static color_quad_u8 unpack_color5(uint16 packed_color5, bool scaled, uint alpha = 255U);
      static void unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color, bool scaled);

      static bool unpack_color5(color_quad_u8& result, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha = 255U);
      static bool unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha = 255U);

      // Results range from -4 to 3 (cETC1ColorDeltaMin to cETC1ColorDeltaMax)
      static void unpack_delta3(int& r, int& g, int& b, uint16 packed_delta3);

      static color_quad_u8 unpack_color4(uint16 packed_color4, bool scaled, uint alpha = 255U);
      static void unpack_color4(uint& r, uint& g, uint& b, uint16 packed_color4, bool scaled);

      // subblock colors
      static void get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint table_idx);
      static bool get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint16 packed_delta3, uint table_idx);
      static void get_abs_subblock_colors(color_quad_u8* pDst, uint16 packed_color4, uint table_idx);

      static inline void unscaled_to_scaled_color(color_quad_u8& dst, const color_quad_u8& src, bool color4)
      {
         if (color4)
         {
            dst.r = src.r | (src.r << 4);
            dst.g = src.g | (src.g << 4);
            dst.b = src.b | (src.b << 4);
         }
         else
         {
            dst.r = (src.r >> 2) | (src.r << 3);
            dst.g = (src.g >> 2) | (src.g << 3);
            dst.b = (src.b >> 2) | (src.b << 3);
         }
         dst.a = src.a;
      }
   };

#undef RG_ETC1_GET_KEY
#undef RG_ETC1_GET_KEY_FROM_INDEX

   color_quad_u8 etc1_block::unpack_color5(uint16 packed_color5, bool scaled, uint alpha)
   {
      uint b = packed_color5 & 31U;
      uint g = (packed_color5 >> 5U) & 31U;
      uint r = (packed_color5 >> 10U) & 31U;

      if (scaled)
      {
         b = (b << 3U) | (b >> 2U);
         g = (g << 3U) | (g >> 2U);
         r = (r << 3U) | (r >> 2U);
      }

      return color_quad_u8(cNoClamp, r, g, b, rg_etc1::minimum(alpha, 255U));
   }

   void etc1_block::unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color5, bool scaled)
   {
      color_quad_u8 c(unpack_color5(packed_color5, scaled, 0));
      r = c.r;
      g = c.g;
      b = c.b;
   }

   bool etc1_block::unpack_color5(color_quad_u8& result, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha)
   {
      int dc_r, dc_g, dc_b;
      unpack_delta3(dc_r, dc_g, dc_b, packed_delta3);
      
      int b = (packed_color5 & 31U) + dc_b;
      int g = ((packed_color5 >> 5U) & 31U) + dc_g;
      int r = ((packed_color5 >> 10U) & 31U) + dc_r;

      bool success = true;
      if (static_cast<uint>(r | g | b) > 31U)
      {
         success = false;
         r = rg_etc1::clamp<int>(r, 0, 31);
         g = rg_etc1::clamp<int>(g, 0, 31);
         b = rg_etc1::clamp<int>(b, 0, 31);
      }

      if (scaled)
      {
         b = (b << 3U) | (b >> 2U);
         g = (g << 3U) | (g >> 2U);
         r = (r << 3U) | (r >> 2U);
      }

      result.set_noclamp_rgba(r, g, b, rg_etc1::minimum(alpha, 255U));
      return success;
   }

   bool etc1_block::unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha)
   {
      color_quad_u8 result;
      const bool success = unpack_color5(result, packed_color5, packed_delta3, scaled, alpha);
      r = result.r;
      g = result.g;
      b = result.b;
      return success;
   }
     
   void etc1_block::unpack_delta3(int& r, int& g, int& b, uint16 packed_delta3)
   {
      r = (packed_delta3 >> 6) & 7;
      g = (packed_delta3 >> 3) & 7;
      b = packed_delta3 & 7;
      if (r >= 4) r -= 8;
      if (g >= 4) g -= 8;
      if (b >= 4) b -= 8;
   }

   color_quad_u8 etc1_block::unpack_color4(uint16 packed_color4, bool scaled, uint alpha)
   {
      uint b = packed_color4 & 15U;
      uint g = (packed_color4 >> 4U) & 15U;
      uint r = (packed_color4 >> 8U) & 15U;

      if (scaled)
      {
         b = (b << 4U) | b;
         g = (g << 4U) | g;
         r = (r << 4U) | r;
      }

      return color_quad_u8(cNoClamp, r, g, b, rg_etc1::minimum(alpha, 255U));
   }
   
   void etc1_block::unpack_color4(uint& r, uint& g, uint& b, uint16 packed_color4, bool scaled)
   {
      color_quad_u8 c(unpack_color4(packed_color4, scaled, 0));
      r = c.r;
      g = c.g;
      b = c.b;
   }

   void etc1_block::get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint table_idx)
   {
      RG_ETC1_ASSERT(table_idx < cETC1IntenModifierValues);
      const int *pInten_modifer_table = &g_etc1_inten_tables[table_idx][0];

      uint r, g, b;
      unpack_color5(r, g, b, packed_color5, true);

      const int ir = static_cast<int>(r), ig = static_cast<int>(g), ib = static_cast<int>(b);

      const int y0 = pInten_modifer_table[0];
      pDst[0].set(ir + y0, ig + y0, ib + y0);

      const int y1 = pInten_modifer_table[1];
      pDst[1].set(ir + y1, ig + y1, ib + y1);

      const int y2 = pInten_modifer_table[2];
      pDst[2].set(ir + y2, ig + y2, ib + y2);

      const int y3 = pInten_modifer_table[3];
      pDst[3].set(ir + y3, ig + y3, ib + y3);
   }
   
   bool etc1_block::get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint16 packed_delta3, uint table_idx)
   {
      RG_ETC1_ASSERT(table_idx < cETC1IntenModifierValues);
      const int *pInten_modifer_table = &g_etc1_inten_tables[table_idx][0];

      uint r, g, b;
      bool success = unpack_color5(r, g, b, packed_color5, packed_delta3, true);

      const int ir = static_cast<int>(r), ig = static_cast<int>(g), ib = static_cast<int>(b);

      const int y0 = pInten_modifer_table[0];
      pDst[0].set(ir + y0, ig + y0, ib + y0);

      const int y1 = pInten_modifer_table[1];
      pDst[1].set(ir + y1, ig + y1, ib + y1);

      const int y2 = pInten_modifer_table[2];
      pDst[2].set(ir + y2, ig + y2, ib + y2);

      const int y3 = pInten_modifer_table[3];
      pDst[3].set(ir + y3, ig + y3, ib + y3);

      return success;
   }
   
   void etc1_block::get_abs_subblock_colors(color_quad_u8* pDst, uint16 packed_color4, uint table_idx)
   {
      RG_ETC1_ASSERT(table_idx < cETC1IntenModifierValues);
      const int *pInten_modifer_table = &g_etc1_inten_tables[table_idx][0];

      uint r, g, b;
      unpack_color4(r, g, b, packed_color4, true);
      
      const int ir = static_cast<int>(r), ig = static_cast<int>(g), ib = static_cast<int>(b);

      const int y0 = pInten_modifer_table[0];
      pDst[0].set(ir + y0, ig + y0, ib + y0);
      
      const int y1 = pInten_modifer_table[1];
      pDst[1].set(ir + y1, ig + y1, ib + y1);

      const int y2 = pInten_modifer_table[2];
      pDst[2].set(ir + y2, ig + y2, ib + y2);

      const int y3 = pInten_modifer_table[3];
      pDst[3].set(ir + y3, ig + y3, ib + y3);
   }
      
   bool unpack_etc1_block(const void* pETC1_block, unsigned int* pDst_pixels_rgba, bool preserve_alpha)
   {
      color_quad_u8* pDst = reinterpret_cast<color_quad_u8*>(pDst_pixels_rgba);
      const etc1_block& block = *static_cast<const etc1_block*>(pETC1_block);

      const bool diff_flag = block.get_diff_bit();
      const bool flip_flag = block.get_flip_bit();
      const uint table_index0 = block.get_inten_table(0);
      const uint table_index1 = block.get_inten_table(1);

      color_quad_u8 subblock_colors0[4];
      color_quad_u8 subblock_colors1[4];
      bool success = true;

      if (diff_flag)
      {
         const uint16 base_color5 = block.get_base5_color();
         const uint16 delta_color3 = block.get_delta3_color();
         etc1_block::get_diff_subblock_colors(subblock_colors0, base_color5, table_index0);
            
         if (!etc1_block::get_diff_subblock_colors(subblock_colors1, base_color5, delta_color3, table_index1))
            success = false;
      }
      else
      {
         const uint16 base_color4_0 = block.get_base4_color(0);
         etc1_block::get_abs_subblock_colors(subblock_colors0, base_color4_0, table_index0);

         const uint16 base_color4_1 = block.get_base4_color(1);
         etc1_block::get_abs_subblock_colors(subblock_colors1, base_color4_1, table_index1);
      }

      if (preserve_alpha)
      {
         if (flip_flag)
         {
            for (uint y = 0; y < 2; y++)
            {
               pDst[0].set_rgb(subblock_colors0[block.get_selector(0, y)]);
               pDst[1].set_rgb(subblock_colors0[block.get_selector(1, y)]);
               pDst[2].set_rgb(subblock_colors0[block.get_selector(2, y)]);
               pDst[3].set_rgb(subblock_colors0[block.get_selector(3, y)]);
               pDst += 4;
            }

            for (uint y = 2; y < 4; y++)
            {
               pDst[0].set_rgb(subblock_colors1[block.get_selector(0, y)]);
               pDst[1].set_rgb(subblock_colors1[block.get_selector(1, y)]);
               pDst[2].set_rgb(subblock_colors1[block.get_selector(2, y)]);
               pDst[3].set_rgb(subblock_colors1[block.get_selector(3, y)]);
               pDst += 4;
            }
         }
         else
         {
            for (uint y = 0; y < 4; y++)
            {
               pDst[0].set_rgb(subblock_colors0[block.get_selector(0, y)]);
               pDst[1].set_rgb(subblock_colors0[block.get_selector(1, y)]);
               pDst[2].set_rgb(subblock_colors1[block.get_selector(2, y)]);
               pDst[3].set_rgb(subblock_colors1[block.get_selector(3, y)]);
               pDst += 4;
            }
         }
      }
      else 
      {
         if (flip_flag)
         {
            // 0000
            // 0000
            // 1111
            // 1111
            for (uint y = 0; y < 2; y++)
            {
               pDst[0] = subblock_colors0[block.get_selector(0, y)];
               pDst[1] = subblock_colors0[block.get_selector(1, y)];
               pDst[2] = subblock_colors0[block.get_selector(2, y)];
               pDst[3] = subblock_colors0[block.get_selector(3, y)];
               pDst += 4;
            }

            for (uint y = 2; y < 4; y++)
            {
               pDst[0] = subblock_colors1[block.get_selector(0, y)];
               pDst[1] = subblock_colors1[block.get_selector(1, y)];
               pDst[2] = subblock_colors1[block.get_selector(2, y)];
               pDst[3] = subblock_colors1[block.get_selector(3, y)];
               pDst += 4;
            }
         }
         else
         {
            // 0011
            // 0011
            // 0011
            // 0011
            for (uint y = 0; y < 4; y++)
            {
               pDst[0] = subblock_colors0[block.get_selector(0, y)];
               pDst[1] = subblock_colors0[block.get_selector(1, y)];
               pDst[2] = subblock_colors1[block.get_selector(2, y)];
               pDst[3] = subblock_colors1[block.get_selector(3, y)];
               pDst += 4;
            }
         }
      }
      
      return success;
   }
         
} // namespace rg_etc1

void CKLBTextureAsset::onFirstReference()
{
	CKLBGridTextureObject* grid = m_pTextureState;
	u16 cellIndex = this - grid->m_cellTextures;
	CKLBGridTextureObject::CellState& cell = grid->m_cellStates[cellIndex];
	grid->unlinkCell(CKLBGridTextureObject::LIST_FREE, cellIndex);

	u16 oldHead = grid->m_listHeads[CKLBGridTextureObject::LIST_LIVE];
	cell.next = oldHead;
	cell.previous = NULL_IDX;
	cell.status.bytes.lifecycle &= 0xf;
	grid->m_listHeads[CKLBGridTextureObject::LIST_LIVE] = cellIndex;
	if (oldHead != NULL_IDX) {
		grid->m_cellStates[oldHead].previous = cellIndex;
	} else {
		grid->m_listTails[CKLBGridTextureObject::LIST_LIVE] = cellIndex;
	}
	grid->incrementRefCount();
}

void CKLBTextureAsset::onLastReference()
{
	m_pTextureState->releaseCell(this);
}

void CKLBGridTextureObject::activateCell(CKLBTextureAsset* texture)
{
	u16 cellIndex = texture - m_cellTextures;
	CellState& cell = m_cellStates[cellIndex];
	unlinkCell(LIST_FREE, cellIndex);

	u16 oldHead = m_listHeads[LIST_LIVE];
	cell.next = oldHead;
	cell.previous = NULL_IDX;
	cell.status.bytes.lifecycle &= 0xf;
	m_listHeads[LIST_LIVE] = cellIndex;
	if (oldHead != NULL_IDX) {
		m_cellStates[oldHead].previous = cellIndex;
	} else {
		m_listTails[LIST_LIVE] = cellIndex;
	}
	texture->m_pTextureState->incrementRefCount();
}

#include "jpeglib.h"
#include "png.h"
#include <setjmp.h>

struct GridTextureJpegError {
	jpeg_error_mgr	manager;
	jmp_buf			recovery;
};

void gridTextureJpegErrorExit(j_common_ptr decoder)
{
	GridTextureJpegError* error =
		reinterpret_cast<GridTextureJpegError*>(decoder->err);
	longjmp(error->recovery, 1);
}

void gridTextureJpegInitSource(j_decompress_ptr)
{
}

boolean gridTextureJpegFillInputBuffer(j_decompress_ptr)
{
	return TRUE;
}

void gridTextureJpegSkipInputData(j_decompress_ptr decoder, long byteCount)
{
	if (byteCount > 0) {
		decoder->src->next_input_byte += byteCount;
		decoder->src->bytes_in_buffer -= byteCount;
	}
}

void gridTextureJpegTermSource(j_decompress_ptr)
{
}

void gridTextureJpegSetSource(
	j_decompress_ptr decoder,
	const u8* data,
	size_t size
)
{
	if (!decoder->src) {
		decoder->src = reinterpret_cast<jpeg_source_mgr*>(
			decoder->mem->alloc_small(
				reinterpret_cast<j_common_ptr>(decoder),
				JPOOL_PERMANENT,
				sizeof(jpeg_source_mgr)
			)
		);
	}

	decoder->src->init_source = gridTextureJpegInitSource;
	decoder->src->fill_input_buffer = gridTextureJpegFillInputBuffer;
	decoder->src->skip_input_data = gridTextureJpegSkipInputData;
	decoder->src->resync_to_restart = jpeg_resync_to_restart;
	decoder->src->term_source = gridTextureJpegTermSource;
	decoder->src->bytes_in_buffer = size;
	decoder->src->next_input_byte = data;
}

struct GridTexturePngSource {
	const u8* cursor;
	u32 consumed;
	u32 remaining;
};

void gridTexturePngRead(
	png_structp decoder,
	png_bytep destination,
	u32 byteCount
)
{
	GridTexturePngSource* source =
		static_cast<GridTexturePngSource*>(png_get_progressive_ptr(decoder));
	if (source->remaining < byteCount) {
		png_error(decoder, "EOF");
	}

	for (u32 index = 0; index < byteCount; index++) {
		destination[index] = source->cursor[index];
	}
	source->cursor += byteCount;
	source->remaining -= byteCount;
}

bool decodeTextureImage(
	u8* source,
	u32 sourceSize,
	u32 expectedWidth,
	u32 expectedHeight,
	u8* pixels,
	u32 byteCount,
	u32* channelCount,
	TextureDecodeTarget* target
)
{
	png_structp decoder = NULL;
	png_infop information = NULL;
	*channelCount = 0;
	bool failed = true;
	if (!png_sig_cmp(source, 0, 8)) {
		decoder = png_create_read_struct(
			PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (decoder) {
			information = png_create_info_struct(decoder);
			if (information && !setjmp(png_jmpbuf(decoder))) {
				GridTexturePngSource input;
				input.cursor = source;
				input.remaining = sourceSize;
				input.consumed = 0;
				png_set_read_fn(
					decoder,
					&input,
					reinterpret_cast<png_rw_ptr>(gridTexturePngRead));
				png_read_info(decoder, information);

				const u32 width = png_get_image_width(decoder, information);
				const s32 height = png_get_image_height(decoder, information);
				png_bytep* rows = KLBNEWA(png_bytep, height);
				if ((!expectedWidth || width == expectedWidth) &&
					(!expectedHeight || height == expectedHeight)) {
					const u8 bitDepth =
						png_get_bit_depth(decoder, information);
					u32 channels =
						png_get_channels(decoder, information);
					const u8 colorType =
						png_get_color_type(decoder, information);
					bool grayscale;
					if (colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
						grayscale = true;
					} else if (colorType == PNG_COLOR_TYPE_PALETTE) {
						png_set_palette_to_rgb(decoder);
						channels = 3;
						grayscale = false;
					} else if (colorType == PNG_COLOR_TYPE_GRAY) {
						grayscale = true;
					} else {
						grayscale = false;
					}

					if (target) {
						target->width = width;
						target->height = height;
						target->channelCount = channels;
						if (target->allocate) {
							target->pixels = pixels;
							if (target->allocate(target)) {
								pixels = target->pixels;
								byteCount = target->byteCount;
							}
						}
					}

					const u32 requiredBytes = width * height * channels;
					bool invalidBuffer = false;
					if (byteCount) {
						if (requiredBytes > byteCount) {
							invalidBuffer = true;
						} else {
							invalidBuffer = grayscale;
						}
					} else {
						pixels = KLBNEWA(u8, requiredBytes);
					}

					if (!invalidBuffer) {
						if (png_get_valid(
							decoder, information, PNG_INFO_tRNS)) {
							png_set_tRNS_to_alpha(decoder);
							channels++;
						}
						if (bitDepth == 16) {
							png_set_strip_16(decoder);
						}
						png_set_interlace_handling(decoder);
						png_read_update_info(decoder, information);

						for (s32 row = 0; row < height; row++) {
							const u32 line = (u32)row;
							rows[line] = pixels + line * width * channels;
						}
						png_read_image(decoder, rows);
						png_read_end(decoder, information);
						*channelCount = channels;
						failed = false;
					}
				}
				KLBDELETEA(rows);
			}
		}
		png_destroy_read_struct(&decoder, &information, NULL);
	}
	return !failed;
}

CKLBImageAsset*
CKLBTextureAsset::createImageAlias(CKLBImageAsset* source, const char* aliasName)
{
	const char* copiedName = CKLBUtility::copyString(aliasName);
	CKLBImageAsset* image = source->clone();
	if (copiedName && image) {
		image->m_fileSource = copiedName;
		image->m_pOwnerTexture = this;
		image->m_pNextAlias = m_pAliasList;
		m_pAliasList = image;
	} else {
		KLBDELETE(image);
		KLBDELETEA(copiedName);
	}
	return image;
}

u16 CKLBGridTextureObject::registerTexture(CKLBGridTextureObject* texture)
{
	u32 count = s_gridTextureCount;
	u32 index = 0;
	for (; index < count; index++) {
		if (!s_gridTextures[index]) {
			s_gridTextures[index] = texture;
			return (u16)index;
		}
	}
	if (index <= 15) {
		s_gridTextures[index] = texture;
		s_gridTextureCount = count + 1;
	} else {
		klb_assertAlways("Reached maximum of 16 grid at the same time");
	}
	return (u16)count;
}

void CKLBGridTextureObject::releaseTextures()
{
	CKLBGridTextureObject** slot = s_gridTextures;
	for (u32 index = 0; index < s_gridTextureCount; slot++, index++) {
		if (*slot) {
			KLBDELETE(*slot);
			*slot = NULL;
		}
	}
	s_gridTextureCount = 0;
}

static void releaseGridTexturesOnPluginShutdown()
{
	u32 index = 0;
	CKLBGridTextureObject** slot = s_gridTextures;
	for (; index < s_gridTextureCount; index++, slot++) {
		if (*slot) {
			KLBDELETE(*slot);
			*slot = NULL;
		}
	}
	s_gridTextureCount = 0;
}

CKLBGridTextureObject* CKLBGridTextureObject::create(
	s32 cellWidth,
	s32 cellHeight,
	s32 cellCount,
	bool rgba,
	bool border
)
{
	CKLBGridTextureObject* texture = KLBNEW(CKLBGridTextureObject);
	g_gridTextureError = 0;
	if (texture && !texture->init(
			cellWidth,
			cellHeight,
			cellCount,
			rgba,
			border
	)) {
		KLBDELETE(texture);
		texture = NULL;
	}

	if (g_gridTextureError && !g_gridTextureFirstError) {
		g_gridTextureFirstError = g_gridTextureError;
	}
	return texture;
}

bool CKLBGridTextureObject::init(
	s32 cellWidth,
	s32 cellHeight,
	s32 cellCount,
	bool rgba,
	bool border
)
{
	// Width/height pairs, smallest capacity first.
	static const u32 textureSizes[] = {
		  64,   64,   128,   64,    64,  128,
		 256,   64,   128,  128,    64,  256,
		 512,   64,   256,  128,   128,  256,
		  64,  512,  1024,   64,   512,  128,
		 256,  256,   128,  512,    64, 1024,
		2048,   64,  1024,  128,   512,  256,
		 256,  512,   128, 1024,    64, 2048,
		2048,  128,  1024,  256,   512,  512,
		 256, 1024,   128, 2048,  2048,  256,
		1024,  512,   512, 1024,   256, 2048,
		2048,  512,  1024, 1024,   512, 2048,
		2048, 1024,  1024, 2048,  2048, 2048
	};

	klb_assertNull(rgba, "Please force grid to use alpha, 24 bit texture openGL driver are not working correctly on Android XPeria Z");
	klb_assert(cellWidth >= 1 && cellWidth <= 2048, "Grid width invalid");

	if (border) {
		m_hasBorder = true;
		cellWidth += 2;
		cellHeight += 2;
	}

	const u32 storedCellPixels = (u32)cellWidth * (u32)cellHeight;
	const u32 totalStoredPixels = storedCellPixels * (u32)cellCount;
	if (totalStoredPixels > 0x400000) {
		g_gridTextureError = 4;
		return false;
	}

	// The atlas-size table above is sorted by capacity, so a search can skip
	// straight past every entry too small to hold the requested cell count.
	u32 index;
	if (totalStoredPixels > 0x200000) {
		index = 70;
	} else if (totalStoredPixels > 0x100000) {
		index = 66;
	} else if (totalStoredPixels > 0x80000) {
		index = 60;
	} else if (totalStoredPixels > 0x40000) {
		index = 52;
	} else if (totalStoredPixels > 0x20000) {
		index = 42;
	} else if (totalStoredPixels > 0x10000) {
		index = 30;
	} else if (totalStoredPixels > 0x8000) {
		index = 20;
	} else if (totalStoredPixels > 0x4000) {
		index = 12;
	} else {
		index = (totalStoredPixels > 0x2000) ? 6 : 0;
	}
	for (; index != sizeof(textureSizes) / sizeof(textureSizes[0]); index += 2) {
		m_columnCount = (u16)(textureSizes[index] / (u32)cellWidth);
		m_rowCount = (u16)(textureSizes[index + 1] / (u32)cellHeight);
		if ((u32)m_columnCount * m_rowCount >= (u32)cellCount) {
			m_textureWidth = (u16)textureSizes[index];
			m_textureHeight = (u16)textureSizes[index + 1];
			break;
		}
	}
	if (index == sizeof(textureSizes) / sizeof(textureSizes[0])) {
		klb_assertAlways("Too many item for a 2048x2048 texture");
	}

	const u16 gridID = registerTexture(this);
	if (gridID == NULL_IDX) {
		return false;
	}
	m_gridID = gridID;

	m_quadIndices[0] = 0;
	m_quadIndices[1] = 1;
	m_quadIndices[2] = 3;
	m_quadIndices[3] = 1;
	m_quadIndices[4] = 2;
	m_quadIndices[5] = 3;

	const u32 cellCapacity = (u32)m_columnCount * m_rowCount;
	m_gridImages = KLBNEWA(CKLBImageAsset, cellCapacity);
	m_imagePointers = KLBNEWA(CKLBImageAsset*, cellCapacity);
	m_cellTextures = KLBNEWA(CKLBTextureAsset, cellCapacity);
	m_cellStates = KLBNEWA(CellState, cellCapacity);
	m_cellGeometry = KLBNEWA(float, cellCapacity * 16);
	m_channelCount = 4;

	CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();
	m_pTexture = renderer.createTexture(
		m_textureWidth,
		m_textureHeight,
		GL_UNSIGNED_BYTE,
		CKLBOGLWrapper::RGBA,
		NULL,
		(s32)m_textureWidth * m_textureHeight * m_channelCount
	);
	m_pTextureUsage = m_pTexture ? m_pTexture->createUsage() : NULL;

	m_uploadBuffer = KLBNEWA(u8, m_channelCount * storedCellPixels);
	m_decodeBuffer = KLBNEWA(u8, m_channelCount * storedCellPixels);

	if (!m_pTexture || !m_pTextureUsage || !m_gridImages ||
		!m_imagePointers || !m_cellTextures || !m_cellGeometry ||
		!m_cellStates) {
		g_gridTextureError = 6;
		return false;
	}

	const u32 textureWidth = m_textureWidth;
	const u32 textureHeight = m_textureHeight;
	const u32 borderPixels = m_hasBorder ? 1 : 0;
	m_cellWidth = (u16)(cellWidth - borderPixels * 2);
	m_cellHeight = (u16)(cellHeight - borderPixels * 2);

	const float uvScaleX = 1.0f / textureWidth;
	const float uvScaleY = 1.0f / textureHeight;
	const float floatCellWidth = (float)m_cellWidth;
	const float floatCellHeight = (float)m_cellHeight;
	const float floatStoredWidth = (float)(u32)cellWidth;
	const float floatStoredHeight = (float)(u32)cellHeight;
	const float floatBorder = (float)borderPixels;

	index = 0;
	for (s32 row = 0; row < m_rowCount; row++) {
		const float rowOffset =
			(float)row * floatStoredHeight + floatBorder;
		const float bottom = (floatCellHeight + rowOffset) * uvScaleY;
		const float top = rowOffset * uvScaleY;
		for (s32 column = 0; column < m_columnCount; column++, index++) {
			CellState& state = m_cellStates[index];
			state.previous = (u16)(index - 1);
			state.next = (u16)(index + 1);
			state.source = NULL;
			state.status.value = (u16)(LIST_RECLAIM << 4);

			CKLBTextureAsset& cellTexture = m_cellTextures[index];
			cellTexture.setNameDirect(NULL);
			cellTexture.m_totalVertexCount = 0;
			cellTexture.m_totalIndexCount = 0;
			cellTexture.m_width = 0;
			cellTexture.m_height = 0;
			cellTexture.m_type = 0;
			cellTexture.m_imageCount = 1;
			cellTexture.resetAssetID();
			cellTexture.m_refCountControlsResource = true;
			cellTexture.m_pTextureState = this;
			cellTexture.m_softTexture = NULL;
			cellTexture.m_bitmap = NULL;
			cellTexture.m_indexBufferTotal = NULL;
			cellTexture.m_floatBufferTotal = NULL;
			cellTexture.m_pTexture = m_pTexture;
			cellTexture.m_pTextureUsage = m_pTextureUsage;
			cellTexture.m_pImages = &m_imagePointers[index];
			cellTexture.m_bytePerPix = (u8)m_channelCount;

			m_imagePointers[index] = &m_gridImages[index];

			CKLBImageAsset& image = m_gridImages[index];
			image.setNameDirect(NULL);
			image.m_uiSubTileCount = 1;
			image.m_usageType |= CKLBImageAsset::IS_STANDARD_RECT;
			image.m_uiVertexCount = 4;
			image.m_uiIndexCount = 6;
			image.m_imageSize.m_iRight = (s16)m_cellWidth;
			image.m_imageSize.m_iBottom = (s16)m_cellHeight;
			image.m_imageSize.m_iLeft = 0;
			image.m_imageSize.m_iTop = 0;
			image.m_iCenterX = 0;
			image.m_iCenterY = 0;
			image.resetAssetID();
			image.m_pUVCoord = &m_cellGeometry[index * 16];
			image.m_pXYCoord = image.m_pUVCoord + 8;
			image.m_pIndex = m_quadIndices;
			image.m_pTextureAsset = &cellTexture;

			image.m_pXYCoord[0] = 0.0f;
			image.m_pXYCoord[1] = 0.0f;
			image.m_pXYCoord[2] = floatCellWidth;
			image.m_pXYCoord[3] = 0.0f;
			image.m_pXYCoord[4] = floatCellWidth;
			image.m_pXYCoord[5] = floatCellHeight;
			image.m_pXYCoord[6] = 0.0f;
			image.m_pXYCoord[7] = floatCellHeight;

			const float columnOffset =
				(float)column * floatStoredWidth + floatBorder;
			const float right = (floatCellWidth + columnOffset) * uvScaleX;
			const float left = columnOffset * uvScaleX;
			image.m_pUVCoord[0] = left;
			image.m_pUVCoord[1] = top;
			image.m_pUVCoord[2] = right;
			image.m_pUVCoord[3] = top;
			image.m_pUVCoord[4] = right;
			image.m_pUVCoord[5] = bottom;
			image.m_pUVCoord[6] = left;
			image.m_pUVCoord[7] = bottom;

			image.m_boundWidth = (float)m_cellWidth;
			image.m_boundHeight = (float)m_cellHeight;
		}
	}

	m_cellStates[0].previous = NULL_IDX;
	m_cellStates[cellCapacity - 1].next = NULL_IDX;

	m_listHeads[LIST_LIVE] = NULL_IDX;
	m_listTails[LIST_LIVE] = NULL_IDX;
	m_listHeads[LIST_FREE] = NULL_IDX;
	m_listTails[LIST_FREE] = NULL_IDX;
	m_listHeads[LIST_RECLAIM] = 0;
	m_listTails[LIST_RECLAIM] = (u16)(index - 1);
	return true;
}

namespace {
bool decodeGridJpeg(
	const u8* stream,
	u32 streamSize,
	u32 expectedWidth,
	u32 expectedHeight,
	u8* pixels,
	u32 capacity,
	u32* channelCount
)
{
	JSAMPROW rows[1];
	jpeg_decompress_struct decoder;
	GridTextureJpegError error;
	decoder.err = jpeg_std_error(&error.manager);
	error.manager.error_exit = gridTextureJpegErrorExit;
	if (setjmp(error.recovery)) {
		jpeg_destroy_decompress(&decoder);
		return true;
	}

	jpeg_create_decompress(&decoder);
	gridTextureJpegSetSource(&decoder, stream, streamSize);
	jpeg_read_header(&decoder, TRUE);
	jpeg_start_decompress(&decoder);

	const u32 sourceChannels = decoder.num_components;
	const u32 width = decoder.output_width;
	const u32 height = decoder.output_height;
	bool unsupported;
	if ((decoder.num_components == 1) || (decoder.num_components == 3)) {
		unsupported = false;
	} else {
		unsupported = true;
	}
	bool decoded = false;
	if (!expectedWidth || width == expectedWidth) {
		const u32 pixelCount = width * height;
		const u32 requiredCapacity = pixelCount * 3;
		if (!((expectedHeight && height != expectedHeight) ||
			  (requiredCapacity > capacity) ||
			  unsupported)) {
			const u32 rowBytes = width * sourceChannels;
			while (decoder.output_scanline < height) {
				rows[0] = pixels + decoder.output_scanline * rowBytes;
				jpeg_read_scanlines(&decoder, rows, 1);
			}
			if (sourceChannels == 1) {
				// Grey source: widen to RGB in place, from the tail so the
				// three-byte texel never overwrites an unread sample.
				const u8* sample = &pixels[pixelCount - 1];
				u8* texel = &pixels[pixelCount * 3 - 3];
				for (u32 i = 0; i < pixelCount; ++i) {
					const u8 grey = *sample--;
					texel[0] = grey;
					texel[1] = grey;
					texel[2] = grey;
					texel -= 3;
				}
			}
			*channelCount = 3;
			decoded = true;
		}
	}
	jpeg_finish_decompress(&decoder);
	jpeg_destroy_decompress(&decoder);
	return decoded;
}
}

bool CKLBGridTextureObject::updateCell(
	u32 column,
	u32 row,
	AssetGridSource* source
)
{
	u8* const* decodedSource;
	u32 decodedLength;
	u8 sourceChannels;
	bool decodeFailed;

	if (source->option >= 5) {
		u32 decodedChannels;
		bool decodeOk = false;
		if (source->option != 6) {
			if (source->option == 5) {
				decodedChannels = 4;
				if (decodeTextureImage(
						source->data,
						source->length,
						m_cellWidth,
						m_cellHeight,
						m_decodeBuffer,
						m_cellWidth * m_cellHeight * 4,
						&decodedChannels,
						NULL)) {
					decodeOk = true;
				} else {
					g_gridTextureError = -1;
				}
			}
		} else {
			decodedChannels = 3;
			if (decodeGridJpeg(
					source->data,
					source->length,
					m_cellWidth,
					m_cellHeight,
					m_decodeBuffer,
					m_cellWidth * m_cellHeight * 4,
					&decodedChannels)) {
				decodeOk = true;
			} else {
				g_gridTextureError = -1;
			}
		}
		if (decodeOk) {
			sourceChannels = decodedChannels;
			decodeFailed = false;
		} else {
			// Unknown or failed payload encoding: leave the cell blank.
			sourceChannels = m_channelCount;
			decodeFailed = true;
		}
		decodedSource = &m_decodeBuffer;
		decodedLength = sourceChannels * m_cellHeight * m_cellWidth;
	} else {
		decodedSource = &source->data;
		decodedLength = source->length;
		sourceChannels = source->option;
		decodeFailed = false;
	}

	const u8* decoded = *decodedSource;
	const u32 height = m_cellHeight;
	const u32 width = m_cellWidth;
	const u32 requiredLength = height * sourceChannels * width;
	const u8* uploadPixels = decoded;
	if (sourceChannels != m_channelCount) {
		if (requiredLength > decodedLength) {
			decodeFailed = true;
		} else if (sourceChannels > m_channelCount) {
			const u8* pixel = decoded;
			u8* destination = m_uploadBuffer;
			for (u32 index = 0; index < width * height; index++) {
				*destination++ = *pixel++;
				*destination++ = *pixel++;
				*destination++ = *pixel++;
				pixel++;
			}
		} else {
			const u8* pixel = decoded;
			u8* destination = m_uploadBuffer;
			for (u32 index = 0; index < width * height; index++) {
				*destination++ = *pixel++;
				*destination++ = *pixel++;
				*destination++ = *pixel++;
				*destination++ = 255;
			}
		}
		uploadPixels = m_uploadBuffer;
	} else if (requiredLength > decodedLength) {
		decodeFailed = true;
	}

	if (decodeFailed) {
		u8* blankTarget = const_cast<u8*>(uploadPixels);
		if (uploadPixels == source->data) {
			blankTarget = m_uploadBuffer;
		}
		const u32 blankLength =
			(u32)m_channelCount * ((u32)m_cellWidth * (u32)m_cellHeight);
		memset(blankTarget, 0, blankLength);
	}

	if (m_hasBorder) {
		const u32 channels = m_channelCount;
		const u32 borderedWidth = (u32)m_cellWidth + 2;
		const u32 destinationStride = borderedWidth * channels;
		const u32 sourceStride = (u32)m_cellWidth * channels;

		for (s32 y = (s32)m_cellHeight - 1; y >= 0; y--) {
			memcpy(
				m_uploadBuffer + (y + 1) * destinationStride + m_channelCount,
				uploadPixels + y * sourceStride,
				sourceStride);
		}
		for (u32 y = 0; y < m_cellHeight; y++) {
			u8* leftBorder = m_uploadBuffer + (y + 1) * destinationStride;
			memcpy(leftBorder, leftBorder + m_channelCount, m_channelCount);
			u8* rightBorder =
				m_uploadBuffer + (y + 2) * destinationStride - m_channelCount;
			memcpy(rightBorder, rightBorder - m_channelCount, m_channelCount);
		}
		memcpy(
			m_uploadBuffer,
			m_uploadBuffer + destinationStride,
			destinationStride);
		memcpy(
			m_uploadBuffer + (m_cellHeight + 1) * destinationStride,
			m_uploadBuffer + m_cellHeight * destinationStride,
			destinationStride);

		if (m_channelCount == 4) {
			u8* topRow = m_uploadBuffer;
			u8* bottomRow =
				topRow + ((u32)m_cellHeight + 1) * borderedWidth * channels;
			for (u32 offset = 0; offset < destinationStride; offset += 4) {
				topRow[offset + 3] = 0;
				bottomRow[offset + 3] = 0;
			}
			const u32 cellWidth = m_cellWidth;
			u8* borderedRow = m_uploadBuffer + destinationStride;
			for (u32 y = 0; y < m_cellHeight; y++) {
				borderedRow[3] = 0;
				borderedRow[cellWidth * 4 + 7] = 0;
				borderedRow += destinationStride;
			}
		}

		if (!decodeFailed) {
			const u32 uploadWidth = m_cellWidth + 2;
			const u32 uploadHeight = m_cellHeight + 2;
			return m_pTexture->updateTexture(
				column * uploadWidth,
				row * uploadHeight,
				uploadWidth,
				uploadHeight,
				m_uploadBuffer,
				uploadWidth * uploadHeight * m_channelCount);
		}
	} else if (!decodeFailed) {
		const u32 uploadWidth = m_cellWidth;
		const u32 uploadHeight = m_cellHeight;
		return m_pTexture->updateTexture(
			column * uploadWidth,
			row * uploadHeight,
			uploadWidth,
			uploadHeight,
			const_cast<u8*>(uploadPixels),
			uploadWidth * uploadHeight * m_channelCount);
	}
	return false;
}

u16 CKLBGridTextureObject::loadImage(
	AssetGridSource* source,
	const char* name
)
{
	CKLBAssetManager& manager = CKLBAssetManager::getInstance();
	if (manager.findAsset(name)) {
		return reloadImage(source, name);
	}

	u16 cellIndex = popFront(LIST_RECLAIM);
	if (cellIndex == NULL_IDX) {
		cellIndex = popBack(LIST_FREE);
		if (cellIndex == NULL_IDX) {
			g_gridTextureError = 3;
			klb_assertAlways("GRID TEXTURE FULL, IMAGE LOADING FAIL");
		}
		// Recycling a live cell: its texture still owns a manager slot.
		manager.freeAssetSlot(m_imagePointers[cellIndex]->getTexture()->getAssetID());
	}

	KLBDELETEA(m_imagePointers[cellIndex]->m_pName);
	KLBDELETEA(m_cellTextures[cellIndex].m_pName);

	m_cellTextures[cellIndex].setNameDirect(name);
	char imageName[1024];
	sprintf(imageName, "%s.imag", name);
	m_imagePointers[cellIndex]->setNameDirect(imageName);

	const u16 columnCount = m_columnCount;
	const u16 row = cellIndex / columnCount;
	const u32 column = (u32)cellIndex - (u32)columnCount * row;
	updateCell(column, row, source);

	manager.allocateAssetSlot(&m_cellTextures[cellIndex]);

	pushFront(LIST_FREE, cellIndex);
	return cellIndex | m_gridID;
}

u16 CKLBGridTextureObject::findImage(const char* name) const
{
	const CKLBAbstractAsset* asset =
		CKLBAssetManager::getInstance().findAsset(name);
	u16 assetID = NULL_IDX;
	if (asset) {
		assetID = const_cast<CKLBAbstractAsset*>(asset)->getAssetID();
		const u16 gridID = assetID >> 12;
		const u16 cellIndex = assetID & 0x0fff;
		if ((gridID != m_gridID) ||
			(cellIndex >= (u32)m_rowCount * m_columnCount) ||
			(&m_cellTextures[cellIndex] != asset)) {
			assetID = NULL_IDX;
		}
	}
	return assetID;
}

bool gridRequestCache(
	void* grid,
	AssetGridSource* source,
	const char* name
)
{
	g_gridTextureError = 0;
	if (grid) {
		CKLBGridTextureObject* texture =
			static_cast<CKLBGridTextureObject*>(grid);
		if (texture->findImage(name) != NULL_IDX) {
			return true;
		}
		return texture->loadImage(source, name) != NULL_IDX;
	}

	g_gridTextureError = 1;
	if (!g_gridTextureFirstError) {
		g_gridTextureFirstError = g_gridTextureError;
	}
	return false;
}

u32 gridLoadImage(
	void* grid,
	AssetGridSource* source,
	const char* name,
	bool reload
)
{
	g_gridTextureError = 0;
	CKLBGridTextureObject* texture =
		static_cast<CKLBGridTextureObject*>(grid);
	u32 assetID = NULL_IDX;
	if (texture) {
		if (reload) {
			assetID = texture->reloadImage(source, name);
		} else {
			assetID = texture->loadImage(source, name);
		}
	} else {
		g_gridTextureError = 1;
	}

	if (g_gridTextureError && !g_gridTextureFirstError) {
		g_gridTextureFirstError = g_gridTextureError;
	}
	return assetID;
}

u16 CKLBGridTextureObject::reloadImage(
	AssetGridSource* source,
	const char* name
)
{
	const CKLBAbstractAsset* asset =
		CKLBAssetManager::getInstance().findAsset(name);
	if (!asset) {
		return NULL_IDX;
	}

	const u16 assetID =
		const_cast<CKLBAbstractAsset*>(asset)->getAssetID();
	const u16 gridID = assetID >> 12;
	const u16 cellIndex = assetID & 0x0fff;
	if (gridID != m_gridID) {
		return NULL_IDX;
	}
	const u16 columnCount = m_columnCount;
	const u16 row = cellIndex / columnCount;
	const u32 column = (u32)cellIndex - (u32)columnCount * row;
	updateCell(column, row, source);
	return assetID;
}
