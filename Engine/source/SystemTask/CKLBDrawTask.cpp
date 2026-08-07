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
#include "CKLBDrawTask.h"
#include "CKLBObject.h"
#include "CKLBScriptEnv.h"
#include "CKLBUtility.h"
#include "KLBPlatformMetrics.h"
#include <math.h>
;
CKLBDrawResource::CKLBDrawResource()
:m_hasBorder		(true)
,m_allowLog		(false)
,m_safeAreaUpdatePending(false)
,m_inSafeAreaCallback(false)
,m_frameCount	(0)
,m_safeAreaListener(NULL)
,m_safeAreaCallback(NULL)
{
	m_clearColor[0] = 1.0f;
	m_clearColor[1] = 0.7f;
	m_clearColor[2] = 0.2039f;
	m_clearColor[3] = 1.0f;
}

CKLBDrawResource::~CKLBDrawResource()
{
	KLBDELETEA(m_safeAreaCallback);
}

CKLBDrawResource&
CKLBDrawResource::getInstance() {
	static CKLBDrawResource instance;
	return instance;
}

void
CKLBDrawResource::setBorderless(bool hasNoBorder) {
	m_hasBorder = !hasNoBorder;
}

bool
CKLBDrawResource::setLogicalResolution(int width, int height)
{
	if (width  < 1) width  = 1;
	if (height < 1) height = 1;
	m_width = width;
	m_height = height;
	/*
	GLfloat matrix[16] = {
		(2.0f/m_width),	0.0f,					0.0f,	0.0f,
		0.0f,			(-2.0f/m_height),			0.0f,	0.0f,
		0.0f,			0.0f,					0.0f,	0.0f,
		-1.0f,			1.0f,					1.0f,	1.0f, };
	*/

	float inset[4];
	CPFInterface::getInstance().platform().getSafeAreaInset(inset);

	int safeWidth  = (int)((float)m_phisical_width  - (inset[0] + inset[1]));
	int safeHeight = (int)((float)m_phisical_height - (inset[2] + inset[3]));
	float scaleX = (float)safeWidth  / (float)m_width;
	float scaleY = (float)safeHeight / (float)m_height;
	float phisicalScaleX = (float)m_phisical_width  / (float)m_width;
	float phisicalScaleY = (float)m_phisical_height / (float)m_height;
	m_scaleX = scaleX;
	m_scaleY = scaleY;
	m_phisicalScaleX = phisicalScaleX;
	m_phisicalScaleY = phisicalScaleY;

	m_scale = (scaleX < scaleY) ? scaleX : scaleY;
	m_phisicalScaleMin = (scaleX < scaleY)
		? (phisicalScaleX / scaleX)
		: (phisicalScaleY / scaleY);
	m_invPhisicalScale = 1.0f / m_scale;
	m_phisicalScale = m_scale;

	for (int edge = 0; edge < 4; edge++) {
		m_unsafeArea[edge] = truncf(inset[edge] * m_invPhisicalScale);
	}

	m_vp_width	= m_width * m_scale;
	m_vp_height = m_height * m_scale;

	// 画面内の原点位置 m_ox, m_oy を計算
	m_ox = (int)((safeWidth  - m_vp_width ) * 0.5f);
	m_oy = (int)((safeHeight - m_vp_height) * 0.5f);
	m_screenBorderX = (int)(inset[0] + (float)m_ox);
	m_screenBorderY = (int)(inset[2] + (float)m_oy);
	m_borderX = m_ox * m_invPhisicalScale;
	m_borderY = m_oy * m_invPhisicalScale;

	// Select if 0,0 in coordinate is screen physical 0,0
	float glYFactor = m_hasBorder ? 0.0f : 2.0f / (float)m_phisical_height;
	float glTX		= m_hasBorder ? -1.0f : (-1.0f) + ((2.0f / (float)m_phisical_width)  * (float)m_screenBorderX);
	float glTY		= m_hasBorder ? +1.0f : (+1.0f) - (glYFactor * (float)m_screenBorderY);

	// Viewport pixel count width/height same as logical size ?
	// Yes : has border
	// No  : is borderless
	float glScaleX	= m_hasBorder ? (2.0/m_width)       : ( 2.0f * m_scale) / (float)m_phisical_width;
	float glScaleY	= m_hasBorder ? (-2.0f/m_height)   : (-2.0f * m_scale) / (float)m_phisical_height;

	GLfloat matrix[16] = {
		glScaleX,	0.0f,		0.0f,	0.0f,
		0.0f,		glScaleY,	0.0f,	0.0f,
		0.0f,		0.0f,		1.0f,	0.0f,
		glTX,		glTY,		0.0f,	1.0f, 
	};
		
	bool bResult = CKLBOGLWrapper::getInstance().init(matrix);
	if(!bResult) return bResult;

	// Perform centering and scaling at GL matrix level.
	ResetViewport();

	bResult = CKLBRenderingManager::getInstance().setClearColor(1.0f, 0.7f, 0.2039f, 1.0f);
	dglClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
	dglDisable( GL_CULL_FACE );

	return bResult;
}

bool
CKLBDrawResource::initResource(bool /*bLandscape*/, int width, int height)
{
	bool bResult = false;

	m_phisical_width = width;
	m_phisical_height = height;
	float inset[4];
	CPFInterface::getInstance().platform().getSafeAreaInset(inset);
	int logicalWidth  = (int)((float)width  - (inset[0] + inset[1]));
	int logicalHeight = (int)((float)height - (inset[2] + inset[3]));

	CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();
	if (pRdrMgr.setup(65000, 65000)) {

		if(setLogicalResolution(logicalWidth, logicalHeight)) {
			//
			// 3. Create Root Node
			//
			m_gpRootNode = KLBNEW(CKLBNode);
			if (m_gpRootNode) {
				m_gpRootNode->asRoot();
				m_gpRootNode->setScale(1.0f,1.0f);
				bResult = true;
			}
		}
	}

	return bResult;
}

bool
CKLBDrawResource::setClearColor(float r, float g, float b, float a)
{
	m_clearColor[0] = r;
	m_clearColor[1] = g;
	m_clearColor[2] = b;
	m_clearColor[3] = a;
	dglClearColor(r, g, b, a);
	return true;
}

void
CKLBDrawResource::freeResource()
{
	if (m_gpRootNode) {
		KLBDELETE(m_gpRootNode); m_gpRootNode = NULL;
	}
}

void
CKLBDrawResource::changeProjectionMatrix(float * /*matrix*/, int /*width*/, int /*height*/)
{
	m_safeAreaUpdatePending = true;
	if (m_gpRootNode) {
		m_gpRootNode->markUpMatrix();
	}
}

void
CKLBDrawResource::changeProjectionMatrix()
{
	m_safeAreaUpdatePending = true;
	if (m_gpRootNode) {
		m_gpRootNode->markUpMatrix();
	}
}

void
CKLBDrawResource::ResetViewport(void)
{
	if (m_hasBorder) {
		float inset[4];
		CPFInterface::getInstance().platform().getSafeAreaInset(inset);
		int viewportX = (int)((float)m_screenBorderX + inset[0]);
		int viewportY = (int)((float)m_screenBorderY + inset[3]);
		dglViewport(viewportX, viewportY, m_vp_width, m_vp_height);
	} else {
		dglViewport(0, 0, m_phisical_width, m_phisical_height);
	}
}

bool
CKLBDrawResource::allowLog() {
	return m_allowLog;
}

void
CKLBDrawResource::setLog(bool activate) {
	m_allowLog = activate;
}

bool
CKLBDrawResource::isInSafeAreaCallback() const {
	return m_inSafeAreaCallback;
}

void
CKLBDrawResource::registerSafeAreaChangeCallback(const char* callback) {
	KLBDELETEA(m_safeAreaCallback);
	m_safeAreaCallback = callback ? CKLBUtility::copyString(callback) : NULL;
}

void
CKLBDrawResource::registerSafeAreaListener(bool enable, CKLBTask* task) {
	if (enable) {
		m_safeAreaListener = task;
	} else if (m_safeAreaListener == task) {
		m_safeAreaListener = NULL;
	}
}

u32 CKLBDrawResource::incrementFrame() {
	return m_frameCount++;
}

void CKLBDrawResource::recompute()
{
	if (m_safeAreaUpdatePending) {
		m_safeAreaUpdatePending = false;
		setLogicalResolution(m_width, m_height);

		if (m_safeAreaCallback) {
			CKLBScriptEnv::getInstance().call_safeAreaChanged(m_safeAreaCallback);
		}

		if (m_safeAreaListener) {
			m_inSafeAreaCallback = true;
			m_safeAreaListener->execute(0);
			m_inSafeAreaCallback = false;
		}
	}

	m_gpRootNode->recompute();
}


CKLBDrawTask::CKLBDrawTask() : CKLBTask() {}
CKLBDrawTask::~CKLBDrawTask() {}

CKLBDrawTask *
CKLBDrawTask::create(bool rotate, int width, int height)
{
	CKLBDrawTask * pTask = KLBNEW(CKLBDrawTask);
	if(!pTask) return NULL;
	if(!pTask->init(rotate, width, height)) {
		KLBDELETE(pTask);
		return NULL;
	}
#ifdef DEBUG_PERFORMANCE
	pTask->m_recompute->init(pTask);
	pTask->m_draw->init(pTask);
#endif
	return pTask;
}

bool
CKLBDrawTask::onPause(bool /*bPause*/)
{
	// このタスクはpauseされてはならない
	return false;
}

#include "CKLBTexturePacker.h" 

void
CKLBDrawTask::execute(u32 deltaT)
{
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();

	CKLBDrawResource& drawRes = CKLBDrawResource::getInstance();
	bool gLogFrameTime = drawRes.allowLog();
	CKLBDrawResource::getInstance().incrementFrame();

	if (gLogFrameTime) {
		CPFInterface::getInstance().platform().nanotime();
	}

	// 5. Execution animation list (Spline, SWF movies)
	//
	if (!CKLBTaskMgr::getInstance().getFreeze()) {
		draw.performAnimationUpdate(deltaT);
	}

	TexturePacker::getInstance().refreshTextures();

	if (gLogFrameTime) {
		CPFInterface::getInstance().platform().nanotime();
	}
#ifndef DEBUG_PERFORMANCE
	// パフォーマンステストのため、処理を分割
	//
	// 6. Tree update
	//
	draw.recompute();

	if (gLogFrameTime) {
		CPFInterface::getInstance().platform().nanotime();
	}
	//
	// 7. Render Draw
	//
	draw.draw();

	//
	// 9. Rendering close frame.
	//
	CKLBOGLWrapper&		pOGLMgr			= CKLBOGLWrapper::getInstance();
	pOGLMgr.endFrame();
#endif // DEBUG_PERFORMANCE
}

void
CKLBDrawTask::die()
{
	CKLBDrawResource::getInstance().freeResource();
}

u32
CKLBDrawTask::getClassID()
{
	return CLS_KLBTASKDRAW;
}


bool
CKLBDrawTask::init(bool rotate, int width, int height)
{
	bool bResult = CKLBDrawResource::getInstance().initResource(rotate, width, height);

    if(!bResult) return false;
#ifdef DEBUG_PERFORMANCE
	m_recompute = KLBNEW(CKLBDrawTask_recompute);
	m_draw      = KLBNEW(CKLBDrawTask_draw);
#endif
	return regist(NULL, P_DRAW);
}


#ifdef DEBUG_PERFORMANCE
CKLBDrawTask_recompute::CKLBDrawTask_recompute() : CKLBTask() { 
}
CKLBDrawTask_recompute::~CKLBDrawTask_recompute() {}

bool
CKLBDrawTask_recompute::init(CKLBDrawTask * pParent)
{
	return regist(pParent, P_DRAW);
}


void
CKLBDrawTask_recompute::execute(u32 deltaT)
{
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	//
	// 6. Tree update
	//
	CKLBNode::s_matrixRecomputeCount = 0;
	CKLBNode::s_vertexRecomputeCount = 0;
	CKLBNode::s_colorRecomputeCount  = 0;

	draw.recompute();
}

void
CKLBDrawTask_recompute::die()
{
}

CKLBDrawTask_draw::CKLBDrawTask_draw() : CKLBTask() {
}
CKLBDrawTask_draw::~CKLBDrawTask_draw() {}

bool
CKLBDrawTask_draw::init(CKLBDrawTask * pParent)
{
	return regist(pParent, P_DRAW);
}

void
CKLBDrawTask_draw::execute(u32 deltaT)
{
	CKLBDrawResource& draw = CKLBDrawResource::getInstance();
	//
	// 7. Render Draw
	//
	draw.draw();

	//
	// 9. Rendering close frame.
	//
	CKLBOGLWrapper&		pOGLMgr			= CKLBOGLWrapper::getInstance();
	pOGLMgr.endFrame();
}

void
CKLBDrawTask_draw::die()
{
}

#endif // DEBUG_PERFORMANCE
