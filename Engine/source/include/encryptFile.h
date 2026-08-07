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
#ifndef H_HEADER_PROTECT_ENCRYPTFILE_H
#define H_HEADER_PROTECT_ENCRYPTFILE_H

#include "BaseType.h"
#include "encryptUserContext.h"

namespace NMAsset {
	extern s32 g_nmAssetKeyLength;
	extern const char* g_assetFilterCallback;
}

// Releases the process-wide tables and key used by named-asset decryption.
void initNMAsset(u32 initializationMode);
void releaseNMAsset();

/*!
    @class  DecryptBaseClass
    @brief  複合化基礎クラス
 */
class CDecryptBaseClass {
friend void initNMAsset(u32 initializationMode);
public:
	SUserStruct	m_userCtx;
	u16		m_payloadOffset;
	u16		m_headerSize;
	bool		m_decrypt;
	u32		m_allowedFormats;
	u32		m_primaryMultiplier;
	u32		m_primaryIncrement;
	u8		m_primaryShift;
	u8		m_secondaryShift;
	u32		m_secondaryMultiplier;
	u32		m_secondaryIncrement;
	u32		m_schemeCounter;
	u16		m_transformIndex;
	u8		m_format;
	u8		m_useNew;
private:
	void		decrypt(void* ptr, u32 length);
	void		decryptNone(void* ptr, u32 length);
	void		decryptReservedA(void* ptr, u32 length);
	void		decryptReservedB(void* ptr, u32 length);
	void		decryptLCG24(void* ptr, u32 length);
	void		decryptLCG(void* ptr, u32 length);
	void		decryptFeedback(void* ptr, u32 length);
	void		decryptDualLCG(void* ptr, u32 length);
	void		decryptDualLCGStride2(void* ptr, u32 length);
	void		decryptCounterKey(void* ptr, u32 length);
	void		decryptLehmer(void* ptr, u32 length);
	void		gotoOffsetNone(u32 offset);
	void		gotoOffsetReservedA(u32 offset);
	void		gotoOffsetReservedB(u32 offset);
	void		gotoOffsetLCG(u32 offset);
	void		gotoOffsetLCGVariant(u32 offset);
	void		gotoOffsetFeedback(u32 offset);
	void		gotoOffsetDualLCG(u32 offset);
	void		gotoOffsetDualLCGVariant(u32 offset);
	void		gotoOffsetCounterKey(u32 offset);
	void		gotoOffsetLehmer(u32 offset);
public:
	CDecryptBaseClass(u32 allowedFormats = 0);
	void		decryptBlck(void* ptr, u32 length);

	void		encryptSetup(const u8* ptr, u8* hdr, u32* headerSize);
	void		decryptSetup(const u8* ptr, const u8* hdr, u32* headerSize);
	void		finishSetup(const u8* extendedHeader, const char* path);
	void		gotoOffset	(u32 offset);

	// Stream setup driven by the game script.  It is a member rather than a
	// free initializer because it is the one setup that talks to the script
	// state, which only friends of CLuaState may do.
	static void	initializeUserKeyed(CDecryptBaseClass* decryptor,
				const u8* extendedHeader, const char* path);

	u16		getHeaderSize() const { return m_headerSize; }
	bool		isUserEncrypted() const;
};

namespace NMAsset {
	// Root of every per-asset secret: the MD5 digest of a fixed prefix
	// followed by the file name part of an asset path.
	void hashAssetName(const char* path, u32* hash0, u32* hash1, u32* hash2,
		u32* hash3, u32* nameLength);

	// Identifies an asset header against the keys derived from its path and
	// publishes those keys.  Returns the header format, 1, 2 or 3.
	s32 parseAssetHeader(const u8* hdr, const char* path, u8* headerKey,
		u8* workKey, u8* primaryKey, u8* secondaryKey, u32* streamOffset,
		u16* headerSize);

	void initializeHeaderSeeded(CDecryptBaseClass*, const u8*, const char*);
	void initializeHeaderSeededInverted(CDecryptBaseClass*, const u8*, const char*);
	void initializeSchemeDispatch(CDecryptBaseClass*, const u8*, const char*);
	void initializeHeaderKeyed(CDecryptBaseClass*, const u8*, const char*);
	void initializePathKeyed(CDecryptBaseClass*, const u8*, const char*);
	void initializePathKeyedDual(CDecryptBaseClass*, const u8*, const char*);
	void initializeReservedA(CDecryptBaseClass*, const u8*, const char*);
	void initializeReservedB(CDecryptBaseClass*, const u8*, const char*);
}

#endif
