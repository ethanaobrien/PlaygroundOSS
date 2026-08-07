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
#ifndef CKLBUIParticle_h
#define CKLBUIParticle_h

#include "CKLBUITask.h"

class CKLBParticleAsset;
class KLBParticleMovie;
namespace TLFX {
	class Effect;
	class ParticleManager;
}

class CKLBUIParticle : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUIParticle>;
private:
	CKLBUIParticle();
	virtual ~CKLBUIParticle();

public:
	static CKLBUIParticle* create(CKLBUITask* parent, CKLBNode* node,
								 u32 order, float x, float y,
								 const char* assetName, const char* effectName,
								 u32 particleCount);

	virtual u32 getClassID();
	virtual u32 getOrder();
	virtual void setOrder(u32 order);
	virtual bool initUI(CLuaState& lua);
	virtual int commandUI(CLuaState& lua, int argc, int cmd);
	virtual void execute(u32 deltaT);
	virtual void dieUI();

	const char* getAsset();

private:
	static PROP_V2 ms_propItems[];

	bool init(CKLBUITask* parent, CKLBNode* node, u32 order, float x, float y,
			  const char* assetName, const char* effectName, u32 particleCount);
	bool initCore(u32 order, float x, float y, const char* assetName,
				  const char* effectName, u32 particleCount);

	struct ParticleBounds {
		float left;
		float top;
		float right;
		float bottom;
	};

	u32					m_order;
	s32					m_frameSteps;
	const char*			m_assetName;
	ParticleBounds		m_bounds;
	KLBParticleMovie*	m_movie;
	TLFX::Effect*		m_effect;
	TLFX::Effect*		m_libraryEffect;
	char**				m_textureNames;
	s32					m_textureNameCount;
	CKLBParticleAsset*	m_asset;
};

#endif // CKLBUIParticle_h
