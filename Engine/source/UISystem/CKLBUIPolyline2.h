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
#ifndef CKLBUIPolyline2_h
#define CKLBUIPolyline2_h

#include "CKLBUITask.h"
#include "CKLBRendering.h"
#include <vector>

class CKLBUIPolyline2 : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUIPolyline2>;

	enum COMMAND {
		CLEAR,
		ADD_POINT,
		BUILD
	};

private:
	CKLBUIPolyline2();
	virtual ~CKLBUIPolyline2();
	bool init(CKLBUITask* parent, CKLBNode* node, u32 order);
	bool initCore(s32 order);
	void releaseStrokeTexture();
	void prepareStrokeTexture(
		float width, float* u, float* upperV, float* lowerV);
	void build(u32 color, bool close, float width, bool antialias);

public:
	static CKLBUIPolyline2* create(
		CKLBUITask* parent, CKLBNode* node, u32 order);
	virtual u32 getClassID();
	void execute(u32 deltaT);
	void dieUI();

private:
	bool initUI(CLuaState& lua);
	int commandUI(CLuaState& lua, int argc, int cmd);

	static PROP_V2			ms_propItems[];

	u32						m_order;
	float					m_textureOriginX;
	float					m_textureOriginY;
	float					m_cachedStrokeWidth;
	float					m_textureUScale;
	float					m_textureVScale;
	float					m_textureVOffset;
	float					m_textureCoverageScale;
	CTexture*				m_strokeTexture;
	CTextureUsage*			m_strokeTextureUsage;
	CKLBDynSprite*		m_pMesh;
	std::vector<float>	m_points;
};

#endif // CKLBUIPolyline2_h
