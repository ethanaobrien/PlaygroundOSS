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
#ifndef __TEXTURE_PACKER__
#define __TEXTURE_PACKER__

#include "BaseType.h"
#include "CKLBRendering.h"

// If defined, allocated buffer are filled with color for debugging.
// #define INTERNAL_FILL_WITH_COLOR_TEXPACKER

class TexturePackerOnce;

typedef void (*SurfaceCompactionCallback)(void* owner, u32 surfaceID, u32 newSurfaceID);
typedef void (*SurfaceOwnerReleaseCallback)(void* owner);

#define FORMAT_8888		(4)
#define FORMAT_4444		(2)
#define FORMAT_8		(1)

#define STARTUP_FORMAT	(FORMAT_8888)

class CKLBLuaLibPackerControl;

class TexturePacker {
	friend class TexturePackerOnce;
	friend class CKLBLuaLibPackerControl;
public:
	struct SurfaceHandle {
		SurfaceHandle(u16 value) : value(value) {}
		operator u16() const { return value; }
		SurfaceHandle& operator|=(u16 marker) {
			value |= marker;
			return *this;
		}

		u16 value;
	};

	TexturePacker();
	~TexturePacker();

	inline
	static u8 getCurrentModeTexture() {
		return s_currentTextureMode;
	}

	inline
	static TexturePacker& getInstance() {
		extern TexturePacker g_texturePackerInstances[2];
		return g_texturePackerInstances[0];
	}
	static TexturePacker& getInstance(u32 index) {
		extern TexturePacker g_texturePackerInstances[2];
		return g_texturePackerInstances[index];
	}

	bool init				(u16 width,			u16 height, u16 format, bool allocateDefault = true);
	static bool initAll		(u16 width,			u16 height, u16 format, bool allocateSecondary);
	void release			();
	void releaseOwners		();
	static void releaseAll		();
	static void dump			(bool detail);
	static void refreshTextures	();
	static void unloadSurface		();
	static void reloadSurfaces		();

	SurfaceHandle
	     allocateSurface	(u16 w,		u16 h,		void* owner, SurfaceCompactionCallback compaction, SurfaceOwnerReleaseCallback releaseOwner);
	SurfaceHandle
	     reallocateSurface	(SurfaceHandle surface,	u16 w,		u16 h,	void* owner, SurfaceCompactionCallback compaction, SurfaceOwnerReleaseCallback releaseOwner);
	void releaseSurface		(SurfaceHandle surface);
	void setSurfaceOwner	(SurfaceHandle surface, void* owner);
	void getSurfaceInfo		(SurfaceHandle surface,	u32*& pixel, float& u0, float& v0, float& u1, float& v1, float& stepU, float& stepV);
	CTextureUsage*
		 getTextureUsage	(SurfaceHandle surface);
	u16	 getSurfaceStride	()	{ return m_width;			}
	void updateTexture		(SurfaceHandle surface);
	void setFormat			(u16 format);

#ifdef DEBUG_TEXTURE_PACKER
	void scan				(void* ctx);
	void setCurrentDelete	(void* ptr);
#endif

private:
	void refreshInstanceTextures();
	void dumpInstance		(bool detail);
	void unloadInstance		();
	void reloadInstance		();
	void resetPendingSurfaces();
	void addPendingSurface	(SurfaceHandle surface);
	bool notInPendingSurfaces(SurfaceHandle surface) const;
	SurfaceHandle
		 useOtherAlloc		(u16 w,		u16 h,		void* owner, SurfaceCompactionCallback compaction, SurfaceOwnerReleaseCallback releaseOwner);
	TexturePackerOnce*
		 allocateAllocator	(u16 w, u16 h);
	bool notInIgnoreList	(TexturePackerOnce* pack);
	void removeIgnoreList	(TexturePackerOnce* pack);
	void addIgnoreList		(TexturePackerOnce* pack);

	// Surface handles use eleven bits for the surface and five for the texture.
	#define SURFACE_INDEX_BITS	(11)
	#define SURFACE_INDEX_MASK	((1U << SURFACE_INDEX_BITS) - 1U)
	#define TEXTURE_INDEX_MASK	((u16)~SURFACE_INDEX_MASK)
	#define MAX_TEXTURES		(32)
	u16					m_width;
	u16					m_height;
	u16					m_currFormat;
	// Mode 8888, 4444, 8
	TexturePackerOnce*	m_lastUsedPacker	[5];
	TexturePackerOnce*	m_allocatedPacker	[MAX_TEXTURES];

	TexturePackerOnce*	ignore				[MAX_TEXTURES];
	u8					ignoreCount;

	// Released global surface handles awaiting deferred processing.
	u16					m_pendingSurfaces	[1024];
	s32					m_pendingSurfaceCount;

	static
	u8					s_currentTextureMode;
};

#include "ILuaFuncLib.h"
#include "CKLBLibRegistrator.h"

class CKLBLuaLibPackerControl : public ILuaFuncLib
{
private:
	CKLBLuaLibPackerControl();

	static int use8888Texture(lua_State * L);
	static CKLBLibRegistrator::LIBREGISTSTRUCT* ms_libRegStruct;
public:
	CKLBLuaLibPackerControl(DEFCONST * arrConstDef);
	virtual ~CKLBLuaLibPackerControl();

	void addLibrary			();
};

#endif 	// ifndef __TEXTURE_PACKER__
