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
#include "FontRendering.h"

#include "CPFInterface.h"
#include "CKLBUtility.h"
#include "utf8.h"

// Prototype to avoid warning.
void test();

class FntDebug {
public:
	static void check();
private:
	static void checkTreeRec(u32 depth, u32 idx, u32 code);
};

// ==========================================================================================
//   Global Static Variables
// ==========================================================================================

/*static*/ CharDictionnary	CharDictionnary::s_dicoArray[DICO_ELEMENT_COUNT];
/*static*/ u16				CharDictionnary::s_freeList = 0xFFFF;
/*static*/ u16				CharDictionnary::s_usedList = 0xFFFF;

/*static*/ CharCache		CharCache::s_cacheArray[CHAR_CACHE_SIZE];
/*static*/ u16				CharCache::s_cacheStart		= 0xFFFF;
/*static*/ u16				CharCache::s_cacheEnd		= 0;// First allocated object.
/*static*/ u16				CharCache::s_allocCounter	= 0;

/*static*/ FontObject*		FontObject::s_list			= NULL;
/*static*/ bool				FontObject::s_init			= false;
/*static*/ FT_Library		FontObject::s_library;
/*static*/ FontObject::FONTALIAS	FontObject::g_fonts[5];
/*static*/ u32				FontObject::g_fontInstalled	= 0;
/*static*/ u32*			FontObject::s_textCodepoints	= NULL;
/*static*/ u32*			FontObject::s_textFormatting	= NULL;
/*static*/ u32				FontObject::s_textCapacity	= 0;
/*static*/ bool				FontObject::s_useHinting		= true;

static bool s_nativeFont;

/*static*/ MemoryBlock*		MemoryBlock::s_blockList	= NULL;
/*static*/ u16				MemoryBlock::s_blockCounter	= 0;

void IPlatformRequest::setNativeFont(bool native) {
	s_nativeFont = native;
}

void* IPlatformRequest::getFont(int size, const char* fontName, u32 type) {
	if(s_nativeFont) {
		s_nativeFont = false;
		IFontIF* system = static_cast<IFontIF*>(getFontSystem());
		return system->getFont(size, type);
	}
	return FontObject::createFont(fontName, size);
}

void IPlatformRequest::deleteFontResource(void* font) {
	if(s_nativeFont) {
		s_nativeFont = false;
		IFontIF* system = static_cast<IFontIF*>(getFontSystem());
		system->deleteFont(font);
	} else {
		FontObject::destroyFont(static_cast<FontObject*>(font));
	}
}

bool IPlatformRequest::renderText(const char* text, void* font, u32 color,
								  u16 width, u16 height, u8* buffer,
								  s16 stride, s16 baseX, s16 baseY,
								  u32 pixelBytes, float scaleX, float scaleY) {
	if(s_nativeFont) {
		s_nativeFont = false;
		IFontIF* system = static_cast<IFontIF*>(getFontSystem());
		return system->renderText(text, font, color, width, height, buffer,
								  stride, baseX, baseY, pixelBytes,
								  scaleX, scaleY);
	}
	FontObject* fontObject = static_cast<FontObject*>(font);
	if(fontObject) {
		fontObject->renderText(baseX, baseY, text, buffer, color,
							   width, height, stride, pixelBytes, scaleX, scaleY);
	}
	return true;
}

bool IPlatformRequest::getTextInfo(const char* text, void* font, STextInfo* info,
								   float scaleX, float scaleY) {
	if(s_nativeFont) {
		s_nativeFont = false;
		IFontIF* system = static_cast<IFontIF*>(getFontSystem());
		return system->getTextInfo(text, font, info, scaleX, scaleY);
	}
	if(font) {
		static_cast<FontObject*>(font)->getTextInfo(text, info, scaleX, scaleY);
	} else {
		info->characterCount = 0;
		info->width = 0.0f;
		info->height = 0.0f;
		info->ascent = 0.0f;
		info->descent = 0.0f;
		info->top = 0.0f;
		info->bottom = 0.0f;
	}
	return true;
}

// ==========================================================================================
//   Global reboot function
// ==========================================================================================

int g_checkCount = 0;
int g_counterChar;

void FntDebug::check() {
	/*
	// 1. For each font
	FontObject* pFnt = FontObject::s_list;
	int totalCount = 0;
	while (pFnt) {
//		printf("===== Font : '%s' [%i]\n", pFnt->m_name, pFnt->m_refCount);
		g_counterChar = 0;
		// 2. Parse the cache tree and lookup.
		checkTreeRec(0,pFnt->m_dicoStart, 0);
		pFnt = pFnt->m_next;
//		printf("===== Has %i unicode entry\n", g_counterChar);
		totalCount += g_counterChar;
	}
//	printf("===== Total %i charFont, Alloc Total : %i\n", totalCount, CharCache::s_allocCounter);
	g_checkCount++;
	*/
}

void FntDebug::checkTreeRec(u32 depth, u32 idx, u32 code) {
	if (depth < 8) {
		CharDictionnary* pEntry = &CharDictionnary::s_dicoArray[idx];
//		printf("D:%i %04i ", depth, idx);

		for (u32 n=0; n < depth; n++) {
//			printf(" ");
		}

		for (u32 n=0; n < 16; n++) {
			if (pEntry->m_idxTbl[n] != 0xFFFF) {
//				printf("[%2i]%04i ", n, pEntry->m_idxTbl[n]);
			} else {
//				printf("[%2i]NULL ", n);
			}
		}
//		printf("\n");
		
		for (int n=0; n < 16; n++) {
			if (pEntry->m_idxTbl[n] != 0xFFFF) {
				checkTreeRec(depth + 1, pEntry->m_idxTbl[n], (code << 4) | n);
			}
		}
	} else {
		CharCache* pChar = &CharCache::s_cacheArray[idx];
		if (pChar->m_unicode != code) {
			// A leaf is reached after consuming all eight Unicode nibbles.
			// Its cache entry must therefore retain the same reconstructed code.
			// Any mismatch means the dictionary traversal no longer identifies it.
			klb_assertAlways(" Adr Code : %08X <-> Char : %08X\n", code, pChar->m_unicode);
		} else {
//			printf(" Char : %08X\n", code);
			g_counterChar++;
		}
	}
}

namespace FontSystem {
	void reboot() {
		// Release Font
		FontObject::releaseFontSystem(false);
		// Clean Dictionnary entries.
		CharDictionnary::reboot();
		// Clean Character cache.
		CharCache::reboot();
		// Clean Memory Block
		MemoryBlock::reboot();
	}

	void shutdown() {
		FontObject::releaseFontSystem(true);
		reboot();
	}
} // end FontSystem

/*static*/ void test() {
	FontObject::test();
	CharDictionnary::test();
	MemoryBlock::test();
	CharCache::test();
}

/*static*/ void FontObject::test() {
	// DEBUG_PRINT("Font Object Test====\n");
	FontObject* p    = s_list;
	FontObject* prev = NULL;
	// DEBUG_PRINT("Init : %i\n", s_init ? 1 : 0);
	// DEBUG_PRINT("Forward Parse.\n");
	while (p) {
		// DEBUG_PRINT("\t%8X RefCount\n",p, p->m_refCount);
		prev = p;
		p = p->m_next;
	}
	p = prev;
	// DEBUG_PRINT("Backward Parse.\n");
	while (p) {
		// DEBUG_PRINT("\t%8X\n",p);
		p = p->m_prev;
	}
}

/*static*/ void CharDictionnary::test() {
	// DEBUG_PRINT("Char Dictionnary Test====\n");
	u16 p		= s_freeList;
	u16 prev	= 0xFFFF;
	// DEBUG_PRINT("Forward Parse Free List.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		prev = p;
		p = s_dicoArray[p].m_next;
	}

	p = prev;
	// DEBUG_PRINT("Backward Parse free list.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		p = s_dicoArray[p].m_prev;
	}

	p		= s_usedList;
	prev	= 0xFFFF;
	// DEBUG_PRINT("Forward Parse used List.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		prev = p;
		p = s_dicoArray[p].m_next;
	}

	p = (u16)prev;
	// DEBUG_PRINT("Backward Parse used list.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		p = s_dicoArray[p].m_prev;
	};
}

/*static*/ void CharCache::test() {
	// DEBUG_PRINT("Char Cache Test====\n");
	// DEBUG_PRINT("\tAlloc Count : %i\n",s_allocCounter);

	u16 p		= s_cacheStart;
	u16 prev	= 0xFFFF;
	// DEBUG_PRINT("Forward Parse Allocated List.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		prev = p;
		p = s_cacheArray[p].m_next;
	}

	if ((prev != s_cacheEnd) && (s_cacheStart != 0xFFFF)) {
		// DEBUG_PRINT("ERROR !!! Char cache");
	}

	p = prev;
	// DEBUG_PRINT("Backward Parse Allocated list.\n");
	while (p != 0xFFFF) {
		// DEBUG_PRINT("\t%4X\n",p);
		p = s_cacheArray[p].m_prev;
	}
}

/*static*/ void MemoryBlock::test() {
}



struct SubEntry {
public:
	u16 offsetBitTable;
};

CharCache::CharCache() {
}

CharCache::~CharCache() {
}

// ==========================================================================================
//   Implementation CharDictionnary
// ==========================================================================================

CharDictionnary::CharDictionnary()
{
	m_next = 0xFFFF;
	m_prev = 0xFFFF;
	memset(m_idxTbl, 0xFF, 16 * sizeof(u16));
}

/*static*/ CharCache* CharDictionnary::getChar(u32 uniCode, FontObject* pFont, s32 scaleX, s32 scaleY) {
	FntDebug::check();	// Only place with one check.

	CharCache* pEntry = findEntry(pFont, uniCode, scaleX, scaleY);

	if (pEntry == NULL) {
		CharDictionnary* pParse = &s_dicoArray[pFont->m_dicoStart];
		u32 copyCode = uniCode;
		for (int n=0; n < 7; n++) {
			u16 idx = pParse->m_idxTbl[copyCode>>28];
			if (idx == 0xFFFF) {
				idx = CharDictionnary::createDicoEntry();
				pParse->m_idxTbl[copyCode>>28] = idx;
				if (idx == 0xFFFF) {
					return NULL;
				}
				pParse = &s_dicoArray[idx];
			} else {
				pParse = &s_dicoArray[idx];
			}
			copyCode <<=4;
		}

		u16 idxChar;
		pEntry = CharCache::createEntry(uniCode, &idxChar, pFont, scaleX, scaleY);
		if (pEntry) {
			pParse->m_idxTbl[copyCode>>28] = idxChar;
		}
	}
	return pEntry;
}

/*static*/ CharCache* CharDictionnary::findEntry(FontObject* pFont, u32 uniCode, s32 scaleX, s32 scaleY) {
	CharDictionnary* pParse = &s_dicoArray[pFont->m_dicoStart];
	u16 idx = 0;
	for (int n=0; n < 8; n++) {
		idx = pParse->m_idxTbl[uniCode>>28];
		if (idx == 0xFFFF) {
			break;
		}
		pParse = &s_dicoArray[idx];
		uniCode <<=4;
	}

	if (idx != 0xFFFF) {
		CharCache* entry = &CharCache::s_cacheArray[idx];
		if ((entry->m_scaleX == scaleY) && (entry->m_scaleY == scaleY)) {
			return entry;
		}
		entry->m_unicode = (u32)-1;
	} else {
		return NULL;
	}
	return NULL;
}

/*static*/ u16		 CharDictionnary::createDicoEntry() {
	if (s_freeList == 0xFFFF && s_usedList == 0xFFFF) {
		for (int n=0; n < DICO_ELEMENT_COUNT; n++) {
			s_dicoArray[n].m_next = n+1;
			s_dicoArray[n].m_prev = n-1;
		}
		s_dicoArray[DICO_ELEMENT_COUNT-1].m_next = 0xFFFF;

		s_usedList = 0xFFFF;
		s_freeList = 0;
	}

	if (s_freeList != 0xFFFF) {
		if (s_dicoArray[s_freeList].m_next != 0xFFFF) {
			s_dicoArray[s_dicoArray[s_freeList].m_next].m_prev = s_dicoArray[s_freeList].m_prev;
		}

		if (s_dicoArray[s_freeList].m_prev != 0xFFFF) {
			s_dicoArray[s_dicoArray[s_freeList].m_prev].m_next = s_dicoArray[s_freeList].m_next;			
		}

		if (s_usedList != 0xFFFF) {
			s_dicoArray[s_usedList].m_prev = s_freeList;
		}
		
		u16 nextFree = s_dicoArray[s_freeList].m_next;

		s_dicoArray[s_freeList].m_next = s_usedList;
		s_dicoArray[s_freeList].m_prev = 0xFFFF;
		memset(s_dicoArray[s_freeList].m_idxTbl, 0xFF, 16 * sizeof(u16));

		s_usedList = s_freeList;
		s_freeList = nextFree;
		return s_usedList;
	} else {
		return 0xFFFF;
	}
}

/*static*/ void CharDictionnary::destroyTree(u16 entry, u16 depth) {
	if (entry != 0xFFFF) {
		if (depth <= 7) {
			u16* p16 = s_dicoArray[entry].m_idxTbl;
			for (int n=0; n < 16; n++) {
				destroyTree(p16[n], depth+1);
			}

			removeEntry(entry);
		}
	}
}

/*static*/ void CharDictionnary::removeEntry(u16 entry) {
	if (s_dicoArray[entry].m_next != 0xFFFF) {
		s_dicoArray[s_dicoArray[entry].m_next].m_prev = s_dicoArray[entry].m_prev;
	}

	if (s_dicoArray[entry].m_prev != 0xFFFF) {
		s_dicoArray[s_dicoArray[entry].m_prev].m_next = s_dicoArray[entry].m_next;			
	} else {
		s_usedList = s_dicoArray[entry].m_next;
	}

	s_dicoArray[entry].m_next = s_freeList;
	s_dicoArray[entry].m_prev = 0xFFFF;

	if (s_freeList != 0xFFFF) {
		s_dicoArray[s_freeList].m_prev = entry;
	}

	s_freeList = entry;
}

/*static*/ void	CharDictionnary::removeDicoEntry(u16 startEntry, u32 unicode) {
	removeDicoEntryRec(startEntry, 0, unicode,startEntry);
}

/*static*/ u16 CharDictionnary::removeDicoEntryRec(u16 entry, u32 depth, u32 unicode,u16 startEntry) {
	if (depth < 7) {
		u16 next = s_dicoArray[entry].m_idxTbl[unicode>>28];
		u16 idx  = 0xFFFF;
		if (next != 0xFFFF) {
			idx = removeDicoEntryRec(next, depth + 1, unicode << 4, startEntry);
			if (idx != 0xFFFF) {
				s_dicoArray[entry].m_idxTbl[unicode>>28] = 0xFFFF;
			}
		}
	} else {
		// Remove entry.
		s_dicoArray[entry].m_idxTbl[unicode>>28] = 0xFFFF;
	}

	u16* p16 = s_dicoArray[entry].m_idxTbl;
	if ((p16[0] == 0xFFFF) && (p16[1] == 0xFFFF) && (p16[2] == 0xFFFF) && (p16[3] == 0xFFFF) && (p16[4] == 0xFFFF) && (p16[5] == 0xFFFF) && (p16[6] == 0xFFFF) && (p16[7] == 0xFFFF) 
		&& (p16[8] == 0xFFFF) && (p16[9] == 0xFFFF) && (p16[10] == 0xFFFF) && (p16[11] == 0xFFFF) && (p16[12] == 0xFFFF) && (p16[13] == 0xFFFF) && (p16[14] == 0xFFFF) && (p16[15] == 0xFFFF)) {

		if (entry != startEntry) {	// Never delete the root node
			removeEntry(entry);
		}

		return entry;
	} else {
		return 0xFFFF;
	}
}

// ==========================================================================================
//   Implementation Memory Block
// ==========================================================================================

/*static*/ void MemoryBlock::reboot() {
	MemoryBlock* parse = s_blockList;
	while (parse) {
		MemoryBlock* next = parse->m_next;
		delete parse;
		parse = next;
	}
	s_blockList		= NULL;
	s_blockCounter	= 0;
}

/*static*/ void MemoryBlock::freeBlock(u16 blockID, u16 subID) {
	MemoryBlock* parse = s_blockList;
	MemoryBlock* prev  = NULL;
	while (parse) {
		if (parse->m_id == blockID) {
			parse->m_allocFlag &= ~(1<<subID);	// Reset flag subID

			if (parse->m_allocFlag == 0) {
				if (parse != s_blockList) {	// Better to keep always ONE block as we need to render text anyway.
					// ... prev is then ALWAYS a valid pointer.
					prev->m_next = parse->m_next;
					delete parse;
				}
			}
			break;
		}
		prev  = parse;
		parse = parse->m_next;
	}
}

/*static*/ bool MemoryBlock::allocBlock(u16* returnBlock, u16* returnSubID, u8** ppData) {
	MemoryBlock* parse = s_blockList;
	while (parse) {
		u32 flag = parse->m_allocFlag; 
		if (flag != 0xFFFFFFFF) {
			u16 size = 16;
			u16 mask = (1 << size)-1;
			u16 idx  = 0;

			// log free bit search : 5 iteration only.
			while (size > 1) {
				if (((flag>>idx) & mask) != mask) {
					// Lower part
				} else {
					// Higher part
					idx += size;
				}
				size >>= 1;
				mask >>= size;
			}

			if (((flag>>idx) & mask) == mask) {
				// Higher part
				idx++;
			}
			parse->m_allocFlag |= 1<<idx;
			*returnBlock	= parse->m_id;
			*returnSubID	= idx;
			*ppData			= &parse->m_mem[MB_BLOCK_ITEM_SIZE * idx];
			return true;
		}
		parse = parse->m_next;
	}

	parse = new MemoryBlock();
	if (parse) {
		parse->m_next	= s_blockList;
		s_blockList		= parse;

		parse->m_allocFlag	= 1;
		parse->m_id			= s_blockCounter++;
		parse->partSize		= MB_BLOCK_ITEM_SIZE;

		*returnBlock	= parse->m_id;
		*returnSubID	= 0;
		*ppData			= &parse->m_mem[0];

		return true;
	} else {
		return false;
	}
}

/*static*/ const char* FontObject::getFileFromFontName(const char* fontName, char* tmpBuffer, FONTALIAS** fallbackFont, bool* useHinting) {

	u32 idx			= 0xFFFF;
	u32 defaultIdx	= 0xFFFF;

	// Search logical name
	for (s32 n = g_fontInstalled - 1; n >= 0; n--) {
		if (fontName && stricmp(fontName, g_fonts[n].logicalName) == 0) {
			idx = n;
			break;
		}
		if (g_fonts[n].isDefault) {
			if (defaultIdx == 0xFFFF) {
				defaultIdx = n;
			}
		}
	}

	// Search physical name
	if ((idx == 0xFFFF) && fontName) {
		for (s32 n = g_fontInstalled - 1; n >= 0; n--) {
			if (strstr(g_fonts[n].physicalName, fontName)) {
				idx = n;
				break;
			}
		}
	}

	// If not found, use default.
	if (idx == 0xFFFF) {
		idx = defaultIdx;
	}

	if (idx == 0xFFFF) {
		return NULL;
	} else {
		// Copy back to user
		sprintf(tmpBuffer,"%s",g_fonts[idx].physicalName);
		if (fallbackFont) {
			*fallbackFont = g_fonts[idx].fallbackFont;
		}
		if (useHinting) {
			*useHinting = g_fonts[idx].useHinting;
		}

		// return input buffer;
		return tmpBuffer;
	}
}

/*static*/ void FontObject::releaseFontSystem(bool releaseAllAliases) {
	if (s_init) {
		s_init = false;
		while (s_list) {
			// s_list modified by destroyFont
			destroyFont(s_list, true);
		}
		FT_Done_FreeType( s_library );
		s_library = NULL;
	}
	for (u32 n=0; n < g_fontInstalled; n++) {
		if (releaseAllAliases || !g_fonts[n].isSystemFont) {
			KLBDELETEA(g_fonts[n].physicalName);
			g_fonts[n].physicalName = NULL;
			KLBDELETEA(g_fonts[n].logicalName);
			g_fonts[n].logicalName = NULL;
		}
	}
	if (!releaseAllAliases) {
		g_fontInstalled = g_fonts[0].isSystemFont;
	}
	KLBDELETEA(s_textCodepoints);
	KLBDELETEA(s_textFormatting);
	s_textCodepoints = NULL;
	s_textFormatting = NULL;
	s_textCapacity = 0;
}

/*static*/ bool	FontObject::registerFont(const char* logicalName, const char* physicalFont, bool asDefault, bool useHinting) {
	if ((g_fontInstalled < 5) && logicalName && physicalFont) {
		IPlatformRequest& platform = CPFInterface::getInstance().platform();
		const char* fullpath = platform.getFullPath(physicalFont);	// Check also that file exists.
		if (fullpath) {
			for (u32 n = 0; n < g_fontInstalled; n++) {
				if (strcmp(logicalName, g_fonts[n].logicalName) == 0) {
					if (asDefault) {
						for (u32 idx = 0; idx < g_fontInstalled; idx++) {
							g_fonts[idx].isDefault = false;
						}
						g_fonts[n].isDefault = true;
					}
					return true;
				}
			}

			const char* logicN	= CKLBUtility::copyString(logicalName);
			if (logicN) {
				if (asDefault) {
					for (u32 n=0; n < g_fontInstalled;n++) {
						g_fonts[n].isDefault = false;						
					}
					g_fonts[g_fontInstalled		].isDefault		= true;
				} else {
					bool hasDefault = false;
					for (u32 n=0; n < g_fontInstalled;n++) {
						hasDefault |= g_fonts[n].isDefault;
					}

					if (!hasDefault) {
						g_fonts[g_fontInstalled].isDefault		= true;
					}
				}

				g_fonts[g_fontInstalled		].isSystemFont	= s_useHinting;
				g_fonts[g_fontInstalled		].fallbackFont	= NULL;
				g_fonts[g_fontInstalled		].logicalName	= logicN;
				g_fonts[g_fontInstalled		].physicalName	= fullpath;
				g_fonts[g_fontInstalled++	].useHinting	= useHinting;

				return true;
			}
			delete fullpath;
		}
	}
	return false;
}

/*static*/ void FontObject::disableHinting() {
	s_useHinting = false;
}

bool IPlatformRequest::registerFont(const char* logicalName, const char* fallbackName) {
	return FontObject::registerFont(logicalName, fallbackName);
}

bool IPlatformRequest::registerFont(const char* logicalName, const char* physicalFont,
									bool asDefault, bool useHinting) {
	return FontObject::registerFont(logicalName, physicalFont, asDefault, useHinting) | m_bNoDefaultFont;
}

/*static*/ bool FontObject::registerFont(const char* logicalName, const char* fallbackName) {
	if (logicalName) {
		u32 fontCount = g_fontInstalled;
		s32 logicalIndex = -1;
		if (fontCount) {
			for (u32 n = 0; n < fontCount; n++) {
				if (strcmp(g_fonts[n].logicalName, logicalName) == 0) {
					logicalIndex = n;
					break;
				}
			}
		}

		for (u32 n = 0; n < g_fontInstalled; n++) {
			if ((strcmp(g_fonts[n].logicalName, fallbackName) == 0) && (logicalIndex >= 0)) {
				g_fonts[logicalIndex].fallbackFont = &g_fonts[n];
				return true;
			}
		}
	}
	return false;
}

/*
	We render the same size IN PIXEL
	We do not want the font to have the same PHYSICAL size in millimeter on each device !
	Thus using the SAME DPI for any screen.
*/
#define FIXED_DPI	(60)

/*static*/ FontObject* FontObject::createFont(const char* fontName, u32 size) {
	// 0. Init library if not done yet.
	s32 error;
	if (!s_init) {
		if (s_textCapacity < 1500) {
			u32* codepoints = KLBNEWA(u32, 1501);
			u32* formatting = KLBNEWA(u32, 1501);
			KLBDELETEA(s_textCodepoints);
			KLBDELETEA(s_textFormatting);
			s_textCodepoints = codepoints;
			s_textFormatting = formatting;
			s_textCapacity = 1501;
		}
		error = FT_Init_FreeType( &s_library );              /* initialize library */
		if (error) {
			return NULL;
		}
		s_init = true;
	}

	// 1. Search matching font
	FontObject* pFont = s_list;
	
	size_t lenN = 0;
	if (fontName) {
		lenN = strlen(fontName);
		while (pFont) {
			if ((pFont->m_lenName == lenN) &&
				(pFont->m_size == size) &&
				(strcmp(pFont->m_name, fontName) == 0)) {
				pFont->m_refCount++;
				return pFont;
			}
			pFont = pFont->m_next;
		}
	} else {
		while (pFont) {
			if ((pFont->m_lenName == 0) &&
				(pFont->m_size == size) &&
				(pFont->m_name == NULL)) {
				pFont->m_refCount++;
				return pFont;
			}
			pFont = pFont->m_next;
		}
	}
	
	FT_Face face;
	FONTALIAS* fallbackFont = NULL;
	bool useHinting = true;
	
	// Internal Font
	char buff[512];

	error = FT_New_Face( s_library, FontObject::getFileFromFontName(fontName, buff, &fallbackFont, &useHinting), 0, &face );/* create face object */
	if ( !error ) {
		error = FT_Set_Char_Size( 	face, 		/* handle to face object			*/ 
									0,			/* char_width same as char_height	*/
									size<<6,	/* char_height in 1/64th of points	*/ 
									FIXED_DPI,	/* horizontal device resolution		*/
									FIXED_DPI 	/* vertical device resolution		*/
								);
		/* TODO potential API usage instead.
		error = FT_Set_Pixel_Sizes( face,	// Handle to face object
									0, 		// Width Same as height (pixel)
									size * ratio);
		 */
		if ( !error ) {
			pFont = new FontObject();
			
			if (pFont) {
				// 2. If not found, add at the beginning of the list
				pFont->m_prev = NULL;
				if (s_list) {
					s_list->m_prev = pFont;
					pFont->m_next = s_list;
				} else {
					pFont->m_next = NULL;
				}
				s_list = pFont;
				
				pFont->m_name		= fontName ? CKLBUtility::copyString(fontName) : NULL;
				pFont->m_size		= size;
				pFont->m_refCount	= 1;
				pFont->m_face		= face;
				pFont->m_lenName	= lenN;
				pFont->m_hasKerning = FT_HAS_KERNING( face );
				pFont->m_loadFlags = useHinting ? 0 : FT_LOAD_NO_HINTING;
				pFont->m_dicoStart	= CharDictionnary::createDicoEntry();
				if (pFont->m_dicoStart != 0xFFFF) {
					if (fallbackFont) {
						pFont->m_fallback = createFont(fallbackFont->logicalName, size);
						if (pFont->m_fallback == NULL) {
							destroyFont(pFont);
							return NULL;
						}
					}
					return pFont;
				}
				destroyFont(pFont);
				return NULL;
			}
		}
		FT_Done_Face ( face );
	} else {
		if (fontName) {
			// Try to create default font.
			if (strcmp(fontName, "")!=0) {
				return createFont("", size);
			}
		}
	}
	// Error.
	return NULL;
}

/*static*/ void FontObject::destroyFont(FontObject* pFont, bool force) {
	if (pFont) {
		if ((--pFont->m_refCount == 0) || force) {
			// 1. Remove from list
			if (pFont->m_prev != NULL) {
				pFont->m_prev->m_next	= pFont->m_next;
			} else {
				s_list = pFont->m_next;
			}
			
			if (pFont->m_next != NULL) {
				pFont->m_next->m_prev = pFont->m_prev;
			}
			
			// 2. Destroy face
			FT_Done_Face ( pFont->m_face );

			CharDictionnary::destroyTree(pFont->m_dicoStart, 0);
			if (pFont->m_fallback) {
				destroyFont(pFont->m_fallback);
			}

			// 3. Destroy object
			delete pFont;
		}
	}
}

FontObject::FontObject()
{
	m_fallback = NULL;
	m_name = NULL;
}

FontObject::~FontObject()
{
	// Scan all the allocated character and remove any pointer reference to font object.
	for (int n=0; n < CharCache::s_allocCounter; n++) {
		if (CharCache::s_cacheArray[n].m_pFontObj == this) {
			CharCache::s_cacheArray[n].m_pFontObj = NULL;
		}
	}

	KLBDELETEA(m_name);
	m_name = NULL;
}

float FontObject::getAscent() {
	FT_Size_Metrics* met = &this->m_face->size->metrics;
	return (float)(met->ascender >> 6);
}

FT_GlyphSlot FontObject::renderChar(u32 unicode, s32 scaleX, s32 scaleY, FontObject** selectedFont) {
	FT_Face face = m_face;
	FT_GlyphSlot glyphslt = face->glyph;
	u32 glyph_index = FT_Get_Char_Index(face, unicode);
	if ((glyph_index == 0) && m_fallback) {
		face = m_fallback->m_face;
		glyphslt = face->glyph;
		glyph_index = FT_Get_Char_Index(face, unicode);
		*selectedFont = m_fallback;
	} else {
		*selectedFont = this;
	}

	FT_Matrix transform = {
		(FT_Fixed)(scaleX << 2), 0,
		0, (FT_Fixed)(scaleY << 2)
	};
	FT_Set_Transform(face, &transform, NULL);
	FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | m_loadFlags);
	if (!error) {
		return glyphslt;
	} else {
		return NULL;
	}
}

/*static*/ CharCache* CharCache::createEntry(u32 uniCode, u16* outIdx, FontObject* pFont, s32 scaleX, s32 scaleY) {
	CharCache* pItem = NULL;

	// Render Glyph
	FontObject* renderFont;
	FT_GlyphSlot glyphslt = pFont->renderChar(uniCode, scaleX, scaleY, &renderFont);
	
	// Add to block	
	u16 blockIdx;
	u16 blockCharIdx;
	u8* ptrBuff;

	if (glyphslt && MemoryBlock::allocBlock(&blockIdx, &blockCharIdx, &ptrBuff)) {
		u16 idx;
		if (s_allocCounter < CHAR_CACHE_SIZE) {
			idx = s_allocCounter++;
		} else {
			idx = s_cacheEnd;
			CharCache* pItem = &s_cacheArray[idx];
			// 1.Remove from link list -> maintain head AND queue.
			if (pItem->m_prev != 0xFFFF) {
				s_cacheArray[pItem->m_prev].m_next = 0xFFFF;
				s_cacheEnd = pItem->m_prev;
			} else {
				s_cacheEnd = s_cacheStart; // Should never arrive
			}

			// 2.Remove data from block
			MemoryBlock::freeBlock(pItem->m_blockIndex, pItem->m_blockCharIndex);

			// 3.Remove entry from dictionnary
			if (pItem->m_pFontObj) {
				CharDictionnary::removeDicoEntry(pItem->m_pFontObj->m_dicoStart, pItem->m_unicode);
			}
		}
		
		pItem = &s_cacheArray[idx];
		*outIdx = idx;
		pItem->m_unicode = uniCode;

		// Add to link list
		pItem->m_next	 = s_cacheStart;
		pItem->m_prev	 = 0xFFFF;
		if (s_cacheStart != 0xFFFF) {
			s_cacheArray[s_cacheStart].m_prev = idx;
		}
		s_cacheStart = idx;	// s_cacheEnd is 0 by default -> always point to first allocated item.

		FT_Bitmap* bmp		= &glyphslt->bitmap;
		pItem->m_width		= bmp->width;
		pItem->m_height		= bmp->rows;
		pItem->m_offsetX	= glyphslt->bitmap_left;
		pItem->m_offsetY	= -glyphslt->bitmap_top;
		pItem->m_advanceX	= (s16)glyphslt->advance.x;
		pItem->m_blockIndex	= blockIdx;
		pItem->m_blockCharIndex = blockCharIdx;
		pItem->m_ptr		= ptrBuff;
		pItem->m_pFontObj	= pFont;
		pItem->m_pRenderFont = renderFont;
		pItem->m_scaleX		= scaleX;
		pItem->m_scaleY		= scaleY;

		u8* write	= pItem->m_ptr;
		u8* src		= bmp->buffer;
//#define PRESKIP

#ifdef PRESKIP
		write++;
#endif
		for (int y=0; y < bmp->rows; y++) {
#ifdef PRESKIP
			u8 start = 0xFF;
			for (int x=0; x < bmp->width; x++) {
				if ((*src) && (start==0xFF)) {
					start = x;
				}
				*write++ = *src++;
			}
			write[-(bmp->width+1)] = start;
			write++;
#else
			memcpy(write, src, bmp->width);
			src		+= bmp->pitch;
			write	+= bmp->width;
#endif
		}

		/*
		 * Each cache slot stores two views of the rendered glyph.
		 * The first view is the byte-per-pixel FreeType coverage copied above.
		 *
		 * The second view is a compact column mask used by clipped text drawing.
		 * It starts after the coverage bytes at the next four-byte boundary,
		 * allowing the mask to be addressed as an array of u32 values.
		 *
		 * Each output word covers up to 32 horizontal pixels on one scanline.
		 * Consecutive words advance through the glyph rows before moving to
		 * the next 32-pixel horizontal group.
		 *
		 * Only complete groups use all 32 bits.  For the final group, ex keeps
		 * iteration within the actual glyph width and leaves higher bits clear.
		 *
		 * Consumers can therefore reject transparent 32-pixel spans quickly
		 * without re-reading every coverage byte, while retaining the original
		 * bitmap immediately before the table for alpha compositing.
		 * Keeping both views in one MemoryBlock allocation also preserves their
		 * shared eviction lifetime and avoids a second cache lookup during
		 * rendering.
		 */
		pItem->m_tblOffset = ((pItem->m_width * bmp->rows) + 3) & 0xFFFC;
		u32 size = ((bmp->width+31)>>5)*(bmp->rows<<2);	// number of 32 bit mask for storage.
		if ((pItem->m_tblOffset < MB_BLOCK_ITEM_SIZE) && ((pItem->m_tblOffset+size) <= MB_BLOCK_ITEM_SIZE)) {
			u32* p = (u32*)&pItem->m_ptr[pItem->m_tblOffset];
			for (int x=0; x < bmp->width; x += 32) {
				bool last = (x + 32) >= bmp->width;
				u32 ex = 32;

				if (last) {
					if (bmp->width != 32) {
						ex = bmp->width & 0x1F;
					}
				}

				for (int y=0; y < bmp->rows; y++) {
					u32 mask = 0;

					u8* ptr = &pItem->m_ptr[x + (y * bmp->width)]; 
					for (u32 xi=0; xi < ex; xi++) {
						if (*ptr++) {
							mask |= 1 << xi;
						}
					}
					*p++ = mask;
				}
			}
		} else {
			klb_assertAlways("Font char does not fit into cache size : MB_BLOCK_ITEM_SIZE");
		}
	}

	return pItem;
}

void FontObject::renderText	(s32 x, s32 y, const char* text, u8* Buffer8888, u32 colorARGB8888, u32 buffWidth, u32 buffHeight, s32 strideByte, u32 pixelBytes, float scaleX, float scaleY) {
	// FT_GlyphSlot glyphslt = m_face->glyph;
	s32 scaledX = (s32)(scaleX * 16384.0f);
	s32 scaledY = (s32)(scaleY * 16384.0f);
	s32 fixedScaleX = (scaledX < 0) ? 0 : scaledX;
	s32 fixedScaleY = (scaledY < 0) ? 0 : scaledY;
	fixedScaleX = (u16)fixedScaleX;
	fixedScaleY = (u16)fixedScaleY;

	klb_assertNull((((colorARGB8888 >> 24) + (colorARGB8888 >> 31)) == 256), "RENDERING IGNORE ALPHA FOR TEXT");

	// ARGB
	u32 rgb32 = 0;
	u8* pCol = (u8*)&rgb32;
	pCol[0] = colorARGB8888>>16;
	pCol[1] = colorARGB8888>>8;
	pCol[2] = colorARGB8888;

	u16 rgb16 = ((colorARGB8888 >> 8) & 0xF000)		// Red
			  | ((colorARGB8888 >> 4) & 0x0F00)		// Green
			  | ((colorARGB8888 >> 0) & 0x00F0)		// Blue
			  ;

	s32 currX = x << 6;
	s32 currY = y;

	u32* codepoints;
	size_t charCount = s_textCapacity;
	for (;;) {
		codepoints = s_textCodepoints;
		s32 result = wind_utf8ucs4(text, codepoints, NULL, &charCount);
		if (result == WIND_ERR_INVALID_UTF8) {
			return;
		}
		if (result == 0) {
			break;
		}
		u32 required = strlen(text);
		if (s_textCapacity < required) {
			required++;
			u32* grownCodepoints = KLBNEWA(u32, required);
			u32* grownFormatting = KLBNEWA(u32, required);
			KLBDELETEA(s_textCodepoints);
			KLBDELETEA(s_textFormatting);
			s_textCodepoints = grownCodepoints;
			s_textFormatting = grownFormatting;
			s_textCapacity = required;
		}
		charCount = s_textCapacity;
	}

	for (u32 n=0; n < (u32)charCount; n++) {
		u32 charcode = codepoints[n];
		// DEBUG_PRINT("RENDERING; letter: %x(%c)", charcode, (char)charcode);

		CharCache* pChar = CharDictionnary::getChar(charcode, this, fixedScaleX, fixedScaleY);
		
		if (pChar) {
			//========================================
			//========================================
			// Rendering
			//========================================
			//========================================


			//----------------------------------------
			//  Clipping
			//----------------------------------------

			s32 Wwidth		= pChar->m_width;
			s32 Wheight		= pChar->m_height;	// Number of line to process.

			s32 px			= (currX >> 6) + pChar->m_offsetX;
			s32 py			= currY + pChar->m_offsetY;
			u32 startX		= 0;
			u32 startY		= 0;
			// DEBUG_PRINT("RENDERING; -- stepping into rendering path: %x(%c)", charcode, (char)charcode);
			// DEBUG_PRINT("RENDERING; px=%d, py=%d, buffWidth=%d, buffHeight=%d", px, py, buffWidth, buffHeight);

			// Top Left Clipping
			if (px < 0)
			{
				Wwidth	+= px;
				startX  -= px;
				px		= -(startX & 3);
			}

			if (py < 0) {
				Wheight	+= py;
				startY  -= py;
				py		= 0;
			}

			// Bottom right Clipping
			s32 overRight = buffWidth  - (Wwidth + px);
			s32 overBottom= buffHeight - (Wheight+ py);

			if (overRight < 0) {
				Wwidth += overRight;
			}

			if (overBottom < 0) {
				Wheight += overBottom;
			}

			if ((Wwidth<=0) || (Wheight<=0)) {
				currX += pChar->m_advanceX;
				continue;
			}
			// DEBUG_PRINT("RENDERING; survived clipping");

			// Process by vertical 32 pixel chunks.
			u32 slab32	= 0;
			u32 slab	= 0;
			while (slab < pChar->m_width) {

				// Code for continue...
				slab	= slab32;
				slab32 	= slab+32;

				if (startX >= slab32) {
					continue;	// This slab has no drawn pixel.
				}
				// DEBUG_PRINT("RENDERING; ---- this slab has at least one pixel: %x(%c)", charcode, (char)charcode);

				u32 endX = startX + Wwidth;

				if (endX <= slab) {
					continue;
				}

				// Now endX is INCLUDED in clip area.
				endX--;

				u32 clipMask = (u32)(-1);

				
				// Left side clipping inside slab.
				if ((startX >= slab) && (startX < slab32)) {
					clipMask -= ((1<<(startX & 0x1F))-1);
				}

				// Right side clipping inside slab.
				if ((endX >= slab) && (endX < slab32)) {
					clipMask &= (0xFFFFFFFF)>>(31 - (endX & 0x1F));
				}

				if (clipMask == 0) {
					continue;
				}

				//----------------------------------------
				//  Rendering
				//----------------------------------------
				u32* pMaskInfo	= (u32*)&pChar->m_ptr[pChar->m_tblOffset + ((pChar->m_height<<2)*(slab>>5)) + (startY<<2)];
#ifdef PRESKIP
				u8* buffSrc		= &pChar->m_ptr[1];
				s32 strideSrc	= pChar->m_width - 1;
#else
				u8* buffSrc		= pChar->m_ptr;
				s32 strideSrc	= pChar->m_width;
#endif
				u32 roundX		= (((startX & 0x1F)>>2)<<2);
				u8* pSrcL		= &buffSrc		[roundX + (startY * strideSrc) + slab];

				if (pixelBytes == 1) {
					u8* pDstL	= &Buffer8888[px + (py * strideByte) + slab];
					u8* pDstE	= &pDstL[Wheight * strideByte];

					while (pDstL < pDstE) {
#ifdef PRESKIP
						s32 delta = pSrcL[-1] - roundX;
						delta	= (delta < 0) ? 0 : (delta>>2)<<2;
						roundX	+= delta;
#endif
						u32 mask = (*pMaskInfo++) & clipMask;
						mask >>= roundX;
#ifdef PRESKIP
						u8* src = &pSrcL[delta];
#else
						u8* src = pSrcL;
#endif
						u8* dst = pDstL;

						while (mask) {
							#define ALPHA(idx) dst[idx] = src[idx]

							switch (mask & 0xF) {
							case 0x0: break;
							case 0x1: ALPHA(0); break;
							case 0x2: ALPHA(1); break;
							case 0x3: ALPHA(0); ALPHA(1); break;
							case 0x4: ALPHA(2); break;
							case 0x5: ALPHA(0); ALPHA(2); break;
							case 0x6: ALPHA(1); ALPHA(2); break;
							case 0x7: ALPHA(0); ALPHA(1); ALPHA(2); break;
							case 0x8: ALPHA(3); break;
							case 0x9: ALPHA(0); ALPHA(3); break;
							case 0xA: ALPHA(1); ALPHA(3); break;
							case 0xB: ALPHA(0); ALPHA(1); ALPHA(3); break;
							case 0xC: ALPHA(2); ALPHA(3); break;
							case 0xD: ALPHA(0); ALPHA(2); ALPHA(3); break;
							case 0xE: ALPHA(1); ALPHA(2); ALPHA(3); break;
							case 0xF: ALPHA(0); ALPHA(1); ALPHA(2); ALPHA(3); break;
							}
							#undef ALPHA
							dst += 4;
							src += 4;
							mask >>= 4;
						}

						pSrcL += strideSrc;
						pDstL += strideByte;
					}
				} else {
					klb_assertNull(pixelBytes == 4, "INVALID PIXEL FORMAT");
					u8* pDstL		= &Buffer8888	[(px<<2) + (py * strideByte) + (slab << 2)];
					u8* pDstE		= &pDstL		[Wheight * strideByte];
				
					//
					// Could do a fct ptr here if various rendering needed (ie alpha)
					// fct(pDstL, pDstE, pSrcL, pMaskInfo, clipMask, roundX, rgb32)
					//
					while (pDstL < pDstE) {
						//
						// Render the slab
						//
						// 1. recompute roundX with start : roundX += roundX - start>roundX)

	#ifdef PRESKIP
						s32 delta = pSrcL[-1] - roundX;
						delta	= (delta < 0) ? 0 : (delta>>2)<<2;
						roundX	+= delta;
	#endif
						u32 mask = (*pMaskInfo++) & clipMask;
						mask >>= roundX;
	#ifdef PRESKIP
						u8* src = &pSrcL[delta];
	#else
						u8* src = pSrcL;
	#endif
						u8* dst = pDstL;

						#define DBG(idx)	(*((u32*)(&dst[idx<<2])))=0xFFFF00FF;
						// DBG(0);

						while (mask) {
							// Write RGBA then overwrite alpha.
							#define RGB(idx)	(*((u32*)(&dst[idx<<2])))=rgb32

							switch (mask & 0xF) {
							case 0x0: break;
							case 0x1: RGB(0); dst[ 3] = src[0]; break;
							case 0x2: RGB(1); dst[ 7] = src[1]; break;
							case 0x3: RGB(0); dst[ 3] = src[0]; RGB(1); dst[ 7] = src[1]; break;
							case 0x4: RGB(2); dst[11] = src[2]; break;
							case 0x5: RGB(0); dst[ 3] = src[0]; RGB(2); dst[11] = src[2]; break;
							case 0x6: RGB(1); dst[ 7] = src[1]; RGB(2); dst[11] = src[2]; break;
							case 0x7: RGB(0); dst[ 3] = src[0]; RGB(1); dst[ 7] = src[1]; RGB(2); dst[11] = src[2]; break;
							case 0x8: RGB(3); dst[15] = src[3]; break;
							case 0x9: RGB(0); dst[ 3] = src[0]; RGB(3); dst[15] = src[3]; break;
							case 0xA: RGB(1); dst[ 7] = src[1]; RGB(3); dst[15] = src[3]; break;
							case 0xB: RGB(0); dst[ 3] = src[0]; RGB(1); dst[ 7] = src[1]; RGB(3); dst[15] = src[3]; break;
							case 0xC: RGB(2); dst[11] = src[2]; RGB(3); dst[15] = src[3]; break;
							case 0xD: RGB(0); dst[ 3] = src[0]; RGB(2); dst[11] = src[2]; RGB(3); dst[15] = src[3]; break;
							case 0xE: RGB(1); dst[ 7] = src[1]; RGB(2); dst[11] = src[2]; RGB(3); dst[15] = src[3]; break;
							case 0xF: RGB(0); dst[ 3] = src[0]; RGB(1); dst[ 7] = src[1]; RGB(2); dst[11] = src[2]; RGB(3); dst[15] = src[3]; break;
							}
							#undef RGB
							dst += 4*4;
							src += 4;
							mask >>= 4;
						}

						pSrcL += strideSrc;
						pDstL += strideByte;
					}
				}
			}
			currX += pChar->m_advanceX;
		}
	}
}

void FontObject::getTextInfo(const char* text, STextInfo* result, float scaleX, float scaleY) {
	s32 fixedScaleX = (s32)(scaleX * 16384.0f);

	size_t charCount = s_textCapacity;
	u32* codepoints;
	u32* formatting;
	s32 conversionResult;
	for (;;) {
		codepoints = s_textCodepoints;
		formatting = s_textFormatting;
		conversionResult = wind_utf8ucs4(text, codepoints, formatting, &charCount);
		if (conversionResult == WIND_ERR_INVALID_UTF8) {
			break;
		}
		if (conversionResult == 0) {
			break;
		}
		ensureTextCapacity(strlen(text));
		charCount = s_textCapacity;
	}

	if (conversionResult != 0) {
		result->characterCount = 0;
		result->height = 0.0f;
		result->ascent = 0.0f;
		result->descent = 0.0f;
		result->top = 0.0f;
		result->bottom = 0.0f;
		result->outlineExtraHeight = 0.0f;
		return;
	}

	FT_Size_Metrics* metrics = &m_face->size->metrics;
	u32 resultCapacity = result->characterCount;
	result->characterCount = 0;
	s32 ascender26_6 = (s32)(metrics->ascender * scaleY);
	s32 roundedAscender26_6 = ascender26_6 + 31;
	s32 descender26_6 = (s32)(metrics->descender * scaleY);
	s32 roundedDescender26_6 = descender26_6 + 31;

	s32 currX = 0;
	u32 baseFontCharacters = 0;
	u32 fallbackCharacters = 0;
	float fallbackHeight;
	float fallbackAscent;
	float fallbackDescent;
	bool hasOutline = false;

	for (u32 n = 0; n < (u32)charCount; n++) {
		u32 charcode = codepoints[n];
		if (charcode == '{' && result->parseInlineFormatting
			&& codepoints[n + 1] == 'b') {
			u8 format = (u8)codepoints[n + 2];
			bool validFormat = ((format >= '0') && (format <= '9'))
				|| ((format >= 'X') && (format <= 'Z'))
				|| ((format >= 'x') && (format <= 'z'));
			if (validFormat) {
				hasOutline |= ((format >= 'x') && (format <= 'z'));
				u32 close = n + 3;
				if (codepoints[close] == '}') {
					u32 formattingStart = n;
					n += 3;
					if (n < resultCapacity) {
						result->characterCount = n + 1;
						for (u32 skipped = 0; skipped < 3; skipped++) {
							result->characterEndX26_6[formattingStart] = currX;
							result->characterEndByteOffsets[formattingStart] = formatting[formattingStart];
						}
					}
					continue;
				}
			}
		}

		CharCache* pChar = CharDictionnary::getChar(charcode, this, fixedScaleX, (s32)scaleY);
		if (pChar) {
			FontObject* renderFont = pChar->m_pRenderFont;
			if (renderFont == this) {
				baseFontCharacters++;
			} else if (renderFont) {
				FT_Size_Metrics* fallbackMetrics = &renderFont->m_face->size->metrics;
				s32 fallbackAscender = (s32)(fallbackMetrics->ascender * scaleY);
				s32 roundedFallbackAscender = fallbackAscender + 31;
				s32 fallbackDescender = (s32)(fallbackMetrics->descender * scaleY);
				s32 roundedFallbackDescender = fallbackDescender + 31;
				fallbackHeight = (float)((roundedFallbackAscender - roundedFallbackDescender + 63) >> 6);
				fallbackAscent = (float)(roundedFallbackAscender >> 6);
				fallbackDescent = (float)(roundedFallbackDescender >> 6);
				fallbackCharacters++;
			}

			currX += pChar->m_advanceX;
			if (n < resultCapacity) {
				result->characterCount = n + 1;
				result->characterEndX26_6[n] = currX;
				result->characterEndByteOffsets[n] = formatting[n];
			}
		}
	}

	result->width = (float)(currX >> 6);
	if (fallbackCharacters) {
		if (baseFontCharacters) {
			result->height = (float)((roundedAscender26_6 - roundedDescender26_6 + 63) >> 6);
			result->height = (result->height > fallbackHeight) ? result->height : fallbackHeight;
			result->ascent = (float)(roundedAscender26_6 >> 6);
			result->ascent = (result->ascent > fallbackAscent) ? result->ascent : fallbackAscent;
			result->descent = (float)(roundedDescender26_6 >> 6);
			result->descent = (result->descent > fallbackDescent) ? result->descent : fallbackDescent;
		} else {
			result->height = fallbackHeight;
			result->ascent = fallbackAscent;
			result->descent = fallbackDescent;
		}
	} else {
		result->height = (float)((roundedAscender26_6 - roundedDescender26_6 + 63) >> 6);
		result->ascent = (float)(roundedAscender26_6 >> 6);
		result->descent = (float)(roundedDescender26_6 >> 6);
	}

	result->top = result->ascent + (result->height - (result->ascent - result->descent)) / 3.0f;
	result->bottom = result->top - result->height;
	float outlineExtraHeight = hasOutline ? 1.0f : 0.0f;
	result->height += outlineExtraHeight;
	result->outlineExtraHeight = outlineExtraHeight;
}

bool FontObject::ensureTextCapacity(u32 required) {
	if (s_textCapacity < required) {
		required++;
		u32* codepoints = KLBNEWA(u32, required);
		u32* formatting = KLBNEWA(u32, required);
		KLBDELETEA(s_textCodepoints);
		KLBDELETEA(s_textFormatting);
		s_textCodepoints = codepoints;
		s_textFormatting = formatting;
		s_textCapacity = required;
	}
	return true;
}

