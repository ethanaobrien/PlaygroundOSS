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
#include "CKLBAsset.h"
#include "mem.h"
#include "Dictionnary.h"
#include "CPFInterface.h"
#include "CKLBUtility.h"
#include "CKLBScriptEnv.h"
#include "TextureManagement.h"
#include "CKLBDataHandler.h"
#include "CKLBDrawTask.h"
#include "CKLBTask.h"

extern void KLBUnregisterObjectName(void* object, const char* className);
extern void KLBRegisterObjectName(void* object, const char* className, int flags);

// --------------------------------------------------------------
//   Asset Manager
// --------------------------------------------------------------

CKLBAssetManager::CKLBAssetManager()
: m_pAssetDecoders  	(NULL)
, m_bIsInit         	(false)
, m_freeList        	(NULL_IDX)
, m_dictionnary     	(NULL)
, m_AsyncMode       	(false)
, m_ThreadWait      	(false)
, m_currAsset       	(NULL)
, m_currentLoadingFile	(NULL)
, m_maxAssetEntry   	(0)
, m_unloaded			(false)
, m_loadingNotFound		(false)
, m_assetNotFoundEnable	(true)
, m_keepLinkStream		(false)
, m_placeHolderAsset	(NULL)
, m_assetNotFoundCallback(NULL)
, m_lastAssetPath		(NULL)
, m_loadBuffer			(NULL)
, m_loadBufferSize		(0)
{
	KLBRegisterObjectName(this, "CKLBAssetManager", 0);
	memset32(m_arrayByCharCode, NULL, 256*sizeof(IKLBAssetPlugin*));
//	init();	// 2012.12.11  Reboot時に通らない為、外で明示的に呼びます
}

CKLBAssetManager::~CKLBAssetManager() 
{
	KLBUnregisterObjectName(this, "CKLBAssetManager");
	_release();
}

void
CKLBAssetManager::setAssetNotFoundEnable(bool enable)
{
	m_assetNotFoundEnable = enable;
}

void
CKLBAssetManager::setKeepLinkStream(bool enable)
{
	m_keepLinkStream = enable;
}

void
CKLBAssetManager::setLastNotFoundName(const char* asset)
{
	KLBDELETEA(m_lastAssetPath);
	m_lastAssetPath = NULL;
	m_lastAssetPath = CKLBUtility::copyString(asset);
}

const char*
CKLBAssetManager::getLastNotFoundName()
{
	return m_lastAssetPath;
}

u8*
CKLBAssetManager::getLoadBuffer(u32 size)
{
	if (m_loadBufferSize < size) {
		u32 allocationSize;
		if (m_loadBufferSize == 0 && size < 0x200000) {
			allocationSize = 0x200000;
		} else {
			allocationSize = size;
		}
		KLBDELETEA(m_loadBuffer);
		m_loadBuffer = KLBNEWA(u8, allocationSize);
		m_loadBufferSize = allocationSize;
	}
	return m_loadBuffer;
}

bool 
CKLBAssetManager::init(u32 maxAssetEntry, u32 dicoNodeMax) 
{
	klb_assertNull(dicoNodeMax   < 0xFFFF,  "Do not support more than 65534 nodes.");
	klb_assertNull(maxAssetEntry < 0xFFFF,  "Do not support more than 65534 assets.");

	m_maxAssetEntry	= maxAssetEntry;
	m_assetRecord	= KLBNEWA(SAssetEntry,m_maxAssetEntry);
	m_bIsInit		= m_assetRecord != NULL;
	if (m_assetRecord) {
		m_freeList	= 0;

		for (u32 n = 0; n < m_maxAssetEntry; n++) {
			m_assetRecord[n].m_next = n + 1;
			m_assetRecord[n].m_prev = n - 1;
			m_assetRecord[n].m_isFree = true;
		}

		// All entries are unused.
		m_assetRecord[0].m_prev					= NULL_IDX;
		m_assetRecord[m_maxAssetEntry-1].m_next	= NULL_IDX;

		m_dictionnary	= KLBNEWC(Dictionnary,(NULL,NULL));	// No callback, no callback context.
		if (m_dictionnary) {
			return m_dictionnary->init(dicoNodeMax);
		}
	}
	return false;
}

void 
CKLBAssetManager::registerAssetPlugIn(IKLBAssetPlugin* plugin) 
{
	if (plugin) {
		plugin->m_pNext		= this->m_pAssetDecoders;
		u8 charHeader = plugin->charHeader();
		klb_assert(charHeader, "plugin char header is zero");
		m_arrayByCharCode[charHeader] = plugin;
		m_pAssetDecoders	= plugin;
	} else {
		klb_assertAlways("null pointer");
	}
}

u16 
CKLBAssetManager::allocateAssetSlot(CKLBAbstractAsset* pAsset) 
{
	u16 freeIndex = m_freeList;
	if (freeIndex != NULL_IDX) {
		m_freeList							= m_assetRecord[freeIndex].m_next;
		m_assetRecord[m_freeList].m_prev	= NULL_IDX;	// Not necessary but cleaner.

		m_assetRecord[freeIndex].m_pAsset	= pAsset;
		m_assetRecord[freeIndex].m_isFree	= false;
		pAsset->m_assetID					= freeIndex;

		this->addSearchEntry(pAsset);
		pAsset->onRegisterSubAsset();

		return freeIndex;
	} else {
		klb_assertAlways("ressource slot full");
		return NULL_IDX;
	}
}

CKLBAbstractAsset* 
CKLBAssetManager::freeAssetSlot(u16 index) 
{
	if (index != NULL_IDX) {
		klb_assertNull(m_assetRecord[index].m_isFree == false, "already free");

		CKLBAbstractAsset* pAsset = m_assetRecord[index].m_pAsset;

		this->removeSearchEntry(pAsset->getName());

		pAsset->m_assetID	= NULL_IDX;
		pAsset->onUnregisterSubAsset();

		m_assetRecord[index].m_prev			= NULL_IDX;
		m_assetRecord[index].m_next			= m_freeList;
		m_assetRecord[index].m_isFree		= true;
		m_freeList							= index;
		return pAsset;
	} else {
		// Was not stored in a slot : sub assets.
		return NULL;
	}
}

void 
CKLBAssetManager::registerAsset(CKLBAbstractAsset* pAsset) 
{
	klb_assertNull(pAsset, "null pointer");
	pAsset->m_assetID	= allocateAssetSlot(pAsset);
}

bool
CKLBAssetManager::setPlaceHolder(const char* asset)
{
	m_placeHolderActive = true;
	KLBDELETEA(m_placeHolderAsset);
	m_placeHolderAsset = NULL;

	if (asset && strncmp(asset, "asset://", 8) == 0) {
		m_placeHolderAsset = (char*)CKLBUtility::copyString(asset);
		return m_placeHolderAsset != NULL;
	}
	return false;
}

bool
CKLBAssetManager::setAssetNotFoundHandler(const char* callback)
{
	KLBDELETEA(m_assetNotFoundCallback);
	m_assetNotFoundCallback = NULL;
	if (callback) {
		m_assetNotFoundCallback = CKLBUtility::copyString(callback);
		return m_assetNotFoundCallback != NULL;
	}
	return false;
}

void
CKLBAssetManager::_release()
{
	IKLBAssetPlugin* pList = m_pAssetDecoders;

	while (pList) {
		IKLBAssetPlugin* next = pList->m_pNext;
		KLBDELETE(pList);
		pList = next;
	}

	m_pAssetDecoders = NULL;

	if (m_assetRecord) {
#if defined(DEBUG_MEMORY)
		for (int n = 0; n < m_maxAssetEntry; n++) {
			if (m_assetRecord[n].m_isFree == false) {
				klb_assertAlways("Asset Leak : %s.", m_assetRecord[n].m_pAsset->m_pName);
			}
		}
#endif
		KLBDELETEA(m_assetRecord);
		m_assetRecord = NULL;
	}

	KLBDELETEA(m_loadBuffer);
	m_loadBuffer = NULL;
	m_loadBufferSize = 0;

	KLBDELETEA(m_placeHolderAsset);
	m_placeHolderAsset = NULL;
	KLBDELETEA(m_assetNotFoundCallback);
	m_assetNotFoundCallback = NULL;
	KLBDELETEA(m_lastAssetPath);
	m_lastAssetPath = NULL;

	KLBDELETE(m_dictionnary);
	m_dictionnary = NULL;
}

bool 
CKLBAssetManager::isAsyncLoading() 
{
	return m_AsyncMode;	
}

void 
CKLBAssetManager::setAsyncLoading(bool mode) 
{
	m_AsyncMode = mode;
}

void 
CKLBAssetManager::setCurrentAsyncAsset(CKLBAssetManager::SAsset* pCurrAsset) 
{
	m_currAsset = pCurrAsset;
}

CKLBAssetManager::SAsset* 
CKLBAssetManager::getCurrentAsyncAsset() 
{
	return m_currAsset;
}

void 
CKLBAssetManager::setMainThreadTexture(CKLBTextureAsset* pTexAsset, GLenum pixelFormat, CKLBOGLWrapper::TEX_CHANNEL channel, u32 opt, u32 textureSize) 
{
	SAsset* pAss		= getCurrentAsyncAsset();
	pAss->pTexAsset		= pTexAsset;
	pAss->pixelFormat	= pixelFormat;
	pAss->channel		= channel;
	pAss->opt			= opt;
	pAss->processByMainThread	= false;
	pAss->textureSize	= textureSize;

	m_ThreadWait = true;
	while (pAss->processByMainThread == false) {
		// Thread Wait for Main Task...
	}
	m_ThreadWait = false;
}

#include "CKLBTexturePacker.h"

void
CKLBAssetManager::unloadAsset() {
	if (!m_unloaded) {
		m_unloaded = true;
		for (u32 n=0; n < m_maxAssetEntry; n++) {
			if (!m_assetRecord[n].m_isFree) {
				if (m_assetRecord[n].m_pAsset) {
					m_assetRecord[n].m_pAsset->unloadRessource();
				}
			}
		}

		TexturePacker::getInstance().unloadSurface();
	}
}

void
CKLBAssetManager::restoreAsset() {
	if (m_unloaded) {
		m_unloaded = false;
		TexturePacker::getInstance().reloadSurfaces();

		for (u32 n=0; n < m_maxAssetEntry; n++) {
			if (!m_assetRecord[n].m_isFree) {
				if (m_assetRecord[n].m_pAsset) {
					if (m_assetRecord[n].m_pAsset->m_fileSource) {
						// We have the file of the original data
						// TEXTURE ONLY for now.
						// So waste of search time here...
						IKLBAssetPlugin* plg = this->m_pAssetDecoders;
						while (plg) {
							if (plg->charHeader() == 'T') {
								// Need to skip header also.
								plg->setReloadingAsset(m_assetRecord[n].m_pAsset);

								IPlatformRequest& pfif = CPFInterface::getInstance().platform();
								IReadStream * pReadStream = pfif.openReadStream(m_assetRecord[n].m_pAsset->m_fileSource, pfif.useEncryption(), 14);

								if (pReadStream && (pReadStream->getStatus() == IReadStream::NORMAL)) {
									int size = pReadStream->getSize() - pReadStream->getPosition();
									if (size) {
										size -= 8;
										pReadStream->readU32(); // Ignore Header
										pReadStream->readU32(); // Ignore Size
										u8* pBuffer = KLBNEWA(u8,size);
										if (pBuffer) {
											if (pReadStream->readBlock(pBuffer, size)) {
												plg->loadAsset(pBuffer, size);
											}
											KLBDELETEA(pBuffer);
										}
									}
								}

								if (pReadStream) {
									delete pReadStream;
								}

								plg->setReloadingAsset(NULL);
							}
							plg = plg->m_pNext;
						}
					}
				}
			}
		}
	}
}

CKLBAbstractAsset*
CKLBAssetManager::loadAssetByFileName(const char* fileName, IKLBAssetPlugin* plugin, bool noStream, bool asyncLoad) {
	checkAsync(asyncLoad);
	klb_assert(fileName, "Null file name");

	// Resolve the abstract asset name before opening a backing stream.
	// FileName to Abstract Asset Name
	// Search -> OK return asset
	// else openStream...

	// Asset loaded
	CKLBAbstractAsset* pAsset = NULL;
	if (strncmp(fileName, "asset://", 8) == 0) {
		const char* url = &fileName[8];//
		u16 entry = this->searchEntry(url);

		if (entry == NULL_IDX) {
			if (!m_loadingNotFound) {
				KLBDELETEA(m_lastAssetPath);
				m_lastAssetPath = NULL;
				m_lastAssetPath = CKLBUtility::copyString(url);
			}

			if (noStream) {
				const char* abstractName = CKLBUtility::copyString(url);
				if (abstractName) {
					pAsset = plugin->loadByFileName(fileName);	// Audio only for now
					if (pAsset) {
						pAsset->setDefaultImageName(NULL);
						pAsset->m_pNameBuff = abstractName;
						pAsset->m_pName		= abstractName;

						allocateAssetSlot(pAsset);
					} else {
						KLBDELETEA(abstractName);
					}
				}
			} else {
				//
				// Plugin Based, stream loading.
				//
				IPlatformRequest& pfif = CPFInterface::getInstance().platform();
				m_currentLoadingFile = fileName;
				IReadStream * pStream = pfif.openReadStream(fileName, pfif.useEncryption(), 14);
				bool bResult;
				if (pStream && pStream->getStatus() == IReadStream::NORMAL) {
					bResult = loadAssetStream(pStream, (CKLBAbstractAsset **)&pAsset, plugin, asyncLoad);
				} else if (pStream && pStream->getStatus() == IReadStream::NOT_FOUND
				        && handleAssetNotFound(fileName, &pAsset)) {
					bResult = true;
				} else {
					delete pStream;
					m_currentLoadingFile = NULL;
					if (m_assetNotFoundEnable) {
						klb_assertAlways("Asset loader fail file %s", fileName);
					}
					return 0;
				}
				m_currentLoadingFile = NULL;
				delete pStream;
				if(!bResult || !pAsset) {
					return NULL;
				}
			}
		} else {
			pAsset = m_assetRecord[entry].m_pAsset;
		}
	} else {
		/*
			Changing that policy could generate trouble :

			Texture A (internal)
			Texture B (external)
			may register some common 'abstract' asset name in the dictionnary with different images.

			Asset support loading in both system with priority on external and should fit all needs.

			The dictionary is shared by streamed and directly loaded assets.
			Accepting arbitrary paths here would make registration order
			select which backing resource owns a common abstract name.
			Restricting this entry point to asset URLs keeps that lookup
			policy deterministic for every plugin.

		 */
		klb_assertAlways("Asset loader is not supporting other path than asset:// (not %s) for now.", fileName);
	}

	if (m_loadingNotFound && pAsset) {
		pAsset = pAsset->clone();
	}
	return pAsset;
}

bool
CKLBAssetManager::loadAssetStream(IReadStream* pReadStream, CKLBAbstractAsset** ppAsset, IKLBAssetPlugin* plugIn, bool useAsync)
{
	checkAsync(useAsync);

	bool res = false;
	if (ppAsset) {
		*ppAsset = NULL; // Reset first in case of error.
	}
	
	if (pReadStream && (pReadStream->getStatus() == IReadStream::NORMAL)) {
		int size = pReadStream->getSize() - pReadStream->getPosition();
		if (size) {
			u8* pBuffer = getLoadBuffer(size);
			klb_assert(pBuffer, "null pointer");
			if (pReadStream->readBlock(pBuffer, size)) {
				res = loadAsset(pBuffer, size, ppAsset, plugIn,useAsync);
			}
		}
	} else if (m_assetNotFoundEnable) {
		klb_assertAlways("File not found or invalid stream");
	}

	return res;
};

void
CKLBAssetManager::checkAsync(bool asyncMode)
{
	if (asyncMode == m_AsyncMode) {
		//
		// Sync with Sync mode.
		// Async with Async mode.
		// Go through...
		//
	} else {
		// Trying to do a sync while async mode.
		// Trying to do an async mode while async not activated (never happens)
		while (asyncMode != m_AsyncMode) {
			// Main thread is locked until thread finishes.
			// --> Possible deadlock here, allow unmatching loading if threadWait Flag activated
			if (m_ThreadWait == true) {
				break;
			}
		}
	}
}

bool
CKLBAssetManager::handleAssetNotFound(const char* fileName, CKLBAbstractAsset** ppAsset)
{
	CKLBAbstractAsset* asset = NULL;
	if (m_assetNotFoundEnable && m_assetNotFoundCallback) {
		CKLBScriptEnv::getInstance().call_assetNotFound(m_assetNotFoundCallback, NULL, m_lastAssetPath, fileName);
		if (m_placeHolderAsset && !m_loadingNotFound) {
			m_loadingNotFound = true;
			asset = loadAssetByFileName(m_placeHolderAsset);
			if (asset) {
				asset->setDefaultImageName(m_placeHolderAsset + 8);
				if (!asset->m_fileSource) {
					asset->m_fileSource = CKLBUtility::copyString(fileName);
				}
			}
			m_loadingNotFound = false;
			*ppAsset = asset;
			return true;
		}
	}
	return false;
}

// =========================================================================
/* NO DICO
bool CKLBAbstractAsset::include(const char* name)
{
	return (m_pName && (strcmp(name, m_pName) == 0));
}*/

void
CKLBAssetManager::addSearchEntry(CKLBAbstractAsset* pAsset)
{
	m_dictionnary->add(pAsset->getName(),pAsset);
}

void
CKLBAssetManager::addSearchSubEntry(CKLBAbstractAsset* pAsset, const char* name)
{
	m_dictionnary->add(name, pAsset);
}

void
CKLBAssetManager::removeSearchEntry(const char* name)
{
	m_dictionnary->remove(name);
}

const CKLBAbstractAsset*
CKLBAssetManager::findAsset(const char* name)
{
	return static_cast<const CKLBAbstractAsset*>(m_dictionnary->find(name));
}

/**
 * Load an asset from a complete in-memory stream.
 *
 * LINK chunks resolve an existing asset; ordinary chunks use a decoder.
 * ppAsset receives the resolved or decoded asset when requested.
 * Successful decodes are registered with the manager.
 * useAsync must agree with the manager's active loading mode.
 */
bool
CKLBAssetManager::loadAsset(u8* stream, size_t streamSize, CKLBAbstractAsset** ppAsset, IKLBAssetPlugin* plugIn, bool useAsync)
{
	if (useAsync != m_AsyncMode) {
		while (useAsync != m_AsyncMode) {
			if (m_ThreadWait) {
				break;
			}
		}
	}

	bool noError = true;
	klb_assertNull(stream, "null pointer");
	klb_assertNull(streamSize >= 8, "too small for asset");

	u32 pluginType32 = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
	if (pluginType32 == CHUNK_TAG('L','I','N','K')) {
		stream += 8;
		//
		// Load another ressource instead.
		//

		u16 id = getAssetIDFromName((char*)stream, 0, 1 /*Force to not try loading on failure*/);
		if (id == NULL_IDX) {

			char fileName[500];
			sprintf(fileName, "asset://%s", stream);	// C String.
			IPlatformRequest& pf = CPFInterface::getInstance().platform();
			m_currentLoadingFile = fileName;
			IReadStream* pStream = pf.openReadStream(fileName, pf.useEncryption(), 14);
			if (pStream && pStream->getStatus() == IReadStream::NORMAL) {
				if (m_assetNotFoundEnable || m_keepLinkStream) {
					bool result = loadAssetStream(pStream, ppAsset, NULL, useAsync);
					delete pStream;	// DO NOT USE KLBDELETE : platform use "new"
					return result;
				}
				if (ppAsset) {
					CKLBFakeAsset* asset = KLBNEW(CKLBFakeAsset);
					asset->m_assetID = NULL_IDX - 1;
					*ppAsset = asset;
				}
				delete pStream;
				return true;
			}
			if (pStream) {
				if (!m_assetNotFoundEnable) {
					KLBDELETEA(m_lastAssetPath);
					m_lastAssetPath = NULL;
					m_lastAssetPath = CKLBUtility::copyString((const char*)stream);
				}
				delete pStream;
			}
			if (handleAssetNotFound(fileName, ppAsset)) {
				return true;
			}
			if (m_assetNotFoundEnable) {
				klb_assertAlways("No stream for file %s", fileName);
			}
			noError = false;
			streamSize = 0;
		} else {
			// Asset already exist : ID is not NULL_IDX
			if (ppAsset) {
				*ppAsset = this->getAsset(id);
			}
		}
		streamSize = 0;
		if (!noError) {
			// LATER RP : count the number of successfull asset in this stream.
			// As some resource have loaded anyway... keep running.

		}
	} else {
		if (plugIn == NULL) {
			//
			// Parse list of chunks.
			//
			while (streamSize >= 8) {
				pluginType32 = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
				stream		+= 4;
				streamSize	-= 4;
				u32 size	 = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
				stream		+= 4;
				streamSize	-= 4;

				IKLBAssetPlugin* plugIn = m_pAssetDecoders;
				while (plugIn) {
					if (plugIn->getChunkID() == pluginType32) {
						break;
					}
					plugIn = plugIn->m_pNext;
				}

				CKLBAbstractAsset* pAsset = NULL;
				if (plugIn) {
					plugIn->setCurrentFileName(m_currentLoadingFile);
					u16 id = getAssetIDFromName((char*)stream, 0, 1 /*Force to not try loading on failure*/);
					pAsset = plugIn->loadAsset(stream, size);
					klb_assert(pAsset,
						"could not load/allocate asset %s", m_currentLoadingFile);
				}
				klb_assert(plugIn,
					"plugin not found. streamSize = %d, pluginType32 = %d, isPlugInNULL = %s, path = %s",
					streamSize, pluginType32, plugIn ? "false" : "true", m_currentLoadingFile ? m_currentLoadingFile : "NULL");
				if (pAsset) {
					this->registerAsset(pAsset);
					if (ppAsset) {
						*ppAsset = pAsset;
					}
				} else {
					noError = false;
				}
				if (streamSize >= size) {
					streamSize -= size;
				} else {
					noError = false;
					break;
				}
			}
		} else {
			// A supplied plugin decodes the whole remaining stream directly.
			CKLBAbstractAsset* pAsset = plugIn->loadAsset(stream, streamSize);
			klb_assert(pAsset, "could not load/allocate asset");
			if (pAsset) {
				this->registerAsset(pAsset);
				if (ppAsset) {
					*ppAsset = pAsset;
				}
			} else {
				noError = false;
			}
			// Direct loading, reset
			streamSize = 0;
		}

	}

	klb_assertNull((streamSize == 0), "Chunk size does not match parsing, data remain at the end.");

	return noError;
}

void 
CKLBAssetManager::freeAsset(u16 assetID) 
{
	if (assetID != NULL_IDX) {
		CKLBAbstractAsset* pAsset = this->freeAssetSlot(assetID);
		KLBDELETE(pAsset);	// Also free memory slot if any or reference count.
	}
}

u16 
CKLBAssetManager::searchEntry(const char* name) 
{
    /* DICO */
	const void* ptr = m_dictionnary->find(name);
	CKLBAbstractAsset*	m_pAsset = (CKLBAbstractAsset*)ptr;
	return m_pAsset ? m_pAsset->getAssetID() : NULL_IDX;
	/* NO DICO
	int idx = 0;
	while (idx < MAX_ASSET_ENTRY) {
		if (m_assetRecord[idx].m_isFree == false) {
			if (m_assetRecord[idx].m_pAsset->include(name)) {
				return idx;
			}
		}
		idx++;
	}
	return NULL_IDX;*/
}

u16 
CKLBAssetManager::getAssetIDFromName(const char* name, char plugin, u32 retryCount, CKLBAbstractAsset** ppLoadedAsset)
{
retry:
	// Search all ressources for now.
	// if (plugin == 0) {
	
	u16 idx = this->searchEntry(name);
	if (ppLoadedAsset) {
		*ppLoadedAsset = NULL;
	}

	if (idx != NULL_IDX) {
		return idx;
	}
	// }

	if (retryCount == 0) {
		//
		// Load another ressource instead.
		//
		CKLBAbstractAsset* pAsset = NULL;
		char fileName[500];
		IKLBAssetPlugin* plug = plugin ? getPlugin(plugin) : NULL;
		sprintf(fileName, "asset://%s%s", name, plug ? plug->fileExtension() : "");
		IPlatformRequest& pf = CPFInterface::getInstance().platform();
		pf.addExtMsg("lastAssetPath", fileName, false);
		IReadStream* pStream = pf.openReadStream(fileName, pf.useEncryption(), 0x0e);
		if (pStream) {
			m_currentLoadingFile = fileName;
			if (loadAssetStream(pStream,&pAsset)) {
				delete pStream;	// Platform use new and not KLBNEW.
				if (pAsset == NULL) {
					retryCount++;
					goto retry;
				} else {
					if (ppLoadedAsset) {
						*ppLoadedAsset = pAsset;
					}
					return pAsset->getAssetID();
				}
			}
			delete pStream; // Platform use new and not KLBNEW.
		}
	}

	return NULL_IDX;
}

void
CKLBAssetManager::assetReplaceMarking(u16 assetID)
{
	// Just mark the asset.
	this->m_assetRecord[assetID].m_pAsset->m_marked = true;
}

bool 
CKLBAssetManager::assetReplaceNameMatch(CKLBAbstractAsset* oldAsset, CKLBAbstractAsset* newAsset) 
{
	if (oldAsset->m_marked && !newAsset->m_marked) {
		if ((oldAsset->m_pName) && (newAsset->m_pName)) {
			if (strcmp(oldAsset->m_pName,newAsset->m_pName) == 0) {
				return true;
			}
		}
	}
	return false;
}

bool 
CKLBAssetManager::isReplaceMarked(u16 assetID) 
{
	return this->m_assetRecord[assetID].m_pAsset->m_marked;
}

const char*
CKLBAssetManager::getAssetNameFromID(u16 assetID)
{
	if (assetID < this->m_maxAssetEntry) {
		klb_assertNull(this->m_assetRecord[assetID].m_isFree == false, "already free");
		return this->m_assetRecord[assetID].m_pAsset->m_pName;
	} else {
		klb_assertAlways( "invalid assetID");
	}
	return NULL;
}

void 
CKLBAssetManager::rollBackMarking(u16 assetID) 
{
	this->m_assetRecord[assetID].m_pAsset->m_marked = false;
}

void 
CKLBAssetManager::assetReplaceFinishDeletion(u16 assetID) 
{
	freeAsset(assetID);
}
// --------------------------------------------------------------
//   Abstract Asset
// --------------------------------------------------------------

CKLBAbstractAsset::CKLBAbstractAsset()
: m_fileSource	(NULL)
, m_pName		(NULL)
, m_pNameBuff	(NULL)
, m_assetID		(NULL_IDX)
, m_refCount	(0)
, m_marked		(false)
, m_refCountControlsResource(false)
{
}

CKLBAbstractAsset::~CKLBAbstractAsset()
{
	KLBDELETEA(m_fileSource);
	if (m_pNameBuff) {
		KLBDELETEA(m_pNameBuff);
		m_pNameBuff = NULL;
	}
}

void 
CKLBAbstractAsset::incrementRefCount() 
{
	m_refCount++;
	klb_assertNull((getAssetType() != ASSET_IMAGE),"Image Asset should never be using ref count");
	if(m_refCountControlsResource && m_refCount == 1) {
		onFirstReference();
	}
	// printf("Inc %8X Type:%8X Cnt:%i\n", this, this->getAssetType(), m_refCount);
}

bool 
CKLBAbstractAsset::decrementRefCount() 
{
	if(m_refCountControlsResource) {
		if(!m_refCount) {
			return false;
		}

		m_refCount--;
		if(m_refCount) {
			return false;
		}

		onLastReference();
		return true;
	}

	if (m_refCount) {
		m_refCount--;
	}

	// printf("Dec %8X Type:%8X Cnt:%i\n", this, this->getAssetType(), m_refCount);

	if (m_refCount == 0) {
		klb_assertNull((getAssetType() != ASSET_IMAGE),"Image Asset should never be using ref count");

		CKLBAssetManager& pMgr = CKLBAssetManager::getInstance();

		// Autoremoved from list and deleted pointer.
		if (m_assetID != NULL_IDX) {
			pMgr.freeAsset(m_assetID);
		} else {
			if(getClassID() == CLS_ASSETGRIDTEXTURE) {
				destroy();
			} else {
				KLBDELETE(this);
			}
		}

		return true;
	}

	return false;
}

char* 
CKLBAbstractAsset::allocateName(void* ptr, u32 size) 
{
	char* buf = KLBNEWA(char, size + 1);
	if (buf) {
		memcpy(buf,ptr,size + 1);
		this->m_pNameBuff	= buf;
		return &buf[1];
	}
	return NULL;
}

bool 
CKLBAbstractAsset::setNameDirect(const char* name) 
{
	if (name) {
		size_t size = strlen(name) + 1;
		char* buf = KLBNEWA(char, size);
		memcpy(buf,name,size);
		this->m_pNameBuff	= buf;
		this->m_pName		= buf;
	}
	return true;
}

// --------------------------------------------------------------
//   Asset Plugin
// --------------------------------------------------------------

IKLBAssetPlugin::IKLBAssetPlugin()
: m_pNext			(NULL)
, m_pReloadAsset	(NULL)
{
}

/*virtual*/ IKLBAssetPlugin::~IKLBAssetPlugin() {	
}
