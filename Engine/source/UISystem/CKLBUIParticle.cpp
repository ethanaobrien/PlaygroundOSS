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
#include "CKLBUIParticle.h"
#include "PlaygroundParticle.h"
#include "TLFXEffect.h"
#include "CKLBNode.h"

enum {
	UI_PARTICLE_SKIP = 0,
	UI_PARTICLE_INFO
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_PARTICLE_SKIP", UI_PARTICLE_SKIP },
	{ "UI_PARTICLE_INFO", UI_PARTICLE_INFO },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBUIParticle> factory(
	"UI_Particle", 0x00080086, cmd);

CKLBUIParticle::CKLBUIParticle()
: CKLBUITask(P_UIAFTER)
, m_frameSteps(1)
, m_assetName(NULL)
, m_movie(NULL)
, m_effect(NULL)
, m_textureNames(NULL)
, m_textureNameCount(0)
, m_asset(NULL)
{
	m_newScriptModel = true;
}

CKLBUIParticle::~CKLBUIParticle()
{
}

u32
CKLBUIParticle::getClassID()
{
	return 0x00080086;
}

CKLBUIParticle*
CKLBUIParticle::create(CKLBUITask* parent, CKLBNode* node,
					  u32 order, float x, float y,
					  const char* assetName, const char* effectName,
					  u32 particleCount)
{
	CKLBUIParticle* task = KLBNEW(CKLBUIParticle);
	if(!task) { return NULL; }

	if(!task->init(parent, node, order, x, y,
				   assetName, effectName, particleCount)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

bool
CKLBUIParticle::init(CKLBUITask* parent, CKLBNode* node,
					u32 order, float x, float y,
					const char* assetName, const char* effectName,
					u32 particleCount)
{
	if(!setupNode()) { return false; }

	bool result = initCore(order, x, y, assetName, effectName, particleCount);
	result = registUI(parent, result);
	if(node) {
		parent->getNode()->removeNode(getNode());
		node->addNode(getNode());
	}
	return result;
}

u32
CKLBUIParticle::getOrder()
{
	return m_order;
}

void
CKLBUIParticle::setOrder(u32 order)
{
	if (m_order != order) {
		m_order = order;
		getNode()->setPriority(order);
	}
}

bool
CKLBUIParticle::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 7 || argc > 8) { return false; }

	float x = lua.getFloat(3);
	float y = lua.getFloat(4);
	if(argc >= 8) {
		m_textureNames = (char**)replaceAssets(lua, 8, &m_textureNameCount);
	}

	u32 order = lua.getInt(2);
	const char* assetName = lua.getString(5);
	const char* effectName = lua.getString(6);
	u32 particleCount = lua.getInt(7);
	return initCore(order, x, y, assetName, effectName, particleCount);
}

void
CKLBUIParticle::execute(u32)
{
	for (s32 frame = 0; frame < m_frameSteps; ++frame) {
		m_movie->Update();
	}
	if (m_frameSteps > 1) {
		m_frameSteps = 1;
	}

	m_movie->beginParticleBatch();
	m_movie->DrawParticles(1.0f, -1);
	m_movie->finishParticleBatch();
	getNode()->markUpColor();
}

const char*
CKLBUIParticle::getAsset()
{
	return m_assetName;
}

int
CKLBUIParticle::commandUI(CLuaState& lua, int argc, int cmd)
{
	switch(cmd)
	{
	case UI_PARTICLE_SKIP:
		{
			bool restarted = false;
			if(argc >= 3) {
				if(argc >= 4 && lua.getBool(4)) {
					// Restart the effect from a fresh instance of the library template.
					m_movie->ClearAll();
					TLFX::Effect* library = m_libraryEffect;
					TLFX::Effect* effect = new TLFX::Effect(*library, m_movie, false);
					effect->SetPosition(0.0f, 0.0f);
					m_movie->AddEffect(effect, 0);
					m_effect = effect;
					restarted = true;
				}
				s32 frameSteps = lua.getInt(3);
				m_frameSteps = (frameSteps > 0) ? frameSteps : 1;
			}
			lua.retBoolean(restarted);
		}
		break;
	case UI_PARTICLE_INFO:
		lua.retInt(0);
		lua.retFloat(0.0f);
		return 2;
	}
	return 1;
}

void
CKLBUIParticle::dieUI()
{
	CKLBRenderingManager& render = CKLBRenderingManager::getInstance();
	render.releaseCommand(m_movie->m_particleSprite);

	delete m_movie;

	if (m_asset) {
		m_asset->decrementRefCount();
		m_asset = NULL;
	}

	delete [] m_assetName;

	for (s32 index = 0; index < (m_textureNameCount << 1); ++index) {
		delete [] m_textureNames[index];
	}
	delete [] m_textureNames;
}
