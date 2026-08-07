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
#include "CKLBTextureMovie.h"
#include "CKLBAsset.h"
#include "TextureManagement.h"

enum {
	TM_SET_PLAY = 0,
	TM_SET_PAUSE,
	TM_RESET_LOOP
};

static IFactory::DEFCMD cmd[] = {
	{ "TM_SET_PLAY", TM_SET_PLAY },
	{ "TM_SET_PAUSE", TM_SET_PAUSE },
	{ "TM_RESET_LOOP", TM_RESET_LOOP },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBTextureMovie> factory(
	"TextureMovie", 0x00280088, cmd);

void
CKLBTextureMovie::playMovie()
{
	m_movie->setPlay();
}

void
CKLBTextureMovie::pauseMovie()
{
	m_movie->setPause();
}

void
CKLBTextureMovie::resetMovieLoop()
{
	m_movie->resetLoop();
}

CKLBTextureMoviePost::~CKLBTextureMoviePost()
{
}

u32
CKLBTextureMoviePost::getClassID()
{
	return 0x00200089;
}

void
CKLBTextureMoviePost::die()
{
}

void
CKLBTextureMoviePost::execute(u32 /* deltaT */)
{
	m_movie->update();
}

CKLBTextureMovie::~CKLBTextureMovie()
{
}

CKLBTextureMovie::CKLBTextureMovie()
: m_movie(NULL)
, m_textureAsset(NULL)
, m_commands()
{
	m_commands[TM_SET_PLAY] = &CKLBTextureMovie::playMovie;
	m_commands[TM_SET_PAUSE] = &CKLBTextureMovie::pauseMovie;
	m_commands[TM_RESET_LOOP] = &CKLBTextureMovie::resetMovieLoop;
}

CKLBTextureMovie*
CKLBTextureMovie::create(
	CKLBTask* parent,
	const char* movieURL,
	const char* textureName,
	s32 width,
	s32 height
)
{
	CKLBTextureMovie* task = KLBNEW(CKLBTextureMovie);
	CKLBTextureMoviePost* postTask = KLBNEW(CKLBTextureMoviePost);
	if (postTask) {
		if (task->init(
			parent, movieURL, textureName, width, height, postTask
		)) {
			return task;
		}
		KLBDELETE(task);
		KLBDELETE(postTask);
	} else {
		KLBDELETE(task);
	}
	return NULL;
}

u32
CKLBTextureMovie::getClassID()
{
	return 0x00280088;
}

bool
CKLBTextureMovie::init(
	CKLBTask* parent,
	const char* movieURL,
	const char* textureName,
	s32 width,
	s32 height,
	CKLBTextureMoviePost* postTask
)
{
	m_textureAsset = CKLBTextureAsset::createMovieTexture(textureName);
	bool initialized = m_textureAsset ? true : false;
	m_movie = CPFInterface::getInstance().platform().createMoviePlayer(
		movieURL, width, height
	);
	initialized &= m_movie ? true : false;
	if (m_textureAsset) {
		CKLBAssetManager::getInstance().registerAsset(m_textureAsset);
		m_textureAsset->incrementRefCount();
	}

	if (m_movie) {
		m_movie->getSize(&m_width);
		m_movie->getUV(m_uv);
		m_textureAsset->updateMovieTexture(
			0, 0, m_width, m_height, m_uv
		);
	}

	if (!initialized) {
		return false;
	}
	postTask->setMovie(m_movie);
	if (!regist(parent, P_UIPROC)) {
		return false;
	}
	return postTask->registPost(this);
}

bool
CKLBTextureMovie::initScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if (argc < 2 || argc > 4) {
		return false;
	}

	const char* movieURL = lua.getString(1);
	const char* textureName = lua.getString(2);
	s32 height = -1;
	s32 width;
	if (argc < 3) {
		width = -1;
	} else {
		width = lua.getInt(3);
		if (argc >= 4) {
			height = lua.getInt(4);
		}
	}

	CKLBTextureMoviePost* postTask = KLBNEW(CKLBTextureMoviePost);
	return postTask && init(
		NULL, movieURL, textureName, width, height, postTask
	);
}

void
CKLBTextureMovie::die()
{
	if (m_textureAsset) {
		float emptyUV[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (m_movie) {
			m_textureAsset->updateMovieTexture(
				m_movie->getTextureTarget(), 0, 0, 0, emptyUV
			);
		}
		m_textureAsset->decrementRefCount();
		m_textureAsset = NULL;
	}

	if (m_movie) {
		CPFInterface::getInstance().platform().destroyMoviePlayer(m_movie);
		m_movie = NULL;
	}
}

void
CKLBTextureMovie::execute(u32 /*deltaT*/)
{
	if (m_movie->isInfoReady() && m_width == -1) {
		m_movie->getSize(&m_width);
	}

	if (m_movie->isFrameReady()) {
		u32 refreshedTexture;
		m_movie->refreshTexture(&refreshedTexture);
		m_movie->getUV(m_uv);
		m_textureAsset->updateMovieTexture(
			m_movie->getTextureTarget(),
			m_movie->getTextureName(),
			m_width,
			m_height,
			m_uv
		);
	}
}

int
CKLBTextureMovie::commandScript(CLuaState& lua)
{
	if (lua.numArgs() != 2) {
		lua.retBool(false);
		return 1;
	}

	int command = lua.getInt(2);
	klb_assertNull(command < 3, "Error: function idx overflow");
	MovieCommand method = m_commands[command];
	klb_assertNull(method, "Error: function is null");
	(this->*method)();
	return 0;
}
