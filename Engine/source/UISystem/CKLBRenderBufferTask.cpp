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
#include "CKLBRenderBufferTask.h"
#include "CKLBRendering.h"
#include "RenderingFramework.h"
#include "TextureManagement.h"
#include "CKLBAsset.h"
#include "CPFInterface.h"

static IFactory::DEFCMD renderBufferCmd[] = {
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBRenderBufferTask> renderBufferFactory("UI_RenderBuffer", 0x00090055, renderBufferCmd);

CKLBRenderBufferTask*
CKLBRenderBufferTask::create(CKLBTask* parent, const char* assetName,
	u32 order, u32 clearColor)
{
	CKLBRenderBufferTask* task = KLBNEW(CKLBRenderBufferTask);
	if (!task) {
		return NULL;
	}
	if (!task->init(parent, assetName, order, clearColor)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

CKLBRenderBufferTask::CKLBRenderBufferTask()
: m_renderState(NULL)
, m_frame(NULL)
, m_colorAsset(NULL)
, m_depthAsset(NULL)
, m_assetName(NULL)
{
	setNotAlwaysActive();
}

CKLBRenderBufferTask::~CKLBRenderBufferTask()
{
	releaseResources();
}

void
CKLBRenderBufferTask::releaseResources()
{
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	CKLBOGLWrapper& ogl = CKLBOGLWrapper::getInstance();

	if (m_renderState) {
		renderingManager.releaseCommand(m_renderState);
		m_renderState = NULL;
	}

	if (m_colorAsset && m_depthAsset) {
		freeScreenAsset(m_assetName);

		char depthName[2000];
		sprintf(depthName, "_depth_%s", m_assetName);
		freeScreenAsset(depthName);

		KLBDELETEA(m_assetName);
		m_colorAsset = NULL;
		m_depthAsset = NULL;
		m_assetName  = NULL;
	}

	if (m_frame) {
		ogl.releaseFrame(m_frame);
		m_frame = NULL;
	}
}

u32
CKLBRenderBufferTask::getClassID()
{
	return 0x00090055;
}

void
CKLBRenderBufferTask::execute(u32 /*deltaT*/)
{
}

void
CKLBRenderBufferTask::die()
{
	releaseResources();
}

int
CKLBRenderBufferTask::commandScript(CLuaState& lua)
{
	lua.retBool(false);
	return 1;
}

bool
CKLBRenderBufferTask::initScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if (argc < 3) {
		return false;
	}

	CKLBTask* parent = lua.isNil(1) ? NULL
		: static_cast<CKLBTask*>(const_cast<void*>(lua.getScriptPtr(1)));
	const char* assetName = lua.isNil(2) ? NULL : lua.getString(2);
	if (assetName && !assetName[0]) {
		assetName = NULL;
	}
	u32 order = lua.getInt(3);
	u32 color = (argc >= 4) ? (lua.getInt(4) & 0x00ffffff) : 0;
	u32 alpha = (argc >= 5) ? (lua.getInt(5) << 24) : 0xff000000;
	return init(parent, assetName, order, color | alpha);
}

bool
CKLBRenderBufferTask::init(CKLBTask* parent, const char* assetName, u32 order, u32 clearColor)
{
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	CKLBOGLWrapper& ogl = CKLBOGLWrapper::getInstance();
	CKLBRenderState* renderState = renderingManager.allocateCommandState();
	if (!renderState) {
		releaseResources();
		return false;
	}

	if (assetName && assetName[0]) {
		IClientRequest& client = CPFInterface::getInstance().client();
		u32 width = client.getPhysicalScreenWidth();
		u32 height = client.getPhysicalScreenHeight();
		u32 colorFormat = ((clearColor >> 24) == 0xff) ? 3 : 4;
		CKLBTextureAsset* colorAsset = static_cast<CKLBTextureAsset*>(
			createTexture(width, height, assetName, colorFormat, false, NULL));

		char depthName[2000];
		sprintf(depthName, "_depth_%s", assetName);
		CKLBTextureAsset* depthAsset = static_cast<CKLBTextureAsset*>(
			createTexture(width, height, depthName, 1, false, NULL));
		CFrame* frame = ogl.createFrame();
		m_assetName = CKLBUtility::copyString(assetName);

		if (!frame || !colorAsset || !depthAsset || !m_assetName) {
			if (frame) {
				ogl.releaseFrame(frame);
				freeScreenAsset(colorAsset->getName());
				freeScreenAsset(depthAsset->getName());
			}
			renderingManager.releaseCommand(renderState);
			releaseResources();
			return false;
		}

		CKLBAssetManager::getInstance().registerAsset(colorAsset);
		if (colorAsset->getAssetID() != NULL_IDX) {
			colorAsset->incrementRefCount();
		}
		CKLBAssetManager::getInstance().registerAsset(depthAsset);
		if (depthAsset->getAssetID() != NULL_IDX) {
			depthAsset->incrementRefCount();
		}

		frame->setColorBuffer(colorAsset->m_pTexture, 0);
		frame->setDepthBuffer(depthAsset->m_pTexture, 0);

		m_renderState = renderState;
		m_frame = frame;
		m_colorAsset = colorAsset;
		m_depthAsset = depthAsset;

		float red   = ((clearColor >> 16) & 0xff) / 255.0f;
		float green = ((clearColor >> 8)  & 0xff) / 255.0f;
		float blue  = ( clearColor        & 0xff) / 255.0f;
		float alpha = ((clearColor >> 24) & 0xff) / 255.0f;
		renderState->setClearColor(true, red, green, blue, alpha);
		renderState->setClearDepth(true, 1.0f);
		renderState->setUse(false, true, NULL);
		renderState->setRenderTarget(m_frame);
	} else {
		renderState->setRenderTarget(NULL);
		renderState->enableRenderTargetChange();
		renderState->setUse(false, true, NULL);
	}

	renderingManager.addToRendering(renderState, order);
	return regist(parent, P_UIAFTER);
}
