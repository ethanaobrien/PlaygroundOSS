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
#ifndef ImplementationMovie_h
#define ImplementationMovie_h

#include <jni.h>
#include <GLES2/gl2.h>

#include "CKLBTextureMovie.h"

namespace KLBPlaygroundExtension
{
	class Extension
	{
	public:
		virtual void onPause();
		virtual void onResume();
		virtual void onLowMemory();

	protected:
		jclass m_javaClass;
	};
}

class ImplementationMovie : public IMovieInterface,
	public KLBPlaygroundExtension::Extension
{
public:
	ImplementationMovie();
	virtual ~ImplementationMovie();

	bool init(const char* url, s32 width, s32 height);

	virtual u32  getTextureTarget();
	virtual bool isInfoReady();
	virtual bool isFrameReady();
	virtual void refreshTexture(u32* textureName);
	virtual u32  getTextureName();
	virtual void getUV(float* uv);
	virtual void getSize(s32* size);
	virtual void update();
	virtual void setPlay();
	virtual void setPause();
	virtual void resetLoop();
	virtual void release();

	virtual void onLowMemory();

private:
	GLuint m_textureName;
	s32    m_width;
	s32    m_height;
	float  m_uv[4];
	bool   m_infoReady;
	bool   m_frameReady;
	s32    m_movieID;
};

#endif // ImplementationMovie_h
