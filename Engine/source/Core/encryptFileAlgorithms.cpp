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
#include "encryptFile.h"

#ifdef KLB_ENCRYPT_FILE_ALGORITHMS_IMPLEMENTATION

void CDecryptBaseClass::decryptNone(void*, u32) {
}

void CDecryptBaseClass::decryptReservedA(void*, u32) {
}

void CDecryptBaseClass::decryptReservedB(void*, u32) {
}

void CDecryptBaseClass::gotoOffsetNone(u32) {
}

void CDecryptBaseClass::gotoOffsetReservedA(u32) {
}

void CDecryptBaseClass::gotoOffsetReservedB(u32) {
}

void CDecryptBaseClass::decryptLCG24(void* buffer, u32 length) {
	u8* bytes = static_cast<u8*>(buffer);
	u32 state = m_userCtx.m_primaryState;
	for (u32 index = 0; index < length; ++index) {
		bytes[index] ^= static_cast<u8>(state >> 24);
		state = state * m_primaryMultiplier + m_primaryIncrement;
	}
	m_userCtx.m_primaryState = state;
}

void CDecryptBaseClass::decryptLCG(void* buffer, u32 length) {
	u8* bytes = static_cast<u8*>(buffer);
	u32 state = m_userCtx.m_primaryState;
	for (u32 index = 0; index < length; ++index) {
		bytes[index] ^= static_cast<u8>(state >> m_primaryShift);
		state = state * m_primaryMultiplier + m_primaryIncrement;
	}
	m_userCtx.m_primaryState = state;
}

void CDecryptBaseClass::decryptFeedback(void* buffer, u32 length) {
	u8* bytes = static_cast<u8*>(buffer);
	u32 state = m_userCtx.m_primaryState;
	u8 feedback = 0x59;
	while (length) {
		u8 encrypted = *bytes;
		feedback ^= encrypted;
		*bytes = feedback;
		*bytes ^= static_cast<u8>(state >> m_primaryShift);
		state = state * m_primaryMultiplier + m_primaryIncrement;
		++bytes;
		--length;
		feedback = encrypted;
	}
	m_userCtx.m_primaryState = state;
}

/*!
    @brief  Repositioning is not supported for the feedback stream.

    The feedback stream mixes each output byte back into the next one, so the
    state at an arbitrary offset cannot be recomputed without replaying the
    whole stream from the beginning.
 */
void CDecryptBaseClass::gotoOffsetFeedback(u32) {
	klb_assertAlways("Can not do this operation (M5).");
}

void CDecryptBaseClass::decryptDualLCG(void* buffer, u32 length) {
	u8* bytes = static_cast<u8*>(buffer);
	u32 primary = m_userCtx.m_primaryState;
	u32 secondary = m_userCtx.m_secondaryState;
	for (u32 index = 0; index < length; ++index) {
		bytes[index] ^= static_cast<u8>(primary >> m_primaryShift);
		primary = primary * m_primaryMultiplier + m_primaryIncrement;
		bytes[index] ^= static_cast<u8>(secondary >> m_secondaryShift);
		secondary = secondary * m_secondaryMultiplier + m_secondaryIncrement;
	}
	m_userCtx.m_primaryState = primary;
	m_userCtx.m_secondaryState = secondary;
}

void CDecryptBaseClass::decryptDualLCGStride2(void* buffer, u32 length) {
	u8* bytes = static_cast<u8*>(buffer);
	u32 primary = m_userCtx.m_primaryState;
	u32 secondary = m_userCtx.m_secondaryState;
	for (u32 index = 0; index < length; index += 2) {
		bytes[index] ^= static_cast<u8>(primary >> m_primaryShift);
		primary = primary * m_primaryMultiplier + m_primaryIncrement;
		bytes[index] ^= static_cast<u8>(secondary >> m_secondaryShift);
		secondary = secondary * m_secondaryMultiplier + m_secondaryIncrement;
	}
	m_userCtx.m_primaryState = primary;
	m_userCtx.m_secondaryState = secondary;
}

static u32 advanceLCG(u32 initial, u32 multiplier, u32 increment, u32 count) {
	u32 accumulatedMultiplier = 1;
	u32 accumulatedIncrement = 0;
	while (static_cast<s32>(count) > 0) {
		if (count & 1) {
			accumulatedIncrement += increment * accumulatedMultiplier;
			accumulatedMultiplier *= multiplier;
		}
		u32 incrementProduct = increment * multiplier;
		increment += incrementProduct;
		multiplier *= multiplier;
		count = static_cast<u32>(static_cast<s32>(count) >> 1);
	}
	return accumulatedMultiplier * initial + accumulatedIncrement;
}

void CDecryptBaseClass::gotoOffsetLCG(u32 offset) {
	m_userCtx.m_primaryState = advanceLCG(
		m_userCtx.m_primaryInitialState,
		m_primaryMultiplier,
		m_primaryIncrement,
		offset);
}

void CDecryptBaseClass::gotoOffsetLCGVariant(u32 offset) {
	m_userCtx.m_primaryState = advanceLCG(
		m_userCtx.m_primaryInitialState,
		m_primaryMultiplier,
		m_primaryIncrement,
		offset);
}

void CDecryptBaseClass::gotoOffsetDualLCG(u32 offset) {
	u32 currentSecondaryIncrement;
	u32 primaryInitial = m_userCtx.m_primaryInitialState;
	u32 secondaryInitial = m_userCtx.m_secondaryInitialState;
	u32 primaryIncrement = 0;
	u32 primaryMultiplier = 1;
	u32 secondaryIncrement = 0;
	u32 secondaryMultiplier = 1;
	if (static_cast<s32>(offset) > 0) {
		currentSecondaryIncrement = m_secondaryIncrement;
		u32 currentPrimaryMultiplier = m_primaryMultiplier;
		u32 currentPrimaryIncrement = m_primaryIncrement;
		u32 currentSecondaryMultiplier = m_secondaryMultiplier;
		do {
			if (offset & 1) {
				primaryIncrement += currentPrimaryIncrement * primaryMultiplier;
				primaryMultiplier *= currentPrimaryMultiplier;
				secondaryIncrement += currentSecondaryIncrement * secondaryMultiplier;
				secondaryMultiplier *= currentSecondaryMultiplier;
			}
			u32 primaryProduct = currentPrimaryIncrement * currentPrimaryMultiplier;
			currentPrimaryIncrement += primaryProduct;
			currentPrimaryMultiplier *= currentPrimaryMultiplier;
			u32 secondaryProduct = currentSecondaryIncrement * currentSecondaryMultiplier;
			currentSecondaryIncrement += secondaryProduct;
			currentSecondaryMultiplier *= currentSecondaryMultiplier;
			offset = static_cast<u32>(static_cast<s32>(offset) >> 1);
		} while (static_cast<s32>(offset) > 0);
	}
	m_userCtx.m_primaryState = primaryMultiplier * primaryInitial + primaryIncrement;
	m_userCtx.m_secondaryState = secondaryMultiplier * secondaryInitial + secondaryIncrement;
}

// Layout of the header a newly encrypted asset is given: the four
// identifying bytes then an eight byte extended header, whose fourth byte is
// the variant the finish step dispatches on.
static const u8 s_writtenExtendedHeaderSize = 8;
static const u32 s_writtenHeaderSize = 12;
static const u8 s_writtenVariant = 1;

/*!
    @brief  Builds the header for an asset being written out encrypted.

    The header identifies itself the way a format 3 asset does, by carrying
    the complement of the first three work key bytes, and then asks for the
    single generator variant whose seed it also carries.

    @param[in]  ptr             asset path, NUL terminated
    @param[out] hdr             receives the header bytes
    @param[out] headerSize      receives the number of bytes written
 */
void CDecryptBaseClass::encryptSetup(const u8* ptr, u8* hdr, u32* headerSize) {
	u32 headerWord;
	u32 workWord;
	u32 primaryWord;
	u32 secondaryWord;
	u32 nameLength;
	NMAsset::hashAssetName(reinterpret_cast<const char*>(ptr), &headerWord,
		&workWord, &primaryWord, &secondaryWord, &nameLength);

	hdr[0] = static_cast<u8>(~workWord);
	hdr[1] = static_cast<u8>(~(workWord >> 8));
	hdr[2] = static_cast<u8>(~(workWord >> 16));
	hdr[3] = s_writtenExtendedHeaderSize;
	hdr[4] = 0;
	hdr[5] = 0;
	hdr[6] = 0;
	hdr[7] = s_writtenVariant;
	hdr[8] = static_cast<u8>(~hdr[0]);
	hdr[9] = static_cast<u8>(~hdr[1]);
	hdr[10] = static_cast<u8>(~hdr[2]);
	hdr[11] = s_writtenVariant;
	*headerSize = s_writtenHeaderSize;
}

/*!
    @brief  Identifies an asset header and publishes the keys it implies.

    Every key is derived from the digest of the asset's file name.  The header
    itself only says which of the three layouts is in use, and it does so by
    carrying the first four work key bytes either as they are (format 2) or
    complemented (format 3, whose fourth byte instead carries the size of the
    extended header).  Anything else is a format 1 asset, which has no header
    of its own at all and starts its key stream at a name dependent offset.

    @param[in]  hdr             first four header bytes of the asset
    @param[in]  path            asset path, NUL terminated
    @param[out] headerKey       key the header is matched against
    @param[out] workKey         key the stream ciphers consume
    @param[out] primaryKey      primary generator seed, formats 3 only
    @param[out] secondaryKey    secondary generator seed, format 3 only
    @param[out] streamOffset    key stream start offset, format 1 only
    @param[out] headerSize      number of header bytes the asset carries
    @return     the header format, 1, 2 or 3
 */
s32 NMAsset::parseAssetHeader(
	const u8* hdr,
	const char* path,
	u8* headerKey,
	u8* workKey,
	u8* primaryKey,
	u8* secondaryKey,
	u32* streamOffset,
	u16* headerSize)
{
	u32 headerWord;
	u32 workWord;
	u32 nameLength;
	u32 primaryWord;
	u32 secondaryWord;
	hashAssetName(path, &headerWord, &workWord, &primaryWord, &secondaryWord,
		&nameLength);

	workKey[3] = static_cast<u8>(workWord >> 24);
	workKey[2] = static_cast<u8>(workWord >> 16);
	workKey[1] = static_cast<u8>(workWord >> 8);
	workKey[0] = static_cast<u8>(workWord);

	headerKey[3] = static_cast<u8>(headerWord >> 24);
	headerKey[2] = static_cast<u8>(headerWord >> 16);
	headerKey[1] = static_cast<u8>(headerWord >> 8);
	headerKey[0] = static_cast<u8>(headerWord);

	if (hdr[0] == workKey[0] && hdr[1] == workKey[1]
		&& hdr[2] == workKey[2] && hdr[3] == workKey[3]) {
		*headerSize = 4;
		return 2;
	}

	if (hdr[0] == static_cast<u8>(~workKey[0])
		&& hdr[1] == static_cast<u8>(~workKey[1])
		&& hdr[2] == static_cast<u8>(~workKey[2])) {
		*headerSize = static_cast<u16>((hdr[3] & 0x3f) + 4);

		primaryKey[3] = static_cast<u8>(primaryWord >> 24);
		primaryKey[2] = static_cast<u8>(primaryWord >> 16);
		primaryKey[1] = static_cast<u8>(primaryWord >> 8);
		primaryKey[0] = static_cast<u8>(primaryWord);

		secondaryKey[3] = static_cast<u8>(secondaryWord >> 24);
		secondaryKey[2] = static_cast<u8>(secondaryWord >> 16);
		secondaryKey[1] = static_cast<u8>(secondaryWord >> 8);
		secondaryKey[0] = static_cast<u8>(secondaryWord);
		return 3;
	}

	nameLength = (nameLength & 0x3f) + 1;
	*streamOffset = nameLength;
	*headerSize = 0;
	return 1;
}

void CDecryptBaseClass::gotoOffsetDualLCGVariant(u32 offset) {
	u32 currentSecondaryIncrement;
	u32 primaryInitial = m_userCtx.m_primaryInitialState;
	u32 secondaryInitial = m_userCtx.m_secondaryInitialState;
	u32 primaryIncrement = 0;
	u32 primaryMultiplier = 1;
	u32 secondaryIncrement = 0;
	u32 secondaryMultiplier = 1;
	if (static_cast<s32>(offset) > 0) {
		currentSecondaryIncrement = m_secondaryIncrement;
		u32 currentPrimaryMultiplier = m_primaryMultiplier;
		u32 currentPrimaryIncrement = m_primaryIncrement;
		u32 currentSecondaryMultiplier = m_secondaryMultiplier;
		do {
			if (offset & 1) {
				primaryIncrement += currentPrimaryIncrement * primaryMultiplier;
				primaryMultiplier *= currentPrimaryMultiplier;
				secondaryIncrement += currentSecondaryIncrement * secondaryMultiplier;
				secondaryMultiplier *= currentSecondaryMultiplier;
			}
			u32 primaryProduct = currentPrimaryIncrement * currentPrimaryMultiplier;
			currentPrimaryIncrement += primaryProduct;
			currentPrimaryMultiplier *= currentPrimaryMultiplier;
			u32 secondaryProduct = currentSecondaryIncrement * currentSecondaryMultiplier;
			currentSecondaryIncrement += secondaryProduct;
			currentSecondaryMultiplier *= currentSecondaryMultiplier;
			offset = static_cast<u32>(static_cast<s32>(offset) >> 1);
		} while (static_cast<s32>(offset) > 0);
	}
	m_userCtx.m_primaryState = primaryMultiplier * primaryInitial + primaryIncrement;
	m_userCtx.m_secondaryState = secondaryMultiplier * secondaryInitial + secondaryIncrement;
}

#endif
