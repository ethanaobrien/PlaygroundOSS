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
#ifndef CKLBTextureMovie_h
#define CKLBTextureMovie_h

#include "CKLBLuaTask.h"

class IMovieInterface
{
public:
	virtual ~IMovieInterface() {}

	virtual u32  getTextureTarget() = 0;
	virtual bool isInfoReady() = 0;
	virtual bool isFrameReady() = 0;
	virtual void refreshTexture(u32* textureName) = 0;
	virtual u32  getTextureName() = 0;
	virtual void getUV(float* uv) = 0;
	virtual void getSize(s32* size) = 0;
	virtual void update() = 0;
	virtual void setPlay() = 0;
	virtual void setPause() = 0;
	virtual void resetLoop() = 0;
	virtual void release() = 0;
};

class CKLBTextureAsset;

class CKLBTextureMoviePost : public CKLBTask
{
public:
	virtual ~CKLBTextureMoviePost();

	virtual u32 getClassID();

	void setMovie(IMovieInterface* movie) {
		m_movie = movie;
	}
	bool registPost(CKLBTask* parent) {
		return regist(parent, P_END);
	}

protected:
	virtual void die();
	virtual void execute(u32 deltaT);

private:
	IMovieInterface* m_movie;
};

class CKLBTextureMovie : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBTextureMovie>;
private:
	CKLBTextureMovie();
	virtual ~CKLBTextureMovie();

	typedef void (CKLBTextureMovie::*MovieCommand)();

	void playMovie();
	void pauseMovie();
	void resetMovieLoop();

public:
	static CKLBTextureMovie* create(
		CKLBTask* parent,
		const char* movieURL,
		const char* textureName,
		s32 width,
		s32 height
	);

	virtual u32 getClassID();
	virtual bool initScript(CLuaState& lua);
	virtual int commandScript(CLuaState& lua);
	virtual void execute(u32 deltaT);
	virtual void die();

private:
	bool init(
		CKLBTask* parent,
		const char* movieURL,
		const char* textureName,
		s32 width,
		s32 height,
		CKLBTextureMoviePost* postTask
	);

	IMovieInterface *m_movie;
	CKLBTextureAsset* m_textureAsset;
	u32 m_legacyTextureName;
	s32 m_width;
	s32 m_height;
	float m_uv[4];
	MovieCommand m_commands[3];
};

#endif // CKLBTextureMovie_h
