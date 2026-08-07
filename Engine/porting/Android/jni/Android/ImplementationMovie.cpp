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
#include "ImplementationMovie.h"
#include "CAndroidRequest.h"
#include "CAndroidPathConv.h"

ImplementationMovie::ImplementationMovie()
: m_textureName(0)
, m_width(0)
, m_height(0)
, m_infoReady(false)
, m_frameReady(true)
, m_movieID(0)
{
}

bool
ImplementationMovie::init(const char* url, s32 width, s32 height)
{
	glGenTextures(1, &m_textureName);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureName);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	const char* path = CKLBPathConv::getInstance().fullpath(url);
	if (!path) {
		return false;
	}

	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieCreate", 'I', "SIII",
		path, m_textureName, width, height);
	m_movieID = result.i;
	if (!m_movieID) {
		return false;
	}

	m_infoReady = true;
	m_frameReady = true;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 0);
	m_width = static_cast<s32>(result.f);
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 1);
	m_height = static_cast<s32>(result.f);
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 2);
	m_uv[0] = result.f;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 3);
	m_uv[1] = result.f;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 4);
	m_uv[2] = result.f;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieGetInfo", 'F', "II", m_movieID, 5);
	m_uv[3] = result.f;
	return true;
}

void
KLBPlaygroundExtension::Extension::onPause()
{
}

void
KLBPlaygroundExtension::Extension::onResume()
{
}

void
KLBPlaygroundExtension::Extension::onLowMemory()
{
}

ImplementationMovie::~ImplementationMovie()
{
	if(m_textureName) {
		glDeleteTextures(1, &m_textureName);
		m_textureName = 0;
	}

	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieRelease", 'V', "I", m_movieID);
	m_movieID = 0;
}

u32
ImplementationMovie::getTextureTarget()
{
	return GL_TEXTURE_EXTERNAL_OES;
}

bool
ImplementationMovie::isInfoReady()
{
	return m_infoReady;
}

bool
ImplementationMovie::isFrameReady()
{
	return m_frameReady;
}

void
ImplementationMovie::refreshTexture(u32* textureName)
{
	*textureName = 0;
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieRefreshTexture", 'V', "I", m_movieID);
}

u32
ImplementationMovie::getTextureName()
{
	return m_textureName;
}

void
ImplementationMovie::getUV(float* uv)
{
	jvalue result;
	for(s32 index = 0; index < 4; ++index) {
		CAndroidRequest::getInstance()->callJavaMethod(
			NULL, result, "MovieGetInfo", 'F', "II", m_movieID, index + 2);
		uv[index] = result.f;
	}
}

void
ImplementationMovie::getSize(s32* size)
{
	size[0] = m_width;
	size[1] = m_height;
}

void
ImplementationMovie::update()
{
}

void
ImplementationMovie::setPlay()
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieSetPlay", 'V', "I", m_movieID);
}

void
ImplementationMovie::setPause()
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieSetPause", 'V', "I", m_movieID);
}

void
ImplementationMovie::resetLoop()
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, result, "MovieResetLoop", 'V', "I", m_movieID);
}

void
ImplementationMovie::release()
{
}

void
ImplementationMovie::onLowMemory()
{
}
