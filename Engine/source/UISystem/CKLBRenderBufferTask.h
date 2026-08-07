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
#ifndef CKLBRenderBufferTask_h
#define CKLBRenderBufferTask_h

#include "CKLBLuaTask.h"

class CKLBRenderState;
class CFrame;
class CKLBTextureAsset;

class CKLBRenderBufferTask : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBRenderBufferTask>;

private:
	CKLBRenderBufferTask();
	virtual ~CKLBRenderBufferTask();

public:
	static CKLBRenderBufferTask* create(CKLBTask* parent, const char* assetName,
		u32 order, u32 clearColor);

	virtual u32 getClassID();
	virtual void execute(u32 deltaT);
	virtual void die();
	virtual bool initScript(CLuaState& lua);
	virtual int commandScript(CLuaState& lua);

private:
	bool init(CKLBTask* parent, const char* assetName, u32 order, u32 clearColor);
	void releaseResources();

	CKLBRenderState*	m_renderState;
	CFrame*			m_frame;
	CKLBTextureAsset*	m_colorAsset;
	CKLBTextureAsset*	m_depthAsset;
	const char*		m_assetName;
};

#endif // CKLBRenderBufferTask_h
